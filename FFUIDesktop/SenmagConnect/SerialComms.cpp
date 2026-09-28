#include "SerialComms.h"

int SerialComms::openPort(int portIndex) {
    if (portIndex < 0)
        return -1;



    FT_STATUS ftStatus = FT_Open(portIndex, &deviceHandle);

    if (ftStatus != FT_OK) {
        std::cerr << "Failed to open device at index " << portIndex
            << " (status=" << ftStatus << ")\n";
        deviceHandle = nullptr;
        return -1;
    }
            
    
    FT_SetBaudRate(deviceHandle, 1500000);
    FT_SetDataCharacteristics(deviceHandle, FT_BITS_8, FT_STOP_BITS_1, FT_PARITY_NONE);
    FT_SetFlowControl(deviceHandle, FT_FLOW_NONE, 0, 0);
    FT_SetLatencyTimer(deviceHandle, 1);

    //Both were previously 0. Per FTDI's own documentation and support forum: a ReadTimeout of 0
    //means "wait indefinitely", not "return immediately" - checkUpdates() below only ever calls
    //FT_Read after FT_GetQueueStatus has already confirmed bytes are waiting, so this wasn't
    //normally a hang risk, but it did mean an unbounded wait in the rare case the device
    //vanished between those two calls. A WriteTimeout of 0 means FT_Write returns immediately
    //without waiting for the transmission to actually complete at all - FTDI's own forum flags
    //this directly as a data-corruption risk for back-to-back writes, which is exactly what
    //sendTargets() does every single haptic frame via sendPacket() below. 50ms is long enough
    //that a healthy read/write (normally well under a millisecond at this baud rate) is never
    //affected, but short enough that even a stalled device can only cost this real-time loop a
    //single frame's worth of delay, not an unbounded one.
    FT_SetTimeouts(deviceHandle, 50, 50);


    sendPing();

    return 0;

}

void SerialComms::closePort() {
    if (!deviceHandle) return;
    FT_Close(&deviceHandle);
}


int SerialComms::sendTargets(LibreOne_targets targets) {
    CommsPacket txPacket;
    txPacket.meta.packetType = packetType_targets;
    txPacket.meta.dataLength = sizeof(LibreOne_targets);
    txPacket.targets = targets;
    sendPacket(txPacket);
    return 1;
}


int SerialComms::sendPacket(CommsPacket packet) {
    if (!deviceHandle) return -1;
    for (int x = 0; x < COMMS_HEADERLEN; x++) packet.header[x] = COMMS_HEADER;
    packet.meta.checksum = calcCrc((uint8_t*)&packet.dataBytes, packet.meta.dataLength);

    DWORD bytesWritten = 0;
    FT_STATUS ftStatus = FT_Write(deviceHandle,
        &packet,
        COMMS_HEADERLEN + sizeof(CommsPacketMetadata) + packet.meta.dataLength,
        &bytesWritten);

    return (ftStatus == FT_OK);
}

