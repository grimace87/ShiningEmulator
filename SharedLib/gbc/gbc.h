#pragma once

#include "inputset.h"
#include "romdefs.h"
#include "framemanager.h"
#include "sram.h"
#include "sgbmodule.h"
#include "debugwindowmodule.h"

#include <cstdint>

class Gbc {
    inline unsigned int HL();
    inline unsigned char R8_HL();
    inline void W8_HL(unsigned char byte);
    inline void SETZ_ON_ZERO(unsigned char testValue);
    inline void SETZ_ON_COND(bool test);
    inline void SETH_ON_ZERO(unsigned char testValue);
    inline void SETH_ON_COND(bool test);
    inline void SETC_ON_COND(bool test);

    int execute(int ticks);
    unsigned int performOp();
    static void throwException(unsigned char instruction);

    unsigned char read8(unsigned int address);
    void read16(unsigned int address, unsigned char* msb, unsigned char* lsb);
    void write8(unsigned int address, unsigned char byte);
    void write16(unsigned int address, unsigned char msb, unsigned char lsb);
    unsigned char readIO(unsigned int ioIndex);
    void writeIO(unsigned int ioIndex, unsigned char byte);
    void translatePaletteBg(unsigned int paletteData);
    void translatePaletteObj1(unsigned int paletteData);
    void translatePaletteObj2(unsigned int paletteData);
    void latchTimerData();

    // Colour palettes
    uint32_t translatedPaletteBg[4];
    uint32_t translatedPaletteObj[8];
    uint32_t sgbPaletteTranslationBg[4];
    uint32_t sgbPaletteTranslationObj[8];

    // CPU stats
    int clocksAcc;
    int clocksRun;
    int cpuClockFreq;
    int gpuClockFactor;
    int gpuTimeInMode;
    int gpuMode;
    bool cpuIme;
    bool cpuHalted;
    bool cpuStopped;
    unsigned int cpuDividerCount;
    unsigned int cpuTimerCount;
    unsigned cpuTimerIncTime;
    bool cpuTimerRunning;
    bool serialRequest;
    bool serialIsTransferring;
    bool serialClockIsExternal;
    int serialTimer;

    // RAM stats
    bool accessOam;
    unsigned int wramBankOffset;

    // VRAM stats
    bool accessVram;
    unsigned int vramBankOffset;

    // CGB stats
    unsigned char cgbBgPalData[64];
    unsigned int cgbBgPalIndex;
    unsigned int cgbBgPalIncr;
    unsigned char cgbObjPalData[64];
    unsigned int cgbObjPalIndex;
    unsigned int cgbObjPalIncr;

    // Block memory
    unsigned int* tileSet;

    // Other variables
    unsigned int lastLYCompare;
    bool blankedScreen;
    bool needClear;

    // Line-processing functions
    void (Gbc::* readLine)(uint32_t*);
    void readLineGb(uint32_t* frameBuffer);
    void readLineSgb(uint32_t* frameBuffer);
    void readLineCgb(uint32_t* frameBuffer);

public:
    void doWork(uint64_t timeDiffMillis, InputSet& inputs);
    FrameManager frameManager;

    // Block memory accessible by debug window
    unsigned char* rom;
    unsigned char* wram;
    unsigned char* vram;
    unsigned char* ioPorts;

    // Sprite data
    unsigned char oam[160];

    // Colour GB palettes
    uint32_t cgbBgPalette[32];
    uint32_t cgbObjPalette[32];

    // ROM stats
    RomProperties romProperties;
	unsigned int bankOffset;

    // SRAM and SGB
    Sram sram;
    SgbModule sgb;

    // CPU registers
    unsigned int cpuPc;
    unsigned int cpuSp;
    unsigned char cpuA;
    unsigned char cpuF;
    unsigned char cpuB;
    unsigned char cpuC;
    unsigned char cpuD;
    unsigned char cpuE;
    unsigned char cpuH;
    unsigned char cpuL;

    // Public members
    bool running;
    InputSet keys;
    bool keyStateChanged;
    int clockMultiply;
    int clockDivide;

    // Constructor/deconstructor
    Gbc();
    ~Gbc();

    // Other public functions
    bool loadRom(std::string fileName, const unsigned char* data, int dataLength, AppPlatform& appPlatform);
    void reset();
	void pause();
	void resume();
};
