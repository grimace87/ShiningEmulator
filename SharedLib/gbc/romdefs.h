#pragma once

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
