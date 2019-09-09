#pragma once

#include <cstdio>
#include <string>

class AppPlatform;

class Sram {
    FILE* sramFile = nullptr;
public:
    unsigned char* data;
    bool hasBattery = false;
    bool hasTimer = false;
    unsigned char timerData[5] = { '\0', '\0', '\0', '\0', '\0' };
    unsigned int timerMode = 0;
    unsigned int timerLatch = 0;
    unsigned int bankOffset = 0;
    unsigned char sizeEnum = '\0';
    long sizeBytes = 0L;
    unsigned char bankSelectMask = '\0';
    bool enableFlag = false;

    Sram();
    ~Sram();
    void openSramFile(std::string& romFileName, AppPlatform& appPlatform);
    void writeTimerData(unsigned int timerMode, unsigned char byte);
    void write(unsigned int address, unsigned char byte);
    unsigned char read(unsigned int address);
};
