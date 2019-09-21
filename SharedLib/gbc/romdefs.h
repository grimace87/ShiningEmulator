#pragma once

#include <cstdint>

#define MBC_NONE 0x00U
#define MBC1     0x01U
#define MBC2     0x02U
#define MBC3     0x03U
#define MBC4     0x04U
#define MBC5     0x05U
#define MMM01    0x11U

struct RomProperties {
    bool valid;
    char title[17];
    unsigned int mbc;
    bool cgbFlag;
    bool sgbFlag;
    bool hasSram;
    bool hasRumble;
    long sizeBytes;
    uint32_t bankSelectMask;

    uint32_t mbcMode;
    uint32_t cartType;
    uint32_t checkSum;
    uint32_t sizeEnum;
};
