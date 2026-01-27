#pragma once

#include <cstdint>


#if defined(_MSC_VER)     // MSVC
#define packed_struct(name) \
        __pragma(pack(push, 1)) struct name __pragma(pack(pop))
#elif defined(__GNUC__)   // GCC/Clang
#define packed_struct(name) \
        struct __attribute__((packed, aligned(1))) name
#else
#error "Compiler not supported"
#endif


#define MESSAGE_MAXLEN	100
#define COMMS_HEADER		0xFF	//the value used to identify the start of packets
#define COMMS_HEADERLEN		4		//the number of bytes in the header
#define COMMS_MAXDATALEN	500
#define COMMS_CRCSEED		0x55

typedef enum CommsPacketType {
    packetType_ack = 0,
    packetType_ping = 1,
    packetType_settings = 2,
    packetType_status = 3,
    packetType_targets = 4,
    packetType_command = 5,
    packetType_message = 6,
    packetType_calibration = 7,

    packetType_bootloaderCmd = 100,
    packetType_firmwareMetaData = 101,
    packetType_firmwareData = 102,
    packetType_bootloaderError = 103,
};

#pragma pack(push, 1)
typedef struct{
    uint8_t     packetType;
    uint16_t    dataLength;
    uint8_t     checksum;
}CommsPacketMetadata;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct{
    float	    position[3];
    float	    orientation[4];
    uint8_t	    toolInputs;
    float 	    supplyVoltage;
    uint8_t	    toolType;
    uint8_t	    statusBits;
}LibreOne_status;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    float	    targets[3];			//3 targets, either force or position
    uint8_t     targetType;			//LibreOne_controlType
}LibreOne_targets;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint8_t 	messageType;					//of type MessageType
    uint8_t		messageSource;					//of type SystemModules
    char		message[MESSAGE_MAXLEN];
}LibreOne_message;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t    startAddress;
    uint32_t	firmwareLength;
    int32_t		firmwareCRC;		//flag set by bootloader in firmware is written successfully...
}LibreOne_FrimwareMeta;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    float       voltage;
    float       current;
}USBCPD_config;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    float		rateLimit;
    float		K;
    float		I;
    float		ILim;
    float		D;
}PidConfig;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint16_t    dataLength;
    uint32_t    targetAddress;
}FirmwarePacketMeta;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    FirmwarePacketMeta          meta;
    uint8_t                     data[256];
}FirmwarePacketData;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    char 					    devClass[20];			//read-only, set by firmware
    char 					    firmwareVersion[20];	//read-only, set by firmware
    char 					    devName[20];			//a user-assigned name for the device
    uint8_t					    debugLevel;				//the level of debug messages to display
    USBCPD_config 			    powerSupply;			//desired settings for the USBC power supply
    uint8_t					    toolType;
    float					    maxForceTarget;
    uint16_t				    controlFramerateTarget;
    uint16_t				    tooltipFramerateTarget;
    float					    inertiaComp[3];
    PidConfig				    pidConfig[3];
    float					    forceGain;

    LibreOne_FrimwareMeta	    firmwareValidation;	//a block of data used by the bootloader to validate the installed firmware

    uint8_t					    crc;
}LibreOne_settings;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct  {
    uint8_t                     header[COMMS_HEADERLEN];
    CommsPacketMetadata         meta;
    union {
        LibreOne_status         status;
        LibreOne_targets        targets;
        LibreOne_settings       settings;
        LibreOne_message        message;
        LibreOne_FrimwareMeta   firmwareMeta;
        FirmwarePacketData      firmwareData;
        uint8_t                 dataBytes[COMMS_MAXDATALEN];

    };
}CommsPacket;
#pragma pack(pop)