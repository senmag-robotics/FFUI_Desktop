#pragma once

#include "CDM-v2.12.36.20-WHQL-Certified/ftd2xx.h"
#include <cstdint>
#include "commsTypes.h"
#include <mutex>
#include <iostream>



struct FTDI_DeviceContext {
    FT_HANDLE handle = nullptr;
    std::mutex mutex;
};



typedef struct {
    uint8_t			rxByte;
    uint8_t			rxHeaderProgress;
    int				rxMetaProgress;
    int16_t			rxDataProgress;
    CommsPacket 	rxPacket;
    uint8_t         gotHeader;
}CommsController;


class SerialComms {
public:
    
    int openPort(int portIndex);
    void closePort();
    int sendTargets(LibreOne_targets targets);
    int uptdateReciever(void);
    int checkUpdates();
    void sendPing();
    

    CommsPacket         rPacket;

private:

    uint8_t calcCrc(uint8_t* data, uint16_t length);
    int sendPacket(CommsPacket packet);

    


    FT_HANDLE           deviceHandle;
    CommsController     commsController;
    


};