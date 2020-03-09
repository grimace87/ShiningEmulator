#pragma once

#include "inputset.h"
#include "romdefs.h"
#include "framemanager.h"
#include "sram.h"
#include "sgbmodule.h"
#include "audiounit.h"
#include "debugwindowmodule.h"

#include <cstdint>
#include <iostream>

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
    int32_t clocksAcc;
    int64_t cpuClockFreq;
    int32_t gpuClockFactor;
    int32_t gpuTimeInMode;
    uint32_t gpuMode{};
    uint32_t cpuMode;
    uint32_t cpuDividerCount;
    uint32_t cpuTimerCount{};
    uint32_t cpuTimerIncTime{};
    bool cpuTimerRunning{};
    bool serialRequest{};
    bool serialIsTransferring{};
    bool serialClockIsExternal{};
    int32_t serialTimer{};

    // RAM stats
    bool accessOam{};
    uint32_t wramBankOffset{};

    // VRAM stats
    bool accessVram{};
    uint32_t vramBankOffset{};

    // CGB stats
    uint8_t cgbBgPalData[64]{};
    uint32_t cgbBgPalIndex{};
    uint32_t cgbBgPalIncr{};
    uint8_t cgbObjPalData[64]{};
    uint32_t cgbObjPalIndex{};
    uint32_t cgbObjPalIncr{};

    // Block memory
    uint32_t* tileSet;

    // Other variables
    uint32_t lastLYCompare{};
    bool blankedScreen;
    bool needClear;

    // Line-processing functions
    void (Gbc::* readLine)(uint32_t*){};
    void readLineGb(uint32_t* frameBuffer);
    void readLineSgb(uint32_t* frameBuffer);
    void readLineCgb(uint32_t* frameBuffer);

    // Name of current loaded ROM file, in format that can be opened directly using AppPlatform
    std::string currentOpenedFile;

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
	uint32_t bankOffset{};

    // Public modules
    Sram sram;
    SgbModule sgb{};
    AudioUnit audioUnit;

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
    int64_t clockMultiply;
    int64_t clockDivide;
    int32_t currentClockMultiplierCombo;

    // Constructor/deconstructor
    Gbc();
    ~Gbc();

    // Other public functions
    bool loadRom(std::string fileName, const uint8_t* data, int dataLength, AppPlatform& appPlatform);
    std::string getLoadedFileName();
    void reset();
    void speedUp();
    void slowDown();
    void loadSaveState(std::istream& stream);
    void saveSaveState(std::ostream& stream);
};
