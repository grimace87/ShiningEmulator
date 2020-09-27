#include "gbc.h"

#include "colourutils.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cassert>

#ifdef _WIN32
#include "debugwindowmodule.h"
extern DebugWindowModule debugger;
#endif

uint32_t stockPaletteBg[4] = { 0xffffffffU, 0xff88b0b0U, 0xff507878U, 0xff000000U };
uint32_t stockPaletteObj1[4] = { 0xffffffffU, 0xff5050f0U, 0xff2020a0U, 0xff000000U };
uint32_t stockPaletteObj2[4] = { 0xffffffffU, 0xffa0a0a0U, 0xff404040U, 0xff000000U };

// Values set while draw across a row - zero allows sprite to be drawn if OBJ priority is set, non-zero will
// possibly block sprites being drawn (depends on several factors, e.g. OBJ priority attribute as well as
// BG priority attribute in VRAM bank 1 for CGB mode)
uint32_t bgColorNumbers[160];
uint32_t bgDisplayPriorities[160];

constexpr int MULTIPLIER_ARRAY_SIZE = 21;
constexpr int CLOCK_MULTIPLIERS[MULTIPLIER_ARRAY_SIZE] = { 1,  1,  1, 1, 1,  2, 1, 4, 2, 4,  1,  5, 3, 7, 2, 5,  3, 5, 8, 12, 20 };
constexpr int CLOCK_DIVISORS[MULTIPLIER_ARRAY_SIZE] =    { 20, 12, 8, 5, 3,  5, 2, 7, 3, 5,  1,  4, 2, 4, 1, 2,  1, 1, 1, 1,  1  };

#define CPU_RUNNING   0x00U
#define CPU_HALTED    0x01U
#define CPU_STOPPED   0x02U

#define GPU_HBLANK    0x00U
#define GPU_VBLANK    0x01U
#define GPU_SCAN_OAM  0x02U
#define GPU_SCAN_VRAM 0x03U

#define GB_FREQ  4194304
#define SGB_FREQ 4295454
#define GBC_FREQ 8400000

const uint8_t OFFICIAL_LOGO[48] = {
        0xceU, 0xedU, 0x66U, 0x66U, 0xccU, 0x0dU, 0x00U, 0x0bU, 0x03U, 0x73U, 0x00U, 0x83U, 0x00U, 0x0cU, 0x00U, 0x0dU,
        0x00U, 0x08U, 0x11U, 0x1fU, 0x88U, 0x89U, 0x00U, 0x0eU, 0xdcU, 0xccU, 0x6eU, 0xe6U, 0xddU, 0xddU, 0xd9U, 0x99U,
        0xbbU, 0xbbU, 0x67U, 0x63U, 0x6eU, 0x0eU, 0xecU, 0xccU, 0xddU, 0xdcU, 0x99U, 0x9fU, 0xbbU, 0xb9U, 0x33U, 0x3eU
};

Gbc::Gbc() {

    // Initialise vars, many will be overwritten when reset() is called
    cpuPc = cpuSp = 0;
    cpuA = cpuB = cpuC = cpuD = cpuE = cpuF = cpuH = cpuL = 0;
    clocksAcc = 0;
    cpuClockFreq = 1;
    cpuDividerCount = 1;
    gpuClockFactor = 1;
    gpuTimeInMode = 0;
    blankedScreen = false;
    needClear = true;
    cpuMode = CPU_RUNNING;
    gpuMode = GPU_VBLANK;

    // Flag not running and no ROM loaded
    isRunning = false;
    isPaused = false;
    romProperties.valid = false;
    clockMultiply = 1;
    clockDivide = 1;
    currentClockMultiplierCombo = 10;

    // Allocate emulated RAM
    rom.resize(256 * 16384);
    wram.resize(8 * 4096);
    vram.resize(2 * 8192);
    ioPorts.resize(256);
    tileSet = new uint32_t[2 * 384 * 8 * 8]; // 2 VRAM banks, 384 tiles, 8 rows, 8 pixels per row
    sgb.monoData = new uint32_t[160 * 152];
    sgb.mappedVramForTrnOp = new uint8_t[4096];
    sgb.palettes = new uint32_t[4 * 4];
    sgb.sysPalettes = new uint32_t[512 * 4]; // 512 palettes, 4 colours per palette, RGB
    sgb.chrPalettes = new uint32_t[18 * 20];

    currentOpenedFile = "";
}

Gbc::~Gbc() {
    // Release emulated RAM
    delete[] tileSet;
    delete[] sgb.monoData;
    delete[] sgb.mappedVramForTrnOp;
    delete[] sgb.palettes;
    delete[] sgb.sysPalettes;
    delete[] sgb.chrPalettes;
}

// Return how many clock ticks to consume when this happens,
// also perform any other behaviour deemed necessary
int Gbc::runInvalidInstruction(uint8_t instruction) {
    isPaused = true;
    return clocksAcc;
    //std::string msg = "Illegal operation - " + std::to_string((int)instruction);
    //throw new std::runtime_error(msg);
}

void Gbc::doWork(uint64_t timeDiffMillis, InputSet& inputs) {
    if (isRunning && !isPaused) {
        // Determine how many clock cycles to emulate, cap at 1000000 (about a quarter of a second)
        const int64_t adjustedFrequency = cpuClockFreq * clockMultiply / clockDivide;
        clocksAcc += (int)((double)timeDiffMillis * 0.001 * (double)adjustedFrequency);
        auto approxMultiplier = (const int32_t)(clockMultiply / clockDivide + 1);
        if (clocksAcc > (1000000 * approxMultiplier)) {
            clocksAcc = 1000000 * approxMultiplier;
        }

        // Copy inputs
        keys.keyDir = inputs.keyDir;
        keys.keyBut = inputs.keyBut;

        if (clocksAcc < 2000) {
            return;
        }

        // Execute this many clock cycles and catch errors
        //try
        //{
        executeAccumulatedClocks();
        //}
        //catch (const std::runtime_error& err)
        //{
        //	accumulatedClocks = 0;
        //	std::cerr << err.what() << std::endl;
        //}
    }
}

bool Gbc::loadRom(std::string fileName, const uint8_t* data, int dataLength, AppPlatform& appPlatform) {
    if (data == nullptr || dataLength < 32768) {
        romProperties.valid = false;
        return false;
    }

    // Read ROM header. Starts at 0x0100 with NOP and a jump (usually to 0x0150, after the header) (total 4 bytes)
    // Logo at 0x0104 (48 bytes)
    for (int n = 0; n < 48; n++) {
        if (data[n + 0x0104] != OFFICIAL_LOGO[n]) {
            //romProperties.valid = false;
            //return false;
            break;
        }
    }
    romProperties.valid = true;

    // Game title at 0x0134 (15 bytes), note last byte is colour compatibility
    memcpy(romProperties.title, data + 0x0134, 16);
    uint8_t lastByte = romProperties.title[15];
    if ((lastByte == 0x80) || (lastByte == 0xc0)) {
        romProperties.cgbFlag = true;
        romProperties.title[11] = '\0';
    } else {
        romProperties.cgbFlag = false;
        romProperties.title[16] = '\0';
    }

    // Next two bytes are licensee codes
    // Then is an SGB flag, cartridge type, and cartridge ROM and RAM sizes
    romProperties.sgbFlag = data[0x0146] == 0x03U;
    romProperties.cartType = data[0x0147];
    romProperties.sizeEnum = data[0x0148];
    sram.sizeEnum = data[0x0149];

    // Get ROM MBC type and other features based on cartridge type enum
    romProperties.hasSram = false;
    sram.hasBattery = false;
    sram.hasTimer = false;
    romProperties.hasRumble = false;
    switch (romProperties.cartType) {
        // No MBC
        case 0x00:
            romProperties.mbc = MBC_NONE;
            break;
        case 0x08:
            romProperties.mbc = MBC_NONE;
            romProperties.hasSram = true;
            break;
        case 0x09:
            romProperties.mbc = MBC_NONE;
            romProperties.hasSram = true;
            sram.hasBattery = true;
            break;

            // MBC1
        case 0x01:
            romProperties.mbc = MBC1;
            break;
        case 0x02:
            romProperties.mbc = MBC1;
            romProperties.hasSram = true;
            break;
        case 0x03:
            romProperties.mbc = MBC1;
            romProperties.hasSram = true;
            sram.hasBattery = true;
            break;

            // MBC2
        case 0x05:
            romProperties.mbc = MBC2;
            break;
        case 0x06:
            romProperties.mbc = MBC2;
            sram.hasBattery = true;
            break;

            // MMM01 - not supported
            // case 0x0b: case 0x0c: case 0x0d: romProperties.mbc = MMM01; break;

            // MBC3
        case 0x0f:
            romProperties.mbc = MBC3;
            sram.hasBattery = true;
            sram.hasTimer = true;
            break;
        case 0x10:
            romProperties.mbc = MBC3;
            romProperties.hasSram = true;
            sram.hasBattery = true;
            sram.hasTimer = true;
            break;
        case 0x11:
            romProperties.mbc = MBC3;
            break;
        case 0x12:
            romProperties.mbc = MBC3;
            romProperties.hasSram = true;
            break;
        case 0x13:
            romProperties.mbc = MBC3;
            romProperties.hasSram = true;
            sram.hasBattery = true;
            break;

            // MBC4 - not supported
            // case 0x15: case 0x16: case 0x17: romProperties.mbc = MBC4; break;

            // MBC5
        case 0x19:
            romProperties.mbc = MBC5;
            break;
        case 0x1a:
            romProperties.mbc = MBC5;
            romProperties.hasSram = true;
            break;
        case 0x1b:
            romProperties.mbc = MBC5;
            romProperties.hasSram = true;
            sram.hasBattery = true;
            break;
        case 0x1c:
            romProperties.mbc = MBC5;
            romProperties.hasRumble = true;
            break;
        case 0x1d:
            romProperties.mbc = MBC5;
            romProperties.hasSram = true;
            romProperties.hasRumble = true;
            break;
        case 0x1e:
            romProperties.mbc = MBC5;
            romProperties.hasSram = true;
            sram.hasBattery = true;
            romProperties.hasRumble = true;
            break;

        default:
            romProperties.valid = false;
            return false;
    }

    switch (romProperties.sizeEnum) {
        case 0x00:
            romProperties.sizeBytes = 32768;
            romProperties.bankSelectMask = 0x0;
            break;
        case 0x01:
            romProperties.sizeBytes = 65536;
            romProperties.bankSelectMask = 0x03;
            break;
        case 0x02:
            romProperties.sizeBytes = 131072;
            romProperties.bankSelectMask = 0x07;
            break;
        case 0x03:
            romProperties.sizeBytes = 262144;
            romProperties.bankSelectMask = 0x0fU;
            break;
        case 0x04:
            romProperties.sizeBytes = 524288;
            romProperties.bankSelectMask = 0x1f;
            break;
        case 0x05:
            romProperties.sizeBytes = 1048576;
            romProperties.bankSelectMask = 0x3f;
            break;
        case 0x06:
            romProperties.sizeBytes = 2097152;
            romProperties.bankSelectMask = 0x7fU;
            break;
        case 0x07:
            romProperties.sizeBytes = 4194304;
            romProperties.bankSelectMask = 0xff;
            break;
        case 0x08:
            romProperties.sizeBytes = 8388608;
            romProperties.bankSelectMask = 0x1ff;
            break;
        case 0x52:
            romProperties.sizeBytes = 1179648;
            romProperties.bankSelectMask = 0x7fU;
            break;
        case 0x53:
            romProperties.sizeBytes = 1310720;
            romProperties.bankSelectMask = 0x7fU;
            break;
        case 0x54:
            romProperties.sizeBytes = 1572864;
            romProperties.bankSelectMask = 0x7fU;
            break;
        default:
            romProperties.valid = false;
            return false;
    }

    switch (sram.sizeEnum) {
        case 0x00:
            if (romProperties.mbc == MBC2) {
                sram.sizeBytes = 512; // MBC2 includes 512x4 bits of RAM (and still says 0x00 for RAM size)
            } else {
                sram.sizeBytes = 0;
            }
            break;
        case 0x01:
            sram.sizeBytes = 2048;
            break;
        case 0x02:
            sram.sizeBytes = 8192;
            break;
        case 0x03:
            sram.sizeBytes = 32768;
            break;
        default:
            romProperties.valid = false;
            return false;
    }

    if (sram.hasBattery) {
        sram.openSramFile(fileName, appPlatform);
    }

    // 0x4a - 0x4c contain a japanese designatioon, an old licensee number and a version number
    // 0x4d is the header checksum. Game won't work if it checks incorrectly
    romProperties.checkSum = data[0x014d];
    // Check the checksum; using bytes 0x0134 to 0x014c, apply x = x - mem[addr] - 1 (starting x = 0)
    unsigned int sum = 0;
    for (int n = 0; n < 25; n++) {
        uint8_t testByte = data[0x0134 + n];
        sum = sum - (unsigned int)testByte - 1;
    }
    if (sum % 256 != (unsigned int)romProperties.checkSum) {
        //romProperties.valid = false;
        //return false;
    }

    // Bytes 0x4e and 0x4f are a global checksum (all bytes in ROM except these two). Is never checked.
    // Load ROM data into memory.
    if (romProperties.sizeBytes > dataLength) {
        romProperties.valid = false;
        return false;
    }
    if (dataLength > 4194304) {
        dataLength = 4194304;
    }
    memcpy(rom.data(), data, dataLength);

    // Remember this file to re-open next time
    //saveLastFileName(FileName, 512);

    currentOpenedFile = fileName;

    return true;
}

std::string Gbc::getLoadedFileName() {
    return currentOpenedFile;
}

void Gbc::reset() {
    // Make sure a valid ROM is loaded
    if (!romProperties.valid) {
        isRunning = false;
        return;
    }

    // Reset clock multipliers
    currentClockMultiplierCombo = 10;
    clockMultiply = 1;
    clockDivide = 1;

    // Initialise control variables
    cpuIme = false;
    cpuMode = CPU_RUNNING;
    gpuMode = GPU_SCAN_OAM;
    gpuTimeInMode = 0;
    keys.clear();
    keyStateChanged = false;
    accessOam = true;
    accessVram = true;
    serialRequest = false;
    serialClockIsExternal = false;
    needClear = false;

#ifdef _WIN32
	debugger.setBreakCode(DebugWindowModule::BreakCode::NONE);
#endif

    // Clear graphic caches
    std::fill(tileSet, tileSet + 2 * 384 * 8 * 8, 0);
    std::fill(sgb.monoData, sgb.monoData + 160 * 152, 0);
    std::fill(sgb.palettes, sgb.palettes + 4 * 4, 0);
    std::fill(sgb.sysPalettes, sgb.sysPalettes + 512 * 4, 0);
    std::fill(sgb.chrPalettes, sgb.chrPalettes + 18 * 20, 0);

    // Resetting IO ports may avoid graphical glitches when switching to a colour game. Clearing VRAM may help too.
    std::fill(ioPorts.data(), ioPorts.data() + 256, 0);
    std::fill(vram.data(), vram.data() + 16384, 0);

    // Initialise emulated memory, registers, IO
    bankOffset = 0x4000;
    wramBankOffset = 0x1000;
    vramBankOffset = 0x0000;
    cpuPc = 0x0100;
    cpuSp = 0xfffe;
    cpuF = 0xb0;
    cpuB = 0x00;
    cpuC = 0x13;
    cpuD = 0x00;
    cpuE = 0xd8;
    cpuH = 0x01U;
    cpuL = 0x4d;
    ioPorts[5] = 0x00;
    ioPorts[6] = 0x00;
    ioPorts[7] = 0x00;
    ioPorts[16] = 0x80U;
    ioPorts[17] = 0xbf;
    ioPorts[18] = 0xf3;
    ioPorts[20] = 0xbf;
    ioPorts[22] = 0x3f;
    ioPorts[23] = 0x00;
    ioPorts[25] = 0xbf;
    ioPorts[26] = 0x7fU;
    ioPorts[27] = 0xff;
    ioPorts[28] = 0x9f;
    ioPorts[30] = 0xbf;
    ioPorts[32] = 0xff;
    ioPorts[33] = 0x00;
    ioPorts[34] = 0x00;
    ioPorts[35] = 0xbf;
    ioPorts[36] = 0x77;
    ioPorts[37] = 0xf3;
    ioPorts[64] = 0x91;
    ioPorts[66] = 0x00;
    ioPorts[67] = 0x00;
    ioPorts[69] = 0x00;
    ioPorts[71] = 0xfc;
    ioPorts[72] = 0xff;
    ioPorts[73] = 0xff;
    ioPorts[74] = 0x00;
    ioPorts[75] = 0x00;
    ioPorts[85] = 0xff;
    ioPorts[255] = 0x00;

    // Set things specific to the type of emulated device
    if (romProperties.cgbFlag) {
        romProperties.sgbFlag = false;
        cpuClockFreq = GB_FREQ;
        gpuClockFactor = 1;
        readLine = &Gbc::readLineCgb;
        cpuA = 0x11;
        ioPorts[38] = 0xf1;
        sgb.freezeScreen = false;
        sgb.multEnabled = false;
    } else if (romProperties.sgbFlag) {
        romProperties.cgbFlag = false;
        cpuClockFreq = SGB_FREQ;
        gpuClockFactor = 1;
        readLine = &Gbc::readLineSgb;
        cpuA = 0x01U;
        ioPorts[38] = 0xf0;
        sgb.readingCommand = false;
        sgb.freezeScreen = false;
        sgb.multEnabled = false;
        sgb.noPacketsSent = 0;
        sgb.noPacketsToSend = 0;
        sgb.readJoypadID = 0x0c;
        cgbBgPalIndex = 0;
        cgbObjPalIndex = 0;
    } else {
        romProperties.sgbFlag = false;
        romProperties.cgbFlag = false;
        cpuClockFreq = GB_FREQ;
        gpuClockFactor = 1;
        readLine = &Gbc::readLineGb;
        cpuA = 0x01U;
        ioPorts[38] = 0xf1;
        sgb.freezeScreen = false;
        sgb.multEnabled = false;
    }

    // Other stuff:
    audioUnit.reset(ioPorts.data(), cpuClockFreq);
    serialTimer = 0;
    cpuDividerCount = 0;
    cpuTimerCount = 0;
    cpuTimerIncTime = 1024;
    cpuTimerRunning = false;
    romProperties.mbcMode = 0;
    sram.enableFlag = false;
    sram.timerMode = 0x00;
    sram.timerLatch = 0x00;
    lastLYCompare = 1; // Will prevent LYC causing interrupts immediately
    blankedScreen = false;

    isRunning = true;
    isPaused = false;
}

