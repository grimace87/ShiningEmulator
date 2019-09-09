#pragma once

#define MBC_NONE 0x00
#define MBC1     0x01
#define MBC2     0x02
#define MBC3     0x03
#define MBC4     0x04
#define MBC5     0x05
#define MMM01    0x11

struct RomProperties {
    bool valid;
    char title[17];
    unsigned int mbc;
    unsigned char cgbFlag;
    unsigned char sgbFlag;
    bool hasSram;
    bool hasRumble;
    long sizeBytes;
    unsigned char bankSelectMask;

    int mbcMode;
    unsigned char cartType;
    unsigned char checkSum;
    unsigned char sizeEnum;
};
