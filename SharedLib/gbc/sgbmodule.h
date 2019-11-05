#pragma once

#include <cstdint>

class Gbc;

class SgbModule {
    void mapVramForTrnOp(Gbc* gbc);

public:
    bool readingCommand;
    uint32_t commandBytes[7][16];
    uint8_t commandBits[8];
    uint32_t command;
    int32_t readCommandBits;
    int32_t readCommandBytes;
    bool freezeScreen;
    uint32_t freezeMode;
    bool multEnabled;
    uint32_t noPlayers;
    uint32_t noPacketsSent;
    uint32_t noPacketsToSend;
    uint32_t readJoypadID;

    uint32_t* monoData;
    uint8_t* mappedVramForTrnOp;
    uint32_t* palettes;
    uint32_t* sysPalettes;
    uint32_t* chrPalettes;

    void checkByte();
    void checkPackets(Gbc* ggbc);
    void colouriseFrame(uint32_t* imageData);
};