// Run emulation - accept number of clocks needing to run as an argument,
// return how many were actually consumed
void Gbc::executeAccumulatedClocks() {

    // Handle key state if required
    if (keyStateChanged) {
        // Adjust value in keypad register
        if ((ioPorts[0x00] & 0x30U) == 0x20) {
            ioPorts[0x00] &= 0xf0U;
            ioPorts[0x00] |= keys.keyDir;
        } else if ((ioPorts[0x00] & 0x30U) == 0x10U) {
            ioPorts[0x00] &= 0xf0U;
            ioPorts[0x00] |= keys.keyBut;
        }

        // Set interrupt request flag
        ioPorts[0x0f] |= 0x10U;
        keyStateChanged = false;
    }

    while (clocksAcc > 0) {
#ifdef _WIN32
        if (debugger.breakCodeIsSet()) {
            isPaused = true;
            break;
        }
#endif

        // Run appropriate opcode; returns how many clocks it consumes
        int clocksPassedByInstruction = performOp();
        cpuPc &= 0xffffU; // Clamp PC to 16 bits
        clocksAcc -= clocksPassedByInstruction;

        // Check for interrupts:
        const bool cpuHalted = cpuMode == CPU_HALTED;
        if (cpuIme || cpuHalted) {
            uint8_t triggeredInterrupts = ioPorts[0xff] & ioPorts[0x0f] & 0x1fU;
            if (triggeredInterrupts) {
                uint32_t toAddress = cpuPc;
                if (triggeredInterrupts & 0x01U) {
                    // VBlank
                    ioPorts[0x0f] &= 0x1eU;
                    toAddress = 0x0040;
                } else if (triggeredInterrupts & 0x02U) {
                    // LCD Stat
                    ioPorts[0x0f] &= 0x1dU;
                    toAddress = 0x0048;
                } else if (triggeredInterrupts & 0x04U) {
                    // Timer
                    ioPorts[0x0f] &= 0x1bU;
                    toAddress = 0x0050;
                } else if (triggeredInterrupts & 0x08U) {
                    // Serial
                    ioPorts[0x0f] &= 0x17U;
                    toAddress = 0x0058;
                } else if (triggeredInterrupts & 0x10U) {
                    // Joypad
                    ioPorts[0x0f] &= 0x0fU;
                    toAddress = 0x0060;
                }

                // Unless halted with IME unset, push PC onto stack and go to interrupt handler address
                if (!cpuHalted || cpuIme) {
                    cpuSp -= 2;
                    write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
                    cpuPc = toAddress;
                }
                cpuMode = CPU_RUNNING;
                cpuIme = false;
            }
        }

#ifdef _WIN32
        if (debugger.breakOnPc) {
            if (cpuPc == debugger.breakPcAddr) {
                debugger.setBreakCode(DebugWindowModule::BreakCode::REACHED_ADDRESS);
            }
        }
#endif
        // While CPU is in stop mode, nothing much still runs
        if (cpuMode == CPU_STOPPED) {
            if (switchRunningSpeed()) {
                clocksAcc -= 131072;
                cpuMode = CPU_RUNNING;
            }
            continue;
        }

        bool displayEnabled = ioPorts[0x0040] & 0x80U;

        // Permanent compare of LY and LYC
        if ((ioPorts[0x44] == ioPorts[0x45]) && displayEnabled) {
            ioPorts[0x41] |= 0x04U; // Set coincidence flag
            // Request interrupt if this signal goes low to high
            if (((ioPorts[0x41] & 0x40U) != 0x00) && (lastLYCompare == 0)) {
                ioPorts[0x0f] |= 0x02U;
            }
            lastLYCompare = 1;
        } else {
            ioPorts[0x41] &= 0xfbU; // Clear coincidence flag
            lastLYCompare = 0;
        }

        // Handling of timers
        cpuDividerCount += clocksPassedByInstruction;
        if (cpuDividerCount >= 256) {
            cpuDividerCount -= 256;
            ioPorts[0x04]++;
        }
        if (cpuTimerRunning != 0) {
            cpuTimerCount++;
            if (cpuTimerCount >= cpuTimerIncTime) {
                cpuTimerCount -= cpuTimerIncTime;
                ioPorts[0x05]++;
                if (ioPorts[0x05] == 0x00) {
                    ioPorts[0x05] = ioPorts[0x06];
                    ioPorts[0x0f] |= 0x04U;
                }
            }
        }

        // Handle audio
        audioUnit.simulate(clocksPassedByInstruction / gpuClockFactor);

        // Handle serial port timeout
        if (serialIsTransferring) {
            if (!serialClockIsExternal) {
                serialTimer -= clocksPassedByInstruction;
                if (serialTimer <= 0) {
                    serialIsTransferring = false;
                    ioPorts[0x02] &= 0x03U; // Clear the transferring indicator
                    ioPorts[0x0f] |= 0x08U; // Request a serial interrupt
                    ioPorts[1] = 0xffU;
                }
            } else {
                if (serialTimer == 1) {
                    serialTimer = 0;
                }
            }
        }

        // Handle GPU timings
        if (displayEnabled) {
            // Double CPU speed mode affects instruction rate but should not affect GPU speed
            gpuTimeInMode += clocksPassedByInstruction / gpuClockFactor;
            switch (gpuMode) {
                case GPU_HBLANK:
                    // Spends 204 cycles here, then moves to next line. After 144th hblank, move to vblank.
                    if (gpuTimeInMode >= 204) {
                        gpuTimeInMode -= 204;
                        ioPorts[0x0044]++;
                        if (ioPorts[0x0044] == 144) {
                            gpuMode = GPU_VBLANK;
                            ioPorts[0x0041] &= 0xfcU;
                            ioPorts[0x0041] |= GPU_VBLANK;
                            // Set interrupt request for VBLANK
                            ioPorts[0x000f] |= 0x01U;
                            accessOam = true;
                            accessVram = true;
                            if ((ioPorts[0x0041] & 0x10U) != 0x00) {
                                // Request status int if condition met
                                ioPorts[0x000f] |= 0x02U;
                            }
                            // This is where stuff can be drawn - on the beginning of the vblank
                            if (!sgb.freezeScreen) {
                                if (frameManager.frameIsInProgress()) {
                                    auto frameBuffer = frameManager.getInProgressFrameBuffer();
                                    if ((frameBuffer != nullptr) && romProperties.sgbFlag) {
                                        sgb.colouriseFrame(frameBuffer);
                                    }
                                    frameManager.finishCurrentFrame();
                                }
                            }
                        } else {
                            gpuMode = GPU_SCAN_OAM;
                            ioPorts[0x0041] &= 0xfcU;
                            ioPorts[0x0041] |= GPU_SCAN_OAM;
                            accessOam = false;
                            accessVram = true;
                            if (ioPorts[0x0041] & 0x20U) {
                                // Request status int if condition met
                                ioPorts[0x000f] |= 0x02U;
                            }
                        }
                    }
                    break;
                case GPU_VBLANK:
                    if (gpuTimeInMode >= 456) {
                        // 10 of these lines in vblank
                        gpuTimeInMode -= 456;
                        ioPorts[0x0044]++;
                        if (ioPorts[0x0044] >= 154) {
                            gpuMode = GPU_SCAN_OAM;
                            ioPorts[0x0041] &= 0xfcU;
                            ioPorts[0x0041] |= GPU_SCAN_OAM;
                            ioPorts[0x0044] = 0;
                            accessOam = false;
                            accessVram = true;
                            if (ioPorts[0x0041] & 0x20U) {
                                // Request status int if condition met
                                ioPorts[0x000f] |= 0x02U;
                            }

                            // LCD starting at top of frame, ready a frame buffer if available
                            frameManager.beginNewFrame();
                        }
                    }
                    break;
                case GPU_SCAN_OAM:
                    if (gpuTimeInMode >= 80) {
                        gpuTimeInMode -= 80;
                        gpuMode = GPU_SCAN_VRAM;
                        ioPorts[0x0041] &= 0xfcU;
                        ioPorts[0x0041] |= GPU_SCAN_VRAM;
                        accessOam = false;
                        accessVram = false;
                    }
                    break;
                case GPU_SCAN_VRAM:
                    if (gpuTimeInMode >= 172) {
                        gpuTimeInMode -= 172;
                        gpuMode = GPU_HBLANK;
                        ioPorts[0x0041] &= 0xfcU;
                        ioPorts[0x0041] |= GPU_HBLANK;
                        accessOam = true;
                        accessVram = true;
                        if (ioPorts[0x0041] & 0x08U) {
                            // Request status int if condition met
                            ioPorts[0x000f] |= 0x02U;
                        }
                        // Run DMA if applicable
                        if (ioPorts[0x55] < 0xff) {
                            unsigned int tempAddr, tempAddr2;
                            // H-blank DMA currently active
                            tempAddr = (ioPorts[0x51] << 8U) + ioPorts[0x52]; // DMA source
                            if ((tempAddr & 0xe000U) == 0x8000U) {
                                // Don't do transfers within VRAM
                                goto end_dma_op;
                            }
                            if (tempAddr >= 0xe000U) {
                                // Don't take source data from these addresses either
                                goto end_dma_op;
                            }
                            tempAddr2 = (ioPorts[0x53] << 8U) + ioPorts[0x54] + 0x8000U; // DMA destination
                            for (int count = 0; count < 16; count++) {
                                write8(tempAddr2, read8(tempAddr));
                                tempAddr++;
                                tempAddr2++;
                                tempAddr2 &= 0x9fffU; // Keep it within VRAM
                            }
                            end_dma_op:
                            //if (ClockFreq == GBC_FREQ) clocks_acc -= 64;
                            //else clocks_acc -= 32;
                            ioPorts[0x55]--;
                            if (ioPorts[0x55] < 0x80U) {
                                // End the DMA
                                ioPorts[0x55] = 0xffU;
                            }
                        }

                        // Process current line's graphics
                        if (frameManager.frameIsInProgress()) {
                            (*this.*readLine)(frameManager.getInProgressFrameBuffer());
                        }
                    }
                    break;
                default: // Error that should never happen:
                    isRunning = false;
                    clocksAcc = 0;
                    break;
            }
        } else {
            if (!blankedScreen) {
                accessOam = true;
                accessVram = true;
                ioPorts[0x0044] = 0;
                gpuTimeInMode = 0;
                gpuMode = GPU_SCAN_OAM;
                blankedScreen = true;

                // Mark any started frames as complete - probably won't do much
                while (frameManager.frameIsInProgress()) {
                    frameManager.finishCurrentFrame();
                }
            }
        }
    }
}

uint8_t Gbc::read8(unsigned int address) {
    address &= 0xffffU;
#ifdef _WIN32
    if (debugger.breakOnRead) {
        if (address == debugger.breakReadAddr) {
            debugger.setBreakCode(DebugWindowModule::BreakCode::READ_FROM_ADDRESS);

            // Recall read function, prevent recursive calls
            debugger.breakOnRead = false;
            debugger.breakReadByte = (unsigned int)read8(address);
            debugger.breakOnRead = true;
        }
    }
#endif

    if (address < 0x4000U) {
        return rom[address];
    } else if (address < 0x8000U) {
        return rom[bankOffset + (address & 0x3fffU)];
    } else if (address < 0xa000U) {
        if (accessVram) {
            return vram[vramBankOffset + (address & 0x1fffU)];
        } else {
            return 0xffU;
        }
    }
    else if (address < 0xc000U) {
        if (sram.enableFlag) {
            if (!sram.hasTimer) {
                return sram.read(address & 0x1fffU);
            } else {
                /*if (sram.timerMode > 0)
                    return sram.timerData[(unsigned int)sram.timerMode - 0x08];
                else */
                if (sram.bankOffset < 0x8000U) {
                    return sram.read(address & 0x1fffU);
                } else {
                    return 0;
                }
            }
        } else {
            return 0x00;
        }
    } else if (address < 0xd000U) {
        return wram[address & 0x0fffU];
    } else if (address < 0xe000U) {
        return wram[wramBankOffset + (address & 0x0fffU)];
    } else if (address < 0xf000U) {
        return wram[address & 0x0fffU];
    } else if (address < 0xfe00U) {
        return wram[wramBankOffset + (address & 0x0fffU)];
    } else if (address < 0xfea0U) {
        if (accessOam) {
            return oam[(address & 0x00ffU) % 160];
        } else {
            return 0xffU;
        }
    } else if (address < 0xff00U) {
        return 0xff;
    } else if (address < 0xff80U) {
        return readIO(address & 0x7fU);
    } else {
        return ioPorts[(address & 0xffU)];
    }
}

void Gbc::read16(unsigned int address, uint8_t* msb, uint8_t* lsb) {
    address &= 0xffffU;
    if (address < 0x4000U) {
        *msb = rom[address];
        *lsb = rom[address + 1];
        return;
    } else if (address < 0x8000U) {
        *msb = rom[bankOffset + (address & 0x3fffU)];
        *lsb = rom[bankOffset + ((address + 1) & 0x3fffU)];
        return;
    } else if (address < 0xa000U) {
        if (accessVram) {
            *msb = vram[vramBankOffset + (address & 0x1fffU)];
            *lsb = vram[vramBankOffset + ((address + 1) & 0x1fffU)];
            return;
        } else {
            *msb = 0xffU;
            *lsb = 0xffU;
            return;
        }
    } else if (address < 0xc000U) {
        if (sram.enableFlag) {
            *msb = sram.read(address);
            *lsb = sram.read(address + 1);
            return;
        } else {
            *msb = 0xffU;
            *lsb = 0xffU;
            return;
        }
    } else if (address < 0xd000U) {
        *msb = wram[address & 0x0fffU];
        *lsb = wram[(address + 1) & 0x0fffU];
        return;
    } else if (address < 0xe000U) {
        *msb = wram[wramBankOffset + (address & 0x0fffU)];
        *lsb = wram[wramBankOffset + ((address + 1) & 0x0fffU)];
        return;
    } else if (address < 0xf000U) {
        *msb = wram[address & 0x0fffU];
        *lsb = wram[(address + 1) & 0x0fffU];
        return;
    } else if (address < 0xfe00U) {
        *msb = wram[wramBankOffset + (address & 0x0fffU)];
        *lsb = wram[wramBankOffset + ((address + 1) & 0x0fffU)];
        return;
    } else if (address < 0xfea0U) {
        if (accessOam) {
            *msb = oam[(address & 0x00ffU) % 160];
            *lsb = oam[((address + 1) & 0x00ffU) % 160];
            return;
        } else {
            *msb = 0xff;
            *lsb = 0xff;
            return;
        }
    } else if (address < 0xff00) {
        *msb = 0xff;
        *lsb = 0xff;
        return;
    } else if (address < 0xff80) {
        *msb = readIO(address & 0x7fU);
        *lsb = readIO((address + 1) & 0x7fU);
        return;
    } else {
        *msb = ioPorts[address & 0xffU];
        *lsb = ioPorts[(address + 1) & 0xffU];
        return;
    }
}

void Gbc::write8(unsigned int address, uint8_t byte) {
    address &= 0xffffU;
#ifdef _WIN32
    if (debugger.breakOnWrite) {
        if (address == debugger.breakWriteAddr) {
            debugger.breakWriteByte = (unsigned int)byte;
            debugger.setBreakCode(DebugWindowModule::BreakCode::WROTE_TO_ADDRESS);
        }
    }
#endif
    if (address < 0x8000U) {
        switch (romProperties.mbc) {
            case MBC1:
                if (address < 0x2000U) {
                    // Only 4 bits are used. Writing 0xa enables SRAM
                    byte = byte & 0x0fU;
                    sram.enableFlag = byte == 0x0aU;
                    return;
                } else if (address < 0x4000U) {
                    // Set low 5 bits of bank number
                    bankOffset &= 0xfff80000U;
                    byte = byte & 0x1fU;
                    if (byte == 0x00) {
                        byte++;
                    }
                    bankOffset |= ((unsigned int)byte * 0x4000U);
                    bankOffset &= (romProperties.bankSelectMask * 0x4000U);
                    return;
                } else if (address < 0x6000U) {
                    byte &= 0x03U;
                    if (romProperties.mbcMode != 0) {
                        sram.bankOffset = (unsigned int)byte * 0x2000U; // Select RAM bank
                    } else {
                        bankOffset &= 0xffe7c000U;
                        bankOffset |= (unsigned int)byte * 0x80000U;
                        bankOffset &= (romProperties.bankSelectMask * 0x4000U);
                    }
                    return;
                } else {
                    if (sram.sizeBytes > 8192) {
                        romProperties.mbcMode = byte & 0x01U;
                    } else {
                        romProperties.mbcMode = 0;
                    }
                    return;
                }
            case MBC2:
                if (address < 0x1000U) {
                    // Only 4 bits are used. Writing 0xa enables SRAM.
                    byte = byte & 0x0fU;
                    sram.enableFlag = byte == 0x0aU;
                    return;
                } else if (address < 0x2100U) {
                    return;
                } else if (address < 0x21ffU) {
                    byte &= 0x0fU;
                    byte &= romProperties.bankSelectMask;
                    if (byte == 0) {
                        byte++;
                    }
                    bankOffset = (unsigned int)byte * 0x4000U;
                    return;
                }
                return;
            case MBC3:
                if (address < 0x2000U) {
                    byte = byte & 0x0fU;
                    sram.enableFlag = byte == 0x0aU; // Also enables timer registers
#ifdef _WIN32
                    if (sram.enableFlag) {
                        if (debugger.breakOnSramEnable) {
                            debugger.setBreakCode(DebugWindowModule::BreakCode::ENABLED_SRAM);
                        }
                    } else {
                        if (debugger.breakOnSramDisable) {
                            debugger.setBreakCode(DebugWindowModule::BreakCode::DISABLED_SRAM);
                        }
                    }
#endif
                    return;
                } else if (address < 0x4000U) {
                    byte &= romProperties.bankSelectMask;
                    if (byte == 0) {
                        byte++;
                    }
                    bankOffset = (unsigned int)byte * 0x4000U;
                    return;
                } else if (address < 0x6000U) {
                    byte &= 0x0fU;
                    if (byte < 0x04U) {
                        sram.bankOffset = (unsigned int)byte * 0x2000U;
                        sram.timerMode = 0;
                    } else if ((byte >= 0x08U) && (byte < 0x0dU)) {
                        sram.timerMode = (unsigned int)byte;
                    } else {
                        sram.timerMode = 0;
                    }
                    return;
                } else {
                    byte &= 0x01U;
                    if ((sram.timerLatch == 0x00U) && (byte == 0x01U)) {
                        latchTimerData();
                    }
                    sram.timerLatch = (unsigned int)byte;
                    return;
                }
                break;
            case MBC5:
                if (address < 0x2000U) {
                    // RAMG - 4 bits, enable external RAM by writing 0xa
                    byte = byte & 0x0fU;
                    sram.enableFlag = byte == 0x0aU;
                    return;
                } else if (address < 0x3000U) {
                    // ROMB0 - lower 8 bits of 9-bit ROM bank (note MBC5 can select bank 0 here)
                    uint32_t maskedByte = byte & romProperties.bankSelectMask;
                    bankOffset &= 0x00400000U;
                    bankOffset |= (maskedByte * 0x4000U);
                    return;
                }
                else if (address < 0x4000U) {
                    // ROMB1 - 1 bit, upper bit of 9-bit RAM bank (note MBC5 can select bank 0 here)
                    byte &= 0x01U;
                    bankOffset &= 0x003fc000U;
                    if (byte != 0x00U) {
                        bankOffset |= 0x00400000U;
                    }
                    bankOffset &= (romProperties.bankSelectMask * 0x4000U);
                    return;
                } else if (address < 0x6000U) {
                    // RAMB - 4-bit RAM bank
                    byte &= 0x0fU;
                    sram.bankOffset = (unsigned int)byte * 0x2000U;
                    return;
                }

                // Writing to 0x6000 - 0x7fff does nothing
                return;
        }
    } else if (address < 0xa000U) {
        if (accessVram) {
            // Mask to address within range 0x0000-0x1fff and write to that VRAM address
            address = address & 0x1fffU;
            vram[vramBankOffset + address] = byte;

            // Decode character set from GB format to something more computer-friendly
            if (address < 0x1800U) {

                // Get the pair of bytes just modified (i.e. one row in the character)
                // Note there are 384 characters in the map, per VRAM bank, each stored with 16 bytes
                size_t relativeVramAddress = address & 0x1ffeU;
                uint32_t byte1 = 0x000000ffU & (uint32_t)vram[vramBankOffset + relativeVramAddress];
                uint32_t byte2 = 0x000000ffU & (uint32_t)vram[vramBankOffset + relativeVramAddress + 1];

                // Find the address into decoded data to update now
                // The output format uses 64 bytes per tile rather than 16, hence input address * 4
                size_t outputAddress = relativeVramAddress * 4;
                if (vramBankOffset) {
                    outputAddress += 24576;
                }
                tileSet[outputAddress++] = ((byte2 >> 6U) & 0x02U) + (byte1 >> 7U);
                tileSet[outputAddress++] = ((byte2 >> 5U) & 0x02U) + ((byte1 >> 6U) & 0x01U);
                tileSet[outputAddress++] = ((byte2 >> 4U) & 0x02U) + ((byte1 >> 5U) & 0x01U);
                tileSet[outputAddress++] = ((byte2 >> 3U) & 0x02U) + ((byte1 >> 4U) & 0x01U);
                tileSet[outputAddress++] = ((byte2 >> 2U) & 0x02U) + ((byte1 >> 3U) & 0x01U);
                tileSet[outputAddress++] = ((byte2 >> 1U) & 0x02U) + ((byte1 >> 2U) & 0x01U);
                tileSet[outputAddress++] = (byte2 & 0x02U) + ((byte1 >> 1U) & 0x01U);
                tileSet[outputAddress] = ((byte2 << 1U) & 0x02U) + (byte1 & 0x01U);
            }
        }
    } else if (address < 0xc000U) {
        if (sram.enableFlag) {
            if (sram.hasTimer) {
                if (sram.timerMode > 0) {
                    latchTimerData();
                    sram.writeTimerData((unsigned int)sram.timerMode, byte);
                    sram.timerData[(int)(sram.timerMode - 0)] = byte;
                } else if (sram.bankOffset < 0x8000U) {
                    sram.write(address, byte);
                }
            } else {
                if (romProperties.mbc == MBC2) {
                    byte &= 0x0fU;
                }
                sram.write(address, byte);
            }
        }
    } else if (address < 0xd000U) {
        wram[address & 0x0fffU] = byte;
    } else if (address < 0xe000U) {
        wram[wramBankOffset + (address & 0x0fffU)] = byte;
    } else if (address < 0xf000U) {
        wram[address & 0x0fffU] = byte;
    } else if (address < 0xfe00U) {
        wram[wramBankOffset + (address & 0x0fffU)] = byte;
    } else if (address < 0xfea0U) {
        if (accessOam) {
            oam[(address & 0x00ffU) % 160] = byte;
        }
    } else if (address < 0xff00U) {
        return; // Unusable
    } else if (address < 0xff80U) {
        writeIO(address & 0x007fU, byte);
    } else {
        ioPorts[address & 0x00ffU] = byte;
    }

}

void Gbc::write16(unsigned int address, uint8_t msb, uint8_t lsb) {
    address &= 0xffffU;
#ifdef _WIN32
    if (debugger.breakOnWrite) {
        if (address == debugger.breakWriteAddr) {
            debugger.breakWriteByte = (unsigned int)msb;
            debugger.setBreakCode(DebugWindowModule::BreakCode::WROTE_TO_ADDRESS);
        } else if ((address + 1) == debugger.breakWriteAddr) {
            debugger.breakWriteByte = (unsigned int)lsb;
            debugger.setBreakCode(DebugWindowModule::BreakCode::WROTE_TO_ADDRESS);
        }
    }
#endif
    if (address < 0x8000U) {
        write8(address, msb);
        write8(address + 1, lsb);
    } else if (address < 0x9fffU) {
        if (accessVram) {
            vram[vramBankOffset + (address & 0x1fffU)] = msb;
            vram[vramBankOffset + ((address + 1) & 0x1fffU)] = lsb;
        }
    } else if (address < 0xbfffU) {
        if (sram.enableFlag) {
            if (romProperties.mbc == MBC2) {
                msb &= 0x0fU;
                lsb &= 0x0fU;
            }
            sram.write(address, msb);
            sram.write(address + 1, lsb);
        }
    } else if (address < 0xcfffU) {
        wram[address & 0x0fffU] = msb;
        wram[(address + 1) & 0x0fffU] = lsb;
    } else if (address < 0xdfffU) {
        wram[wramBankOffset + (address & 0x0fffU)] = msb;
        wram[wramBankOffset + ((address + 1) & 0x0fffU)] = lsb;
    } else if (address < 0xefffU) {
        wram[address & 0x0fffU] = msb;
        wram[(address + 1) & 0x0fffU] = lsb;
    } else if (address < 0xfdffU) {
        wram[wramBankOffset + (address & 0x0fffU)] = msb;
        wram[wramBankOffset + ((address + 1) & 0x0fffU)] = lsb;
    } else if (address < 0xfe9fU) {
        if (accessOam) {
            oam[(address & 0x00ffU) % 160] = msb;
            oam[(address & 0x00ffU + 1) % 160] = lsb;
        }
    } else if (address < 0xfeffU) {
        // Unusable
        return;
    } else if (address < 0xff7fU) {
        writeIO(address & 0x007fU, msb);
        writeIO((address + 1) & 0x007fU, lsb);
    } else {
        ioPorts[address & 0x00ffU] = msb;
        ioPorts[(address + 1) & 0x00ffU] = lsb;
    }
}

