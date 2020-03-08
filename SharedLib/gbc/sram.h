#pragma once

#include <cstdio>
#include <string>
#include <fstream>

class AppPlatform;

class Sram {
public:
    std::fstream sramFile;
    uint8_t* data;
    bool hasBattery = false;
    bool hasTimer = false;
    unsigned char timerData[5] = { '\0', '\0', '\0', '\0', '\0' };
    uint32_t timerMode = 0;
    uint32_t timerLatch = 0;
    uint32_t bankOffset = 0;
    uint8_t sizeEnum = '\0';
    uint32_t sizeBytes = 0;
    unsigned char bankSelectMask = '\0';
    bool enableFlag = false;

    Sram();
    ~Sram();
    void openSramFile(std::string& romFileName, AppPlatform& appPlatform);
    void writeTimerData(unsigned int timerMode, unsigned char byte);
    void write(unsigned int address, unsigned char byte);
    unsigned char read(unsigned int address);
};
