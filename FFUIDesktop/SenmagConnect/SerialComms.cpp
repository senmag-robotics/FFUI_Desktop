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
    FT_SetTimeouts(deviceHandle, 0, 0);


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
                if (commsController.rxDataProgress < commsController.rxPacket.meta.dataLength) {
                    commsController.rxPacket.dataBytes[commsController.rxDataProgress] = commsController.rxByte;
                }
                commsController.rxDataProgress++;
                if (commsController.rxDataProgress >= commsController.rxPacket.meta.dataLength) {
                    commsController.gotHeader = 0;
                    commsController.rxDataProgress = 0;
                    commsController.rxMetaProgress = 0;

                    if (commsController.rxPacket.meta.packetType == 2) {
                        commsController.rxDataProgress = 0;
                        commsController.rxMetaProgress = 0;
                    }

                    if (calcCrc((uint8_t*)&commsController.rxPacket.dataBytes, commsController.rxPacket.meta.dataLength) == commsController.rxPacket.meta.checksum) {
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