uint8_t Gbc::readIO(unsigned int ioIndex) {
    uint8_t byte;
    switch (ioIndex) {
        case 0x00: // Used for keypad status
            byte = ioPorts[0] & 0x30U;
            if (byte == 0x20U) {
                return keys.keyDir; // Note that only bits 0-3 are read here
            } else if (byte == 0x10U) {
                return keys.keyBut;
            } else if (sgb.multEnabled && (byte == 0x30U)) {
                return sgb.readJoypadID;
            } else {
                return 0x0fU;
            }
        case 0x01: // Serial data
            return ioPorts[1];
        case 0x02: // Serial control
            return ioPorts[2];
        case 0x11: // NR11
            return ioPorts[0x11] & 0xc0U;
        case 0x13: // NR13
            return 0;
        case 0x14: // NR14
            return ioPorts[0x14] & 0x40U;
        case 0x16: // NR21
            return ioPorts[0x16] & 0xc0U;
        case 0x18: // NR23
            return 0;
        case 0x19: // NR24
            return ioPorts[0x19] & 0x40U;
        case 0x1d: // NR33
            return 0;
        case 0x1e: // NR34
            return ioPorts[0x1e] & 0x40U;
        case 0x23: // NR44
            return ioPorts[0x23] & 0x40U;
        case 0x69: // CBG background palette data (using address set by 0xff68)
            if (romProperties.cgbFlag == 0) {
                return 0;
            }
            return cgbBgPalData[cgbBgPalIndex];
        case 0x6b: // CBG sprite palette data (using address set by 0xff6a)
            if (romProperties.cgbFlag == 0) {
                return 0;
            }
            return cgbObjPalData[cgbObjPalIndex];
        default:
            return ioPorts[ioIndex];
    }
}

void Gbc::writeIO(unsigned int ioIndex, uint8_t data) {
    uint8_t byte;
    unsigned int word, count;
    switch (ioIndex) {
        case 0x00U:
            byte = data & 0x30U;
            if (romProperties.sgbFlag) {
                if (byte == 0x00U) {
                    if (!sgb.readingCommand) {
                        // Begin command packet transfer:
                        sgb.readingCommand = true;
                        sgb.readCommandBits = 0;
                        sgb.readCommandBytes = 0;
                        sgb.noPacketsSent = 0;
                        sgb.noPacketsToSend = 1; // Will get amended later if needed
                    }
                    ioPorts[0] = byte;
                } else if (byte == 0x20U) {
                    ioPorts[0] = byte;
                    ioPorts[0] |= keys.keyDir;
                    if (sgb.readingCommand) {
                        // Transfer a '0'
                        if (sgb.readCommandBytes >= 16) {
                            sgb.noPacketsSent++;
                            sgb.readCommandBytes = 0;
                            if (sgb.noPacketsSent >= sgb.noPacketsToSend) {
                                sgb.checkPackets(this);
                                sgb.readingCommand = false;
                            }
                            break;
                        }
                        sgb.commandBits[sgb.readCommandBits] = 0;
                        sgb.readCommandBits++;
                        if (sgb.readCommandBits >= 8) {
                            sgb.checkByte();
                        }
                        if (sgb.noPacketsSent >= sgb.noPacketsToSend) {
                            sgb.checkPackets(this);
                            sgb.readingCommand = false;
                            sgb.noPacketsSent = 0;
                            sgb.noPacketsToSend = 0;
                        }
                    }
                } else if (byte == 0x10U) {
                    ioPorts[0] = byte;
                    ioPorts[0] |= keys.keyBut;
                    if (sgb.readingCommand) {
                        // Transfer a '1'
                        if (sgb.readCommandBytes >= 16) {
                            // Error in transmission - 1 at end of packet
                            sgb.readingCommand = false;
                            break;
                        }
                        sgb.commandBits[sgb.readCommandBits] = 1;
                        sgb.readCommandBits++;
                        if (sgb.readCommandBits >= 8) {
                            sgb.checkByte();
                        }
                    }
                } else if ((sgb.multEnabled != 0x00U) && (!sgb.readingCommand)) {
                    if (ioPorts[0] < 0x30U) {
                        sgb.readJoypadID--;
                        if (sgb.readJoypadID < 0x0cU) sgb.readJoypadID = 0x0fU;
                    }
                    ioPorts[0] = byte;
                } else {
                    ioPorts[0] = byte;
                }
            } else {
                ioPorts[0] = byte;
                if (byte == 0x20U) {
                    ioPorts[0] |= keys.keyDir;
                } else if (byte == 0x10U) {
                    ioPorts[0] |= keys.keyBut;
                }
            }
            return;
        case 0x01: // Serial data
            byte = data;
            if (!serialIsTransferring || serialClockIsExternal) {
                ioPorts[1] = data;
            }
            return;
        case 0x02: // Serial transfer control
            if ((data & 0x80U) != 0x00U) {
                byte = data;
                ioPorts[2] = data & 0x83U;
                serialIsTransferring = true;
                if ((data & 0x01U) != 0) {
                    // Attempt to send a byte
                    serialClockIsExternal = false;
                    serialTimer = 512 * 1;
                    if (romProperties.cgbFlag == 0x00U) {
                        ioPorts[2] |= 0x02U;
                    } else if ((data & 0x02U) != 0x00U) {
                        serialTimer /= 32;
                    }
                } else {
                    // Listen for a transfer
                    serialClockIsExternal = true;
                    serialTimer = 1;
                }
            } else {
                ioPorts[2] = data & 0x83U;
                serialIsTransferring = false;
                serialRequest = false;
            }
            return;
        case 0x04: // Divider register (writing resets to 0)
            ioPorts[0x04] = 0x00U;
            return;
        case 0x07: // Timer control
            cpuTimerRunning = data & 0x04U;
            switch (data & 0x03U) {
                case 0:
                    cpuTimerIncTime = 1024;
                    break;
                case 1:
                    cpuTimerIncTime = 16;
                    break;
                case 2:
                    cpuTimerIncTime = 64;
                    break;
                case 3:
                    cpuTimerIncTime = 256;
                    break;
            }
            ioPorts[0x07] = data & 0x07U;
            return;
        case 0x10:
            ioPorts[0x10] = data & 0x7fU;
            return;
        case 0x14: // NR14 (audio channel 1 initialisation)
            ioPorts[0x14] = data & 0xc7U;
            audioUnit.restartChannel1();
            return;
        case 0x19: // NR24 (audio channel 2 initialisation)
            ioPorts[0x19] = data & 0xc7U;
            audioUnit.restartChannel2();
            return;
        case 0x1a:
            byte = data & 0x80U;
            ioPorts[0x1a] = byte;
            if (byte == 0x00U) {
                audioUnit.stopChannel3();
            }
            return;
        case 0x1c:
            ioPorts[0x1c] = data & 0x60U;
            return;
        case 0x1e: // NR34 (audio channel 3 initialisation)
            ioPorts[0x1e] = data & 0xc7U;
            audioUnit.restartChannel3();
            return;
        case 0x20:
            ioPorts[0x20] = data & 0x3fU;
            return;
        case 0x23: // NR44 (audio channel 4 initialisation)
            ioPorts[0x23] = data & 0xc0U;
            audioUnit.restartChannel4();
            return;
        case 0x24: // NR50 (Vin audio enable and volume - not emulated)
            ioPorts[0x24] = data;
            return;
        case 0x25: // NR51 (channel routing to output)
            ioPorts[0x25] = data;
            audioUnit.updateRoutingMasks();
            return;
        case 0x26: // NR52 (sound on/off)
            byte = data & 0x80U;
            if (byte == 0) {
                ioPorts[0x26] = 0;
                audioUnit.stopAllSound();
            } else {
                ioPorts[0x26] = (ioPorts[0x26] & 0x0fU) | byte;
                audioUnit.reenableAudio();
            }
            return;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x3a:
        case 0x3b:
        case 0x3c:
        case 0x3d:
        case 0x3e:
        case 0x3f:
            ioPorts[ioIndex] = data;
            audioUnit.updateWaveformData(ioIndex);
            return;
        case 0x40: // LCD ctrl
            if (data < 128) {
                accessVram = true;
                accessOam = true;
                blankedScreen = false;
                ioPorts[0x41] &= 0xfcU;
                ioPorts[0x44] = 0;
                if (ioPorts[0x40] >= 0x80U) {
                    // Try to clear the screen. Be prepared to wait because frame rate is irrelevant when LCD is disabled.
                    if (!frameManager.frameIsInProgress()) {
                        uint32_t* frameBuffer = frameManager.beginNewFrame();
                        if (frameBuffer == nullptr) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                            frameBuffer = frameManager.beginNewFrame();
                        }

                        // If a frame was obtained, clear it to black and flag it for rendering
                        if (frameBuffer) {
                            for (ioIndex = 0; ioIndex < 160 * 144; ioIndex++) {
                                frameBuffer[ioIndex] = 0x000000ffU;
                            }
                            frameManager.finishCurrentFrame();
                        }
                    }
                }
            } else {
                if (ioPorts[0x40] < 0x80) {
                    // LCD being enabled, attempt to reserve a frame buffer for drawing
                    if (!frameManager.frameIsInProgress()) {
                        frameManager.beginNewFrame();
                    }
                }
            }
            ioPorts[0x40] = data;
            return;
        case 0x41: // LCD status
            ioPorts[0x41] &= 0x07U; // Bits 0-2 are read-only. Bit 7 doesn't exist.
            ioPorts[0x41] |= (data & 0x78U);
            return;
        case 0x44: // LCD Line No (read-only)
            return;
        case 0x46: // Launch OAM DMA transfer
            ioPorts[0x46] = data;
            if (data < 0x80U) {
                return; // Cannot copy from ROM in this way
            }
            word = ((unsigned int)data) << 8U;
            for (count = 0; count < 160; count++) {
                oam[count] = read8(word);
                word++;
            }
            return;
        case 0x47: // Mono palette
            ioPorts[0x47] = data;
            translatePaletteBg((unsigned int)data);
            return;
        case 0x48: // Mono palette
            ioPorts[0x48] = data;
            translatePaletteObj1((unsigned int)data);
            return;
        case 0x49: // Mono palette
            ioPorts[0x49] = data;
            translatePaletteObj2((unsigned int)data);
            return;
        case 0x4d: // KEY1 (changes clock speed)
            if (!romProperties.cgbFlag) {
                // Only works for GBC
                return;
            }
            if (data & 0x01U) {
                ioPorts[0x4d] |= 0x01U;
            } else {
                ioPorts[0x4d] &= 0x80U;
            }
            return;
        case 0x4f: // VRAM bank
            if (romProperties.cgbFlag) {
                ioPorts[0x4f] = data & 0x01U; // 1-bit register
                vramBankOffset = (unsigned int)(data & 0x01U) * 0x2000U;
            }
            return;
        case 0x51: // HDMA1
            ioPorts[0x51] = data;
            return;
        case 0x52: // HDMA2
            ioPorts[0x52] = data & 0xf0U;
            return;
        case 0x53: // HDMA3
            ioPorts[0x53] = data & 0x1fU;
            return;
        case 0x54: // HDMA4
            ioPorts[0x54] = data;
            return;
        case 0x55: // HDMA5 (initiates DMA transfer from ROM or RAM to VRAM)
            if (!romProperties.cgbFlag) {
                return;
            }
            if ((data & 0x80U) == 0x00U) {
                // General purpose DMA
                if (ioPorts[0x55] != 0xff) {
                    // H-blank DMA already running
                    ioPorts[0x55] = data; // Can be used to halt H-blank DMA
                    return;
                }
                word = (ioPorts[0x51] << 8U) + ioPorts[0x52]; // DMA source
                if ((word & 0xe000U) == 0x8000U) {
                    // Don't do transfers within VRAM
                    return;
                }
                if (word >= 0xe000U) {
                    // Don't take source data from these addresses either
                    return;
                }
                unsigned int word2 = (ioPorts[0x53] << 8U) + ioPorts[0x54] + 0x8000U; // DMA destination
                unsigned int bytesToTransfer = data & 0x7fU;
                bytesToTransfer++;
                bytesToTransfer *= 16;
                for (count = 0; count < bytesToTransfer; count++) {
                    write8(word2, read8(word));
                    word++;
                    word2++;
                    word2 &= 0x9fffU; // Keep it within VRAM
                }
                //if (ClockFreq == GBC_FREQ) clocks_acc -= BytesToTransfer * 4;
                //else clocks_acc -= BytesToTransfer * 2;
                ioPorts[0x55] = 0xffU;
            } else {
                // H-blank DMA
                ioPorts[0x55] = data;
            }
            return;
        case 0x56: // Infrared
            ioPorts[0x56] = (data & 0xc1U) | 0x02U; // Setting bit 2 indicates 'no light received'
            return;
        case 0x68: // CGB background palette index
            ioPorts[0x68] = data & 0xbfU; // There is no bit 6
            cgbBgPalIndex = data & 0x3fU;
            if (data & 0x80U) {
                cgbBgPalIncr = 1;
            } else {
                cgbBgPalIncr = 0;
            }
            return;
        case 0x69: // CBG background palette data (using address set by 0xff68)
            cgbBgPalData[cgbBgPalIndex] = data;
            cgbBgPalette[cgbBgPalIndex >> 1U] = REMAP_555_8888((unsigned int)cgbBgPalData[cgbBgPalIndex & 0xfeU], (unsigned int)cgbBgPalData[cgbBgPalIndex | 0x01U]);
            if (cgbBgPalIncr) {
                cgbBgPalIndex++;
                cgbBgPalIndex &= 0x3fU;
                ioPorts[0x68]++;
                ioPorts[0x68] &= 0xbfU;
            }
            return;
        case 0x6a: // CGB sprite palette index
            ioPorts[0x6a] = data & 0xbfU; // There is no bit 6
            cgbObjPalIndex = data & 0x3fU;
            if (data & 0x80U) {
                cgbObjPalIncr = 1;
            } else {
                cgbObjPalIncr = 0;
            }
            return;
        case 0x6b: // CBG sprite palette data (using address set by 0xff6a)
            cgbObjPalData[cgbObjPalIndex] = data;
            cgbObjPalette[cgbObjPalIndex >> 1U] = REMAP_555_8888((unsigned int)cgbObjPalData[cgbObjPalIndex & 0xfeU], (unsigned int)cgbObjPalData[cgbObjPalIndex | 0x01U]);
            if (cgbObjPalIncr) {
                cgbObjPalIndex++;
                cgbObjPalIndex &= 0x3fU;
                ioPorts[0x6a]++;
                ioPorts[0x6a] &= 0xbfU;
            }
            return;
        case 0x70: // WRAM bank
            if (!romProperties.cgbFlag) {
                return;
            }
            data &= 0x07U;
            data = data != 0 ? data : 1;
            wramBankOffset = (unsigned int)data * 0x1000U;
            ioPorts[0x70] = data;
            return;
        default:
            ioPorts[ioIndex] = data;
            return;
    }
}

void Gbc::latchTimerData() {

}

bool Gbc::switchRunningSpeed() {
    bool speedChangeRequested = romProperties.cgbFlag && (ioPorts[0x4d] & 0x01U);
    if (speedChangeRequested) {
        // Speed change was requested in CGB mode
        ioPorts[0x4d] &= 0x80U;
        if (ioPorts[0x4d] == 0x00) {
            ioPorts[0x4d] = 0x80U;
            cpuClockFreq = GBC_FREQ;
            gpuClockFactor = 2;
        } else {
            ioPorts[0x4d] = 0x00;
            cpuClockFreq = GB_FREQ;
            gpuClockFactor = 1;
        }
    }
    return speedChangeRequested;
}
















void Gbc::translatePaletteBg(unsigned int paletteData) {
    translatedPaletteBg[0] = stockPaletteBg[paletteData & 0x03U];
    translatedPaletteBg[1] = stockPaletteBg[(paletteData & 0x0cU) / 4];
    translatedPaletteBg[2] = stockPaletteBg[(paletteData & 0x30U) / 16];
    translatedPaletteBg[3] = stockPaletteBg[(paletteData & 0xc0U) / 64];
    sgbPaletteTranslationBg[0] = paletteData & 0x03U;
    sgbPaletteTranslationBg[1] = (paletteData & 0x0cU) / 4;
    sgbPaletteTranslationBg[2] = (paletteData & 0x30U) / 16;
    sgbPaletteTranslationBg[3] = (paletteData & 0xc0U) / 64;
}

void Gbc::translatePaletteObj1(unsigned int paletteData) {
    translatedPaletteObj[1] = stockPaletteObj1[(paletteData & 0x0cU) / 4];
    translatedPaletteObj[2] = stockPaletteObj1[(paletteData & 0x30U) / 16];
    translatedPaletteObj[3] = stockPaletteObj1[(paletteData & 0xc0U) / 64];
    sgbPaletteTranslationObj[1] = (paletteData & 0x0cU) / 4;
    sgbPaletteTranslationObj[2] = (paletteData & 0x30U) / 16;
    sgbPaletteTranslationObj[3] = (paletteData & 0xc0U) / 64;
}

void Gbc::translatePaletteObj2(unsigned int paletteData) {
    translatedPaletteObj[5] = stockPaletteObj2[(paletteData & 0x0cU) / 4];
    translatedPaletteObj[6] = stockPaletteObj2[(paletteData & 0x30U) / 16];
    translatedPaletteObj[7] = stockPaletteObj2[(paletteData & 0xc0U) / 64];
    sgbPaletteTranslationObj[5] = (paletteData & 0x0cU) / 4;
    sgbPaletteTranslationObj[6] = (paletteData & 0x30U) / 16;
    sgbPaletteTranslationObj[7] = (paletteData & 0xc0U) / 64;
}

