#pragma once

#include <cstdint>

class Gbc;

class SgbModule {
    void mapVramForTrnOp(Gbc* gbc);

public:
    bool readingCommand;
    unsigned int commandBytes[7][16];
    unsigned char commandBits[8];
    unsigned int command;
    int readCommandBits;
    int readCommandBytes;
    bool freezeScreen;
    unsigned int freezeMode;
    bool multEnabled;
    unsigned int noPlayers;
    unsigned int noPacketsSent;
    unsigned int noPacketsToSend;
    unsigned int readJoypadID;
    unsigned int* chrPalettes;

    unsigned int* monoData;
    unsigned char* mappedVramForTrnOp;
    uint32_t* palettes;
    uint32_t* sysPalettes;

    void checkByte();
    void checkPackets(Gbc* ggbc);
    void colouriseFrame(uint32_t* imageData);
};
