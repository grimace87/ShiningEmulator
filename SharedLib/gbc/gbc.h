#pragma once

#include "inputset.h"
#include "romdefs.h"
#include "framemanager.h"
#include "sram.h"
#include "sgbmodule.h"
#include "debugwindowmodule.h"

#include <cstdint>

class Gbc {
    friend class DebugUtils;

    inline unsigned int HL();
    inline uint8_t R8_HL();
    inline void W8_HL(uint8_t byte);
    inline void SETZ_ON_ZERO(uint8_t testValue);
    inline void SETZ_ON_COND(bool test);
    inline void SETH_ON_ZERO(uint8_t testValue);
    inline void SETH_ON_COND(bool test);
    inline void SETC_ON_COND(bool test);

    void executeAccumulatedClocks();
    int performOp();
    int runInvalidInstruction(uint8_t instruction);
    bool switchRunningSpeed();

    uint8_t read8(unsigned int address);
    void read16(unsigned int address, uint8_t* msb, uint8_t* lsb);
    void write8(unsigned int address, uint8_t byte);
    void write16(unsigned int address, uint8_t msb, uint8_t lsb);
    uint8_t readIO(unsigned int ioIndex);
    void writeIO(unsigned int ioIndex, uint8_t byte);
    void translatePaletteBg(unsigned int paletteData);
    void translatePaletteObj1(unsigned int paletteData);
    void translatePaletteObj2(unsigned int paletteData);
    void latchTimerData();

    // Colour palettes
    uint32_t translatedPaletteBg[4]{};
    uint32_t translatedPaletteObj[8]{};
    uint32_t sgbPaletteTranslationBg[4]{};
    uint32_t sgbPaletteTranslationObj[8]{};

    // CPU stats
    int clocksAcc;
    int cpuClockFreq;
    int gpuClockFactor;
    int gpuTimeInMode;
    int gpuMode{};
    int cpuMode;
    unsigned int cpuDividerCount;
    unsigned int cpuTimerCount{};
    unsigned cpuTimerIncTime{};
    bool cpuTimerRunning{};
    bool serialRequest{};
    bool serialIsTransferring{};
    bool serialClockIsExternal{};
    int serialTimer{};

    // RAM stats
    bool accessOam{};
    unsigned int wramBankOffset{};

    // VRAM stats
    bool accessVram{};
    unsigned int vramBankOffset{};

    // CGB stats
    uint8_t cgbBgPalData[64]{};
    unsigned int cgbBgPalIndex{};
    unsigned int cgbBgPalIncr{};
    uint8_t cgbObjPalData[64]{};
    unsigned int cgbObjPalIndex{};
    unsigned int cgbObjPalIncr{};

    // Block memory
    uint32_t* tileSet;

    // Other variables
    unsigned int lastLYCompare{};
    bool blankedScreen;
    bool needClear;

    // Line-processing functions
    void (Gbc::* readLine)(uint32_t*){};
    void readLineGb(uint32_t* frameBuffer);
    void readLineSgb(uint32_t* frameBuffer);
    void readLineCgb(uint32_t* frameBuffer);

public:
    void doWork(uint64_t timeDiffMillis, InputSet& inputs);
    FrameManager frameManager;

    // Block memory accessible by debug window
    uint8_t* rom;
    uint8_t* wram;
    uint8_t* vram;
    uint8_t* ioPorts;

    // Sprite data
    uint8_t oam[160]{};

    // Colour GB palettes
    uint32_t cgbBgPalette[32]{};
    uint32_t cgbObjPalette[32]{};

    // ROM stats
    RomProperties romProperties{};
	unsigned int bankOffset{};

    // SRAM and SGB
    Sram sram;
    SgbModule sgb{};

    // CPU registers
    uint32_t cpuPc;
    uint32_t cpuSp;
    uint8_t cpuA;
    uint8_t cpuF;
    uint8_t cpuB;
    uint8_t cpuC;
    uint8_t cpuD;
    uint8_t cpuE;
    uint8_t cpuH;
    uint8_t cpuL;
    bool cpuIme;

    // Public members
    bool isRunning;
    bool isPaused;
    InputSet keys{};
    bool keyStateChanged{};
    int clockMultiply;
    int clockDivide;
    int currentClockMultiplierCombo;

    // Constructor/deconstructor
    Gbc();
    ~Gbc();

    // Other public functions
    bool loadRom(std::string fileName, const uint8_t* data, int dataLength, AppPlatform& appPlatform);
    void reset();
    void speedUp();
    void slowDown();
};