int SerialComms::checkUpdates() {
    if (!deviceHandle) return -1;
    DWORD rxBytes = 0;
    FT_STATUS ftStatus = FT_GetQueueStatus(deviceHandle, &rxBytes);

    while (rxBytes > 0) {
        DWORD bytesRead = 0;
        ftStatus = FT_Read(deviceHandle, &commsController.rxByte, 1, &bytesRead);

        FT_STATUS ftStatus = FT_GetQueueStatus(deviceHandle, &rxBytes);

        if (commsController.rxByte == 0xFF) {
            commsController.rxHeaderProgress++;
            if (commsController.rxHeaderProgress == COMMS_HEADERLEN) {
                commsController.rxMetaProgress = -1;
                commsController.rxDataProgress = 0;
                commsController.gotHeader = 1;
            }
        }
        else commsController.rxHeaderProgress = 0;

        if (commsController.gotHeader == 1) {
            if (commsController.rxMetaProgress < (int)sizeof(CommsPacketMetadata)) {
                if (commsController.rxMetaProgress >= 0) ((uint8_t*)&commsController.rxPacket.meta)[commsController.rxMetaProgress] = commsController.rxByte;
                commsController.rxMetaProgress++;
            }
            else {
                if (rPacket.meta.packetType == 2) {
                    //commsController.rxDataProgress = 0;
                    //commsController.rxMetaProgress = 0;
                }

                //dataLength comes straight off the wire, in meta, and is untrusted until the CRC
                //check below confirms the whole packet is intact - nothing previously guarded
                //against it being larger than COMMS_MAXDATALEN (the actual size of dataBytes).
                //A byte stream that's even briefly misaligned - e.g. the device is already
                //mid-transmission of a packet, actively changing, at the exact moment the port
                //is opened, which is far more likely if the stylus isn't sitting still in a
                //settled/neutral pose right then, versus an idle device sending a steadier
                //stream - can hand back a bogus dataLength up to 65535. Without this guard, the
                //write below walked straight off the end of the 500-byte dataBytes buffer for
                //however many incoming bytes it took to reach that bogus length, corrupting
                //whatever memory follows it (other fields in this same struct, or other devices
                //in DeviceManager's vector) - and until enough bytes arrived to satisfy that
                //length, gotHeader stayed 1, so the parser never looked for a fresh 0xFF sync
                //either. Both are exactly the kind of unpredictable, hard-to-reproduce "just
                //hangs" symptom reported in practice, and both are fixed by the same guard: never
                //write past COMMS_MAXDATALEN, and never wait past it either - treat reaching the
                //buffer's real capacity as "this packet is done" (successfully or not) instead of
                //trusting an unverified length to say when it should be.
                bool dataLengthPlausible = commsController.rxPacket.meta.dataLength <= COMMS_MAXDATALEN;

                if (commsController.rxDataProgress < commsController.rxPacket.meta.dataLength &&
                    commsController.rxDataProgress < COMMS_MAXDATALEN) {
                    commsController.rxPacket.dataBytes[commsController.rxDataProgress] = commsController.rxByte;
                }
                commsController.rxDataProgress++;
                if (commsController.rxDataProgress >= commsController.rxPacket.meta.dataLength ||
                    commsController.rxDataProgress >= COMMS_MAXDATALEN) {
                    commsController.gotHeader = 0;
                    commsController.rxDataProgress = 0;
                    commsController.rxMetaProgress = 0;

                    if (commsController.rxPacket.meta.packetType == 2) {
                        commsController.rxDataProgress = 0;
                        commsController.rxMetaProgress = 0;
                    }

                    //Skip the CRC entirely for an implausible length - dataBytes wasn't even
                    //fully populated for it (capped at COMMS_MAXDATALEN above), so there's
                    //nothing valid to check. gotHeader is already reset above either way, so the
                    //very next 0xFF sequence in the stream re-syncs cleanly on the next call.
                    if (dataLengthPlausible &&
                        calcCrc((uint8_t*)&commsController.rxPacket.dataBytes, commsController.rxPacket.meta.dataLength) == commsController.rxPacket.meta.checksum) {
                        rPacket = commsController.rxPacket;
                        static int count = 0;
                        count++;
                        return rPacket.meta.packetType;
                    }
                }
            }
        }
    }
    return -1;
}


uint8_t SerialComms::calcCrc(uint8_t* data, uint16_t length) {
    uint16_t crc = COMMS_CRCSEED | (COMMS_CRCSEED << 8);
    uint16_t newData;
    int y = 0;
    if (length > 1000) {
        return 0;
    }

    for (int x = 0; x < length; x++) {
        newData = (data[x] & 0xFF) << y;
        crc ^= newData;
        y++;
        if (y >= 8) y = 0;
    }
    uint8_t crc8 = ((crc & 0xFF) | (crc >> 8));
    return crc8;
}


void SerialComms::sendPing() {
    CommsPacket txPacket;
    txPacket.meta.packetType = packetType_ping;
    txPacket.meta.dataLength = 1;
    txPacket.dataBytes[0] = 0;
    sendPacket(txPacket);
}