void Gbc::readLineGb(uint32_t* frameBuffer) {
    // Get relevant parameters from status registers and such:
    const uint8_t lcdCtrl = ioPorts[0x40];
    uint8_t scrY = ioPorts[0x42];
    uint8_t scrX = ioPorts[0x43];
    const unsigned int lineNo = ioPorts[0x44];
    unsigned int tileSetIndexOffset = lcdCtrl & 0x10U ? 0x0000 : 0x0080;
    unsigned int tileSetIndexInverter = lcdCtrl & 0x10U ? 0x0000 : 0x0080;
    unsigned int tileMapBase = lcdCtrl & 0x08U ? 0x1c00 : 0x1800;

    // More variables
    unsigned int offset, max;
    unsigned int pixX, pixY, tileX, tileY;
    unsigned int pixelNo = 0;
    uint32_t* dstPointer;
    const uint32_t* tileSetPointer;

    // Sprite-specific stuff:
    unsigned int paletteOffset;
    unsigned int getPix;

    // Check if LCD is disabled or all elements (BG, window, sprites) are disabled (write a black row if that's the case):
    if ((lcdCtrl & 0x80U) == 0x00U || (lcdCtrl & 0x23U) == 0x00U) {
        dstPointer = &frameBuffer[lineNo * 160];
        for (pixX = 0; pixX < 160; pixX++) {
            *dstPointer++ = 0x000000ffU;
        }
        return;
    }

    // Draw background if enabled
    if (lcdCtrl & 0x01U) {
        // Set point to draw to
        dstPointer = &frameBuffer[160 * lineNo];

        // Set starting point of VRAM data to read, in terms of pixel coordinates within the tile, and tile coordinates within the tilemap
        pixX = scrX % 8;
        pixY = (lineNo + scrY) % 8;
        tileX = scrX / 8;
        tileY = ((lineNo + scrY) % 256) / 8;

        // Draw first 20 tiles (including partial leftmost tile)
        for (offset = 0; offset < 20; offset++) {
            // Get tile no. and point to the tileset data to read
            unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile
            while (pixX < 8) {
                uint32_t colourIndex = *tileSetPointer++;
                *dstPointer++ = translatedPaletteBg[colourIndex];
                bgColorNumbers[pixelNo++] = colourIndex; // Draw sprites where BG wrote 0 or OBJ has priority
                pixX++;
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial 21st tile. Find out how many pixels of it to draw.
        max = scrX % 8;

        // Get tile no
        unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
        tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

        // Draw up to 8 pixels of this tile
        while (pixX < max) {
            uint32_t colourIndex = *tileSetPointer++;
            *dstPointer++ = translatedPaletteBg[colourIndex];
            bgColorNumbers[pixelNo++] = colourIndex; // Draw sprites where BG wrote 0 or OBJ has priority
            pixX++;
        }
    }

    // Draw window if enabled and on-screen
    scrX = ioPorts[0x4b];
    scrY = ioPorts[0x4a];
    tileMapBase = lcdCtrl & 0x40U ? 0x1c00U : 0x1800U;
    pixelNo = 0;
    if (((lcdCtrl & 0x20U) != 0x00U) && (scrX < 167) && (scrY <= lineNo)) {
        // Subtract 7 from window X pos
        if (scrX > 6) {
            scrX -= 7;
        }

        // Set point to draw to
        dstPointer = &frameBuffer[160 * lineNo + scrX];

        // Set starting point of VRAM data to read, in terms of pixel coordinates within the tile, and tile coordinates within the tilemap
        pixX = 0;
        pixY = (lineNo - scrY) % 8;
        tileX = 0;
        tileY = (lineNo - scrY) / 8;

        // Check how many complete tiles to draw
        max = (160 - scrX) / 8;

        // Draw all the complete tiles until the end of the line
        for (offset = 0; offset < max; offset++) {
            // Get tile no
            unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw the 8 pixels of this tile
            while (pixX < 8) {
                uint32_t colourIndex = *tileSetPointer++;
                *dstPointer++ = translatedPaletteBg[colourIndex];
                bgColorNumbers[pixelNo++] = colourIndex; // Draw sprites where BG wrote 0 or OBJ has priority
                pixX++;
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial last tile. Find out how many pixels of it to draw.
        max = (unsigned int)((168 - scrX) % 8);

        // Get tile no
        unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
        tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

        // Draw up to 8 pixels of this tile
        while (pixX < max) {
            uint32_t colourIndex = *tileSetPointer++;
            *dstPointer++ = translatedPaletteBg[colourIndex];
            bgColorNumbers[pixelNo++] = colourIndex; // Draw sprites where BG wrote 0 or OBJ has priority
            pixX++;
        }
    }

    // Draw sprites if enabled
    if (lcdCtrl & 0x02U) {
        const uint8_t largeSprites = lcdCtrl & 0x04U;

        // Draw each sprite that is visible
        for (offset = 156; offset < 160; offset -= 4) {
            // Get coords check if off-screen
            scrY = oam[offset];
            if (scrY == 0) {
                continue;
            } else if (scrY > 159) {
                continue;
            }
            scrX = oam[offset + 1];
            if (scrX == 0) {
                continue;
            } else if (scrX > 167) {
                continue;
            }

            // Check if the current scan line would pass through an 8x16 sprite
            if (lineNo + 16 < scrY) {
                continue;
            } else if (lineNo >= scrY) {
                continue;
            }
            unsigned int tileNo = oam[offset + 2];
            unsigned int spriteFlags = oam[offset + 3];

            // Check sprite size, and accordingly re-assess visibility and adjust tilemap index
            if (largeSprites) {
                if (lineNo + 8 >= scrY) {
                    tileNo |= 0x01U;
                } else {
                    tileNo &= 0xfeU;
                }
            } else if (lineNo + 8 >= scrY) {
                continue;
            }

            // Get pixel row within tile that will be drawn
            pixY = (lineNo + 16 - scrY) % 8;

            // Set which palette to draw with
            paletteOffset = spriteFlags & 0x10U ? 4 : 0;

            // Get first pixel in tile row to draw, plus how many pixels to draw, and adjust the starting point to write to in the image
            if (scrX < 8) {
                pixX = 8 - scrX;
                max = scrX;
                scrX = 0;
            } else if (scrX > 160) {
                pixX = 0;
                max = 168 - scrX;
                scrX -= 8;
            } else {
                pixX = 0;
                max = 8;
                scrX -= 8;
            }

            // Adjust X if horizontally flipping
            int tileSetPointerDirection;
            if (spriteFlags & 0x20U) {
                pixX = 7 - pixX;
                tileSetPointerDirection = -1;
            } else {
                tileSetPointerDirection = 1;
            }

            // Adjust Y if vertically flipping (inverts which of the two tiles to use also)
            if (spriteFlags & 0x40U) {
                pixY = 7 - pixY;
                if (largeSprites) {
                    tileNo ^= 0x01U;
                }
            }

            // Get priority flag
            unsigned int spriteGivesBgPriority = spriteFlags & 0x80U;

            // Set point to draw to
            dstPointer = &frameBuffer[160 * lineNo + scrX];
            pixelNo = scrX;

            // Get pointer to tile data
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile (skipping over transparent pixels with palette index 0, or if obscured by the background)
            pixX = 0;
            while (pixX < max) {
                getPix = *tileSetPointer;
                tileSetPointer += tileSetPointerDirection;
                if (getPix > 0) {
                    // Draw sprites where BG wrote 0 or OBJ has priority
                    if (spriteGivesBgPriority) {
                        if (bgColorNumbers[pixelNo] == 0) {
                            *dstPointer = translatedPaletteObj[getPix + paletteOffset];
                        }
                    } else {
                        *dstPointer = translatedPaletteObj[getPix + paletteOffset];
                    }
                }
                dstPointer++;
                pixX++;
                pixelNo++;
            }
        }
    }
}

void Gbc::readLineSgb(uint32_t * frameBuffer) {
    // Get relevant parameters from status registers and such:
    const uint8_t lcdCtrl = ioPorts[0x40];
    uint8_t scrY = ioPorts[0x42];
    uint8_t scrX = ioPorts[0x43];
    const unsigned int lineNo = ioPorts[0x44];
    unsigned int tileSetIndexOffset = lcdCtrl & 0x10U ? 0x0000U : 0x0080U;
    unsigned int tileSetIndexInverter = lcdCtrl & 0x10U ? 0x0000U : 0x0080U;
    unsigned int tileMapBase = lcdCtrl & 0x08U ? 0x1c00U : 0x1800U;

    // More variables
    unsigned int offset, max;
    unsigned int pixX, pixY, tileX, tileY;
    uint32_t* dstPointer;
    const uint32_t* tileSetPointer;

    // Sprite-specific stuff:
    unsigned int paletteOffset;
    unsigned int getPix;

    // Draw background if enabled
    if (lcdCtrl & 0x01U) {
        // Set point to draw to
        dstPointer = &sgb.monoData[160 * lineNo];

        // Set starting point of VRAM data to read, in terms of pixel coordinates within the tile, and tile coordinates within the tilemap
        pixX = scrX % 8;
        pixY = (lineNo + scrY) % 8;
        tileX = scrX / 8;
        tileY = ((lineNo + scrY) % 256) / 8;

        // Draw first 20 tiles (including partial leftmost tile)
        for (offset = 0; offset < 20; offset++) {
            // Get tile no. and point to the tileset data to read
            unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile
            while (pixX < 8) {
                *dstPointer++ = sgbPaletteTranslationBg[*tileSetPointer++];
                pixX++;
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial 21st tile. Find out how many pixels of it to draw.
        max = scrX % 8;

        // Get tile no
        unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
        tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

        // Draw up to 8 pixels of this tile
        while (pixX < max) {
            *dstPointer++ = sgbPaletteTranslationBg[*tileSetPointer++];
            pixX++;
        }
    }

    // Draw window if enabled and on-screen
    scrX = ioPorts[0x4b];
    scrY = ioPorts[0x4a];
    tileMapBase = lcdCtrl & 0x40U ? 0x1c00U : 0x1800U;
    if (((lcdCtrl & 0x20U) != 0x00U) && (scrX < 167) && (scrY <= lineNo)) {
        // Subtract 7 from window X pos
        if (scrX > 6) {
            scrX -= 7;
        }

        // Set point to draw to
        dstPointer = &sgb.monoData[160 * lineNo + scrX];

        // Set starting point of VRAM data to read, in terms of pixel coordinates within the tile, and tile coordinates within the tilemap
        pixX = 0;
        pixY = (lineNo - scrY) % 8;
        tileX = 0;
        tileY = (lineNo - scrY) / 8;

        // Check how many complete tiles to draw
        max = (160 - scrX) / 8;

        // Draw all the complete tiles until the end of the line
        for (offset = 0; offset < max; offset++) {
            // Get tile no
            unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw the 8 pixels of this tile
            while (pixX < 8) {
                *dstPointer++ = sgbPaletteTranslationBg[*tileSetPointer++];
                pixX++;
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;

        }

        // Draw partial last tile. Find out how many pixels of it to draw.
        max = (unsigned int)((168 - scrX) % 8);

        // Get tile no
        unsigned int tileNo = ((unsigned int)vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
        tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

        // Draw up to 8 pixels of this tile
        while (pixX < max) {
            *dstPointer++ = sgbPaletteTranslationBg[*tileSetPointer++];
            pixX++;
        }
    }

    // Draw sprites if enabled
    if (lcdCtrl & 0x02U) {
        const uint8_t largeSprites = lcdCtrl & 0x04U;

        // Draw each sprite that is visible
        for (offset = 156; offset < 160; offset -= 4) {
            // Get coords check if off-screen
            scrY = oam[offset];
            if (scrY == 0) {
                continue;
            } else if (scrY > 159) {
                continue;
            }
            scrX = oam[offset + 1];
            if (scrX == 0) {
                continue;
            } else if (scrX > 167) {
                continue;
            }

            // Check if the current scan line would pass through an 8x16 sprite
            if (lineNo + 16 < scrY) {
                continue;
            } else if (lineNo >= scrY) {
                continue;
            }
            unsigned int tileNo = oam[offset + 2];
            unsigned int spriteFlags = oam[offset + 3];

            // Check sprite size, and accordingly re-assess visibility and adjust tilemap index
            if (largeSprites) {
                if (lineNo + 8 >= scrY) {
                    tileNo |= 0x01U;
                } else {
                    tileNo &= 0xfeU;
                }
            } else if (lineNo + 8 >= scrY) {
                continue;
            }

            // Get pixel row within tile that will be drawn
            pixY = (lineNo + 16 - scrY) % 8;

            // Set which palette to draw with
            paletteOffset = spriteFlags & 0x10U ? 4 : 0;
            uint32_t colourZero = translatedPaletteBg[0];

            // Get first pixel in tile row to draw, plus how many pixels to draw, and adjust the starting point to write to in the image
            if (scrX < 8) {
                pixX = 8 - scrX;
                max = scrX;
                scrX = 0;
            } else if (scrX > 160) {
                pixX = 0;
                max = 168 - scrX;
                scrX -= 8;
            } else {
                pixX = 0;
                max = 8;
                scrX -= 8;
            }

            // Adjust X if horizontally flipping
            int tileSetPointerDirection;
            if (spriteFlags & 0x20U) {
                pixX = 7 - pixX;
                tileSetPointerDirection = -1;
            } else {
                tileSetPointerDirection = 1;
            }

            // Adjust Y if vertically flipping (inverts which of the two tiles to use also)
            if (spriteFlags & 0x40U) {
                pixY = 7 - pixY;
                if (largeSprites) {
                    tileNo ^= 0x01U;
                }
            }

            // Get priority flag
            unsigned int spriteGivesBgPriority = spriteFlags & 0x80U;

            // Set point to draw to
            dstPointer = &sgb.monoData[160 * lineNo + scrX];

            // Get pointer to tile data
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile (skipping over transparent pixels with palette index 0, or if obscured by the background)
            pixX = 0;
            while (pixX < max) {
                getPix = *tileSetPointer;
                tileSetPointer += tileSetPointerDirection;
                if (getPix > 0) {
                    if (spriteGivesBgPriority) {
                        if (*dstPointer == colourZero) {
                            *dstPointer = sgbPaletteTranslationObj[getPix + paletteOffset];
                        }
                    } else {
                        *dstPointer = sgbPaletteTranslationObj[getPix + paletteOffset];
                    }
                }
                dstPointer++;
                pixX++;
            }
        }
    }
}

void Gbc::readLineCgb(uint32_t * frameBuffer) {
    // Get relevant parameters from status registers and such:
    const uint8_t lcdCtrl = ioPorts[0x40];
    uint8_t scrY = ioPorts[0x42];
    uint8_t scrX = ioPorts[0x43];
    const unsigned int lineNo = ioPorts[0x44];
    unsigned int tileSetIndexOffset = lcdCtrl & 0x10U ? 0x0000U : 0x0080U;
    unsigned int tileSetIndexInverter = lcdCtrl & 0x10U ? 0x0000U : 0x0080U;
    unsigned int tileMapBase = lcdCtrl & 0x08U ? 0x1c00U : 0x1800U;

    // More variables
    unsigned int offset, max;
    unsigned int pixX, pixY, tileX, tileY;
    unsigned int pixelNo = 0;
    uint32_t* dstPointer;
    const uint32_t* tileSetPointer;

    // Sprite-specific stuff:
    unsigned int paletteOffset;
    unsigned int getPix;

    // Check if LCD is disabled or all elements (BG, window, sprites) are disabled (write a black row if that's the case):
    if ((lcdCtrl & 0x80U) == 0x00U || (lcdCtrl & 0x23U) == 0x00U) {
        dstPointer = &frameBuffer[lineNo * 160];
        for (pixX = 0; pixX < 160; pixX++) {
            *dstPointer++ = 0x000000ffU;
        }
        return;
    }

    // Draw background if enabled
    if (lcdCtrl & 0x01U) {
        // Set point to draw to
        dstPointer = &frameBuffer[160 * lineNo];

        // Set starting point of VRAM data to read, in terms of pixel coordinates within the tile, and tile coordinates within the tilemap
        pixX = scrX % 8;
        pixY = (lineNo + scrY) % 8;
        tileX = scrX / 8;
        tileY = ((lineNo + scrY) % 256) / 8;

        // Draw first 20 tiles (including partial leftmost tile)
        for (offset = 0; offset < 20; offset++) {
            // Get tile no. and point to the tileset data to read
            unsigned int tileMapIndex = tileMapBase + 32 * tileY + tileX;
            unsigned int tileNo = ((unsigned int)vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
            unsigned int tileParams = vram[0x2000U + tileMapIndex];
            unsigned int adjustedY = tileParams & 0x0040U ? 7 - pixY : pixY;
            if (tileParams & 0x0008U) {
                // Using bank 1
                tileNo += 0x0180U;
            }

            // Set a sprite-blocking bit as bit 2 if the BG priority bit is set in the tile map
            uint32_t bgPriorityBit = (tileParams & 0x80U) >> 5U;

            // Set which palette to draw with
            paletteOffset = 4 * (tileParams & 0x07U);

            // Draw up to 8 pixels of this tile
            if (tileParams & 0x0020U) {
                // Flipped horizontally
                unsigned int drawnX = 7 - pixX;
                tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
                while (pixX < 8) {
                    uint32_t colourIndex = *tileSetPointer--;
                    *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                    bgColorNumbers[pixelNo] = colourIndex;
                    bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                    pixX++;
                }
            } else {
                tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];
                while (pixX < 8) {
                    uint32_t colourIndex = *tileSetPointer++;
                    *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                    bgColorNumbers[pixelNo] = colourIndex;
                    bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                    pixX++;
                }
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial 21st tile. Find out how many pixels of it to draw.
        max = scrX % 8;

        // Get tile no
        unsigned int tileMapIndex = tileMapBase + 32 * tileY + tileX;
        unsigned int tileNo = ((unsigned int)vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
        unsigned int tileParams = vram[0x2000U + tileMapIndex];
        unsigned int adjustedY = tileParams & 0x0040U ? 7 - pixY : pixY;
        if (tileParams & 0x0008U) {
            // Using bank 1
            tileNo += 0x0180U;
        }
        tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];

        // Set a sprite-blocking bit as bit 2 if the BG priority bit is set in the tile map
        uint32_t bgPriorityBit = (tileParams & 0x80U) >> 5U;

        // Set which palette to draw with
        paletteOffset = 4 * (tileParams & 0x07U);

        // Draw up to 8 pixels of this tile
        if (tileParams & 0x0020U) {
            // Flipped horizontally
            unsigned int drawnX = 7 - pixX;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
            while (pixX < max) {
                uint32_t colourIndex = *tileSetPointer--;
                *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                bgColorNumbers[pixelNo] = colourIndex;
                bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                pixX++;
            }
        } else {
            while (pixX < max) {
                uint32_t colourIndex = *tileSetPointer++;
                *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                bgColorNumbers[pixelNo] = colourIndex;
                bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                pixX++;
            }
        }
    }

    // Draw window if enabled and on-screen
    scrX = ioPorts[0x4b];
    scrY = ioPorts[0x4a];
    tileMapBase = lcdCtrl & 0x40U ? 0x1c00U : 0x1800U;
    pixelNo = 0;
    if (((lcdCtrl & 0x20U) != 0x00U) && (scrX < 167) && (scrY <= lineNo)) {
        // Subtract 7 from window X pos
        if (scrX > 6) {
            scrX -= 7;
        }

        // Set point to draw to
        dstPointer = &frameBuffer[160 * lineNo + scrX];

        // Set starting point of VRAM data to read, in terms of pixel coordinates within the tile, and tile coordinates within the tilemap
        pixX = 0;
        pixY = (lineNo - scrY) % 8;
        tileX = 0;
        tileY = (lineNo - scrY) / 8;

        // Check how many complete tiles to draw
        max = (160 - scrX) / 8;

        // Draw all the complete tiles until the end of the line
        for (offset = 0; offset < max; offset++) {
            // Get tile no
            unsigned int tileMapIndex = tileMapBase + 32 * tileY + tileX;
            unsigned int tileNo = ((unsigned int)vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
            unsigned int tileParams = vram[0x2000U + tileMapIndex];
            unsigned int adjustedY = tileParams & 0x0040U ? 7 - pixY : pixY;
            if (tileParams & 0x0008U) {
                // Using bank 1
                tileNo += 0x0180U;
            }

            // Set a sprite-blocking bit as bit 2 if the BG priority bit is set in the tile map
            uint32_t bgPriorityBit = (tileParams & 0x80U) >> 5U;

            // Set which palette to draw with
            paletteOffset = 4 * (tileParams & 0x07U);

            // Draw the 8 pixels of this tile
            if (tileParams & 0x0020U) {
                // Flipped horizontally
                unsigned int drawnX = 7 - pixX;
                tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
                while (pixX < 8) {
                    uint32_t colourIndex = *tileSetPointer--;
                    *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                    bgColorNumbers[pixelNo] = colourIndex;
                    bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                    pixX++;
                }
            } else {
                tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];
                while (pixX < 8) {
                    uint32_t colourIndex = *tileSetPointer++;
                    *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                    bgColorNumbers[pixelNo] = colourIndex;
                    bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                    pixX++;
                }
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial last tile. Find out how many pixels of it to draw.
        max = (unsigned int)((168 - scrX) % 8);

        // Get tile no
        unsigned int tileMapIndex = tileMapBase + 32 * tileY + tileX;
        unsigned int tileNo = ((unsigned int)vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
        unsigned int tileParams = vram[0x2000U + tileMapIndex];
        unsigned int adjustedY = tileParams & 0x0040U ? 7 - pixY : pixY;
        if (tileParams & 0x0008U) {
            // Using bank 1
            tileNo += 0x0180U;
        }
        tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];

        // Set a sprite-blocking bit as bit 2 if the BG priority bit is set in the tile map
        uint32_t bgPriorityBit = (tileParams & 0x80U) >> 5U;

        // Set which palette to draw with
        paletteOffset = 4 * (tileParams & 0x07U);

        // Draw up to 8 pixels of this tile
        if (tileParams & 0x0020U) {
            // Flipped horizontally
            unsigned int drawnX = 7 - pixX;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
            while (pixX < max) {
                uint32_t colourIndex = *tileSetPointer--;
                *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                bgColorNumbers[pixelNo] = colourIndex;
                bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                pixX++;
            }
        } else {
            while (pixX < max) {
                uint32_t colourIndex = *tileSetPointer++;
                *dstPointer++ = cgbBgPalette[paletteOffset + colourIndex];
                bgColorNumbers[pixelNo] = colourIndex;
                bgDisplayPriorities[pixelNo++] = bgPriorityBit;
                pixX++;
            }
        }
    }

    // Draw sprites if enabled
    if (lcdCtrl & 0x02U) {
        const uint8_t largeSprites = lcdCtrl & 0x04U;

        // Draw each sprite that is visible
        for (offset = 156; offset < 160; offset -= 4) {
            // Get coords check if off-screen
            scrY = oam[offset];
            if (scrY == 0) {
                continue;
            } else if (scrY > 159) {
                continue;
            }
            scrX = oam[offset + 1];
            if (scrX == 0) {
                continue;
            } else if (scrX > 167) {
                continue;
            }

            // Check if the current scan line would pass through an 8x16 sprite
            if (lineNo + 16 < scrY) {
                continue;
            } else if (lineNo >= scrY) {
                continue;
            }
            unsigned int tileNo = oam[offset + 2];
            unsigned int spriteFlags = oam[offset + 3];

            // Check sprite size, and accordingly re-assess visibility and adjust tilemap index
            if (largeSprites) {
                if (lineNo + 8 >= scrY) {
                    tileNo |= 0x01U;
                } else {
                    tileNo &= 0xfeU;
                }
            } else if (lineNo + 8 >= scrY) {
                continue;
            }
            if (spriteFlags & 0x08U) {
                tileNo += 384;
            }

            // Get pixel row within tile that will be drawn
            pixY = (lineNo + 16 - scrY) % 8;

            // Set which palette to draw with
            paletteOffset = 4 * (spriteFlags & 0x07U);

            // Get first pixel in tile row to draw, plus how many pixels to draw, and adjust the starting point to write to in the image
            if (scrX < 8) {
                pixX = 8 - scrX;
                max = scrX;
                scrX = 0;
            } else if (scrX > 160) {
                pixX = 0;
                max = 168 - scrX;
                scrX -= 8;
            } else {
                pixX = 0;
                max = 8;
                scrX -= 8;
            }

            // Adjust X if horizontally flipping
            int tileSetPointerDirection;
            if (spriteFlags & 0x20U) {
                pixX = 7 - pixX;
                tileSetPointerDirection = -1;
            } else {
                tileSetPointerDirection = 1;
            }

            // Adjust Y if vertically flipping (inverts which of the two tiles to use also)
            if (spriteFlags & 0x40U) {
                pixY = 7 - pixY;
                if (largeSprites) {
                    tileNo ^= 0x01U;
                }
            }

            // Get priority flag
            unsigned int spriteGivesBgPriority = spriteFlags & 0x80U;

            // Set point to draw to
            dstPointer = &frameBuffer[160 * lineNo + scrX];
            pixelNo = scrX;

            // Get pointer to tile data
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile (skipping over transparent pixels with palette index 0, or if obscured by the background)
            pixX = 0;
            while (pixX < max) {
                getPix = *tileSetPointer;
                tileSetPointer += tileSetPointerDirection;
                if (getPix > 0) {
                    uint32_t bgColor = bgColorNumbers[pixelNo];
                    uint32_t bgTakesPriority = bgDisplayPriorities[pixelNo];
                    // Draw sprites where BG wrote zero or neither BG nor sprite flags gave the BG priority
                    if (bgTakesPriority || spriteGivesBgPriority) {
                        if (bgColor == 0) {
                            *dstPointer = cgbObjPalette[getPix + paletteOffset];
                        }
                    } else {
                        *dstPointer = cgbObjPalette[getPix + paletteOffset];
                    }
                }
                dstPointer++;
                pixX++;
                pixelNo++;
            }
        }
    }
}














inline unsigned int Gbc::HL() {
    return ((unsigned int)cpuH << 8U) + (unsigned int)cpuL;
}

inline uint8_t Gbc::R8_HL() {
    return read8(((unsigned int)cpuH << 8U) + (unsigned int)cpuL);
}

inline void Gbc::W8_HL(uint8_t byte) {
    write8(((unsigned int)cpuH << 8U) + (unsigned int)cpuL, byte);
}

inline void Gbc::SETZ_ON_ZERO(uint8_t testValue) {
    if (testValue == 0x00U) {
        cpuF |= 0x80U;
    }
}

inline void Gbc::SETZ_ON_COND(bool test) {
    if (test) {
        cpuF |= 0x80U;
    }
}

inline void Gbc::SETH_ON_ZERO(uint8_t testValue) {
    if (testValue == 0x00U) {
        cpuF |= 0x20U;
    }
}

inline void Gbc::SETH_ON_COND(bool test) {
    if (test) {
        cpuF |= 0x20U;
    }
}

inline void Gbc::SETC_ON_COND(bool test) {
    if (test) {
        cpuF |= 0x10U;
    }
}

int Gbc::performOp() {
    uint8_t instr = read8(cpuPc);
    switch (instr) {
        case 0x00: // nop
            cpuPc++;
            return 4;
        case 0x01: // ld BC, nn
            cpuB = read8(cpuPc + 2);
            cpuC = read8(cpuPc + 1);
            cpuPc += 3;
            return 12;
        case 0x02: // ld (BC), A
            write8(((unsigned int)cpuB << 8U) + (unsigned int)cpuC, cpuA);
            cpuPc++;
            return 8;
        case 0x03: // inc BC
            cpuC++;
            if (cpuC == 0x00) {
                cpuB++;
            }
            cpuPc++;
            return 8;
        case 0x04: // inc B
            cpuF &= 0x10U;
            cpuB += 0x01U;
            SETZ_ON_ZERO(cpuB);
            SETH_ON_ZERO(cpuB & 0x0fU);
            cpuPc++;
            return 4;
        case 0x05: // dec B
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            SETH_ON_ZERO(cpuB & 0x0fU);
            cpuB -= 0x01U;
            SETZ_ON_ZERO(cpuB);
            cpuPc++;
            return 4;
        case 0x06: // ld B, n
            cpuB = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x07: // rlc A (rotate bit 7 to bit 0, and copy bit 7 to carry flag)
        {
            uint8_t tempByte = cpuA & 0x80U; // True if bit 7 is set
            cpuF = 0x00;
            cpuA = cpuA << 1U;
            if (tempByte != 0)
            {
                cpuF |= 0x10U; // Set carry
                cpuA |= 0x01U;
            }
        }
            cpuPc++;
            return 4;
        case 0x08: // ld (nn), SP
            write16(((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1),
                    (uint8_t)(cpuSp & 0x00ffU),
                    (uint8_t)((cpuSp >> 8U) & 0x00ffU)
            );
            cpuPc += 3;
            return 20;
        case 0x09: // add HL, BC
            cpuF &= 0x80U;
            cpuL += cpuC;
            if (cpuL < cpuC)
            {
                cpuH++;
                SETC_ON_COND(cpuH == 0x00);
                SETH_ON_ZERO(cpuH & 0x0fU);
            }
            cpuH += cpuB;
            SETC_ON_COND(cpuH < cpuB);
            SETH_ON_COND((cpuH & 0x0fU) < (cpuB & 0x0fU));
            cpuPc++;
            return 8;
        case 0x0a: // ld A, (BC)
            cpuA = read8(((unsigned int)cpuB << 8U) + (unsigned int)cpuC);
            cpuPc++;
            return 8;
        case 0x0b: // dec BC
            if (cpuC == 0x00)
            {
                cpuB--;
            }
            cpuC--;
            cpuPc++;
            return 8;
        case 0x0c: // inc C
            cpuF &= 0x10U;
            cpuC += 0x01U;
            SETZ_ON_ZERO(cpuC);
            SETH_ON_ZERO(cpuC & 0x0fU);
            cpuPc++;
            return 4;
        case 0x0d: // dec C
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            SETH_ON_ZERO(cpuC & 0x0fU);
            cpuC -= 0x01U;
            SETZ_ON_ZERO(cpuC);
            cpuPc++;
            return 4;
        case 0x0e: // ld C, n
            cpuC = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x0f: // rrc A (8-bit rotation right - bit 0 is moved to carry also)
        {
            uint8_t tempByte = cpuA & 0x01U;
            cpuF = 0x00;
            cpuA = cpuA >> 1U;
            cpuA = cpuA & 0x7fU; // Clear msb in case sign bit preserved by compiler
            if (tempByte != 0) {
                cpuF = 0x10U;
                cpuA |= 0x80U;
            }
        }
            cpuPc++;
            return 4;
        case 0x10: // stop
            cpuMode = CPU_STOPPED;
            cpuPc++;
            return 4;
        case 0x11: // ld DE, nn
            cpuD = read8(cpuPc + 2);
            cpuE = read8(cpuPc + 1);
            cpuPc += 3;
            return 12;
        case 0x12: // ld (DE), A
            write8(((unsigned int)cpuD << 8U) + (unsigned int)cpuE, cpuA);
            cpuPc++;
            return 8;
        case 0x13: // inc DE
            cpuE++;
            if (cpuE == 0x00) {
                cpuD++;
            }
            cpuPc++;
            return 8;
        case 0x14: // inc D
            cpuF &= 0x10U;
            cpuD += 0x01U;
            SETZ_ON_ZERO(cpuD);
            SETH_ON_ZERO(cpuD & 0x0fU);
            cpuPc++;
            return 4;
        case 0x15: // dec D
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            SETH_ON_ZERO(cpuD & 0x0fU);
            cpuD -= 0x01U;
            SETZ_ON_ZERO(cpuD);
            cpuPc++;
            return 4;
        case 0x16: // ld D, n
            cpuD = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x17: // rl A (rotate carry bit to bit 0 of A)
        {
            uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
            cpuF = 0x00;
            SETC_ON_COND((cpuA & 0x80U) != 0); // Copy bit 7 to carry bit
            cpuA = cpuA << 1U;
            if (tempByte != 0)
            {
                cpuA |= 0x01U; // Copy carry flag to bit 0
            }
        }
            cpuPc++;
            return 4;
        case 0x18: // jr d
        {
            uint8_t msb = read8(cpuPc + 1);
            if (msb >= 0x80)
            {
                cpuPc -= 256 - (unsigned int)msb;
            }
            else
            {
                cpuPc += (unsigned int)msb;
            }
        }
            cpuPc += 2;
            return 12;
        case 0x19: // add HL, DE
            cpuF &= 0x80U;
            cpuL += cpuE;
            if (cpuL < cpuE)
            {
                cpuH++;
                SETC_ON_COND(cpuH == 0x00);
                SETH_ON_ZERO(cpuH & 0x0fU);
            }
            cpuH += cpuD;
            SETC_ON_COND(cpuH < cpuD);
            SETH_ON_COND((cpuH & 0x0fU) < (cpuD & 0x0fU));
            cpuPc++;
            return 8;
        case 0x1a: // ld A, (DE)
            cpuA = read8(((unsigned int)cpuD << 8U) + (unsigned int)cpuE);
            cpuPc++;
            return 8;
        case 0x1b: // dec DE
            if (cpuE == 0x00)
            {
                cpuD--;
            }
            cpuE--;
            cpuPc++;
            return 8;
        case 0x1c: // inc E
            cpuF &= 0x10U;
            cpuE += 0x01U;
            SETZ_ON_ZERO(cpuE);
            SETH_ON_ZERO(cpuE & 0x0fU);
            cpuPc++;
            return 4;
        case 0x1d: // dec E
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            SETH_ON_ZERO(cpuE & 0x0fU);
            cpuE -= 0x01U;
            SETZ_ON_ZERO(cpuE);
            cpuPc++;
            return 4;
        case 0x1e: // ld E, n
            cpuE = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x1f: // rr A (9-bit rotation right of A through carry)
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x00;
            SETC_ON_COND(cpuA & 0x01U);
            cpuA = cpuA >> 1U;
            cpuA = cpuA & 0x7fU;
            if (tempByte != 0x00)
            {
                cpuA |= 0x80U;
            }
        }
            cpuPc++;
            return 4;
        case 0x20: // jr NZ, d
            if ((cpuF & 0x80U) != 0)
            {
                cpuPc += 2;
                return 8;
            }
            else
            {
                uint8_t msb = read8(cpuPc + 1);
                if (msb >= 0x80)
                {
                    cpuPc -= 256 - (unsigned int)msb;
                }
                else
                {
                    cpuPc += (unsigned int)msb;
                }
                cpuPc += 2;
                return 12;
            }
        case 0x21: // ld HL, nn
            cpuH = read8(cpuPc + 2);
            cpuL = read8(cpuPc + 1);
            cpuPc += 3;
            return 12;
        case 0x22: // ldi (HL), A
            W8_HL(cpuA);
            cpuL++;
            if (cpuL == 0x00)
            {
                cpuH++; // L overflowed into H
            }
            cpuPc++;
            return 8;
        case 0x23: // inc HL
            cpuL++;
            if (cpuL == 0x00)
            {
                cpuH++;
            }
            cpuPc++;
            return 8;
        case 0x24: // inc H
            cpuF &= 0x10U;
            cpuH += 0x01U;
            SETZ_ON_ZERO(cpuH);
            SETH_ON_ZERO(cpuH & 0x0fU);
            cpuPc++;
            return 4;
        case 0x25: // dec H
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            SETH_ON_ZERO(cpuH & 0x0fU);
            cpuH -= 0x01U;
            SETZ_ON_ZERO(cpuH);
            cpuPc++;
            return 4;
        case 0x26: // ld H, n
            cpuH = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x27: // daa (Decimal Adjust Accumulator - do BCD correction)
            if ((cpuF & 0x40U) == 0x00)
            {
                if (((cpuA & 0x0fU) > 0x09) || ((cpuF & 0x20U) != 0x00)) // If lower 4 bits are non-decimal or H is set, add 0x06
                {
                    cpuA += 0x06;
                }
                uint8_t tempByte = cpuF & 0x10U;
                cpuF &= 0x40U; // Reset C, H and Z flags
                if ((cpuA > 0x9f) || (tempByte != 0x00)) // If upper 4 bits are non-decimal or C was set, add 0x60
                {
                    cpuA += 0x60;
                    cpuF |= 0x10U; // Sets the C flag if this second addition was needed
                }
            }
            else
            {
                if (((cpuA & 0x0fU) > 0x09) || ((cpuF & 0x20U) != 0x00)) // If lower 4 bits are non-decimal or H is set, add 0x06
                {
                    cpuA -= 0x06;
                }
                uint8_t tempByte = cpuF & 0x10U;
                cpuF &= 0x40U; // Reset C, H and Z flags
                if ((cpuA > 0x9f) || (tempByte != 0x00)) // If upper 4 bits are non-decimal or C was set, add 0x60
                {
                    cpuA -= 0x60;
                    cpuF |= 0x10U; // Sets the C flag if this second addition was needed
                }
            }
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0x28: // jr Z, d
            if ((cpuF & 0x80U) != 0x00)
            {
                uint8_t msb = read8(cpuPc + 1);
                if (msb >= 0x80)
                {
                    cpuPc -= 256 - (unsigned int)msb;
                }
                else
                {
                    cpuPc += (unsigned int)msb;
                }
                cpuPc += 2;
                return 12;
            }
            else
            {
                cpuPc += 2;
                return 8;
            }
        case 0x29: // add HL, HL
            cpuF &= 0x80U;
            SETC_ON_COND((cpuH & 0x80U) != 0x00);
            SETH_ON_COND((cpuH & 0x08U) != 0x00);
            if ((cpuL & 0x80U) != 0x00)
            {
                cpuH += cpuH + 1;
                cpuL += cpuL;
            }
            else
            {
                cpuH *= 2;
                cpuL *= 2;
            }
            cpuPc++;
            return 8;
        case 0x2a: // ldi A, (HL)
            cpuA = R8_HL();
            cpuL++;
            if (cpuL == 0x00)
            {
                cpuH++;
            }
            cpuPc++;
            return 8;
        case 0x2b: // dec HL
            if (cpuL == 0x00)
            {
                cpuH--;
            }
            cpuL--;
            cpuPc++;
            return 8;
        case 0x2c: // inc L
            cpuF &= 0x10U;
            cpuL += 0x01U;
            SETZ_ON_ZERO(cpuL);
            SETH_ON_ZERO(cpuL & 0x0fU);
            cpuPc++;
            return 4;
        case 0x2d: // dec L
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            SETH_ON_ZERO(cpuL & 0x0fU);
            cpuL -= 0x01U;
            SETZ_ON_ZERO(cpuL);
            cpuPc++;
            return 4;
        case 0x2e: // ld L, n
            cpuL = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x2f: // cpl A (complement - bitwise NOT)
            cpuA = ~cpuA;
            cpuF |= 0x60U;
            cpuPc++;
            return 4;
        case 0x30: // jr NC, d
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuPc += 2;
                return 8;
            }
            else
            {
                uint8_t msb = read8(cpuPc + 1);
                if (msb >= 0x80)
                {
                    cpuPc -= 256 - (unsigned int)msb;
                }
                else
                {
                    cpuPc += (unsigned int)msb;
                }
                cpuPc += 2;
                return 12;
            }
        case 0x31: // ld SP, nn
            cpuSp = ((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1);
            cpuPc += 3;
            return 12;
        case 0x32: // ldd (HL), A
            W8_HL(cpuA);
            if (cpuL == 0x00)
            {
                cpuH--;
            }
            cpuL--;
            cpuPc++;
            return 8;
        case 0x33: // inc SP
            cpuSp++;
            cpuSp &= 0xffffU;
            cpuPc++;
            return 8;
        case 0x34: // inc (HL)
        {
            cpuF &= 0x10U;
            unsigned int tempAddr = HL();
            uint8_t tempByte = read8(tempAddr) + 1;
            SETZ_ON_ZERO(tempByte);
            SETH_ON_ZERO(tempByte & 0x0fU);
            write8(tempAddr, tempByte);
        }
            cpuPc++;
            return 12;
        case 0x35: // dec (HL)
        {
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            unsigned int tempAddr = HL();
            uint8_t tempByte = read8(tempAddr);
            SETH_ON_ZERO(tempByte & 0x0fU);
            tempByte--;
            SETZ_ON_ZERO(tempByte);
            write8(tempAddr, tempByte);
        }
            cpuPc++;
            return 12;
        case 0x36: // ld (HL), n
            W8_HL(read8(cpuPc + 1));
            cpuPc += 2;
            return 12;
        case 0x37: // SCF (set carry flag)
            cpuF &= 0x80U;
            cpuF |= 0x10U;
            cpuPc++;
            return 4;
        case 0x38: // jr C, n
            if ((cpuF & 0x10U) != 0x00)
            {
                uint8_t msb = read8(cpuPc + 1);
                if (msb >= 0x80)
                {
                    cpuPc -= 256 - (unsigned int)msb;
                }
                else
                {
                    cpuPc += (unsigned int)msb;
                }
                cpuPc += 2;
                return 12;
            }
            else
            {
                cpuPc += 2;
                return 8;
            }
        case 0x39: // add HL, SP
        {
            cpuF &= 0x80U;
            auto tempByte = (uint8_t)(cpuSp & 0xffU);
            cpuL += tempByte;
            if (cpuL < tempByte)
            {
                cpuH++;
            }
            tempByte = (uint8_t)(cpuSp >> 8U);
            cpuH += tempByte;
            SETC_ON_COND(cpuH < tempByte);
            tempByte = tempByte & 0x0fU;
            SETH_ON_COND((cpuH & 0x0fU) < tempByte);
        }
            cpuPc++;
            return 8;
        case 0x3a: // ldd A, (HL)
            cpuA = R8_HL();
            if (cpuL == 0x00)
            {
                cpuH--;
            }
            cpuL--;
            cpuPc++;
            return 8;
        case 0x3b: // dec SP
            cpuSp--;
            cpuSp &= 0xffffU;
            cpuPc++;
            return 8;
        case 0x3c: // inc A
            cpuA++;
            cpuF &= 0x10U;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_ZERO(cpuA & 0x0fU);
            cpuPc++;
            return 4;
        case 0x3d: // dec A
            cpuF &= 0x10U;
            cpuF |= 0x40U;
            SETH_ON_ZERO(cpuA & 0x0fU);
            cpuA -= 0x01U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0x3e: // ld A, n
            cpuA = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x3f: // ccf (invert carry flags)
        {
            cpuF &= 0xb0U;
            uint8_t tempByte = cpuF & 0x30U;
            tempByte = tempByte ^ 0x30U;
            cpuF &= 0x80U;
            cpuF |= tempByte;
        }
            cpuPc++;
            return 4;
        case 0x40: // ld B, B
            cpuPc++;
            return 4;
        case 0x41: // ld B, C
            cpuB = cpuC;
            cpuPc++;
            return 4;
        case 0x42: // ld B, D
            cpuB = cpuD;
            cpuPc++;
            return 4;
        case 0x43: // ld B, E
            cpuB = cpuE;
            cpuPc++;
            return 4;
        case 0x44: // ld B, H
            cpuB = cpuH;
            cpuPc++;
            return 4;
        case 0x45: // ld B, L
            cpuB = cpuL;
            cpuPc++;
            return 4;
        case 0x46: // ld B, (HL)
            cpuB = R8_HL();
            cpuPc++;
            return 8;
        case 0x47: // ld B, A
            cpuB = cpuA;
            cpuPc++;
            return 4;
        case 0x48: // ld C, B
            cpuC = cpuB;
            cpuPc++;
            return 4;
        case 0x49: // ld C, C
            cpuPc++;
            return 4;
        case 0x4a: // ld C, D
            cpuC = cpuD;
            cpuPc++;
            return 4;
        case 0x4b: // ld C, E
            cpuC = cpuE;
            cpuPc++;
            return 4;
        case 0x4c: // ld C, H
            cpuC = cpuH;
            cpuPc++;
            return 4;
        case 0x4d: // ld C, L
            cpuC = cpuL;
            cpuPc++;
            return 4;
        case 0x4e: // ld C, (HL)
            cpuC = R8_HL();
            cpuPc++;
            return 8;
        case 0x4f: // ld C, A
            cpuC = cpuA;
            cpuPc++;
            return 4;
        case 0x50: // ld D, B
            cpuD = cpuB;
            cpuPc++;
            return 4;
        case 0x51: // ld D, C
            cpuD = cpuC;
            cpuPc++;
            return 4;
        case 0x52: // ld D, D
            cpuPc++;
            return 4;
        case 0x53: // ld D, E
            cpuD = cpuE;
            cpuPc++;
            return 4;
        case 0x54: // ld D, H
            cpuD = cpuH;
            cpuPc++;
            return 4;
        case 0x55: // ld D, L
            cpuD = cpuL;
            cpuPc++;
            return 4;
        case 0x56: // ld D, (HL)
            cpuD = R8_HL();
            cpuPc++;
            return 8;
        case 0x57: // ld D, A
            cpuD = cpuA;
            cpuPc++;
            return 4;
        case 0x58: // ld E, B
            cpuE = cpuB;
            cpuPc++;
            return 4;
        case 0x59: // ld E, C
            cpuE = cpuC;
            cpuPc++;
            return 4;
        case 0x5a: // ld E, D
            cpuE = cpuD;
            cpuPc++;
            return 4;
        case 0x5b: // ld E, E
            cpuPc++;
            return 4;
        case 0x5c: // ld E, H
            cpuE = cpuH;
            cpuPc++;
            return 4;
        case 0x5d: // ld E, L
            cpuE = cpuL;
            cpuPc++;
            return 4;
        case 0x5e: // ld E, (HL)
            cpuE = R8_HL();
            cpuPc++;
            return 8;
        case 0x5f: // ld E, A
            cpuE = cpuA;
            cpuPc++;
            return 4;
        case 0x60: // ld H, B
            cpuH = cpuB;
            cpuPc++;
            return 4;
        case 0x61: // ld H, C
            cpuH = cpuC;
            cpuPc++;
            return 4;
        case 0x62: // ld H, D
            cpuH = cpuD;
            cpuPc++;
            return 4;
        case 0x63: // ld H, E
            cpuH = cpuE;
            cpuPc++;
            return 4;
        case 0x64: // ld H, H
            cpuPc++;
            return 4;
        case 0x65: // ld H, L
            cpuH = cpuL;
            cpuPc++;
            return 4;
        case 0x66: // ld H, (HL)
            cpuH = R8_HL();
            cpuPc++;
            return 8;
        case 0x67: // ld H, A
            cpuH = cpuA;
            cpuPc++;
            return 4;
        case 0x68: // ld L, B
            cpuL = cpuB;
            cpuPc++;
            return 4;
        case 0x69: // ld L, C
            cpuL = cpuC;
            cpuPc++;
            return 4;
        case 0x6a: // ld L, D
            cpuL = cpuD;
            cpuPc++;
            return 4;
        case 0x6b: // ld L, E
            cpuL = cpuE;
            cpuPc++;
            return 4;
        case 0x6c: // ld L, H
            cpuL = cpuH;
            cpuPc++;
            return 4;
        case 0x6d: // ld L, L
            cpuPc++;
            return 4;
        case 0x6e: // ld L, (HL)
            cpuL = R8_HL();
            cpuPc++;
            return 8;
        case 0x6f: // ld L, A
            cpuL = cpuA;
            cpuPc++;
            return 4;
        case 0x70: // ld (HL), B
            W8_HL(cpuB);
            cpuPc++;
            return 8;
        case 0x71: // ld (HL), C
            W8_HL(cpuC);
            cpuPc++;
            return 8;
        case 0x72: // ld (HL), D
            W8_HL(cpuD);
            cpuPc++;
            return 8;
        case 0x73: // ld (HL), E
            W8_HL(cpuE);
            cpuPc++;
            return 8;
        case 0x74: // ld (HL), H
            W8_HL(cpuH);
            cpuPc++;
            return 8;
        case 0x75: // ld (HL), L
            W8_HL(cpuL);
            cpuPc++;
            return 8;
        case 0x76: // halt (NOTE THAT THIS GETS INTERRUPTED EVEN WHEN INTERRUPTS ARE DISABLED)
            cpuMode = CPU_HALTED;
            cpuPc++;
            return 4;
        case 0x77: // ld (HL), A
            W8_HL(cpuA);
            cpuPc++;
            return 8;
        case 0x78: // ld A, B
            cpuA = cpuB;
            cpuPc++;
            return 4;
        case 0x79: // ld A, C
            cpuA = cpuC;
            cpuPc++;
            return 4;
        case 0x7a: // ld A, D
            cpuA = cpuD;
            cpuPc++;
            return 4;
        case 0x7b: // ld A, E
            cpuA = cpuE;
            cpuPc++;
            return 4;
        case 0x7c: // ld A, H
            cpuA = cpuH;
            cpuPc++;
            return 4;
        case 0x7d: // ld A, L
            cpuA = cpuL;
            cpuPc++;
            return 4;
        case 0x7e: // ld A, (HL)
            cpuA = R8_HL();
            cpuPc++;
            return 8;
        case 0x7f: // ld A, A
            cpuPc++;
            return 4;
        case 0x80: // add B (add B to A)
            cpuA += cpuB;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuB & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuB > cpuA);
            cpuPc++;
            return 4;
        case 0x81: // add C
            cpuA += cpuC;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuC & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuC > cpuA);
            cpuPc++;
            return 4;
        case 0x82: // add D
            cpuA += cpuD;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuD & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuD > cpuA);
            cpuPc++;
            return 4;
        case 0x83: // add E
            cpuA += cpuE;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuE & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuE > cpuA);
            cpuPc++;
            return 4;
        case 0x84: // add H
            cpuA += cpuH;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuH & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuH > cpuA);
            cpuPc++;
            return 4;
        case 0x85: // add L
            cpuA += cpuL;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuL & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuL > cpuA);
            cpuPc++;
            return 4;
        case 0x86: // add (HL)
        {
            uint8_t tempByte = R8_HL();
            cpuA += tempByte;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(tempByte > cpuA);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 8;
        case 0x87: // add A
            cpuF = 0x00;
            SETH_ON_COND((cpuA & 0x08U) != 0x00);
            SETC_ON_COND((cpuA & 0x80U) != 0x00);
            cpuA += cpuA;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0x88: // adc A, B (add B + carry to A)
        {
            uint8_t tempByte = cpuB;
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 4;
        case 0x89: // adc A, C
        {
            uint8_t tempByte = cpuC;
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 4;
        case 0x8a: // adc A, D
        {
            uint8_t tempByte = cpuD;
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 4;
        case 0x8b: // adc A, E
        {
            uint8_t tempByte = cpuE;
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 4;
        case 0x8c: // adc A, H
        {
            uint8_t tempByte = cpuH;
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 4;
        case 0x8d: // adc A, L
        {
            uint8_t tempByte = cpuL;
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 4;
        case 0x8e: // adc A, (HL)
        {
            uint8_t tempByte = R8_HL();
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 8;
        case 0x8f: // adc A, A
        {
            uint8_t tempByte = cpuA;
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuF = 0x00;
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            else
            {
                cpuF = 0x00;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 4;
        case 0x90: // sub B (sub B from A)
            cpuF = 0x40;
            SETC_ON_COND(cpuB > cpuA);
            SETH_ON_COND((cpuB & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuB;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
            cpuPc++;
            return 4;
        case 0x91: // sub C
            cpuF = 0x40;
            SETC_ON_COND(cpuC > cpuA);
            SETH_ON_COND((cpuC & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuC;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
            cpuPc++;
            return 4;
        case 0x92: // sub D
            cpuF = 0x40;
            SETC_ON_COND(cpuD > cpuA);
            SETH_ON_COND((cpuD & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuD;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
            cpuPc++;
            return 4;
        case 0x93: // sub E
            cpuF = 0x40;
            SETC_ON_COND(cpuE > cpuA);
            SETH_ON_COND((cpuE & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuE;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
            cpuPc++;
            return 4;
        case 0x94: // sub H
            cpuF = 0x40;
            SETC_ON_COND(cpuH > cpuA);
            SETH_ON_COND((cpuH & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuH;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
            cpuPc++;
            return 4;
        case 0x95: // sub L
            cpuF = 0x40;
            SETC_ON_COND(cpuL > cpuA);
            SETH_ON_COND((cpuL & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuL;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
            cpuPc++;
            return 4;
        case 0x96: // sub (HL)
        {
            uint8_t tempByte = R8_HL();
            cpuF = 0x40;
            SETC_ON_COND(tempByte > cpuA);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= tempByte;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
        }
            cpuPc++;
            return 8;
        case 0x97: // sub A
            cpuF = 0xc0;
            cpuA = 0x00;
            cpuPc++;
            return 4;
        case 0x98: // sbc A, B (A = A - (B+carry))
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x40;
            SETC_ON_COND(cpuB > cpuA);
            SETH_ON_COND((cpuB & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuB;
            if (tempByte != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 4;
        case 0x99: // sbc A, C
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x40;
            SETC_ON_COND(cpuC > cpuA);
            SETH_ON_COND((cpuC & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuC;
            if (tempByte != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 4;
        case 0x9a: // sbc A, D
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x40;
            SETC_ON_COND(cpuD > cpuA);
            SETH_ON_COND((cpuD & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuD;
            if (tempByte != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 4;
        case 0x9b: // sbc A, E
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x40;
            SETC_ON_COND(cpuE > cpuA);
            SETH_ON_COND((cpuE & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuE;
            if (tempByte != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 4;
        case 0x9c: // sbc A, H
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x40;
            SETC_ON_COND(cpuH > cpuA);
            SETH_ON_COND((cpuH & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuH;
            if (tempByte != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 4;
        case 0x9d: // sbc A, L
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x40;
            SETC_ON_COND(cpuL > cpuA);
            SETH_ON_COND((cpuL & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= cpuL;
            if (tempByte != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 4;
        case 0x9e: // sbc A, (HL)
        {
            uint8_t tempByte = R8_HL();
            uint8_t tempByte2 = cpuF & 0x10U;
            cpuF = 0x40;
            SETC_ON_COND(tempByte > cpuA);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= tempByte;
            if (tempByte2 != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 8;
        case 0x9f: // sbc A, A
        {
            uint8_t tempByte = cpuF & 0x10U;
            cpuF = 0x40;
            cpuA = 0;
            if (tempByte != 0x00)
            {
                if (cpuA == 0)
                {
                    cpuA = 0xff;
                    cpuF = 0x70;
                }
                else
                {
                    cpuA--;
                }
            }
            SETZ_ON_ZERO(cpuA);
        }
            cpuPc++;
            return 4;
        case 0xa0: // and B (and B against A)
            cpuA = cpuA & cpuB;
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa1: // and C
            cpuA = cpuA & cpuC;
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa2: // and D
            cpuA = cpuA & cpuD;
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa3: // and E
            cpuA = cpuA & cpuE;
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa4: // and H
            cpuA = cpuA & cpuH;
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa5: // and L
            cpuA = cpuA & cpuL;
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa6: // and (HL)
            cpuA = cpuA & R8_HL();
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 8;
        case 0xa7: // and A
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa8: // xor B (A = A XOR B)
            cpuA = cpuA ^ cpuB;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa9: // xor C
            cpuA = cpuA ^ cpuC;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xaa: // xor D
            cpuA = cpuA ^ cpuD;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xab: // xor E
            cpuA = cpuA ^ cpuE;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xac: // xor H
            cpuA = cpuA ^ cpuH;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xad: // xor L
            cpuA = cpuA ^ cpuL;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xae: // xor (HL)
            cpuA = cpuA ^ R8_HL();
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 8;
        case 0xaf: // xor A
            cpuA = 0x00;
            cpuF = 0x80U;
            cpuPc++;
            return 4;
        case 0xb0: // or B (or B against A)
            cpuA = cpuA | cpuB;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xb1: // or C
            cpuA = cpuA | cpuC;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xb2: // or D
            cpuA = cpuA | cpuD;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xb3: // or E
            cpuA = cpuA | cpuE;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xb4: // or H
            cpuA = cpuA | cpuH;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xb5: // or L
            cpuA = cpuA | cpuL;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xb6: // or (HL)
            cpuA = cpuA | R8_HL();
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 8;
        case 0xb7: // or A
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xb8: // cp B
            cpuF = 0x40;
            SETH_ON_COND((cpuB & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuB > cpuA);
            SETZ_ON_COND(cpuA == cpuB);
            cpuPc++;
            return 4;
        case 0xb9: // cp C
            cpuF = 0x40;
            SETH_ON_COND((cpuC & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuC > cpuA);
            SETZ_ON_COND(cpuA == cpuC);
            cpuPc++;
            return 4;
        case 0xba: // cp D
            cpuF = 0x40;
            SETH_ON_COND((cpuD & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuD > cpuA);
            SETZ_ON_COND(cpuA == cpuD);
            cpuPc++;
            return 4;
        case 0xbb: // cp E
            cpuF = 0x40;
            SETH_ON_COND((cpuE & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuE > cpuA);
            SETZ_ON_COND(cpuA == cpuE);
            cpuPc++;
            return 4;
        case 0xbc: // cp H
            cpuF = 0x40;
            SETH_ON_COND((cpuH & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuH > cpuA);
            SETZ_ON_COND(cpuA == cpuH);
            cpuPc++;
            return 4;
        case 0xbd: // cp L
            cpuF = 0x40;
            SETH_ON_COND((cpuL & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(cpuL > cpuA);
            SETZ_ON_COND(cpuA == cpuL);
            cpuPc++;
            return 4;
        case 0xbe: // cp (HL)
        {
            uint8_t tempByte = R8_HL();
            cpuF = 0x40;
            SETC_ON_COND(tempByte > cpuA);
            SETZ_ON_COND(cpuA == tempByte);
            SETH_ON_COND((tempByte & 0x0fU) > (cpuA & 0x0fU));
        }
            cpuPc++;
            return 8;
        case 0xbf: // cp A
            cpuF = 0xc0;
            cpuPc++;
            return 4;
        case 0xc0: // ret NZ
            if ((cpuF & 0x80U) != 0x00) {
                cpuPc++;
                return 8;
            } else {
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallReturned = 1;
                }
#endif
                uint8_t msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 20;
            }
        case 0xc1: // pop BC
            read16(cpuSp, &cpuC, &cpuB);
            cpuSp += 2;
            cpuPc++;
            return 12;
        case 0xc2: // j NZ, nn
            if ((cpuF & 0x80U) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1);
                return 16;
            }
        case 0xc3: // jump to nn
            cpuPc = ((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1);
            return 16;
        case 0xc4: // call NZ, nn
            if ((cpuF & 0x80U) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                uint8_t msb = read8(cpuPc + 1);
                uint8_t lsb = read8(cpuPc + 2);
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
#endif
                cpuPc += 3;
                cpuSp -= 2;
                write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 24;
            }
        case 0xc5: // push BC
            cpuSp -= 2;
            write16(cpuSp, cpuC, cpuB);
            cpuPc++;
            return 16;
        case 0xc6: // add A, n
        {
            uint8_t msb = read8(cpuPc + 1);
            cpuA += msb;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < msb);
            SETH_ON_COND((cpuA & 0x0fU) < (msb & 0x0fU));
        }
            cpuPc += 2;
            return 8;
        case 0xc7: // rst 0 (call routine at 0x0000)
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x00;
            return 16;
        case 0xc8: // ret Z
            if ((cpuF & 0x80U) != 0x00) {
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallReturned = 1;
                }
#endif
                uint8_t msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 20;
            }
            else
            {
                cpuPc++;
                return 8;
            }
        case 0xc9: // return
        {
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallReturned = 1;
            }
#endif
            uint8_t msb, lsb;
            read16(cpuSp, &msb, &lsb);
            cpuSp += 2;
            cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
        }
            return 16;
        case 0xca: // j Z, nn
            if ((cpuF & 0x80U) != 0x00)
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1);
                return 16;
            }
            else
            {
                cpuPc += 3;
                return 12;
            }
        case 0xcb: // extended instructions
            cpuPc += 2;
            switch (read8(cpuPc - 1))
            {
                case 0x00: // rlc B
                {
                    uint8_t tempByte = cpuB & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuB = cpuB << 1U;
                    if (tempByte != 0)
                    {
                        cpuB |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x01: // rlc C
                {
                    uint8_t tempByte = cpuC & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuC = cpuC << 1U;
                    if (tempByte != 0)
                    {
                        cpuC |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x02: // rlc D
                {
                    uint8_t tempByte = cpuD & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuD = cpuD << 1U;
                    if (tempByte != 0)
                    {
                        cpuD |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x03: // rlc E
                {
                    uint8_t tempByte = cpuE & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuE = cpuE << 1U;
                    if (tempByte != 0)
                    {
                        cpuE |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x04: // rlc H
                {
                    uint8_t tempByte = cpuH & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuH = cpuH << 1U;
                    if (tempByte != 0)
                    {
                        cpuH |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x05: // rlc L
                {
                    uint8_t tempByte = cpuL & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuL = cpuL << 1U;
                    if (tempByte != 0)
                    {
                        cpuL |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x06: // rlc (HL)
                {
                    unsigned int tempAddr = HL();
                    uint8_t tempByte2 = read8(tempAddr);
                    uint8_t tempByte = tempByte2 & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    tempByte2 = tempByte2 << 1U;
                    if (tempByte != 0)
                    {
                        tempByte2 |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(tempByte2);
                    write8(tempAddr, tempByte2);
                }
                    return 16;
                case 0x07: // rlc A
                {
                    uint8_t tempByte = cpuA & 0x80U; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuA = cpuA << 1U;
                    if (tempByte != 0)
                    {
                        cpuA |= 0x01U;
                        cpuF = 0x10U; // Set carry
                    }
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x08: // rrc B
                {
                    uint8_t tempByte = cpuB & 0x01U;
                    cpuF = 0x00;
                    cpuB = cpuB >> 1U;
                    cpuB &= 0x7fU;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10U;
                        cpuB |= 0x80U;
                    }
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x09: // rrc C
                {
                    uint8_t tempByte = cpuC & 0x01U;
                    cpuF = 0x00;
                    cpuC = cpuC >> 1U;
                    cpuC &= 0x7fU;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10U;
                        cpuC |= 0x80U;
                    }
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x0a: // rrc D
                {
                    uint8_t tempByte = cpuD & 0x01U;
                    cpuF = 0x00;
                    cpuD = cpuD >> 1U;
                    cpuD &= 0x7fU;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10U;
                        cpuD |= 0x80U;
                    }
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x0b: // rrc E
                {
                    uint8_t tempByte = cpuE & 0x01U;
                    cpuF = 0x00;
                    cpuE = cpuE >> 1U;
                    cpuE &= 0x7fU;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10U;
                        cpuE |= 0x80U;
                    }
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x0c: // rrc H
                {
                    uint8_t tempByte = cpuH & 0x01U;
                    cpuF = 0x00;
                    cpuH = cpuH >> 1U;
                    cpuH &= 0x7fU;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10U;
                        cpuH |= 0x80U;
                    }
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x0d: // rrc L
                {
                    uint8_t tempByte = cpuL & 0x01U;
                    cpuF = 0x00;
                    cpuL = cpuL >> 1U;
                    cpuL &= 0x7fU;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10U;
                        cpuL |= 0x80U;
                    }
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x0e: // rrc (HL)
                {
                    unsigned int tempAddr = HL();
                    uint8_t tempByte = read8(tempAddr);
                    uint8_t tempByte2 = tempByte & 0x01U;
                    cpuF = 0x00;
                    tempByte = tempByte >> 1U;
                    tempByte &= 0x7fU;
                    if (tempByte2 != 0)
                    {
                        cpuF = 0x10U;
                        tempByte |= 0x80U;
                    }
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x0f: // rrc A
                {
                    uint8_t tempByte = cpuA & 0x01U;
                    cpuF = 0x00;
                    cpuA = cpuA >> 1U;
                    cpuA &= 0x7fU;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10U;
                        cpuA |= 0x80U;
                    }
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x10: // rl B (rotate carry bit to bit 0 of B)
                {
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x80U) != 0); // Copy bit 7 to carry bit
                    cpuB = cpuB << 1U;
                    if (tempByte != 0)
                    {
                        cpuB |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x11: // rl C
                {
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x80U) != 0); // Copy bit 7 to carry bit
                    cpuC = cpuC << 1U;
                    if (tempByte != 0)
                    {
                        cpuC |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x12: // rl D
                {
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x80U) != 0); // Copy bit 7 to carry bit
                    cpuD = cpuD << 1U;
                    if (tempByte != 0)
                    {
                        cpuD |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x13: // rl E
                {
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x80U) != 0); // Copy bit 7 to carry bit
                    cpuE = cpuE << 1U;
                    if (tempByte != 0)
                    {
                        cpuE |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x14: // rl H
                {
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x80U) != 0); // Copy bit 7 to carry bit
                    cpuH = cpuH << 1U;
                    if (tempByte != 0)
                    {
                        cpuH |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x15: // rl L
                {
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x80U) != 0); // Copy bit 7 to carry bit
                    cpuL = cpuL << 1U;
                    if (tempByte != 0)
                    {
                        cpuL |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x16: // rl (HL)
                {
                    unsigned int tempAddr = HL();
                    uint8_t tempByte2 = read8(tempAddr);
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((tempByte2 & 0x80U) != 0); // Copy bit 7 to carry bit
                    tempByte2 = tempByte2 << 1U;
                    if (tempByte != 0)
                    {
                        tempByte2 |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(tempByte2);
                    write8(tempAddr, tempByte2);
                }
                    return 16;
                case 0x17: // rl A
                {
                    uint8_t tempByte = cpuF & 0x10U; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x80U) != 0); // Copy bit 7 to carry bit
                    cpuA = cpuA << 1U;
                    if (tempByte != 0)
                    {
                        cpuA |= 0x01U; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x18: // rr B (9-bit rotation incl carry bit)
                {
                    uint8_t tempByte = cpuB & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    cpuB = cpuB >> 1U;
                    cpuB = cpuB & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuB |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x19: // rr C
                {
                    uint8_t tempByte = cpuC & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    cpuC = cpuC >> 1U;
                    cpuC = cpuC & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuC |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x1a: // rr D
                {
                    uint8_t tempByte = cpuD & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    cpuD = cpuD >> 1U;
                    cpuD = cpuD & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuD |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x1b: // rr E
                {
                    uint8_t tempByte = cpuE & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    cpuE = cpuE >> 1U;
                    cpuE = cpuE & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuE |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x1c: // rr H
                {
                    uint8_t tempByte = cpuH & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    cpuH = cpuH >> 1U;
                    cpuH = cpuH & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuH |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x1d: // rr L
                {
                    uint8_t tempByte = cpuL & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    cpuL = cpuL >> 1U;
                    cpuL = cpuL & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuL |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x1e: // rr (HL)
                {
                    unsigned int tempAddr = HL();
                    uint8_t tempByte3 = read8(tempAddr);
                    uint8_t tempByte = tempByte3 & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    tempByte3 = tempByte3 >> 1U;
                    tempByte3 = tempByte3 & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        tempByte3 |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_COND(tempByte3 == 0x00);
                    write8(tempAddr, tempByte3);
                }
                    return 16;
                case 0x1f: // rr A
                {
                    uint8_t tempByte = cpuA & 0x01U;
                    uint8_t tempByte2 = cpuF & 0x10U;
                    cpuA = cpuA >> 1U;
                    cpuA = cpuA & 0x7fU;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuA |= 0x80U;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x20: // sla B (shift B left arithmetically)
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x80U) != 0x00);
                    cpuB = cpuB << 1U;
                    SETZ_ON_ZERO(cpuB);
                    return 8;
                case 0x21: // sla C
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x80U) != 0x00);
                    cpuC = cpuC << 1U;
                    SETZ_ON_ZERO(cpuC);
                    return 8;
                case 0x22: // sla D
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x80U) != 0x00);
                    cpuD = cpuD << 1U;
                    SETZ_ON_ZERO(cpuD);
                    return 8;
                case 0x23: // sla E
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x80U) != 0x00);
                    cpuE = cpuE << 1U;
                    SETZ_ON_ZERO(cpuE);
                    return 8;
                case 0x24: // sla H
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x80U) != 0x00);
                    cpuH = cpuH << 1U;
                    SETZ_ON_ZERO(cpuH);
                    return 8;
                case 0x25: // sla L
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x80U) != 0x00);
                    cpuL = cpuL << 1U;
                    SETZ_ON_ZERO(cpuL);
                    return 8;
                case 0x26: // sla (HL)
                {
                    unsigned int tempAddr = HL();
                    uint8_t tempByte = read8(tempAddr);
                    cpuF = 0x00;
                    SETC_ON_COND((tempByte & 0x80U) != 0x00);
                    tempByte = tempByte << 1U;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x27: // sla A
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x80U) != 0x00);
                    cpuA = cpuA << 1U;
                    SETZ_ON_ZERO(cpuA);
                    return 8;
                case 0x28: // sra B (shift B right arithmetically - preserve sign bit)
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x01U) != 0x00);
                    uint8_t tempByte = cpuB & 0x80U;
                    cpuB = cpuB >> 1U;
                    cpuB |= tempByte;
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x29: // sra C
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x01U) != 0x00);
                    uint8_t tempByte = cpuC & 0x80U;
                    cpuC = cpuC >> 1U;
                    cpuC |= tempByte;
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x2a: // sra D
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x01U) != 0x00);
                    uint8_t tempByte = cpuD & 0x80U;
                    cpuD = cpuD >> 1U;
                    cpuD |= tempByte;
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x2b: // sra E
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x01U) != 0x00);
                    uint8_t tempByte = cpuE & 0x80U;
                    cpuE = cpuE >> 1U;
                    cpuE |= tempByte;
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x2c: // sra H
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x01U) != 0x00);
                    uint8_t tempByte = cpuH & 0x80U;
                    cpuH = cpuH >> 1U;
                    cpuH |= tempByte;
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x2d: // sra L
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x01U) != 0x00);
                    uint8_t tempByte = cpuL & 0x80U;
                    cpuL = cpuL >> 1U;
                    cpuL |= tempByte;
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x2e: // sra (HL)
                {
                    cpuF = 0x00;
                    unsigned int tempAddr = HL();
                    uint8_t tempByte = read8(tempAddr);
                    SETC_ON_COND((tempByte & 0x01U) != 0x00);
                    uint8_t tempByte2 = tempByte & 0x80U;
                    tempByte = tempByte >> 1U;
                    tempByte |= tempByte2;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x2f: // sra A
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x01U) != 0x00);
                    uint8_t tempByte = cpuA & 0x80U;
                    cpuA = cpuA >> 1U;
                    cpuA |= tempByte;
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x30: // swap B
                {
                    uint8_t tempByte = (cpuB << 4U);
                    cpuB = cpuB >> 4U;
                    cpuB &= 0x0fU;
                    cpuB |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x31: // swap C
                {
                    uint8_t tempByte = (cpuC << 4U);
                    cpuC = cpuC >> 4U;
                    cpuC &= 0x0fU;
                    cpuC |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x32: // swap D
                {
                    uint8_t tempByte = (cpuD << 4U);
                    cpuD = cpuD >> 4U;
                    cpuD &= 0x0fU;
                    cpuD |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x33: // swap E
                {
                    uint8_t tempByte = (cpuE << 4U);
                    cpuE = cpuE >> 4U;
                    cpuE &= 0x0fU;
                    cpuE |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x34: // swap H
                {
                    uint8_t tempByte = (cpuH << 4U);
                    cpuH = cpuH >> 4U;
                    cpuH &= 0x0fU;
                    cpuH |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x35: // swap L
                {
                    uint8_t tempByte = (cpuL << 4U);
                    cpuL = cpuL >> 4U;
                    cpuL &= 0x0fU;
                    cpuL |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x36: // swap (HL)
                {
                    unsigned int tempAddr = HL();
                    uint8_t tempByte = read8(tempAddr);
                    uint8_t tempByte2 = (tempByte << 4U);
                    tempByte = tempByte >> 4U;
                    tempByte &= 0x0fU;
                    tempByte |= tempByte2;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x37: // swap A
                {
                    uint8_t tempByte = (cpuA << 4U);
                    cpuA = cpuA >> 4U;
                    cpuA &= 0x0fU;
                    cpuA |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x38: // srl B
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x01U) != 0x00);
                    cpuB = cpuB >> 1U;
                    cpuB &= 0x7fU;
                    SETZ_ON_ZERO(cpuB);
                    return 8;
                case 0x39: // srl C
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x01U) != 0x00);
                    cpuC = cpuC >> 1U;
                    cpuC &= 0x7fU;
                    SETZ_ON_ZERO(cpuC);
                    return 8;
                case 0x3a: // srl D
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x01U) != 0x00);
                    cpuD = cpuD >> 1U;
                    cpuD &= 0x7fU;
                    SETZ_ON_ZERO(cpuD);
                    return 8;
                case 0x3b: // srl E
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x01U) != 0x00);
                    cpuE = cpuE >> 1U;
                    cpuE &= 0x7fU;
                    SETZ_ON_ZERO(cpuE);
                    return 8;
                case 0x3c: // srl H
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x01U) != 0x00);
                    cpuH = cpuH >> 1U;
                    cpuH &= 0x7fU;
                    SETZ_ON_ZERO(cpuH);
                    return 8;
                case 0x3d: // srl L
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x01U) != 0x00);
                    cpuL = cpuL >> 1U;
                    cpuL &= 0x7fU;
                    SETZ_ON_ZERO(cpuL);
                    return 8;
                case 0x3e: // srl (HL)
                {
                    unsigned int tempAddr = HL();
                    uint8_t tempByte = read8(tempAddr);
                    cpuF = 0x00;
                    SETC_ON_COND((tempByte & 0x01U) != 0x00);
                    tempByte = tempByte >> 1U;
                    tempByte &= 0x7fU;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x3f: // srl A
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x01U) != 0x00);
                    cpuA = cpuA >> 1U;
                    cpuA &= 0x7fU;
                    SETZ_ON_ZERO(cpuA);
                    return 8;
                case 0x40: // Test bit 0 of B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x01U);
                    return 8;
                case 0x41: // Test bit 0 of C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x01U);
                    return 8;
                case 0x42: // Test bit 0 of D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x01U);
                    return 8;
                case 0x43: // Test bit 0 of E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x01U);
                    return 8;
                case 0x44: // Test bit 0 of H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x01U);
                    return 8;
                case 0x45: // Test bit 0 of L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x01U);
                    return 8;
                case 0x46: // Test bit 0 of (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x01U);
                    return 12;
                case 0x47: // Test bit 0 of A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x01U);
                    return 8;
                case 0x48: // bit 1, B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x02U);
                    return 8;
                case 0x49: // bit 1, C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x02U);
                    return 8;
                case 0x4a: // bit 1, D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x02U);
                    return 8;
                case 0x4b: // bit 1, E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x02U);
                    return 8;
                case 0x4c: // bit 1, H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x02U);
                    return 8;
                case 0x4d: // bit 1, L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x02U);
                    return 8;
                case 0x4e: // bit 1, (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x02U);
                    return 12;
                case 0x4f: // bit 1, A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x02U);
                    return 8;
                case 0x50: // bit 2, B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x04U);
                    return 8;
                case 0x51: // bit 2, C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x04U);
                    return 8;
                case 0x52: // bit 2, D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x04U);
                    return 8;
                case 0x53: // bit 2, E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x04U);
                    return 8;
                case 0x54: // bit 2, H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x04U);
                    return 8;
                case 0x55: // bit 2, L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x04U);
                    return 8;
                case 0x56: // bit 2, (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x04U);
                    return 12;
                case 0x57: // bit 2, A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x04U);
                    return 8;
                case 0x58: // bit 3, B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x08U);
                    return 8;
                case 0x59: // bit 3, C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x08U);
                    return 8;
                case 0x5a: // bit 3, D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x08U);
                    return 8;
                case 0x5b: // bit 3, E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x08U);
                    return 8;
                case 0x5c: // bit 3, H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x08U);
                    return 8;
                case 0x5d: // bit 3, L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x08U);
                    return 8;
                case 0x5e: // bit 3, (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x08U);
                    return 12;
                case 0x5f: // bit 3, A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x08U);
                    return 8;
                case 0x60: // bit 4, B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x10U);
                    return 8;
                case 0x61: // bit 4, C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x10U);
                    return 8;
                case 0x62: // bit 4, D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x10U);
                    return 8;
                case 0x63: // bit 4, E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x10U);
                    return 8;
                case 0x64: // bit 4, H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x10U);
                    return 8;
                case 0x65: // bit 4, L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x10U);
                    return 8;
                case 0x66: // bit 4, (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x10U);
                    return 12;
                case 0x67: // bit 4, A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x10U);
                    return 8;
                case 0x68: // bit 5, B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x20U);
                    return 8;
                case 0x69: // bit 5, C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x20U);
                    return 8;
                case 0x6a: // bit 5, D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x20U);
                    return 8;
                case 0x6b: // bit 5, E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x20U);
                    return 8;
                case 0x6c: // bit 5, H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x20U);
                    return 8;
                case 0x6d: // bit 5, L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x20U);
                    return 8;
                case 0x6e: // bit 5, (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x20U);
                    return 12;
                case 0x6f: // bit 5, A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x20U);
                    return 8;
                case 0x70: // bit 6, B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x40U);
                    return 8;
                case 0x71: // bit 6, C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x40U);
                    return 8;
                case 0x72: // bit 6, D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x40U);
                    return 8;
                case 0x73: // bit 6, E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x40U);
                    return 8;
                case 0x74: // bit 6, H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x40U);
                    return 8;
                case 0x75: // bit 6, L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x40U);
                    return 8;
                case 0x76: // bit 6, (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x40U);
                    return 12;
                case 0x77: // bit 6, A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x40U);
                    return 8;
                case 0x78: // bit 7, B
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuB & 0x80U);
                    return 8;
                case 0x79: // bit 7, C
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuC & 0x80U);
                    return 8;
                case 0x7a: // bit 7, D
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuD & 0x80U);
                    return 8;
                case 0x7b: // bit 7, E
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuE & 0x80U);
                    return 8;
                case 0x7c: // bit 7, H
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuH & 0x80U);
                    return 8;
                case 0x7d: // bit 7, L
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuL & 0x80U);
                    return 8;
                case 0x7e: // bit 7, (HL)
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(R8_HL() & 0x80U);
                    return 12;
                case 0x7f: // bit 7, A
                    cpuF &= 0x30U;
                    cpuF |= 0x20U;
                    SETZ_ON_ZERO(cpuA & 0x80U);
                    return 8;
                case 0x80: // res 0, B
                    cpuB &= 0xfeU;
                    return 8;
                case 0x81: // res 0, C
                    cpuC &= 0xfeU;
                    return 8;
                case 0x82: // res 0, D
                    cpuD &= 0xfeU;
                    return 8;
                case 0x83: // res 0, E
                    cpuE &= 0xfeU;
                    return 8;
                case 0x84: // res 0, H
                    cpuH &= 0xfeU;
                    return 8;
                case 0x85: // res 0, L
                    cpuL &= 0xfeU;
                    return 8;
                case 0x86: // res 0, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xfeU);
                }
                    return 16;
                case 0x87: // res 0, A
                    cpuA &= 0xfeU;
                    return 8;
                case 0x88: // res 1, B
                    cpuB &= 0xfdU;
                    return 8;
                case 0x89: // res 1, C
                    cpuC &= 0xfdU;
                    return 8;
                case 0x8a: // res 1, D
                    cpuD &= 0xfdU;
                    return 8;
                case 0x8b: // res 1, E
                    cpuE &= 0xfdU;
                    return 8;
                case 0x8c: // res 1, H
                    cpuH &= 0xfdU;
                    return 8;
                case 0x8d: // res 1, L
                    cpuL &= 0xfdU;
                    return 8;
                case 0x8e: // res 1, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xfdU);
                }
                    return 16;
                case 0x8f: // res 1, A
                    cpuA &= 0xfdU;
                    return 8;
                case 0x90: // res 2, B
                    cpuB &= 0xfbU;
                    return 8;
                case 0x91: // res 2, C
                    cpuC &= 0xfbU;
                    return 8;
                case 0x92: // res 2, D
                    cpuD &= 0xfbU;
                    return 8;
                case 0x93: // res 2, E
                    cpuE &= 0xfbU;
                    return 8;
                case 0x94: // res 2, H
                    cpuH &= 0xfbU;
                    return 8;
                case 0x95: // res 2, L
                    cpuL &= 0xfbU;
                    return 8;
                case 0x96: // res 2, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xfbU);
                }
                    return 16;
                case 0x97: // res 2, A
                    cpuA &= 0xfbU;
                    return 8;
                case 0x98: // res 3, B
                    cpuB &= 0xf7U;
                    return 8;
                case 0x99: // res 3, C
                    cpuC &= 0xf7U;
                    return 8;
                case 0x9a: // res 3, D
                    cpuD &= 0xf7U;
                    return 8;
                case 0x9b: // res 3, E
                    cpuE &= 0xf7U;
                    return 8;
                case 0x9c: // res 3, H
                    cpuH &= 0xf7U;
                    return 8;
                case 0x9d: // res 3, L
                    cpuL &= 0xf7U;
                    return 8;
                case 0x9e: // res 3, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xf7U);
                }
                    return 16;
                case 0x9f: // res 3, A
                    cpuA &= 0xf7U;
                    return 8;
                case 0xa0: // res 4, B
                    cpuB &= 0xefU;
                    return 8;
                case 0xa1: // res 4, C
                    cpuC &= 0xefU;
                    return 8;
                case 0xa2: // res 4, D
                    cpuD &= 0xefU;
                    return 8;
                case 0xa3: // res 4, E
                    cpuE &= 0xefU;
                    return 8;
                case 0xa4: // res 4, H
                    cpuH &= 0xefU;
                    return 8;
                case 0xa5: // res 4, L
                    cpuL &= 0xefU;
                    return 8;
                case 0xa6: // res 4, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xefU);
                }
                    return 16;
                case 0xa7: // res 4, A
                    cpuA &= 0xefU;
                    return 8;
                case 0xa8: // res 5, B
                    cpuB &= 0xdfU;
                    return 8;
                case 0xa9: // res 5, C
                    cpuC &= 0xdfU;
                    return 8;
                case 0xaa: // res 5, D
                    cpuD &= 0xdfU;
                    return 8;
                case 0xab: // res 5, E
                    cpuE &= 0xdfU;
                    return 8;
                case 0xac: // res 5, H
                    cpuH &= 0xdfU;
                    return 8;
                case 0xad: // res 5, L
                    cpuL &= 0xdfU;
                    return 8;
                case 0xae: // res 5, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xdfU);
                }
                    return 16;
                case 0xaf: // res 5, A
                    cpuA &= 0xdfU;
                    return 8;
                case 0xb0: // res 6, B
                    cpuB &= 0xbfU;
                    return 8;
                case 0xb1: // res 6, C
                    cpuC &= 0xbfU;
                    return 8;
                case 0xb2: // res 6, D
                    cpuD &= 0xbfU;
                    return 8;
                case 0xb3: // res 6, E
                    cpuE &= 0xbfU;
                    return 8;
                case 0xb4: // res 6, H
                    cpuH &= 0xbfU;
                    return 8;
                case 0xb5: // res 6, L
                    cpuL &= 0xbfU;
                    return 8;
                case 0xb6: // res 6, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xbfU);
                }
                    return 16;
                case 0xb7: // res 6, A
                    cpuA &= 0xbfU;
                    return 8;
                case 0xb8: // res 7, B
                    cpuB &= 0x7fU;
                    return 8;
                case 0xb9: // res 7, C
                    cpuC &= 0x7fU;
                    return 8;
                case 0xba: // res 7, D
                    cpuD &= 0x7fU;
                    return 8;
                case 0xbb: // res 7, E
                    cpuE &= 0x7fU;
                    return 8;
                case 0xbc: // res 7, H
                    cpuH &= 0x7fU;
                    return 8;
                case 0xbd: // res 7, L
                    cpuL &= 0x7fU;
                    return 8;
                case 0xbe: // res 7, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0x7fU);;
                }
                    return 16;
                case 0xbf: // res 7, A
                    cpuA &= 0x7fU;
                    return 8;
                case 0xc0: // set 0, B
                    cpuB |= 0x01U;
                    return 8;
                case 0xc1: // set 0, C
                    cpuC |= 0x01U;
                    return 8;
                case 0xc2: // set 0, D
                    cpuD |= 0x01U;
                    return 8;
                case 0xc3: // set 0, E
                    cpuE |= 0x01U;
                    return 8;
                case 0xc4: // set 0, H
                    cpuH |= 0x01U;
                    return 8;
                case 0xc5: // set 0, L
                    cpuL |= 0x01U;
                    return 8;
                case 0xc6: // set 0, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x01U);
                }
                    return 16;
                case 0xc7: // set 0, A
                    cpuA |= 0x01U;
                    return 8;
                case 0xc8: // set 1, B
                    cpuB |= 0x02U;
                    return 8;
                case 0xc9: // set 1, C
                    cpuC |= 0x02U;
                    return 8;
                case 0xca: // set 1, D
                    cpuD |= 0x02U;
                    return 8;
                case 0xcb: // set 1, E
                    cpuE |= 0x02U;
                    return 8;
                case 0xcc: // set 1, H
                    cpuH |= 0x02U;
                    return 8;
                case 0xcd: // set 1, L
                    cpuL |= 0x02U;
                    return 8;
                case 0xce: // set 1, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x02U);
                }
                    return 16;
                case 0xcf: // set 1, A
                    cpuA |= 0x02U;
                    return 8;
                case 0xd0: // set 2, B
                    cpuB |= 0x04U;
                    return 8;
                case 0xd1: // set 2, C
                    cpuC |= 0x04U;
                    return 8;
                case 0xd2: // set 2, D
                    cpuD |= 0x04U;
                    return 8;
                case 0xd3: // set 2, E
                    cpuE |= 0x04U;
                    return 8;
                case 0xd4: // set 2, H
                    cpuH |= 0x04U;
                    return 8;
                case 0xd5: // set 2, L
                    cpuL |= 0x04U;
                    return 8;
                case 0xd6: // set 2, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x04U);
                }
                    return 16;
                case 0xd7: // set 2, A
                    cpuA |= 0x04U;
                    return 8;
                case 0xd8: // set 3, B
                    cpuB |= 0x08U;
                    return 8;
                case 0xd9: // set 3, C
                    cpuC |= 0x08U;
                    return 8;
                case 0xda: // set 3, D
                    cpuD |= 0x08U;
                    return 8;
                case 0xdb: // set 3, E
                    cpuE |= 0x08U;
                    return 8;
                case 0xdc: // set 3, H
                    cpuH |= 0x08U;
                    return 8;
                case 0xdd: // set 3, L
                    cpuL |= 0x08U;
                    return 8;
                case 0xde: // set 3, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x08U);
                }
                    return 16;
                case 0xdf: // set 3, A
                    cpuA |= 0x08U;
                    return 8;
                case 0xe0: // set 4, B
                    cpuB |= 0x10U;
                    return 8;
                case 0xe1: // set 4, C
                    cpuC |= 0x10U;
                    return 8;
                case 0xe2: // set 4, D
                    cpuD |= 0x10U;
                    return 8;
                case 0xe3: // set 4, E
                    cpuE |= 0x10U;
                    return 8;
                case 0xe4: // set 4, H
                    cpuH |= 0x10U;
                    return 8;
                case 0xe5: // set 4, L
                    cpuL |= 0x10U;
                    return 8;
                case 0xe6: // set 4, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x10U);
                }
                    return 16;
                case 0xe7: // set 4, A
                    cpuA |= 0x10U;
                    return 8;
                case 0xe8: // set 5, B
                    cpuB |= 0x20U;
                    return 8;
                case 0xe9: // set 5, C
                    cpuC |= 0x20U;
                    return 8;
                case 0xea: // set 5, D
                    cpuD |= 0x20U;
                    return 8;
                case 0xeb: // set 5, E
                    cpuE |= 0x20U;
                    return 8;
                case 0xec: // set 5, H
                    cpuH |= 0x20U;
                    return 8;
                case 0xed: // set 5, L
                    cpuL |= 0x20U;
                    return 8;
                case 0xee: // set 5, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x20U);
                }
                    return 16;
                case 0xef: // set 5, A
                    cpuA |= 0x20U;
                    return 8;
                case 0xf0: // set 6, B
                    cpuB |= 0x40U;
                    return 8;
                case 0xf1: // set 6, C
                    cpuC |= 0x40U;
                    return 8;
                case 0xf2: // set 6, D
                    cpuD |= 0x40U;
                    return 8;
                case 0xf3: // set 6, E
                    cpuE |= 0x40U;
                    return 8;
                case 0xf4: // set 6, H
                    cpuH |= 0x40U;
                    return 8;
                case 0xf5: // set 6, L
                    cpuL |= 0x40U;
                    return 8;
                case 0xf6: // set 6, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x40U);
                }
                    return 16;
                case 0xf7: // set 6, A
                    cpuA |= 0x40U;
                    return 8;
                case 0xf8: // set 7, B
                    cpuB |= 0x80U;
                    return 8;
                case 0xf9: // set 7, C
                    cpuC |= 0x80U;
                    return 8;
                case 0xfa: // set 7, D
                    cpuD |= 0x80U;
                    return 8;
                case 0xfb: // set 7, E
                    cpuE |= 0x80U;
                    return 8;
                case 0xfc: // set 7, H
                    cpuH |= 0x80U;
                    return 8;
                case 0xfd: // set 7, L
                    cpuL |= 0x80U;
                    return 8;
                case 0xfe: // set 7, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x80U);
                }
                    return 16;
                case 0xff: // set 7, A
                    cpuA |= 0x80U;
                    return 8;
            }
        case 0xcc: // call Z, nn
            if ((cpuF & 0x80U) != 0x00)
            {
                uint8_t msb = read8(cpuPc + 1);
                uint8_t lsb = read8(cpuPc + 2);
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
#endif
                cpuSp -= 2;
                cpuPc += 3;
                write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 24;
            }
            else
            {
                cpuPc += 3;
                return 12;
            }
        case 0xcd: // call nn
        {
            uint8_t msb = read8(cpuPc + 1);
            uint8_t lsb = read8(cpuPc + 2);
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc += 3;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
        }
            return 24;
        case 0xce: // adc A, n
        {
            uint8_t tempByte = read8(cpuPc + 1);
            uint8_t tempByte2 = cpuF & 0x10U;
            cpuF = 0x00;
            if (tempByte2 != 0x00)
            {
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            tempByte = tempByte & 0x0fU;
            tempByte2 = cpuA & 0x0fU;
            SETH_ON_COND(tempByte > tempByte2);
        }
            cpuPc += 2;
            return 8;
        case 0xcf: // rst 8 (call 0x0008)
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x8;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x0008;
            return 16;
        case 0xd0: // ret NC
            if ((cpuF & 0x10U) != 0x00) {
                cpuPc++;
                return 8;
            } else {
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallReturned = 1;
                }
#endif
                uint8_t msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 20;
            }
        case 0xd1: // pop DE
            read16(cpuSp, &cpuE, &cpuD);
            cpuSp += 2;
            cpuPc++;
            return 12;
        case 0xd2: // j NC, nn
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1);
                return 16;
            }
        case 0xd3: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xd4: // call NC, nn
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                uint8_t msb = read8(cpuPc + 1);
                uint8_t lsb = read8(cpuPc + 2);
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
#endif
                cpuSp -= 2;
                cpuPc += 3;
                write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 24;
            }
        case 0xd5: // push DE
            cpuSp -= 2;
            write16(cpuSp, cpuE, cpuD);
            cpuPc++;
            return 16;
        case 0xd6: // sub A, n
        {
            uint8_t msb = read8(cpuPc + 1);
            cpuF = 0x40;
            SETC_ON_COND(msb > cpuA);
            SETH_ON_COND((msb & 0x0fU) > (cpuA & 0x0fU));
            cpuA -= msb;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
        }
            cpuPc += 2;
            return 8;
        case 0xd7: // rst 10
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x10U;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x0010;
            return 16;
        case 0xd8: // ret C
            if ((cpuF & 0x10U) != 0x00) {
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallReturned = 1;
                }
#endif
                uint8_t msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 20;
            } else {
                cpuPc++;
                return 8;
            }
        case 0xd9: // reti
        {
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallReturned = 1;
            }
#endif
            uint8_t msb, lsb;
            read16(cpuSp, &msb, &lsb);
            cpuSp += 2;
            cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
            cpuIme = true;
        }
            return 16;
        case 0xda: // j C, nn (abs jump if carry)
            if ((cpuF & 0x10U) != 0x00)
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1);
                return 16;
            }
            else
            {
                cpuPc += 3;
                return 12;
            }
        case 0xdb: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xdc: // call C, nn
            if ((cpuF & 0x10U) != 0x00) {
                uint8_t msb = read8(cpuPc + 1);
                uint8_t lsb = read8(cpuPc + 2);
#ifdef _WIN32
                if (debugger.totalBreakEnables > 0) {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
#endif
                cpuSp -= 2;
                cpuPc += 3;
                write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
                cpuPc = ((unsigned int)lsb << 8U) + (unsigned int)msb;
                return 24;
            } else {
                cpuPc += 3;
                return 12;
            }
        case 0xdd: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xde: // sbc A, n
        {
            uint8_t tempByte = cpuA;
            uint8_t tempByte2 = cpuF & 0x10U;
            cpuF = 0x40;
            cpuA -= read8(cpuPc + 1);
            if (tempByte2 != 0x00)
            {
                if (cpuA == 0x00)
                {
                    cpuF |= 0x30U;
                }
                cpuA--;
            }
            SETC_ON_COND(cpuA > tempByte);
            SETZ_ON_ZERO(cpuA);
            tempByte2 = tempByte & 0x0fU;
            tempByte = cpuA & 0x0fU;
            SETH_ON_COND(tempByte > tempByte2);
        }
            cpuPc += 2;
            return 8;
        case 0xdf: // rst 18
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x18;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x0018;
            return 16;
        case 0xe0: // ldh (n), A (load to IO port n - ff00 + n)
            write8(0xff00 + (unsigned int)read8(cpuPc + 1), cpuA);
            cpuPc += 2;
            return 12;
        case 0xe1: // pop HL
            read16(cpuSp, &cpuL, &cpuH);
            cpuSp += 2;
            cpuPc++;
            return 12;
        case 0xe2: // ldh (C), A (load to IO port C - ff00 + C)
            write8(0xff00 + (unsigned int)cpuC, cpuA);
            cpuPc++;
            return 8;
        case 0xe3: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xe4: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xe5: // push HL
            cpuSp -= 2;
            write16(cpuSp, cpuL, cpuH);
            cpuPc++;
            return 16;
        case 0xe6: // and n
            cpuA = cpuA & read8(cpuPc + 1);
            cpuF = 0x20U;
            SETZ_ON_ZERO(cpuA);
            cpuPc += 2;
            return 8;
        case 0xe7: // rst 20
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x20U;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x0020;
            return 16;
        case 0xe8: // add SP, d
        {
            uint8_t msb = read8(cpuPc + 1);
            cpuF = 0x00;
            if (msb >= 0x80)
            {
                unsigned int tempAddr = 256 - (unsigned int)msb;
                cpuSp -= tempAddr;
                SETC_ON_COND((cpuSp & 0x0000ffffU) > (tempAddr & 0x0000ffffU));
                SETH_ON_COND((cpuSp & 0x000000ffU) > (tempAddr & 0x000000ffU));
            }
            else
            {
                auto tempAddr = (unsigned int)msb;
                cpuSp += tempAddr;
                SETC_ON_COND((cpuSp & 0x0000ffffU) < (tempAddr & 0x0000ffffU));
                SETH_ON_COND((cpuSp & 0x000000ffU) < (tempAddr & 0x000000ffU));
            }
        }
            cpuPc += 2;
            return 16;
        case 0xe9: // j HL
            cpuPc = HL();
            return 4;
        case 0xea: // ld (nn), A
            write8(((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1), cpuA);
            cpuPc += 3;
            return 16;
        case 0xeb: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xec: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xed: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xee: // xor n
            cpuA = cpuA ^ read8(cpuPc + 1);
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc += 2;
            return 8;
        case 0xef: // rst 28
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x28;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x0028;
            return 16;
        case 0xf0: // ldh A, (n)
            cpuA = read8(0xff00 + (unsigned int)read8(cpuPc + 1));
            cpuPc += 2;
            return 12;
        case 0xf1: // pop AF
            read16(cpuSp, &cpuF, &cpuA);
            cpuF &= 0xf0U;
            cpuSp += 2;
            cpuPc++;
            return 12;
        case 0xf2: // ldh A, C
            cpuA = read8(0xff00 + (unsigned int)cpuC);
            cpuPc++;
            return 8;
        case 0xf3: // di
            cpuIme = false;
            cpuPc++;
            return 4;
        case 0xf4: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xf5: // push AF
            cpuSp -= 2;
            write16(cpuSp, cpuF, cpuA);
            cpuPc++;
            return 16;
        case 0xf6: // or n
            cpuA = cpuA | read8(cpuPc + 1);
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc += 2;
            return 8;
        case 0xf7: // rst 30
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x30U;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x0030;
            return 16;
        case 0xf8: // ld HL, SP+d
        {
            uint8_t msb = read8(cpuPc + 1);
            cpuF = 0x00;
            unsigned int tempAddr = cpuSp;
            if (msb >= 0x80)
            {
                tempAddr -= 256 - (unsigned int)msb;
                SETC_ON_COND(tempAddr > cpuSp);
                SETH_ON_COND((tempAddr & 0x00ffffffU) > (cpuSp & 0x00ffffffU));
            }
            else
            {
                tempAddr += (unsigned int)msb;
                SETC_ON_COND(cpuSp > tempAddr);
                SETH_ON_COND((cpuSp & 0x00ffffffU) > (tempAddr & 0x00ffffffU));
            }
            cpuH = (uint8_t)(tempAddr >> 8U);
            cpuL = (uint8_t)(tempAddr & 0xffU);
        }
            cpuPc += 2;
            return 12;
        case 0xf9: // ld SP, HL
            cpuSp = HL();
            cpuPc++;
            return 8;
        case 0xfa: // ld A, (nn)
            cpuA = read8(((unsigned int)read8(cpuPc + 2) << 8U) + (unsigned int)read8(cpuPc + 1));
            cpuPc += 3;
            return 16;
        case 0xfb: // ei
            cpuPc++;
            cpuIme = true;
            return 4;
        case 0xfc: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xfd: // REMOVED INSTRUCTION
            return runInvalidInstruction(instr);
        case 0xfe: // cp n
        {
            uint8_t msb = read8(cpuPc + 1);
            cpuF = 0x40;
            SETH_ON_COND((msb & 0x0fU) > (cpuA & 0x0fU));
            SETC_ON_COND(msb > cpuA);
            SETZ_ON_COND(cpuA == msb);
        }
            cpuPc += 2;
            return 8;
        case 0xff: // rst 38
#ifdef _WIN32
            if (debugger.totalBreakEnables > 0) {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x38;
                debugger.breakLastCallReturned = 0;
            }
#endif
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (uint8_t)(cpuPc & 0xffU), (uint8_t)(cpuPc >> 8U));
            cpuPc = 0x0038;
            return 16;
    }
    return 0;
}


void Gbc::speedUp() {
    if (currentClockMultiplierCombo < 20) {
        currentClockMultiplierCombo++;
        clockMultiply = CLOCK_MULTIPLIERS[currentClockMultiplierCombo];
        clockDivide = CLOCK_DIVISORS[currentClockMultiplierCombo];
    }
}

void Gbc::slowDown() {
    if (currentClockMultiplierCombo > 0) {
        currentClockMultiplierCombo--;
        clockMultiply = CLOCK_MULTIPLIERS[currentClockMultiplierCombo];
        clockDivide = CLOCK_DIVISORS[currentClockMultiplierCombo];
    }
}

#define READ_STREAM(var, type) stream.read(reinterpret_cast<char*>(&var), sizeof(type))
#define READ_STREAM_A(var, type, count) stream.read(reinterpret_cast<char*>(var), sizeof(type) * count)
void Gbc::loadSaveState(std::istream& stream) {
    READ_STREAM(cpuPc, uint32_t);
    READ_STREAM(cpuSp, uint32_t);
    READ_STREAM(cpuA, uint8_t);
    READ_STREAM(cpuB, uint8_t);
    READ_STREAM(cpuC, uint8_t);
    READ_STREAM(cpuD, uint8_t);
    READ_STREAM(cpuE, uint8_t);
    READ_STREAM(cpuF, uint8_t);
    READ_STREAM(cpuH, uint8_t);
    READ_STREAM(cpuL, uint8_t);
    READ_STREAM(cpuIme, bool);
    READ_STREAM(clocksAcc, int32_t);
    READ_STREAM(cpuClockFreq, int64_t);
    READ_STREAM(cpuDividerCount, uint32_t);
    READ_STREAM(cpuTimerCount, uint32_t);
    READ_STREAM(cpuTimerIncTime, uint32_t);
    READ_STREAM(cpuTimerRunning, bool);
    READ_STREAM(serialRequest, bool);
    READ_STREAM(serialIsTransferring, bool);
    READ_STREAM(serialClockIsExternal, bool);
    READ_STREAM(serialTimer, int32_t);
    READ_STREAM(gpuClockFactor, int32_t);
    READ_STREAM(gpuTimeInMode, int32_t);
    READ_STREAM(blankedScreen, bool);
    READ_STREAM(needClear, bool);
    READ_STREAM(cpuMode, uint32_t);
    READ_STREAM(gpuMode, uint32_t);
    READ_STREAM(isRunning, bool);
    READ_STREAM(isPaused, bool);
    READ_STREAM(accessOam, bool);
    READ_STREAM(accessVram, bool);
    READ_STREAM(romProperties, RomProperties);
    READ_STREAM(clockMultiply, int64_t);
    READ_STREAM(clockDivide, int64_t);
    READ_STREAM(currentClockMultiplierCombo, int32_t);
    READ_STREAM(bankOffset, uint32_t);
    READ_STREAM(wramBankOffset, uint32_t);
    READ_STREAM(vramBankOffset, uint32_t);
    READ_STREAM_A(wram.data(), uint8_t, 8 * 4096);
    READ_STREAM_A(vram.data(), uint8_t, 2 * 8192);
    READ_STREAM_A(ioPorts.data(), uint8_t, 256);
    READ_STREAM_A(oam, uint8_t, 160);
    READ_STREAM_A(tileSet, uint32_t, 2 * 384 * 8 * 8);
    READ_STREAM(sgb.readingCommand, bool);
    READ_STREAM_A(sgb.commandBytes, uint32_t, 7 * 16);
    READ_STREAM_A(sgb.commandBits, uint8_t, 8);
    READ_STREAM(sgb.command, uint32_t);
    READ_STREAM(sgb.readCommandBits, int32_t);
    READ_STREAM(sgb.readCommandBytes, int32_t);
    READ_STREAM(sgb.freezeScreen, bool);
    READ_STREAM(sgb.freezeMode, uint32_t);
    READ_STREAM(sgb.multEnabled, bool);
    READ_STREAM(sgb.noPlayers, uint32_t);
    READ_STREAM(sgb.noPacketsSent, uint32_t);
    READ_STREAM(sgb.noPacketsToSend, uint32_t);
    READ_STREAM(sgb.readJoypadID, uint32_t);
    READ_STREAM_A(sgb.monoData, uint32_t, 160 * 152);
    READ_STREAM_A(sgb.mappedVramForTrnOp, uint8_t, 4096);
    READ_STREAM_A(sgb.palettes, uint32_t, 4 * 4);
    READ_STREAM_A(sgb.sysPalettes, uint32_t, 512 * 4);
    READ_STREAM_A(sgb.chrPalettes, uint32_t, 18 * 20);
    READ_STREAM_A(translatedPaletteBg, uint32_t, 4);
    READ_STREAM_A(translatedPaletteObj, uint32_t, 8);
    READ_STREAM_A(sgbPaletteTranslationBg, uint32_t, 4);
    READ_STREAM_A(sgbPaletteTranslationObj, uint32_t, 8);
    READ_STREAM_A(cgbBgPalData, uint8_t, 64);
    READ_STREAM(cgbBgPalIndex, uint32_t);
    READ_STREAM(cgbBgPalIncr, uint32_t);
    READ_STREAM_A(cgbBgPalette, uint32_t, 32);
    READ_STREAM_A(cgbObjPalData, uint8_t, 64);
    READ_STREAM(cgbObjPalIndex, uint32_t);
    READ_STREAM(cgbObjPalIncr, uint32_t);
    READ_STREAM_A(cgbObjPalette, uint32_t, 32);
    READ_STREAM(lastLYCompare, uint32_t);
    READ_STREAM(sram.hasBattery, bool);
    READ_STREAM(sram.hasTimer, bool);
    READ_STREAM_A(sram.timerData, unsigned char, 5);
    READ_STREAM(sram.timerMode, uint32_t);
    READ_STREAM(sram.timerLatch, uint32_t);
    READ_STREAM(sram.bankOffset, uint32_t);
    READ_STREAM(sram.sizeEnum, uint8_t);
    READ_STREAM(sram.sizeBytes, uint32_t);
    READ_STREAM(sram.bankSelectMask, unsigned char);
    READ_STREAM(sram.enableFlag, bool);
    READ_STREAM_A(sram.data, uint8_t, sram.sizeBytes);
    READ_STREAM(keys, InputSet);
    READ_STREAM(keyStateChanged, bool);
}

#define WRITE_STREAM(var, type) stream.write(reinterpret_cast<char*>(&var), sizeof(type))
#define WRITE_STREAM_A(var, type, count) stream.write(reinterpret_cast<char*>(var), sizeof(type) * count)
void Gbc::saveSaveState(std::ostream& stream) {
    WRITE_STREAM(cpuPc, uint32_t);
    WRITE_STREAM(cpuSp, uint32_t);
    WRITE_STREAM(cpuA, uint8_t);
    WRITE_STREAM(cpuB, uint8_t);
    WRITE_STREAM(cpuC, uint8_t);
    WRITE_STREAM(cpuD, uint8_t);
    WRITE_STREAM(cpuE, uint8_t);
    WRITE_STREAM(cpuF, uint8_t);
    WRITE_STREAM(cpuH, uint8_t);
    WRITE_STREAM(cpuL, uint8_t);
    WRITE_STREAM(cpuIme, bool);
    WRITE_STREAM(clocksAcc, int32_t);
    WRITE_STREAM(cpuClockFreq, int64_t);
    WRITE_STREAM(cpuDividerCount, uint32_t);
    WRITE_STREAM(cpuTimerCount, uint32_t);
    WRITE_STREAM(cpuTimerIncTime, uint32_t);
    WRITE_STREAM(cpuTimerRunning, bool);
    WRITE_STREAM(serialRequest, bool);
    WRITE_STREAM(serialIsTransferring, bool);
    WRITE_STREAM(serialClockIsExternal, bool);
    WRITE_STREAM(serialTimer, int32_t);
    WRITE_STREAM(gpuClockFactor, int32_t);
    WRITE_STREAM(gpuTimeInMode, int32_t);
    WRITE_STREAM(blankedScreen, bool);
    WRITE_STREAM(needClear, bool);
    WRITE_STREAM(cpuMode, uint32_t);
    WRITE_STREAM(gpuMode, uint32_t);
    WRITE_STREAM(isRunning, bool);
    WRITE_STREAM(isPaused, bool);
    WRITE_STREAM(accessOam, bool);
    WRITE_STREAM(accessVram, bool);
    WRITE_STREAM(romProperties, RomProperties);
    WRITE_STREAM(clockMultiply, int64_t);
    WRITE_STREAM(clockDivide, int64_t);
    WRITE_STREAM(currentClockMultiplierCombo, int32_t);
    WRITE_STREAM(bankOffset, uint32_t);
    WRITE_STREAM(wramBankOffset, uint32_t);
    WRITE_STREAM(vramBankOffset, uint32_t);
    WRITE_STREAM_A(wram.data(), uint8_t, 8 * 4096);
    WRITE_STREAM_A(vram.data(), uint8_t, 2 * 8192);
    WRITE_STREAM_A(ioPorts.data(), uint8_t, 256);
    WRITE_STREAM_A(oam, uint8_t, 160);
    WRITE_STREAM_A(tileSet, uint32_t, 2 * 384 * 8 * 8);
    WRITE_STREAM(sgb.readingCommand, bool);
    WRITE_STREAM_A(sgb.commandBytes, uint32_t, 7 * 16);
    WRITE_STREAM_A(sgb.commandBits, uint8_t, 8);
    WRITE_STREAM(sgb.command, uint32_t);
    WRITE_STREAM(sgb.readCommandBits, int32_t);
    WRITE_STREAM(sgb.readCommandBytes, int32_t);
    WRITE_STREAM(sgb.freezeScreen, bool);
    WRITE_STREAM(sgb.freezeMode, uint32_t);
    WRITE_STREAM(sgb.multEnabled, bool);
    WRITE_STREAM(sgb.noPlayers, uint32_t);
    WRITE_STREAM(sgb.noPacketsSent, uint32_t);
    WRITE_STREAM(sgb.noPacketsToSend, uint32_t);
    WRITE_STREAM(sgb.readJoypadID, uint32_t);
    WRITE_STREAM_A(sgb.monoData, uint32_t, 160 * 152);
    WRITE_STREAM_A(sgb.mappedVramForTrnOp, uint8_t, 4096);
    WRITE_STREAM_A(sgb.palettes, uint32_t, 4 * 4);
    WRITE_STREAM_A(sgb.sysPalettes, uint32_t, 512 * 4);
    WRITE_STREAM_A(sgb.chrPalettes, uint32_t, 18 * 20);
    WRITE_STREAM_A(translatedPaletteBg, uint32_t, 4);
    WRITE_STREAM_A(translatedPaletteObj, uint32_t, 8);
    WRITE_STREAM_A(sgbPaletteTranslationBg, uint32_t, 4);
    WRITE_STREAM_A(sgbPaletteTranslationObj, uint32_t, 8);
    WRITE_STREAM_A(cgbBgPalData, uint8_t, 64);
    WRITE_STREAM(cgbBgPalIndex, uint32_t);
    WRITE_STREAM(cgbBgPalIncr, uint32_t);
    WRITE_STREAM_A(cgbBgPalette, uint32_t, 32);
    WRITE_STREAM_A(cgbObjPalData, uint8_t, 64);
    WRITE_STREAM(cgbObjPalIndex, uint32_t);
    WRITE_STREAM(cgbObjPalIncr, uint32_t);
    WRITE_STREAM_A(cgbObjPalette, uint32_t, 32);
    WRITE_STREAM(lastLYCompare, uint32_t);
    WRITE_STREAM(sram.hasBattery, bool);
    WRITE_STREAM(sram.hasTimer, bool);
    WRITE_STREAM_A(sram.timerData, unsigned char, 5);
    WRITE_STREAM(sram.timerMode, uint32_t);
    WRITE_STREAM(sram.timerLatch, uint32_t);
    WRITE_STREAM(sram.bankOffset, uint32_t);
    WRITE_STREAM(sram.sizeEnum, uint8_t);
    WRITE_STREAM(sram.sizeBytes, uint32_t);
    WRITE_STREAM(sram.bankSelectMask, unsigned char);
    WRITE_STREAM(sram.enableFlag, bool);
    WRITE_STREAM_A(sram.data, uint8_t, sram.sizeBytes);
    WRITE_STREAM(keys, InputSet);
    WRITE_STREAM(keyStateChanged, bool);
}
