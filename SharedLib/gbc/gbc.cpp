#include "gbc.h"

#include "colourutils.h"

#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cassert>

uint32_t stockPaletteBg[4] = { 0xffffffff, 0xff88b0b0, 0xff507878, 0xff000000 };
uint32_t stockPaletteObj1[4] = { 0xffffffff, 0xff5050f0, 0xff2020a0, 0xff000000 };
uint32_t stockPaletteObj2[4] = { 0xffffffff, 0xffa0a0a0, 0xff404040, 0xff000000 };

#define GPU_HBLANK    0x00
#define GPU_VBLANK    0x01
#define GPU_SCAN_OAM  0x02
#define GPU_SCAN_VRAM 0x03

#define GB_FREQ  4194304
#define SGB_FREQ 4295454
#define GBC_FREQ 8400000

#define MBC_NONE 0x00
#define MBC1     0x01
#define MBC2     0x02
#define MBC3     0x03
#define MBC4     0x04
#define MBC5     0x05
#define MMM01    0x11

const unsigned char OFFICIAL_LOGO[48] = {
        0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
        0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
        0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e
};

Gbc::Gbc() {
    // Flag not running and no ROM loaded
    running = false;
    romProperties.valid = false;
    clockMultiply = 1;
    clockDivide = 1;

    // Allocate emulated RAM
    rom = new unsigned char[256 * 16384];
    wram = new unsigned char[4 * 4096];
    vram = new unsigned char[2 * 8192];
    ioPorts = new unsigned char[256];
    tileSet = new unsigned int[2 * 384 * 8 * 8]; // 2 VRAM banks, 384 tiles, 8 rows, 8 pixels per row
    sgb.monoData = new unsigned int[160 * 152];
    sgb.mappedVramForTrnOp = new unsigned char[4096];
    sgb.palettes = new unsigned int[4 * 4];
    sgb.sysPalettes = new unsigned int[512 * 4]; // 512 palettes, 4 colours per palette, RGB
    sgb.chrPalettes = new unsigned int[18 * 20];

    // Clear things that should be cleared
    std::fill(tileSet, tileSet + 2 * 384 * 8 * 8, 0);
    std::fill(sgb.palettes, sgb.palettes + 4 * 4, 0);
    std::fill(sgb.sysPalettes, sgb.sysPalettes + 512 * 4, 0);
    std::fill(sgb.chrPalettes, sgb.chrPalettes + 18 * 20, 0);
}

Gbc::~Gbc() {
    // Release emulated RAM
    delete[] rom;
    delete[] wram;
    delete[] vram;
    delete[] ioPorts;
    delete[] tileSet;
    delete[] sgb.monoData;
    delete[] sgb.mappedVramForTrnOp;
    delete[] sgb.palettes;
    delete[] sgb.sysPalettes;
    delete[] sgb.chrPalettes;
}

void Gbc::throwException(unsigned char instruction) {
    //std::string msg = "Illegal operation - " + std::to_string((int)instruction);
    //throw new std::runtime_error(msg);
}

void Gbc::doWork(uint64_t timeDiffMillis, InputSet& inputs) {
    static int noClocks = 0;
    if (running) {
        // Determine how many clock cycles to emulate, cap at 1000000 (about a quarter of a second)
        noClocks += (int)((double)timeDiffMillis * 0.001 * (double)(cpuClockFreq * clockMultiply / clockDivide));
        if (noClocks > 1000000) {
            noClocks = 1000000;
        }

        // Copy inputs
        keys.keyDir = inputs.keyDir;
        keys.keyBut = inputs.keyBut;

        // Execute this many clock cycles and catch errors
        //try
        //{
        noClocks -= execute(noClocks);
        //}
        //catch (const std::runtime_error& err)
        //{
        //	noClocks = 0;
        //	std::cerr << err.what() << std::endl;
        //}
    }
}

bool Gbc::loadRom(std::string fileName, const unsigned char* data, int dataLength, AppPlatform& appPlatform) {
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
    unsigned char lastByte = romProperties.title[15];
    if ((lastByte == 0x80) || (lastByte == 0xc0)) {
        romProperties.cgbFlag = true;
        romProperties.title[11] = '\0';
    } else {
        romProperties.cgbFlag = false;
        romProperties.title[16] = '\0';
    }

    // Next two bytes are licensee codes
    // Then is an SGB flag, cartridge type, and cartridge ROM and RAM sizes
    romProperties.sgbFlag = data[0x0146];
    romProperties.cartType = data[0x0147];
    romProperties.sizeEnum = data[0x0148];
    sram.sizeEnum = data[0x0149];
    if (romProperties.sgbFlag != 0x03) {
        romProperties.sgbFlag = 0x00;
    }

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
            romProperties.bankSelectMask = 0x0f;
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
            romProperties.bankSelectMask = 0x7f;
            break;
        case 0x07:
            romProperties.sizeBytes = 4194304;
            romProperties.bankSelectMask = 0xff;
            break;
        case 0x52:
            romProperties.sizeBytes = 1179648;
            romProperties.bankSelectMask = 0x7f;
            break;
        case 0x53:
            romProperties.sizeBytes = 1310720;
            romProperties.bankSelectMask = 0x7f;
            break;
        case 0x54:
            romProperties.sizeBytes = 1572864;
            romProperties.bankSelectMask = 0x7f;
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
        unsigned char testByte = data[0x0134 + n];
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
    memcpy(rom, data, dataLength);

    // Remember this file to re-open next time
    //saveLastFileName(FileName, 512);

    return true;
}

void Gbc::reset() {
    // Make sure a valid ROM is loaded
    if (!romProperties.valid) {
        running = false;
        return;
    }

    // Initialise control variables
    cpuIme = false;
    cpuHalted = false;
    cpuStopped = false;
    gpuMode = GPU_SCAN_OAM;
    gpuTimeInMode = 0;
    keys.clear();
    keyStateChanged = false;
    accessOam = true;
    accessVram = true;
    serialRequest = false;
    serialClockIsExternal = false;
    debugger.breakCode = 0;
    needClear = false;

    // Resetting IO ports may avoid graphical glitches when switching to a colour game. Clearing VRAM may help too.
    std::fill(ioPorts, ioPorts + 256, 0);
    std::fill(vram, vram + 16384, 0);

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
    cpuH = 0x01;
    cpuL = 0x4d;
    ioPorts[5] = 0x00;
    ioPorts[6] = 0x00;
    ioPorts[7] = 0x00;
    ioPorts[16] = 0x80;
    ioPorts[17] = 0xbf;
    ioPorts[18] = 0xf3;
    ioPorts[20] = 0xbf;
    ioPorts[22] = 0x3f;
    ioPorts[23] = 0x00;
    ioPorts[25] = 0xbf;
    ioPorts[26] = 0x7f;
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
        cpuA = 0x01;
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
        cpuA = 0x01;
        ioPorts[38] = 0xf1;
        sgb.freezeScreen = false;
        sgb.multEnabled = false;
    }

    // Other stuff:
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

    running = true;
}

int Gbc::execute(int ticks) {
    unsigned char instr, msb, lsb, interruptable;
    int startClocksAcc;
    int displayEnabled;

    // Increment clocks accumulator
    clocksAcc += ticks;
    startClocksAcc = clocksAcc;

    // Handle key state if required
    if (keyStateChanged) {
        // Adjust value in keypad register
        if ((ioPorts[0x00] & 0x30) == 0x20) {
            ioPorts[0x00] &= 0xf0;
            ioPorts[0x00] |= keys.keyDir;
        } else if ((ioPorts[0x00] & 0x30) == 0x10) {
            ioPorts[0x00] &= 0xf0;
            ioPorts[0x00] |= keys.keyBut;
        }

        // Set interrupt request flag
        ioPorts[0x0f] |= 0x10;
        keyStateChanged = false;
    }

    while (clocksAcc > 0) {
        if (debugger.breakCode > 0) {
            running = false;
            break;
        }

        instr = read8(cpuPc);
        msb = read8(cpuPc + 1);
        lsb = read8(cpuPc + 2);

        // Run appropriate RunOp function, depending on opcode
        // Pass three bytes in case all are needed, sets PC increment and clocks taken
        unsigned int clocksSub = performOp();
        cpuPc &= 0xffff; // Clamp PC to 16 bits
        clocksAcc -= clocksSub; // Dependent on instruction run
        clocksRun += clocksSub;

        // Check for interrupts:
        if (cpuIme || cpuHalted) {
            msb = ioPorts[0xff];
            if (msb != 0x00) {
                lsb = ioPorts[0x0f];
                if (lsb != 0x00) {
                    interruptable = msb & lsb;
                    if ((interruptable & 0x01) != 0x00) {
                        // VBlank
                        if (cpuHalted) {
                            cpuPc++;
                        }
                        cpuHalted = false;
                        if (!cpuIme) {
                            instr = read8(cpuPc);
                            goto end_of_int_check;
                        }
                        cpuIme = false;
                        ioPorts[0x0f] &= 0x1e;
                        cpuSp -= 2; // Pushing PC onto stack
                        write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                        cpuPc = 0x0040;
                        instr = read8(cpuPc);
                        goto end_of_int_check;
                    }
                    if ((interruptable & 0x02) != 0x00) {
                        // LCD Stat
                        if (cpuHalted) {
                            cpuPc++;
                        }
                        cpuHalted = false;
                        if (!cpuIme) {
                            instr = read8(cpuPc);
                            goto end_of_int_check;
                        }
                        cpuIme = false;
                        ioPorts[0x0f] &= 0x1d;
                        cpuSp -= 2;
                        write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                        cpuPc = 0x0048;
                        instr = read8(cpuPc);
                        goto end_of_int_check;
                    }
                    if ((interruptable & 0x04) != 0x00) {
                        // Timer
                        if (cpuHalted) {
                            cpuPc++;
                        }
                        cpuHalted = false;
                        if (!cpuIme) {
                            instr = read8(cpuPc);
                            goto end_of_int_check;
                        }
                        cpuIme = false;
                        ioPorts[0x0f] &= 0x1b;
                        cpuSp -= 2;
                        write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                        cpuPc = 0x0050;
                        instr = read8(cpuPc);
                        goto end_of_int_check;
                    }
                    if ((interruptable & 0x08) != 0x00) {
                        // Serial
                        if (cpuHalted) {
                            cpuPc++;
                        }
                        cpuHalted = false;
                        if (!cpuIme) {
                            instr = read8(cpuPc);
                            goto end_of_int_check;
                        }
                        cpuIme = false;
                        ioPorts[0x0f] &= 0x17;
                        cpuSp -= 2;
                        write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                        cpuPc = 0x0058;
                        instr = read8(cpuPc);
                        goto end_of_int_check;
                    }
                    if ((interruptable & 0x10) != 0x00) {
                        // Joypad
                        if (cpuHalted) {
                            cpuPc++;
                        }
                        cpuHalted = false;
                        if (!cpuIme) {
                            instr = read8(cpuPc);
                            goto end_of_int_check;
                        }
                        cpuIme = false;
                        ioPorts[0x0f] &= 0x0f;
                        cpuSp -= 2;
                        write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                        cpuPc = 0x0060;
                        instr = read8(cpuPc);
                        goto end_of_int_check;
                    }
                }
            }
        }

        end_of_int_check:
        if (debugger.breakOnPc) {
            if (cpuPc == debugger.breakPcAddr) {
                debugger.breakCode = 3;
            }
        }

        displayEnabled = ioPorts[0x0040] & 0x80;

        // Permanent compare of LY and LYC
        if ((ioPorts[0x44] == ioPorts[0x45]) && (displayEnabled != 0)) {
            ioPorts[0x41] |= 0x04; // Set coincidence flag
            // Request interrupt if this signal goes low to high
            if (((ioPorts[0x41] & 0x40) != 0x00) && (lastLYCompare == 0)) {
                ioPorts[0x0f] |= 0x02;
            }
            lastLYCompare = 1;
        } else {
            ioPorts[0x41] &= 0xfb; // Clear coincidence flag
            lastLYCompare = 0;
        }

        // Handling of timers
        cpuDividerCount += clocksSub;
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
                    ioPorts[0x0f] |= 0x04;
                }
            }
        }

        // Handle serial port timeout
        if (serialIsTransferring) {
            if (!serialClockIsExternal) {
                serialTimer -= clocksSub;
                if (serialTimer <= 0) {
                    serialIsTransferring = 0;
                    ioPorts[0x02] &= 0x03; // Clear the transferring indicator
                    ioPorts[0x0f] |= 0x08; // Request a serial interrupt
                    /*if (ggbc2.Running != 0) {
                        mem.RequestSerial = 1;
                        gpu.TimeInMode += clocks_sub / GPU_ClockFactor; // Don't skip the LCD timer
                        StartClockAcc -= clocks_acc;
                        clocks_acc = 0;
                        return StartClockAcc;
                    }
                    else*/
                    ioPorts[1] = 0xff;
                }
            } else {
                if (serialTimer == 1) {
                    serialTimer = 0;
                    /*if (ggbc2.Running != 0) {
                        mem.RequestSerial = 1;
                        gpu.TimeInMode += clocks_sub / GPU_ClockFactor; // Don't skip the LCD timer
                        StartClockAcc -= clocks_acc;
                        clocks_acc = 0;
                        return StartClockAcc;
                    }*/
                }
            }
        }

        // Handle GPU timings
        if (displayEnabled != 0) {
            gpuTimeInMode += clocksSub / gpuClockFactor; // GPU clock factor accounts for double speed mode
            switch (gpuMode) {
                case GPU_HBLANK:
                    // Spends 204 cycles here, then moves to next line. After 144th hblank, move to vblank.
                    if (gpuTimeInMode >= 204) {
                        gpuTimeInMode -= 204;
                        ioPorts[0x0044]++;
                        if (ioPorts[0x0044] == 144) {
                            gpuMode = GPU_VBLANK;
                            ioPorts[0x0041] &= 0xfc; // address 0xff41 - LCD status register
                            ioPorts[0x0041] |= GPU_VBLANK;
                            if (displayEnabled) {
                                // Set interrupt request for VBLANK
                                ioPorts[0x000f] |= 0x01;
                            }
                            accessOam = true;
                            accessVram = true;

                            if ((ioPorts[0x0041] & 0x10) != 0x00) {
                                // Request status int if condition met
                                ioPorts[0x000f] |= 0x02;
                            }
                            // This is where stuff can be drawn - on the beginning of the vblank
                            if (!sgb.freezeScreen && (displayEnabled != 0)) {
                                if (frameManager.frameIsInProgress()) {
                                    auto frameBuffer = frameManager.getInProgressFrameBuffer();
                                    if ((frameBuffer != nullptr) && romProperties.sgbFlag) {
                                        sgb.colouriseFrame(frameBuffer);
                                    }
                                    frameManager.finishCurrentFrame();
                                }
                                startClocksAcc -= clocksAcc;
                                clocksAcc = 0;
                                return startClocksAcc;
                            }
                        } else {
                            gpuMode = GPU_SCAN_OAM;
                            ioPorts[0x0041] &= 0xfc;
                            ioPorts[0x0041] |= GPU_SCAN_OAM;
                            if (displayEnabled) {
                                accessOam = false;
                                accessVram = true;
                            }
                            if ((ioPorts[0x0041] & 0x20) != 0x00) {
                                // Request status int if condition met
                                ioPorts[0x000f] |= 0x02;
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
                            ioPorts[0x0041] &= 0xfc;
                            ioPorts[0x0041] |= GPU_SCAN_OAM;
                            ioPorts[0x0044] = 0;
                            if (displayEnabled) {
                                accessOam = false;
                                accessVram = true;
                            }
                            if ((ioPorts[0x0041] & 0x20) != 0x00) {
                                // Request status int if condition met
                                ioPorts[0x000f] |= 0x02;
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
                        ioPorts[0x0041] &= 0xfc;
                        ioPorts[0x0041] |= GPU_SCAN_VRAM;
                        if (displayEnabled) {
                            accessOam = false;
                            accessVram = false;
                        }
                    }
                    break;
                case GPU_SCAN_VRAM:
                    if (gpuTimeInMode >= 172) {
                        gpuTimeInMode -= 172;
                        gpuMode = GPU_HBLANK;
                        ioPorts[0x0041] &= 0xfc;
                        ioPorts[0x0041] |= GPU_HBLANK;
                        accessOam = true;
                        accessVram = true;
                        if ((ioPorts[0x0041] & 0x08) != 0x00) {
                            // Request status int if condition met
                            ioPorts[0x000f] |= 0x02;
                        }
                        // Run DMA if applicable
                        if (ioPorts[0x55] < 0xff) {
                            unsigned int tempAddr, tempAddr2;
                            // H-blank DMA currently active
                            tempAddr = (ioPorts[0x51] << 8) + ioPorts[0x52]; // DMA source
                            if ((tempAddr & 0xe000) == 0x8000) {
                                // Don't do transfers within VRAM
                                goto end_dma_op;
                            }
                            if (tempAddr >= 0xe000) {
                                // Don't take source data from these addresses either
                                goto end_dma_op;
                            }
                            tempAddr2 = (ioPorts[0x53] << 8) + ioPorts[0x54] + 0x8000; // DMA destination
                            for (int count = 0; count < 16; count++) {
                                write8(tempAddr2, read8(tempAddr));
                                tempAddr++;
                                tempAddr2++;
                                tempAddr2 &= 0x9fff; // Keep it within VRAM
                            }
                            end_dma_op:
                            //if (ClockFreq == GBC_FREQ) clocks_acc -= 64;
                            //else clocks_acc -= 32;
                            ioPorts[0x55]--;
                            if (ioPorts[0x55] < 0x80) {
                                // End the DMA
                                ioPorts[0x55] = 0xff;
                            }
                        }

                        // Process current line's graphics
                        if (frameManager.frameIsInProgress()) {
                            (*this.*readLine)(frameManager.getInProgressFrameBuffer());
                        }
                    }
                    break;
                default: // Error that should never happen:
                    running = false;
                    break;
            }
        }
        else {
            ioPorts[0x0044] = 0;
            gpuTimeInMode = 0;
            gpuMode = GPU_SCAN_OAM;
            if (!blankedScreen) {
                accessOam = true;
                accessVram = true;

                ///////////////////////////////////
                // SIGNAL DRAWING HERE
                ///////////////////////////////////

                blankedScreen = true;
                startClocksAcc -= clocksAcc;
                clocksAcc = 0;
                return startClocksAcc;
            }
        }
    }

    startClocksAcc -= clocksAcc;
    clocksAcc = 0;
    return startClocksAcc;
}

unsigned char Gbc::read8(unsigned int address) {
    address &= 0xffff;
    if (debugger.breakOnRead) {
        if ((address == debugger.breakReadAddr) && (debugger.breakCode != 5)) {
            debugger.breakCode = 5;
            debugger.breakReadByte = (unsigned int)read8(address);
        }
    }

    if (address < 0x4000) {
        return rom[address];
    } else if (address < 0x8000) {
        return rom[bankOffset + (address & 0x3fff)];
    } else if (address < 0xa000) {
        if (accessVram) {
            return vram[vramBankOffset + (address & 0x1fff)];
        } else {
            return 0xff;
        }
    }
    else if (address < 0xc000) {
        if (sram.enableFlag) {
            if (!sram.hasTimer) {
                return sram.read(address & 0x1fff);
            } else {
                /*if (sram.timerMode > 0)
                    return sram.timerData[(unsigned int)sram.timerMode - 0x08];
                else */
                if (sram.bankOffset < 0x8000) {
                    return sram.read(address & 0x1fff);
                } else {
                    return 0;
                }
            }
        } else {
            return 0x00;
        }
    } else if (address < 0xd000) {
        return wram[address & 0x0fff];
    } else if (address < 0xe000) {
        return wram[wramBankOffset + (address & 0x0fff)];
    } else if (address < 0xf000) {
        return wram[address & 0x0fff];
    } else if (address < 0xfe00) {
        return wram[wramBankOffset + (address & 0x0fff)];
    } else if (address < 0xfea0) {
        if (accessOam) {
            return oam[(address & 0x00ff) % 160];
        } else {
            return 0xff;
        }
    } else if (address < 0xff00) {
        return 0xff;
    } else if (address < 0xff80) {
        return readIO(address & 0x7f);
    } else {
        return ioPorts[(address & 0xff)];
    }
}

void Gbc::read16(unsigned int address, unsigned char* msb, unsigned char* lsb) {
    address &= 0xffff;
    if (address < 0x4000) {
        *msb = rom[address];
        *lsb = rom[address + 1];
        return;
    } else if (address < 0x8000) {
        *msb = rom[bankOffset + (address & 0x3fff)];
        *lsb = rom[bankOffset + ((address + 1) & 0x3fff)];
        return;
    } else if (address < 0xa000) {
        if (accessVram) {
            *msb = vram[vramBankOffset + (address & 0x1fff)];
            *lsb = vram[vramBankOffset + ((address + 1) & 0x1fff)];
            return;
        } else {
            *msb = 0xff;
            *lsb = 0xff;
            return;
        }
    } else if (address < 0xc000) {
        if (sram.enableFlag) {
            *msb = sram.read(address);
            *lsb = sram.read(address + 1);
            return;
        } else {
            *msb = 0xff;
            *lsb = 0xff;
            return;
        }
    } else if (address < 0xd000) {
        *msb = wram[address & 0x0fff];
        *lsb = wram[(address + 1) & 0x0fff];
        return;
    } else if (address < 0xe000) {
        *msb = wram[wramBankOffset + (address & 0x0fff)];
        *lsb = wram[wramBankOffset + ((address + 1) & 0x0fff)];
        return;
    } else if (address < 0xf000) {
        *msb = wram[address & 0x0fff];
        *lsb = wram[(address + 1) & 0x0fff];
        return;
    } else if (address < 0xfe00) {
        *msb = wram[wramBankOffset + (address & 0x0fff)];
        *lsb = wram[wramBankOffset + ((address + 1) & 0x0fff)];
        return;
    } else if (address < 0xfea0) {
        if (accessOam) {
            *msb = oam[(address & 0x00ff) % 160];
            *lsb = oam[((address + 1) & 0x00ff) % 160];
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
        *msb = readIO(address & 0x7f);
        *lsb = readIO((address + 1) & 0x7f);
        return;
    } else {
        *msb = ioPorts[address & 0xff];
        *lsb = ioPorts[(address + 1) & 0xff];
        return;
    }
}

void Gbc::write8(unsigned int address, unsigned char byte) {
    address &= 0xffff;
    if (debugger.breakOnWrite) {
        if (address == debugger.breakWriteAddr) {
            debugger.breakWriteByte = (unsigned int)byte;
            debugger.breakCode = 4;
        }
    }
    if (address < 0x8000) {
        switch (romProperties.mbc) {
            case MBC1:
                if (address < 0x2000) {
                    // Only 4 bits are used. Writing 0xa enables SRAM
                    byte = byte & 0x0f;
                    sram.enableFlag = byte == 0x0a;
                    return;
                } else if (address < 0x4000) {
                    // Set low 5 bits of bank number
                    bankOffset &= 0xfff80000;
                    byte = byte & 0x1f;
                    if (byte == 0x00) {
                        byte++;
                    }
                    bankOffset |= ((unsigned int)byte * 0x4000);
                    return;
                } else if (address < 0x6000) {
                    byte &= 0x03;
                    if (romProperties.mbcMode != 0) {
                        sram.bankOffset = (unsigned int)byte * 0x2000; // Select RAM bank
                    } else {
                        bankOffset &= 0xffe7c000;
                        bankOffset |= (unsigned int)byte * 0x80000;
                    }
                    return;
                } else {
                    if (sram.sizeBytes > 8192) {
                        romProperties.mbcMode = byte & 0x01;
                    } else {
                        romProperties.mbcMode = 0;
                    }
                    return;
                }
            case MBC2:
                if (address < 0x1000) {
                    // Only 4 bits are used. Writing 0xa enables SRAM.
                    byte = byte & 0x0f;
                    sram.enableFlag = byte == 0x0a;
                    return;
                } else if (address < 0x2100) {
                    return;
                } else if (address < 0x21ff) {
                    byte &= 0x0f;
                    byte &= romProperties.bankSelectMask;
                    if (byte == 0) {
                        byte++;
                    }
                    bankOffset = (unsigned int)byte * 0x4000;
                    return;
                }
                return;
            case MBC3:
                if (address < 0x2000) {
                    byte = byte & 0x0f;
                    sram.enableFlag = byte == 0x0a; // Also enables timer registers
                    if (sram.enableFlag) {
                        if (debugger.breakOnSramEnable) {
                            debugger.breakCode = 1;
                        }
                    } else {
                        if (debugger.breakOnSramDisable) {
                            debugger.breakCode = 2;
                        }
                    }
                    return;
                } else if (address < 0x4000) {
                    byte &= romProperties.bankSelectMask;
                    if (byte == 0) {
                        byte++;
                    }
                    bankOffset = (unsigned int)byte * 0x4000;
                    return;
                } else if (address < 0x6000) {
                    byte &= 0x0f;
                    if (byte < 0x04) {
                        sram.bankOffset = (unsigned int)byte * 0x2000;
                        sram.timerMode = 0;
                    } else if ((byte >= 0x08) && (byte < 0x0d)) {
                        sram.timerMode = (unsigned int)byte;
                    } else {
                        sram.timerMode = 0;
                    }
                    return;
                } else {
                    byte &= 0x01;
                    if ((sram.timerLatch == 0x00) && (byte == 0x01)) {
                        latchTimerData();
                    }
                    sram.timerLatch = (unsigned int)byte;
                    return;
                }
                break;
            case MBC5:
                if (address < 0x2000) {
                    byte = byte & 0x0f;
                    sram.enableFlag = byte == 0x0a;
                    return;
                } else if (address < 0x3000) {
                    // Set lower 8 bits of 9-bit reg in MBC5
                    bankOffset &= 0x00400000;
                    bankOffset |= (unsigned int)byte * 0x4000;
                    if (bankOffset == 0) {
                        // Only exclusion with MBC5 is bank 0
                        bankOffset = 0x4000;
                    }
                    return;
                }
                else if (address < 0x4000) {
                    // Set bit 9
                    byte &= 0x01;
                    bankOffset &= 0x003fc000;
                    if (byte != 0x00) {
                        bankOffset |= 0x00400000;
                    }
                    if (bankOffset == 0) {
                        // Only exclusion with MBC5 is bank 0
                        bankOffset = 0x00004000;
                    }
                    return;
                } else if (address < 0x6000) {
                    // Set 4-bit RAM bank register
                    byte &= 0x0f;
                    sram.bankOffset = (unsigned int)byte * 0x2000;
                    return;
                }

                // Writing to 0x6000 - 0x7fff does nothing
                return;
        }
    } else if (address < 0xa000) {
        if (accessVram) {
            address = address & 0x1fff;
            vram[vramBankOffset + address] = byte;
            if (address < 0x1800) {
                // Pre-calculate pixels in tile set
                address = address & 0x1ffe;
                unsigned int byte1 = 0xff & (unsigned int)vram[vramBankOffset + address];
                unsigned int byte2 = 0xff & (unsigned int)vram[vramBankOffset + address + 1];
                address = address * 4;
                if (vramBankOffset != 0) {
                    address += 24576;
                }
                tileSet[address++] = ((byte2 >> 6) & 0x02) + (byte1 >> 7);
                tileSet[address++] = ((byte2 >> 5) & 0x02) + ((byte1 >> 6) & 0x01);
                tileSet[address++] = ((byte2 >> 4) & 0x02) + ((byte1 >> 5) & 0x01);
                tileSet[address++] = ((byte2 >> 3) & 0x02) + ((byte1 >> 4) & 0x01);
                tileSet[address++] = ((byte2 >> 2) & 0x02) + ((byte1 >> 3) & 0x01);
                tileSet[address++] = ((byte2 >> 1) & 0x02) + ((byte1 >> 2) & 0x01);
                tileSet[address++] = (byte2 & 0x02) + ((byte1 >> 1) & 0x01);
                tileSet[address] = ((byte2 << 1) & 0x02) + (byte1 & 0x01);
            }
        }
    } else if (address < 0xc000) {
        if (sram.enableFlag) {
            if (sram.hasTimer) {
                if (sram.timerMode > 0) {
                    latchTimerData();
                    sram.writeTimerData((unsigned int)sram.timerMode, byte);
                    sram.timerData[(int)(sram.timerMode - 0)] = byte;
                } else if (sram.bankOffset < 0x8000) {
                    sram.write(address, byte);
                }
            } else {
                if (romProperties.mbc == MBC2) {
                    byte &= 0x0f;
                }
                sram.write(address, byte);
            }
        }
    } else if (address < 0xd000) {
        wram[address & 0x0fff] = byte;
    } else if (address < 0xe000) {
        wram[wramBankOffset + (address & 0x0fff)] = byte;
    } else if (address < 0xf000) {
        wram[address & 0x0fff] = byte;
    } else if (address < 0xfe00) {
        wram[wramBankOffset + (address & 0x0fff)] = byte;
    } else if (address < 0xfea0) {
        if (accessOam) {
            oam[(address & 0x00ff) % 160] = byte;
        }
    } else if (address < 0xff00) {
        return; // Unusable
    } else if (address < 0xff80) {
        writeIO(address & 0x007f, byte);
    } else {
        ioPorts[address & 0x00ff] = byte;
    }

}

void Gbc::write16(unsigned int address, unsigned char msb, unsigned char lsb) {
    address &= 0xffff;
    if (debugger.breakOnWrite) {
        if (address == debugger.breakWriteAddr) {
            debugger.breakWriteByte = (unsigned int)msb;
            debugger.breakCode = 4;
        } else if ((address + 1) == debugger.breakWriteAddr) {
            debugger.breakWriteByte = (unsigned int)lsb;
            debugger.breakCode = 4;
        }
    }
    if (address < 0x8000) {
        write8(address, msb);
        write8(address + 1, lsb);
    } else if (address < 0x9fff) {
        if (accessVram) {
            vram[vramBankOffset + (address & 0x1fff)] = msb;
            vram[vramBankOffset + ((address + 1) & 0x1fff)] = lsb;
        }
    } else if (address < 0xbfff) {
        if (sram.enableFlag) {
            if (romProperties.mbc == MBC2) {
                msb &= 0x0f;
                lsb &= 0x0f;
            }
            sram.write(address, msb);
            sram.write(address + 1, lsb);
        }
    } else if (address < 0xcfff) {
        wram[address & 0x0fff] = msb;
        wram[(address + 1) & 0x0fff] = lsb;
    } else if (address < 0xdfff) {
        wram[wramBankOffset + (address & 0x0fff)] = msb;
        wram[wramBankOffset + ((address + 1) & 0x0fff)] = lsb;
    } else if (address < 0xefff) {
        wram[address & 0x0fff] = msb;
        wram[(address + 1) & 0x0fff] = lsb;
    } else if (address < 0xfdff) {
        wram[wramBankOffset + (address & 0x0fff)] = msb;
        wram[wramBankOffset + ((address + 1) & 0x0fff)] = lsb;
    } else if (address < 0xfe9f) {
        if (accessOam) {
            oam[(address & 0x00ff) % 160] = msb;
            oam[(address & 0x00ff + 1) % 160] = lsb;
        }
    } else if (address < 0xfeff) {
        // Unusable
        return;
    } else if (address < 0xff7f) {
        writeIO(address & 0x007f, msb);
        writeIO((address + 1) & 0x007f, lsb);
    } else {
        ioPorts[address & 0x00ff] = msb;
        ioPorts[(address + 1) & 0x00ff] = lsb;
    }
}

unsigned char Gbc::readIO(unsigned int ioIndex) {
    unsigned char byte;
    switch (ioIndex) {
        case 0x00: // Used for keypad status
            byte = ioPorts[0] & 0x30;
            if (byte == 0x20) {
                return keys.keyDir; // Note that only bits 0-3 are read here
            } else if (byte == 0x10) {
                return keys.keyBut;
            } else if ((sgb.multEnabled != 0x00) && (byte == 0x30)) {
                return sgb.readJoypadID;
            } else {
                return 0x0f;
            }
        case 0x01: // Serial data
            return ioPorts[1];
        case 0x02: // Serial control
            return ioPorts[2];
        case 0x11: // NR11
            return ioPorts[0x11] & 0xc0;
        case 0x13: // NR13
            return 0;
        case 0x14: // NR14
            return ioPorts[0x14] & 0x40;
        case 0x16: // NR21
            return ioPorts[0x16] & 0xc0;
        case 0x18: // NR23
            return 0;
        case 0x19: // NR24
            return ioPorts[0x19] & 0x40;
        case 0x1a: // NR30
            return ioPorts[0x1a] & 0x80;
        case 0x1c: // NR32
            return ioPorts[0x14] & 0x60;
        case 0x1d: // NR33
            return 0;
        case 0x1e: // NR34
            return ioPorts[0x1e] & 0x40;
        case 0x23: // NR44
            return ioPorts[0x23] & 0x40;
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

void Gbc::writeIO(unsigned int ioIndex, unsigned char data) {
    unsigned char byte;
    unsigned int word, count;
    switch (ioIndex) {
        case 0x00:
            byte = data & 0x30;
            if (romProperties.sgbFlag) {
                if (byte == 0x00) {
                    if (!sgb.readingCommand) {
                        // Begin command packet transfer:
                        sgb.readingCommand = true;
                        sgb.readCommandBits = 0;
                        sgb.readCommandBytes = 0;
                        sgb.noPacketsSent = 0;
                        sgb.noPacketsToSend = 1; // Will get amended later if needed
                    }
                    ioPorts[0] = byte;
                } else if (byte == 0x20) {
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
                } else if (byte == 0x10) {
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
                } else if ((sgb.multEnabled != 0x00) && (!sgb.readingCommand)) {
                    if (ioPorts[0] < 0x30) {
                        sgb.readJoypadID--;
                        if (sgb.readJoypadID < 0x0c) sgb.readJoypadID = 0x0f;
                    }
                    ioPorts[0] = byte;
                } else {
                    ioPorts[0] = byte;
                }
            } else {
                ioPorts[0] = byte;
                if (byte == 0x20) {
                    ioPorts[0] |= keys.keyDir;
                } else if (byte == 0x10) {
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
            if ((data & 0x80) != 0x00) {
                byte = data;
                ioPorts[2] = data & 0x83;
                serialIsTransferring = true;
                if ((data & 0x01) != 0) {
                    // Attempt to send a byte
                    serialClockIsExternal = false;
                    serialTimer = 512 * 1;
                    if (romProperties.cgbFlag == 0x00) {
                        ioPorts[2] |= 0x02;
                    } else if ((data & 0x02) != 0x00) {
                        serialTimer /= 32;
                    }
                } else {
                    // Listen for a transfer
                    serialClockIsExternal = true;
                    serialTimer = 1;
                }
            } else {
                ioPorts[2] = data & 0x83;
                serialIsTransferring = false;
                serialRequest = false;
            }
            return;
        case 0x04: // Divider register (writing resets to 0)
            ioPorts[0x04] = 0x00;
            return;
        case 0x07: // Timer control
            byte = data & 0x04;
            if (byte != 0x00) {
                cpuTimerRunning = true;
            }
            else cpuTimerRunning = false;
            switch (data & 0x03) {
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
            ioPorts[0x07] = data & 0x07;
            return;
        case 0x40: // LCD ctrl
            if (data < 128) {
                accessVram = true;
                accessOam = true;
                ioPorts[0x44] = 0;
                if (ioPorts[0x40] >= 0x80) {
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
                                frameBuffer[ioIndex] = 0x000000ff;
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
            ioPorts[0x41] &= 0x07; // Bits 0-2 are read-only. Bit 7 doesn't exist.
            ioPorts[0x41] |= (data & 0x78);
            return;
        case 0x44: // LCD Line No (read-only)
            return;
        case 0x46: // Launch OAM DMA transfer
            ioPorts[0x46] = data;
            if (data < 0x80) {
                return; // Cannot copy from ROM in this way
            }
            word = ((unsigned int)data) << 8;
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
            if ((data & 0x01) != 0x00) {
                ioPorts[0x4d] |= 0x01;
            } else {
                ioPorts[0x4d] &= 0x80;
            }
            return;
        case 0x4f: // VRAM bank
            if (romProperties.cgbFlag) {
                ioPorts[0x4f] = data & 0x01; // 1-bit register
                vramBankOffset = (unsigned int)(data & 0x01) * 0x2000;
            }
            return;
        case 0x51: // HDMA1
            ioPorts[0x51] = data;
            return;
        case 0x52: // HDMA2
            ioPorts[0x52] = data & 0xf0;
            return;
        case 0x53: // HDMA3
            ioPorts[0x53] = data & 0x1f;
            return;
        case 0x54: // HDMA4
            ioPorts[0x54] = data;
            return;
        case 0x55: // HDMA5 (initiates DMA transfer from ROM or RAM to VRAM)
            if (!romProperties.cgbFlag) {
                return;
            }
            if ((data & 0x80) == 0x00) {
                // General purpose DMA
                if (ioPorts[0x55] != 0xff) {
                    // H-blank DMA already running
                    ioPorts[0x55] = data; // Can be used to halt H-blank DMA
                    return;
                }
                word = (ioPorts[0x51] << 8) + ioPorts[0x52]; // DMA source
                if ((word & 0xe000) == 0x8000) {
                    // Don't do transfers within VRAM
                    return;
                }
                if (word >= 0xe000) {
                    // Don't take source data from these addresses either
                    return;
                }
                unsigned int word2 = (ioPorts[0x53] << 8) + ioPorts[0x54] + 0x8000; // DMA destination
                unsigned int bytesToTransfer = data & 0x7f;
                bytesToTransfer++;
                bytesToTransfer *= 16;
                for (count = 0; count < bytesToTransfer; count++) {
                    write8(word2, read8(word));
                    word++;
                    word2++;
                    word2 &= 0x9fff; // Keep it within VRAM
                }
                //if (ClockFreq == GBC_FREQ) clocks_acc -= BytesToTransfer * 4;
                //else clocks_acc -= BytesToTransfer * 2;
                ioPorts[0x55] = 0xff;
            } else {
                // H-blank DMA
                ioPorts[0x55] = data;
            }
            return;
        case 0x56: // Infrared
            ioPorts[0x56] = (data & 0xc1) | 0x02; // Setting bit 2 indicates 'no light received'
            return;
        case 0x68: // CGB background palette index
            ioPorts[0x68] = data & 0xbf; // There is no bit 6
            cgbBgPalIndex = data & 0x3f;
            if ((data & 0x80) != 0x00) {
                cgbBgPalIncr = 1;
            } else {
                cgbBgPalIncr = 0;
            }
            return;
        case 0x69: // CBG background palette data (using address set by 0xff68)
            cgbBgPalData[cgbBgPalIndex] = data;
            cgbBgPalette[cgbBgPalIndex >> 1] = REMAP_555_8888((unsigned int)cgbBgPalData[cgbBgPalIndex & 0xfe], (unsigned int)cgbBgPalData[cgbBgPalIndex | 0x01]);
            if (cgbBgPalIncr) {
                cgbBgPalIndex++;
                cgbBgPalIndex &= 0x3f;
                ioPorts[0x68]++;
                ioPorts[0x68] &= 0xbf;
            }
            return;
        case 0x6a: // CGB sprite palette index
            ioPorts[0x6a] = data & 0xbf; // There is no bit 6
            cgbObjPalIndex = data & 0x3f;
            if ((data & 0x80) != 0x00) {
                cgbObjPalIncr = 1;
            } else {
                cgbObjPalIncr = 0;
            }
            return;
        case 0x6b: // CBG sprite palette data (using address set by 0xff6a)
            cgbObjPalData[cgbObjPalIndex] = data;
            cgbObjPalette[cgbObjPalIndex >> 1] = REMAP_555_8888((unsigned int)cgbObjPalData[cgbObjPalIndex & 0xfe], (unsigned int)cgbObjPalData[cgbObjPalIndex | 0x01]);
            if (cgbObjPalIncr) {
                cgbObjPalIndex++;
                cgbObjPalIndex &= 0x3f;
                ioPorts[0x6a]++;
                ioPorts[0x6a] &= 0xbf;
            }
            return;
        case 0x70: // WRAM bank
            if (!romProperties.cgbFlag) {
                return;
            }
            ioPorts[0x70] = data & 0x07;
            if (ioPorts[0x70] == 0x00) {
                ioPorts[0x70]++;
            }
            wramBankOffset = (unsigned int)ioPorts[0x70] * 0x2000;
            return;
        default:
            ioPorts[ioIndex] = data;
            return;
    }
}

void Gbc::latchTimerData() {

}


















void Gbc::translatePaletteBg(unsigned int paletteData) {
    translatedPaletteBg[0] = stockPaletteBg[paletteData & 0x03];
    translatedPaletteBg[1] = stockPaletteBg[(paletteData & 0x0c) / 4];
    translatedPaletteBg[2] = stockPaletteBg[(paletteData & 0x30) / 16];
    translatedPaletteBg[3] = stockPaletteBg[(paletteData & 0xc0) / 64];
    sgbPaletteTranslationBg[0] = paletteData & 0x03;
    sgbPaletteTranslationBg[1] = (paletteData & 0x0c) / 4;
    sgbPaletteTranslationBg[2] = (paletteData & 0x30) / 16;
    sgbPaletteTranslationBg[3] = (paletteData & 0xc0) / 64;
}

void Gbc::translatePaletteObj1(unsigned int paletteData) {
    translatedPaletteObj[1] = stockPaletteObj1[(paletteData & 0x0c) / 4];
    translatedPaletteObj[2] = stockPaletteObj1[(paletteData & 0x30) / 16];
    translatedPaletteObj[3] = stockPaletteObj1[(paletteData & 0xc0) / 64];
    sgbPaletteTranslationObj[1] = (paletteData & 0x0c) / 4;
    sgbPaletteTranslationObj[2] = (paletteData & 0x30) / 16;
    sgbPaletteTranslationObj[3] = (paletteData & 0xc0) / 64;
}

void Gbc::translatePaletteObj2(unsigned int paletteData) {
    translatedPaletteObj[5] = stockPaletteObj2[(paletteData & 0x0c) / 4];
    translatedPaletteObj[6] = stockPaletteObj2[(paletteData & 0x30) / 16];
    translatedPaletteObj[7] = stockPaletteObj2[(paletteData & 0xc0) / 64];
    sgbPaletteTranslationObj[5] = (paletteData & 0x0c) / 4;
    sgbPaletteTranslationObj[6] = (paletteData & 0x30) / 16;
    sgbPaletteTranslationObj[7] = (paletteData & 0xc0) / 64;
}

void Gbc::readLineGb(uint32_t* frameBuffer) {
    // Get relevant parameters from status registers and such:
    const unsigned char lcdCtrl = ioPorts[0x40];
    unsigned char scrY = ioPorts[0x42];
    unsigned char scrX = ioPorts[0x43];
    const unsigned int lineNo = ioPorts[0x44];
    unsigned int tileSetIndexOffset = lcdCtrl & 0x10 ? 0x0000 : 0x0080;
    unsigned int tileSetIndexInverter = lcdCtrl & 0x10 ? 0x0000 : 0x0080;
    unsigned int tileMapBase = lcdCtrl & 0x08 ? 0x1c00 : 0x1800;

    // More variables
    unsigned int offset, max;
    unsigned int pixX, pixY, tileX, tileY;
    uint32_t* dstPointer;
    unsigned int* tileSetPointer;

    // Sprite-specific stuff:
    unsigned int paletteOffset;
    unsigned int getPix;

    // Check if LCD is disabled or all elements (BG, window, sprites) are disabled (write a black row if that's the case):
    if ((lcdCtrl & 0x80) == 0x00 || (lcdCtrl & 0x23) == 0x00) {
        dstPointer = &frameBuffer[lineNo * 160];
        for (pixX = 0; pixX < 160; pixX++) {
            *dstPointer++ = 0x000000ff;
        }
        return;
    }

    // Draw background if enabled
    if (lcdCtrl & 0x01) {
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
            unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile
            while (pixX < 8) {
                *dstPointer++ = translatedPaletteBg[*tileSetPointer++];
                pixX++;
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial 21st tile. Find out how many pixels of it to draw.
        max = scrX % 8;

        // Get tile no
        unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
        tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

        // Draw up to 8 pixels of this tile
        while (pixX < max) {
            *dstPointer++ = translatedPaletteBg[*tileSetPointer++];
            pixX++;
        }
    }

    // Draw window if enabled and on-screen
    scrX = ioPorts[0x4b];
    scrY = ioPorts[0x4a];
    tileMapBase = lcdCtrl & 0x40 ? 0x1c00 : 0x1800;
    if (((lcdCtrl & 0x20) != 0x00) && (scrX < 167) && (scrY <= lineNo)) {
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
            unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw the 8 pixels of this tile
            while (pixX < 8) {
                *dstPointer++ = translatedPaletteBg[*tileSetPointer++];
                pixX++;
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial last tile. Find out how many pixels of it to draw.
        max = scrX % 8;

        // Get tile no
        unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
        tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

        // Draw up to 8 pixels of this tile
        while (pixX < offset) {
            *dstPointer++ = translatedPaletteBg[*tileSetPointer++];
            pixX++;
        }
    }

    // Draw sprites if enabled
    if (lcdCtrl & 0x02) {
        const unsigned char largeSprites = lcdCtrl & 0x04;

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
                    tileNo |= 0x01;
                } else {
                    tileNo &= 0xfe;
                }
            } else if (lineNo + 8 >= scrY) {
                continue;
            }

            // Get pixel row within tile that will be drawn
            pixY = (lineNo + 16 - scrY) % 8;

            // Set which palette to draw with
            paletteOffset = spriteFlags & 0x10 ? 4 : 0;
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
            if (spriteFlags & 0x20) {
                pixX = 7 - pixX;
                tileSetPointerDirection = -1;
            } else {
                tileSetPointerDirection = 1;
            }

            // Adjust Y if vertically flipping
            if (spriteFlags & 0x40) {
                pixY = 7 - pixY;
            }

            // Get priority flag
            unsigned int BackgroundPriority = spriteFlags & 0x80;

            // Set point to draw to
            dstPointer = &frameBuffer[160 * lineNo + scrX];

            // Get pointer to tile data
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile (skipping over transparent pixels with palette index 0, or if obscured by the background)
            pixX = 0;
            while (pixX < max) {
                getPix = *tileSetPointer;
                tileSetPointer += tileSetPointerDirection;
                if (getPix > 0) {
                    if (BackgroundPriority) {
                        if (*dstPointer == colourZero) {
                            *dstPointer = translatedPaletteObj[getPix + paletteOffset];
                        }
                    } else {
                        *dstPointer = translatedPaletteObj[getPix + paletteOffset];
                    }
                }
                dstPointer++;
                pixX++;
            }
        }
    }
}

void Gbc::readLineSgb(uint32_t * frameBuffer) {
    // Get relevant parameters from status registers and such:
    const unsigned char lcdCtrl = ioPorts[0x40];
    unsigned char scrY = ioPorts[0x42];
    unsigned char scrX = ioPorts[0x43];
    const unsigned int lineNo = ioPorts[0x44];
    unsigned int tileSetIndexOffset = lcdCtrl & 0x10 ? 0x0000 : 0x0080;
    unsigned int tileSetIndexInverter = lcdCtrl & 0x10 ? 0x0000 : 0x0080;
    unsigned int tileMapBase = lcdCtrl & 0x08 ? 0x1c00 : 0x1800;

    // More variables
    unsigned int offset, max;
    unsigned int pixX, pixY, tileX, tileY;
    unsigned int* dstPointer;
    unsigned int* tileSetPointer;

    // Sprite-specific stuff:
    unsigned int paletteOffset;
    unsigned int getPix;

    // Draw background if enabled
    if (lcdCtrl & 0x01) {
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
            unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
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
        unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
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
    tileMapBase = lcdCtrl & 0x40 ? 0x1c00 : 0x1800;
    if (((lcdCtrl & 0x20) != 0x00) && (scrX < 167) && (scrY <= lineNo)) {
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
            unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
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
        max = scrX % 8;

        // Get tile no
        unsigned int tileNo = (vram[tileMapBase + 32 * tileY + tileX] ^ tileSetIndexInverter) + tileSetIndexOffset;
        tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

        // Draw up to 8 pixels of this tile
        while (pixX < offset) {
            *dstPointer++ = sgbPaletteTranslationBg[*tileSetPointer++];
            pixX++;
        }
    }

    // Draw sprites if enabled
    if (lcdCtrl & 0x02) {
        const unsigned char largeSprites = lcdCtrl & 0x04;

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
                    tileNo |= 0x01;
                } else {
                    tileNo &= 0xfe;
                }
            } else if (lineNo + 8 >= scrY) {
                continue;
            }

            // Get pixel row within tile that will be drawn
            pixY = (lineNo + 16 - scrY) % 8;

            // Set which palette to draw with
            paletteOffset = spriteFlags & 0x10 ? 4 : 0;
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
            if (spriteFlags & 0x20) {
                pixX = 7 - pixX;
                tileSetPointerDirection = -1;
            } else {
                tileSetPointerDirection = 1;
            }

            // Adjust Y if vertically flipping
            if (spriteFlags & 0x40) {
                pixY = 7 - pixY;
            }

            // Get priority flag
            unsigned int BackgroundPriority = spriteFlags & 0x80;

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
                    if (BackgroundPriority) {
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
    const unsigned char lcdCtrl = ioPorts[0x40];
    unsigned char scrY = ioPorts[0x42];
    unsigned char scrX = ioPorts[0x43];
    const unsigned int lineNo = ioPorts[0x44];
    unsigned int tileSetIndexOffset = lcdCtrl & 0x10 ? 0x0000 : 0x0080;
    unsigned int tileSetIndexInverter = lcdCtrl & 0x10 ? 0x0000 : 0x0080;
    unsigned int tileMapBase = lcdCtrl & 0x08 ? 0x1c00 : 0x1800;

    // More variables
    unsigned int offset, max;
    unsigned int pixX, pixY, tileX, tileY;
    uint32_t* dstPointer;
    unsigned int* tileSetPointer;

    // Sprite-specific stuff:
    unsigned int paletteOffset;
    unsigned int getPix;

    // Check if LCD is disabled or all elements (BG, window, sprites) are disabled (write a black row if that's the case):
    if ((lcdCtrl & 0x80) == 0x00 || (lcdCtrl & 0x23) == 0x00) {
        dstPointer = &frameBuffer[lineNo * 160];
        for (pixX = 0; pixX < 160; pixX++) {
            *dstPointer++ = 0x000000ff;
        }
        return;
    }

    // Draw background if enabled
    if (lcdCtrl & 0x01) {
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
            unsigned int tileNo = (vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
            unsigned int tileParams = vram[0x2000 + tileMapIndex];
            unsigned int adjustedY = tileParams & 0x0040 ? 7 - pixY : pixY;
            if (tileParams & 0x0008) {
                // Using bank 1
                tileNo += 0x0180;
            }

            // Draw up to 8 pixels of this tile
            if (tileParams & 0x0020) {
                // Flipped horizontally
                unsigned int drawnX = 7 - pixX;
                tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
                while (pixX < 8) {
                    *dstPointer++ = cgbBgPalette[4 * (tileParams & 0x07) + *tileSetPointer--];
                    pixX++;
                }
            } else {
                tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];
                while (pixX < 8) {
                    *dstPointer++ = cgbBgPalette[4 * (tileParams & 0x07) + *tileSetPointer++];
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
        unsigned int tileNo = (vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
        unsigned int tileParams = vram[0x2000 + tileMapIndex];
        unsigned int adjustedY = tileParams & 0x0040 ? 7 - pixY : pixY;
        if (tileParams & 0x0008) {
            // Using bank 1
            tileNo += 0x0180;
        }
        tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];

        // Draw up to 8 pixels of this tile
        if (tileParams & 0x0020) {
            // Flipped horizontally
            unsigned int drawnX = 7 - pixX;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
            while (pixX < max) {
                *dstPointer++ = cgbBgPalette[4 * (tileParams & 0x07) + *tileSetPointer--];
                pixX++;
            }
        } else {
            while (pixX < max) {
                *dstPointer++ = cgbBgPalette[4 * (tileParams & 0x07) + *tileSetPointer++];
                pixX++;
            }
        }
    }

    // Draw window if enabled and on-screen
    scrX = ioPorts[0x4b];
    scrY = ioPorts[0x4a];
    tileMapBase = lcdCtrl & 0x40 ? 0x1c00 : 0x1800;
    if (((lcdCtrl & 0x20) != 0x00) && (scrX < 167) && (scrY <= lineNo)) {
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
            unsigned int tileNo = (vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
            unsigned int tileParams = vram[0x2000 + tileMapIndex];
            unsigned int adjustedY = tileParams & 0x0040 ? 7 - pixY : pixY;
            if (tileParams & 0x0008) {
                // Using bank 1
                tileNo += 0x0180;
            }
            tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];

            // Draw the 8 pixels of this tile
            if (tileParams & 0x0020) {
                // Flipped horizontally
                unsigned int drawnX = 7 - pixX;
                tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
                while (pixX < 8) {
                    *dstPointer++ = cgbBgPalette[(tileParams & 0x07) + *tileSetPointer--];
                    pixX++;
                }
            } else {
                while (pixX < 8) {
                    *dstPointer++ = cgbBgPalette[(tileParams & 0x07) + *tileSetPointer++];
                    pixX++;
                }
            }

            // Reset pixel counter and get next tile coordinates
            pixX = 0;
            tileX = (tileX + 1) % 32;
        }

        // Draw partial last tile. Find out how many pixels of it to draw.
        max = scrX % 8;

        // Get tile no
        unsigned int tileMapIndex = tileMapBase + 32 * tileY + tileX;
        unsigned int tileNo = (vram[tileMapIndex] ^ tileSetIndexInverter) + tileSetIndexOffset;
        unsigned int tileParams = vram[0x2000 + tileMapIndex];
        unsigned int adjustedY = tileParams & 0x0040 ? 7 - pixY : pixY;
        if (tileParams & 0x0008) {
            // Using bank 1
            tileNo += 0x0180;
        }
        tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + pixX];

        // Draw up to 8 pixels of this tile
        if (tileParams & 0x0020) {
            // Flipped horizontally
            unsigned int drawnX = 7 - pixX;
            tileSetPointer = &tileSet[tileNo * 64 + 8 * adjustedY + drawnX];
            while (pixX < offset) {
                *dstPointer++ = cgbBgPalette[(tileParams & 0x07) + *tileSetPointer--];
                pixX++;
            }
        } else {
            while (pixX < offset) {
                *dstPointer++ = cgbBgPalette[(tileParams & 0x07) + *tileSetPointer++];
                pixX++;
            }
        }
    }

    // Draw sprites if enabled
    if (lcdCtrl & 0x02) {
        const unsigned char largeSprites = lcdCtrl & 0x04;

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
                    tileNo |= 0x01;
                } else {
                    tileNo &= 0xfe;
                }
            } else if (lineNo + 8 >= scrY) {
                continue;
            }
            if (spriteFlags & 0x08) {
                tileNo += 192;
            }

            // Get pixel row within tile that will be drawn
            pixY = (lineNo + 16 - scrY) % 8;

            // Set which palette to draw with
            paletteOffset = (spriteFlags & 0x07) << 2;

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
            if (spriteFlags & 0x20) {
                pixX = 7 - pixX;
                tileSetPointerDirection = -1;
            } else {
                tileSetPointerDirection = 1;
            }

            // Adjust Y if vertically flipping
            if (spriteFlags & 0x40) {
                pixY = 7 - pixY;
            }

            // Get priority flag
            //unsigned int BackgroundPriority = spriteFlags & 0x80;

            // Set point to draw to
            dstPointer = &frameBuffer[160 * lineNo + scrX];

            // Get pointer to tile data
            tileSetPointer = &tileSet[tileNo * 64 + 8 * pixY + pixX];

            // Draw up to 8 pixels of this tile (skipping over transparent pixels with palette index 0, or if obscured by the background)
            pixX = 0;
            while (pixX < max) {
                getPix = *tileSetPointer;
                tileSetPointer += tileSetPointerDirection;
                if (getPix > 0) {
                    *dstPointer = cgbObjPalette[getPix + paletteOffset];
                }
                dstPointer++;
                pixX++;
            }
        }
    }
}














inline unsigned int Gbc::HL() {
    return ((unsigned int)cpuH << 8) + (unsigned int)cpuL;
}

inline unsigned char Gbc::R8_HL() {
    return read8(((unsigned int)cpuH << 8) + (unsigned int)cpuL);
}

inline void Gbc::W8_HL(unsigned char byte) {
    write8(((unsigned int)cpuH << 8) + (unsigned int)cpuL, byte);
}

inline void Gbc::SETZ_ON_ZERO(unsigned char testValue) {
    if (testValue == 0x00)
    {
        cpuF |= 0x80;
    }
}

inline void Gbc::SETZ_ON_COND(bool test) {
    if (test)
    {
        cpuF |= 0x80;
    }
}

inline void Gbc::SETH_ON_ZERO(unsigned char testValue) {
    if (testValue == 0x00)
    {
        cpuF |= 0x20;
    }
}

inline void Gbc::SETH_ON_COND(bool test) {
    if (test)
    {
        cpuF |= 0x20;
    }
}

inline void Gbc::SETC_ON_COND(bool test) {
    if (test)
    {
        cpuF |= 0x10;
    }
}

unsigned int Gbc::performOp()
{
    unsigned char instr = read8(cpuPc);
    switch (instr)
    {
        case 0x00: // nop
            cpuPc++;
            return 4;
        case 0x01: // ld BC, nn
            cpuB = read8(cpuPc + 2);
            cpuC = read8(cpuPc + 1);
            cpuPc += 3;
            return 12;
        case 0x02: // ld (BC), A
            write8(((unsigned int)cpuB << 8) + (unsigned int)cpuC, cpuA);
            cpuPc++;
            return 8;
        case 0x03: // inc BC
            cpuC++;
            if (cpuC == 0x00)
            {
                cpuB++;
            }
            cpuPc++;
            return 8;
        case 0x04: // inc B
            cpuF &= 0x10;
            cpuB += 0x01;
            SETZ_ON_ZERO(cpuB);
            SETH_ON_ZERO(cpuB & 0x0f);
            cpuPc++;
            return 4;
        case 0x05: // dec B
            cpuF &= 0x10;
            cpuF |= 0x40;
            SETH_ON_ZERO(cpuB & 0x0f);
            cpuB -= 0x01;
            SETZ_ON_ZERO(cpuB);
            cpuPc++;
            return 4;
        case 0x06: // ld B, n
            cpuB = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x07: // rlc A (rotate bit 7 to bit 0, and copy bit 7 to carry flag)
        {
            unsigned char tempByte = cpuA & 0x80; // True if bit 7 is set
            cpuF = 0x00;
            cpuA = cpuA << 1;
            if (tempByte != 0)
            {
                cpuF |= 0x10; // Set carry
                cpuA |= 0x01;
            }
        }
            cpuPc++;
            return 4;
        case 0x08: // ld (nn), SP
            write16(((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1),
                    (unsigned char)(cpuSp & 0x00ff),
                    (unsigned char)((cpuSp >> 8) & 0x00ff)
            );
            cpuPc += 3;
            return 20;
        case 0x09: // add HL, BC
            cpuF &= 0x80;
            cpuL += cpuC;
            if (cpuL < cpuC)
            {
                cpuH++;
                SETC_ON_COND(cpuH == 0x00);
                SETH_ON_ZERO(cpuH & 0x0f);
            }
            cpuH += cpuB;
            SETC_ON_COND(cpuH < cpuB);
            SETH_ON_COND((cpuH & 0x0f) < (cpuB & 0x0f));
            cpuPc++;
            return 8;
        case 0x0a: // ld A, (BC)
            cpuA = read8(((unsigned int)cpuB << 8) + (unsigned int)cpuC);
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
            cpuF &= 0x10;
            cpuC += 0x01;
            SETZ_ON_ZERO(cpuC);
            SETH_ON_ZERO(cpuC & 0x0f);
            cpuPc++;
            return 4;
        case 0x0d: // dec C
            cpuF &= 0x10;
            cpuF |= 0x40;
            SETH_ON_ZERO(cpuC & 0x0f);
            cpuC -= 0x01;
            SETZ_ON_ZERO(cpuC);
            cpuPc++;
            return 4;
        case 0x0e: // ld C, n
            cpuC = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x0f: // rrc A (8-bit rotation right - bit 0 is moved to carry also)
        {
            unsigned char tempByte = cpuA & 0x01;
            cpuF = 0x00;
            cpuA = cpuA >> 1;
            cpuA = cpuA & 0x7f; // Clear msb in case sign bit preserved by compiler
            if (tempByte != 0)
            {
                cpuF = 0x10;
                cpuA |= 0x80;
            }
        }
            cpuPc++;
            return 4;
        case 0x10: // stop
            if ((ioPorts[0x4d] & 0x01) != 0)
            {
                ioPorts[0x4d] &= 0x80;
                if (ioPorts[0x4d] == 0x00) // Switch CPU running speed
                {
                    ioPorts[0x4d] = 0x80;
                    cpuClockFreq = GBC_FREQ;
                    gpuClockFactor = 2;
                }
                else
                {
                    ioPorts[0x4d] = 0x00;
                    cpuClockFreq = GB_FREQ;
                    gpuClockFactor = 1;
                }
                cpuPc++;
            }
            return 4;
        case 0x11: // ld DE, nn
            cpuD = read8(cpuPc + 2);
            cpuE = read8(cpuPc + 1);
            cpuPc += 3;
            return 12;
        case 0x12: // ld (DE), A
            write8(((unsigned int)cpuD << 8) + (unsigned int)cpuE, cpuA);
            cpuPc++;
            return 8;
        case 0x13: // inc DE
            cpuE++;
            if (cpuE == 0x00)
            {
                cpuD++;
            }
            cpuPc++;
            return 8;
        case 0x14: // inc D
            cpuF &= 0x10;
            cpuD += 0x01;
            SETZ_ON_ZERO(cpuD);
            SETH_ON_ZERO(cpuD & 0x0f);
            cpuPc++;
            return 4;
        case 0x15: // dec D
            cpuF &= 0x10;
            cpuF |= 0x40;
            SETH_ON_ZERO(cpuD & 0x0f);
            cpuD -= 0x01;
            SETZ_ON_ZERO(cpuD);
            cpuPc++;
            return 4;
        case 0x16: // ld D, n
            cpuD = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x17: // rl A (rotate carry bit to bit 0 of A)
        {
            unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
            cpuF = 0x00;
            SETC_ON_COND((cpuA & 0x80) != 0); // Copy bit 7 to carry bit
            cpuA = cpuA << 1;
            if (tempByte != 0)
            {
                cpuA |= 0x01; // Copy carry flag to bit 0
            }
        }
            cpuPc++;
            return 4;
        case 0x18: // jr d
        {
            unsigned char msb = read8(cpuPc + 1);
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
            cpuF &= 0x80;
            cpuL += cpuE;
            if (cpuL < cpuE)
            {
                cpuH++;
                SETC_ON_COND(cpuH == 0x00);
                SETH_ON_ZERO(cpuH & 0x0f);
            }
            cpuH += cpuD;
            SETC_ON_COND(cpuH < cpuD);
            SETH_ON_COND((cpuH & 0x0f) < (cpuD & 0x0f));
            cpuPc++;
            return 8;
        case 0x1a: // ld A, (DE)
            cpuA = read8(((unsigned int)cpuD << 8) + (unsigned int)cpuE);
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
            cpuF &= 0x10;
            cpuE += 0x01;
            SETZ_ON_ZERO(cpuE);
            SETH_ON_ZERO(cpuE & 0x0f);
            cpuPc++;
            return 4;
        case 0x1d: // dec E
            cpuF &= 0x10;
            cpuF |= 0x40;
            SETH_ON_ZERO(cpuE & 0x0f);
            cpuE -= 0x01;
            SETZ_ON_ZERO(cpuE);
            cpuPc++;
            return 4;
        case 0x1e: // ld E, n
            cpuE = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x1f: // rr A (9-bit rotation right of A through carry)
        {
            unsigned char tempByte = cpuF & 0x10;
            cpuF = 0x00;
            SETC_ON_COND(cpuA & 0x01);
            cpuA = cpuA >> 1;
            cpuA = cpuA & 0x7f;
            if (tempByte != 0x00)
            {
                cpuA |= 0x80;
            }
        }
            cpuPc++;
            return 4;
        case 0x20: // jr NZ, d
            if ((cpuF & 0x80) != 0)
            {
                cpuPc += 2;
                return 8;
            }
            else
            {
                unsigned char msb = read8(cpuPc + 1);
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
            cpuF &= 0x10;
            cpuH += 0x01;
            SETZ_ON_ZERO(cpuH);
            SETH_ON_ZERO(cpuH & 0x0f);
            cpuPc++;
            return 4;
        case 0x25: // dec H
            cpuF &= 0x10;
            cpuF |= 0x40;
            SETH_ON_ZERO(cpuH & 0x0f);
            cpuH -= 0x01;
            SETZ_ON_ZERO(cpuH);
            cpuPc++;
            return 4;
        case 0x26: // ld H, n
            cpuH = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x27: // daa (Decimal Adjust Accumulator - do BCD correction)
            if ((cpuF & 0x40) == 0x00)
            {
                if (((cpuA & 0x0f) > 0x09) || ((cpuF & 0x20) != 0x00)) // If lower 4 bits are non-decimal or H is set, add 0x06
                {
                    cpuA += 0x06;
                }
                unsigned char tempByte = cpuF & 0x10;
                cpuF &= 0x40; // Reset C, H and Z flags
                if ((cpuA > 0x9f) || (tempByte != 0x00)) // If upper 4 bits are non-decimal or C was set, add 0x60
                {
                    cpuA += 0x60;
                    cpuF |= 0x10; // Sets the C flag if this second addition was needed
                }
            }
            else
            {
                if (((cpuA & 0x0f) > 0x09) || ((cpuF & 0x20) != 0x00)) // If lower 4 bits are non-decimal or H is set, add 0x06
                {
                    cpuA -= 0x06;
                }
                unsigned char tempByte = cpuF & 0x10;
                cpuF &= 0x40; // Reset C, H and Z flags
                if ((cpuA > 0x9f) || (tempByte != 0x00)) // If upper 4 bits are non-decimal or C was set, add 0x60
                {
                    cpuA -= 0x60;
                    cpuF |= 0x10; // Sets the C flag if this second addition was needed
                }
            }
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0x28: // jr Z, d
            if ((cpuF & 0x80) != 0x00)
            {
                unsigned char msb = read8(cpuPc + 1);
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
            cpuF &= 0x80;
            SETC_ON_COND((cpuH & 0x80) != 0x00);
            SETH_ON_COND((cpuH & 0x08) != 0x00);
            if ((cpuL & 0x80) != 0x00)
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
            cpuF &= 0x10;
            cpuL += 0x01;
            SETZ_ON_ZERO(cpuL);
            SETH_ON_ZERO(cpuL & 0x0f);
            cpuPc++;
            return 4;
        case 0x2d: // dec L
            cpuF &= 0x10;
            cpuF |= 0x40;
            SETH_ON_ZERO(cpuL & 0x0f);
            cpuL -= 0x01;
            SETZ_ON_ZERO(cpuL);
            cpuPc++;
            return 4;
        case 0x2e: // ld L, n
            cpuL = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x2f: // cpl A (complement - bitwise NOT)
            cpuA = ~cpuA;
            cpuF |= 0x60;
            cpuPc++;
            return 4;
        case 0x30: // jr NC, d
            if ((cpuF & 0x10) != 0x00)
            {
                cpuPc += 2;
                return 8;
            }
            else
            {
                unsigned char msb = read8(cpuPc + 1);
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
            cpuSp = ((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1);
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
            cpuSp &= 0xffff;
            cpuPc++;
            return 8;
        case 0x34: // inc (HL)
        {
            cpuF &= 0x10;
            unsigned int tempAddr = HL();
            unsigned char tempByte = read8(tempAddr) + 1;
            SETZ_ON_ZERO(tempByte);
            SETH_ON_ZERO(tempByte & 0x0f);
            write8(tempAddr, tempByte);
        }
            cpuPc++;
            return 12;
        case 0x35: // dec (HL)
        {
            cpuF &= 0x10;
            cpuF |= 0x40;
            unsigned int tempAddr = HL();
            unsigned char tempByte = read8(tempAddr);
            SETH_ON_ZERO(tempByte & 0x0f);
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
            cpuF &= 0x80;
            cpuF |= 0x10;
            cpuPc++;
            return 4;
        case 0x38: // jr C, n
            if ((cpuF & 0x10) != 0x00)
            {
                unsigned char msb = read8(cpuPc + 1);
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
            cpuF &= 0x80;
            unsigned char tempByte = (unsigned char)(cpuSp & 0xff);
            cpuL += tempByte;
            if (cpuL < tempByte)
            {
                cpuH++;
            }
            tempByte = (unsigned char)(cpuSp >> 8);
            cpuH += tempByte;
            SETC_ON_COND(cpuH < tempByte);
            tempByte = tempByte & 0x0f;
            SETH_ON_COND((cpuH & 0x0f) < tempByte);
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
            cpuSp &= 0xffff;
            cpuPc++;
            return 8;
        case 0x3c: // inc A
            cpuA++;
            cpuF &= 0x10;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_ZERO(cpuA & 0x0f);
            cpuPc++;
            return 4;
        case 0x3d: // dec A
            cpuF &= 0x10;
            cpuF |= 0x40;
            SETH_ON_ZERO(cpuA & 0x0f);
            cpuA -= 0x01;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0x3e: // ld A, n
            cpuA = read8(cpuPc + 1);
            cpuPc += 2;
            return 8;
        case 0x3f: // ccf (invert carry flags)
        {
            cpuF &= 0xb0;
            unsigned char tempByte = cpuF & 0x30;
            tempByte = tempByte ^ 0x30;
            cpuF &= 0x80;
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
            cpuHalted = true;
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
            SETH_ON_COND((cpuB & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuB > cpuA);
            cpuPc++;
            return 4;
        case 0x81: // add C
            cpuA += cpuC;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuC & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuC > cpuA);
            cpuPc++;
            return 4;
        case 0x82: // add D
            cpuA += cpuD;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuD & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuD > cpuA);
            cpuPc++;
            return 4;
        case 0x83: // add E
            cpuA += cpuE;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuE & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuE > cpuA);
            cpuPc++;
            return 4;
        case 0x84: // add H
            cpuA += cpuH;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuH & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuH > cpuA);
            cpuPc++;
            return 4;
        case 0x85: // add L
            cpuA += cpuL;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETH_ON_COND((cpuL & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuL > cpuA);
            cpuPc++;
            return 4;
        case 0x86: // add (HL)
        {
            unsigned char tempByte = R8_HL();
            cpuA += tempByte;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(tempByte > cpuA);
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 8;
        case 0x87: // add A
            cpuF = 0x00;
            SETH_ON_COND((cpuA & 0x08) != 0x00);
            SETC_ON_COND((cpuA & 0x80) != 0x00);
            cpuA += cpuA;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0x88: // adc A, B (add B + carry to A)
        {
            unsigned char tempByte = cpuB;
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 4;
        case 0x89: // adc A, C
        {
            unsigned char tempByte = cpuC;
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 4;
        case 0x8a: // adc A, D
        {
            unsigned char tempByte = cpuD;
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 4;
        case 0x8b: // adc A, E
        {
            unsigned char tempByte = cpuE;
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 4;
        case 0x8c: // adc A, H
        {
            unsigned char tempByte = cpuH;
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 4;
        case 0x8d: // adc A, L
        {
            unsigned char tempByte = cpuL;
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 4;
        case 0x8e: // adc A, (HL)
        {
            unsigned char tempByte = R8_HL();
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 8;
        case 0x8f: // adc A, A
        {
            unsigned char tempByte = cpuA;
            if ((cpuF & 0x10) != 0x00)
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
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 4;
        case 0x90: // sub B (sub B from A)
            cpuF = 0x40;
            SETC_ON_COND(cpuB > cpuA);
            SETH_ON_COND((cpuB & 0x0f) > (cpuA & 0x0f));
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
            SETH_ON_COND((cpuC & 0x0f) > (cpuA & 0x0f));
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
            SETH_ON_COND((cpuD & 0x0f) > (cpuA & 0x0f));
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
            SETH_ON_COND((cpuE & 0x0f) > (cpuA & 0x0f));
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
            SETH_ON_COND((cpuH & 0x0f) > (cpuA & 0x0f));
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
            SETH_ON_COND((cpuL & 0x0f) > (cpuA & 0x0f));
            cpuA -= cpuL;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
            cpuPc++;
            return 4;
        case 0x96: // sub (HL)
        {
            unsigned char tempByte = R8_HL();
            cpuF = 0x40;
            SETC_ON_COND(tempByte > cpuA);
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = cpuF & 0x10;
            cpuF = 0x40;
            SETC_ON_COND(cpuB > cpuA);
            SETH_ON_COND((cpuB & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = cpuF & 0x10;
            cpuF = 0x40;
            SETC_ON_COND(cpuC > cpuA);
            SETH_ON_COND((cpuC & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = cpuF & 0x10;
            cpuF = 0x40;
            SETC_ON_COND(cpuD > cpuA);
            SETH_ON_COND((cpuD & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = cpuF & 0x10;
            cpuF = 0x40;
            SETC_ON_COND(cpuE > cpuA);
            SETH_ON_COND((cpuE & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = cpuF & 0x10;
            cpuF = 0x40;
            SETC_ON_COND(cpuH > cpuA);
            SETH_ON_COND((cpuH & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = cpuF & 0x10;
            cpuF = 0x40;
            SETC_ON_COND(cpuL > cpuA);
            SETH_ON_COND((cpuL & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = R8_HL();
            unsigned char tempByte2 = cpuF & 0x10;
            cpuF = 0x40;
            SETC_ON_COND(tempByte > cpuA);
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
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
            unsigned char tempByte = cpuF & 0x10;
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
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa1: // and C
            cpuA = cpuA & cpuC;
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa2: // and D
            cpuA = cpuA & cpuD;
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa3: // and E
            cpuA = cpuA & cpuE;
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa4: // and H
            cpuA = cpuA & cpuH;
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa5: // and L
            cpuA = cpuA & cpuL;
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 4;
        case 0xa6: // and (HL)
            cpuA = cpuA & R8_HL();
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc++;
            return 8;
        case 0xa7: // and A
            cpuF = 0x20;
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
            cpuF = 0x80;
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
            SETH_ON_COND((cpuB & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuB > cpuA);
            SETZ_ON_COND(cpuA == cpuB);
            cpuPc++;
            return 4;
        case 0xb9: // cp C
            cpuF = 0x40;
            SETH_ON_COND((cpuC & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuC > cpuA);
            SETZ_ON_COND(cpuA == cpuC);
            cpuPc++;
            return 4;
        case 0xba: // cp D
            cpuF = 0x40;
            SETH_ON_COND((cpuD & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuD > cpuA);
            SETZ_ON_COND(cpuA == cpuD);
            cpuPc++;
            return 4;
        case 0xbb: // cp E
            cpuF = 0x40;
            SETH_ON_COND((cpuE & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuE > cpuA);
            SETZ_ON_COND(cpuA == cpuE);
            cpuPc++;
            return 4;
        case 0xbc: // cp H
            cpuF = 0x40;
            SETH_ON_COND((cpuH & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuH > cpuA);
            SETZ_ON_COND(cpuA == cpuH);
            cpuPc++;
            return 4;
        case 0xbd: // cp L
            cpuF = 0x40;
            SETH_ON_COND((cpuL & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(cpuL > cpuA);
            SETZ_ON_COND(cpuA == cpuL);
            cpuPc++;
            return 4;
        case 0xbe: // cp (HL)
        {
            unsigned char tempByte = R8_HL();
            cpuF = 0x40;
            SETC_ON_COND(tempByte > cpuA);
            SETZ_ON_COND(cpuA == tempByte);
            SETH_ON_COND((tempByte & 0x0f) > (cpuA & 0x0f));
        }
            cpuPc++;
            return 8;
        case 0xbf: // cp A
            cpuF = 0xc0;
            cpuPc++;
            return 4;
        case 0xc0: // ret NZ
            if ((cpuF & 0x80) != 0x00)
            {
                cpuPc++;
                return 8;
            }
            else
            {
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallReturned = 1;
                }
                unsigned char msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 20;
            }
        case 0xc1: // pop BC
            read16(cpuSp, &cpuC, &cpuB);
            cpuSp += 2;
            cpuPc++;
            return 12;
        case 0xc2: // j NZ, nn
            if ((cpuF & 0x80) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1);
                return 16;
            }
        case 0xc3: // jump to nn
            cpuPc = ((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1);
            return 16;
        case 0xc4: // call NZ, nn
            if ((cpuF & 0x80) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                unsigned char msb = read8(cpuPc + 1);
                unsigned char lsb = read8(cpuPc + 2);
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
                cpuPc += 3;
                cpuSp -= 2;
                write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 24;
            }
        case 0xc5: // push BC
            cpuSp -= 2;
            write16(cpuSp, cpuC, cpuB);
            cpuPc++;
            return 16;
        case 0xc6: // add A, n
        {
            unsigned char msb = read8(cpuPc + 1);
            cpuA += msb;
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < msb);
            SETH_ON_COND((cpuA & 0x0f) < (msb & 0x0f));
        }
            cpuPc += 2;
            return 8;
        case 0xc7: // rst 0 (call routine at 0x0000)
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = 0x00;
            return 16;
        case 0xc8: // ret Z
            if ((cpuF & 0x80) != 0x00)
            {
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallReturned = 1;
                }
                unsigned char msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 20;
            }
            else
            {
                cpuPc++;
                return 8;
            }
        case 0xc9: // return
        {
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallReturned = 1;
            }
            unsigned char msb, lsb;
            read16(cpuSp, &msb, &lsb);
            cpuSp += 2;
            cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
        }
            return 16;
        case 0xca: // j Z, nn
            if ((cpuF & 0x80) != 0x00)
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1);
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
                    unsigned char tempByte = cpuB & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuB = cpuB << 1;
                    if (tempByte != 0)
                    {
                        cpuB |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x01: // rlc C
                {
                    unsigned char tempByte = cpuC & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuC = cpuC << 1;
                    if (tempByte != 0)
                    {
                        cpuC |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x02: // rlc D
                {
                    unsigned char tempByte = cpuD & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuD = cpuD << 1;
                    if (tempByte != 0)
                    {
                        cpuD |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x03: // rlc E
                {
                    unsigned char tempByte = cpuE & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuE = cpuE << 1;
                    if (tempByte != 0)
                    {
                        cpuE |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x04: // rlc H
                {
                    unsigned char tempByte = cpuH & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuH = cpuH << 1;
                    if (tempByte != 0)
                    {
                        cpuH |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x05: // rlc L
                {
                    unsigned char tempByte = cpuL & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuL = cpuL << 1;
                    if (tempByte != 0)
                    {
                        cpuL |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x06: // rlc (HL)
                {
                    unsigned int tempAddr = HL();
                    unsigned char tempByte2 = read8(tempAddr);
                    unsigned char tempByte = tempByte2 & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    tempByte2 = tempByte2 << 1;
                    if (tempByte != 0)
                    {
                        tempByte2 |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(tempByte2);
                    write8(tempAddr, tempByte2);
                }
                    return 16;
                case 0x07: // rlc A
                {
                    unsigned char tempByte = cpuA & 0x80; // True if bit 7 is set
                    cpuF = 0x00; // Reset all other flags
                    cpuA = cpuA << 1;
                    if (tempByte != 0)
                    {
                        cpuA |= 0x01;
                        cpuF = 0x10; // Set carry
                    }
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x08: // rrc B
                {
                    unsigned char tempByte = cpuB & 0x01;
                    cpuF = 0x00;
                    cpuB = cpuB >> 1;
                    cpuB &= 0x7f;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10;
                        cpuB |= 0x80;
                    }
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x09: // rrc C
                {
                    unsigned char tempByte = cpuC & 0x01;
                    cpuF = 0x00;
                    cpuC = cpuC >> 1;
                    cpuC &= 0x7f;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10;
                        cpuC |= 0x80;
                    }
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x0a: // rrc D
                {
                    unsigned char tempByte = cpuD & 0x01;
                    cpuF = 0x00;
                    cpuD = cpuD >> 1;
                    cpuD &= 0x7f;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10;
                        cpuD |= 0x80;
                    }
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x0b: // rrc E
                {
                    unsigned char tempByte = cpuE & 0x01;
                    cpuF = 0x00;
                    cpuE = cpuE >> 1;
                    cpuE &= 0x7f;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10;
                        cpuE |= 0x80;
                    }
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x0c: // rrc H
                {
                    unsigned char tempByte = cpuH & 0x01;
                    cpuF = 0x00;
                    cpuH = cpuH >> 1;
                    cpuH &= 0x7f;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10;
                        cpuH |= 0x80;
                    }
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x0d: // rrc L
                {
                    unsigned char tempByte = cpuL & 0x01;
                    cpuF = 0x00;
                    cpuL = cpuL >> 1;
                    cpuL &= 0x7f;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10;
                        cpuL |= 0x80;
                    }
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x0e: // rrc (HL)
                {
                    unsigned int tempAddr = HL();
                    unsigned char tempByte = read8(tempAddr);
                    unsigned char tempByte2 = tempByte & 0x01;
                    cpuF = 0x00;
                    tempByte = tempByte >> 1;
                    tempByte &= 0x7f;
                    if (tempByte2 != 0)
                    {
                        cpuF = 0x10;
                        tempByte |= 0x80;
                    }
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x0f: // rrc A
                {
                    unsigned char tempByte = cpuA & 0x01;
                    cpuF = 0x00;
                    cpuA = cpuA >> 1;
                    cpuA &= 0x7f;
                    if (tempByte != 0)
                    {
                        cpuF = 0x10;
                        cpuA |= 0x80;
                    }
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x10: // rl B (rotate carry bit to bit 0 of B)
                {
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x80) != 0); // Copy bit 7 to carry bit
                    cpuB = cpuB << 1;
                    if (tempByte != 0)
                    {
                        cpuB |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x11: // rl C
                {
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x80) != 0); // Copy bit 7 to carry bit
                    cpuC = cpuC << 1;
                    if (tempByte != 0)
                    {
                        cpuC |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x12: // rl D
                {
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x80) != 0); // Copy bit 7 to carry bit
                    cpuD = cpuD << 1;
                    if (tempByte != 0)
                    {
                        cpuD |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x13: // rl E
                {
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x80) != 0); // Copy bit 7 to carry bit
                    cpuE = cpuE << 1;
                    if (tempByte != 0)
                    {
                        cpuE |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x14: // rl H
                {
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x80) != 0); // Copy bit 7 to carry bit
                    cpuH = cpuH << 1;
                    if (tempByte != 0)
                    {
                        cpuH |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x15: // rl L
                {
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x80) != 0); // Copy bit 7 to carry bit
                    cpuL = cpuL << 1;
                    if (tempByte != 0)
                    {
                        cpuL |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x16: // rl (HL)
                {
                    unsigned int tempAddr = HL();
                    unsigned char tempByte2 = read8(tempAddr);
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((tempByte2 & 0x80) != 0); // Copy bit 7 to carry bit
                    tempByte2 = tempByte2 << 1;
                    if (tempByte != 0)
                    {
                        tempByte2 |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(tempByte2);
                    write8(tempAddr, tempByte2);
                }
                    return 16;
                case 0x17: // rl A
                {
                    unsigned char tempByte = cpuF & 0x10; // True if carry flag was set
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x80) != 0); // Copy bit 7 to carry bit
                    cpuA = cpuA << 1;
                    if (tempByte != 0)
                    {
                        cpuA |= 0x01; // Copy carry flag to bit 0
                    }
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x18: // rr B (9-bit rotation incl carry bit)
                {
                    unsigned char tempByte = cpuB & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    cpuB = cpuB >> 1;
                    cpuB = cpuB & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuB |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x19: // rr C
                {
                    unsigned char tempByte = cpuC & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    cpuC = cpuC >> 1;
                    cpuC = cpuC & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuC |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x1a: // rr D
                {
                    unsigned char tempByte = cpuD & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    cpuD = cpuD >> 1;
                    cpuD = cpuD & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuD |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x1b: // rr E
                {
                    unsigned char tempByte = cpuE & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    cpuE = cpuE >> 1;
                    cpuE = cpuE & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuE |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x1c: // rr H
                {
                    unsigned char tempByte = cpuH & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    cpuH = cpuH >> 1;
                    cpuH = cpuH & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuH |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x1d: // rr L
                {
                    unsigned char tempByte = cpuL & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    cpuL = cpuL >> 1;
                    cpuL = cpuL & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuL |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x1e: // rr (HL)
                {
                    unsigned int tempAddr = HL();
                    unsigned char tempByte3 = read8(tempAddr);
                    unsigned char tempByte = tempByte3 & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    tempByte3 = tempByte3 >> 1;
                    tempByte3 = tempByte3 & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        tempByte3 |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_COND(tempByte3 == 0x00);
                    write8(tempAddr, tempByte3);
                }
                    return 16;
                case 0x1f: // rr A
                {
                    unsigned char tempByte = cpuA & 0x01;
                    unsigned char tempByte2 = cpuF & 0x10;
                    cpuA = cpuA >> 1;
                    cpuA = cpuA & 0x7f;
                    cpuF = 0x00;
                    if (tempByte2 != 0x00)
                    {
                        cpuA |= 0x80;
                    }
                    SETC_ON_COND(tempByte != 0x00);
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x20: // sla B (shift B left arithmetically)
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x80) != 0x00);
                    cpuB = cpuB << 1;
                    SETZ_ON_ZERO(cpuB);
                    return 8;
                case 0x21: // sla C
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x80) != 0x00);
                    cpuC = cpuC << 1;
                    SETZ_ON_ZERO(cpuC);
                    return 8;
                case 0x22: // sla D
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x80) != 0x00);
                    cpuD = cpuD << 1;
                    SETZ_ON_ZERO(cpuD);
                    return 8;
                case 0x23: // sla E
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x80) != 0x00);
                    cpuE = cpuE << 1;
                    SETZ_ON_ZERO(cpuE);
                    return 8;
                case 0x24: // sla H
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x80) != 0x00);
                    cpuH = cpuH << 1;
                    SETZ_ON_ZERO(cpuH);
                    return 8;
                case 0x25: // sla L
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x80) != 0x00);
                    cpuL = cpuL << 1;
                    SETZ_ON_ZERO(cpuL);
                    return 8;
                case 0x26: // sla (HL)
                {
                    unsigned int tempAddr = HL();
                    unsigned char tempByte = read8(tempAddr);
                    cpuF = 0x00;
                    SETC_ON_COND((tempByte & 0x80) != 0x00);
                    tempByte = tempByte << 1;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x27: // sla A
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x80) != 0x00);
                    cpuA = cpuA << 1;
                    SETZ_ON_ZERO(cpuA);
                    return 8;
                case 0x28: // sra B (shift B right arithmetically - preserve sign bit)
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x01) != 0x00);
                    unsigned char tempByte = cpuB & 0x80;
                    cpuB = cpuB >> 1;
                    cpuB |= tempByte;
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x29: // sra C
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x01) != 0x00);
                    unsigned char tempByte = cpuC & 0x80;
                    cpuC = cpuC >> 1;
                    cpuC |= tempByte;
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x2a: // sra D
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x01) != 0x00);
                    unsigned char tempByte = cpuD & 0x80;
                    cpuD = cpuD >> 1;
                    cpuD |= tempByte;
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x2b: // sra E
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x01) != 0x00);
                    unsigned char tempByte = cpuE & 0x80;
                    cpuE = cpuE >> 1;
                    cpuE |= tempByte;
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x2c: // sra H
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x01) != 0x00);
                    unsigned char tempByte = cpuH & 0x80;
                    cpuH = cpuH >> 1;
                    cpuH |= tempByte;
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x2d: // sra L
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x01) != 0x00);
                    unsigned char tempByte = cpuL & 0x80;
                    cpuL = cpuL >> 1;
                    cpuL |= tempByte;
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x2e: // sra (HL)
                {
                    cpuF = 0x00;
                    unsigned int tempAddr = HL();
                    unsigned char tempByte = read8(tempAddr);
                    SETC_ON_COND((tempByte & 0x01) != 0x00);
                    unsigned char tempByte2 = tempByte & 0x80;
                    tempByte = tempByte >> 1;
                    tempByte |= tempByte2;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x2f: // sra A
                {
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x01) != 0x00);
                    unsigned char tempByte = cpuA & 0x80;
                    cpuA = cpuA >> 1;
                    cpuA |= tempByte;
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x30: // swap B
                {
                    unsigned char tempByte = (cpuB << 4);
                    cpuB = cpuB >> 4;
                    cpuB &= 0x0f;
                    cpuB |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuB);
                }
                    return 8;
                case 0x31: // swap C
                {
                    unsigned char tempByte = (cpuC << 4);
                    cpuC = cpuC >> 4;
                    cpuC &= 0x0f;
                    cpuC |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuC);
                }
                    return 8;
                case 0x32: // swap D
                {
                    unsigned char tempByte = (cpuD << 4);
                    cpuD = cpuD >> 4;
                    cpuD &= 0x0f;
                    cpuD |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuD);
                }
                    return 8;
                case 0x33: // swap E
                {
                    unsigned char tempByte = (cpuE << 4);
                    cpuE = cpuE >> 4;
                    cpuE &= 0x0f;
                    cpuE |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuE);
                }
                    return 8;
                case 0x34: // swap H
                {
                    unsigned char tempByte = (cpuH << 4);
                    cpuH = cpuH >> 4;
                    cpuH &= 0x0f;
                    cpuH |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuH);
                }
                    return 8;
                case 0x35: // swap L
                {
                    unsigned char tempByte = (cpuL << 4);
                    cpuL = cpuL >> 4;
                    cpuL &= 0x0f;
                    cpuL |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuL);
                }
                    return 8;
                case 0x36: // swap (HL)
                {
                    unsigned int tempAddr = HL();
                    unsigned char tempByte = read8(tempAddr);
                    unsigned char tempByte2 = (tempByte << 4);
                    tempByte = tempByte >> 4;
                    tempByte &= 0x0f;
                    tempByte |= tempByte2;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x37: // swap A
                {
                    unsigned char tempByte = (cpuA << 4);
                    cpuA = cpuA >> 4;
                    cpuA &= 0x0f;
                    cpuA |= tempByte;
                    cpuF = 0x00;
                    SETZ_ON_ZERO(cpuA);
                }
                    return 8;
                case 0x38: // srl B
                    cpuF = 0x00;
                    SETC_ON_COND((cpuB & 0x01) != 0x00);
                    cpuB = cpuB >> 1;
                    cpuB &= 0x7f;
                    SETZ_ON_ZERO(cpuB);
                    return 8;
                case 0x39: // srl C
                    cpuF = 0x00;
                    SETC_ON_COND((cpuC & 0x01) != 0x00);
                    cpuC = cpuC >> 1;
                    cpuC &= 0x7f;
                    SETZ_ON_ZERO(cpuC);
                    return 8;
                case 0x3a: // srl D
                    cpuF = 0x00;
                    SETC_ON_COND((cpuD & 0x01) != 0x00);
                    cpuD = cpuD >> 1;
                    cpuD &= 0x7f;
                    SETZ_ON_ZERO(cpuD);
                    return 8;
                case 0x3b: // srl E
                    cpuF = 0x00;
                    SETC_ON_COND((cpuE & 0x01) != 0x00);
                    cpuE = cpuE >> 1;
                    cpuE &= 0x7f;
                    SETZ_ON_ZERO(cpuE);
                    return 8;
                case 0x3c: // srl H
                    cpuF = 0x00;
                    SETC_ON_COND((cpuH & 0x01) != 0x00);
                    cpuH = cpuH >> 1;
                    cpuH &= 0x7f;
                    SETZ_ON_ZERO(cpuH);
                    return 8;
                case 0x3d: // srl L
                    cpuF = 0x00;
                    SETC_ON_COND((cpuL & 0x01) != 0x00);
                    cpuL = cpuL >> 1;
                    cpuL &= 0x7f;
                    SETZ_ON_ZERO(cpuL);
                    return 8;
                case 0x3e: // srl (HL)
                {
                    unsigned int tempAddr = HL();
                    unsigned char tempByte = read8(tempAddr);
                    cpuF = 0x00;
                    SETC_ON_COND((tempByte & 0x01) != 0x00);
                    tempByte = tempByte >> 1;
                    tempByte &= 0x7f;
                    SETZ_ON_ZERO(tempByte);
                    write8(tempAddr, tempByte);
                }
                    return 16;
                case 0x3f: // srl A
                    cpuF = 0x00;
                    SETC_ON_COND((cpuA & 0x01) != 0x00);
                    cpuA = cpuA >> 1;
                    cpuA &= 0x7f;
                    SETZ_ON_ZERO(cpuA);
                    return 8;
                case 0x40: // Test bit 0 of B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x01);
                    return 8;
                case 0x41: // Test bit 0 of C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x01);
                    return 8;
                case 0x42: // Test bit 0 of D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x01);
                    return 8;
                case 0x43: // Test bit 0 of E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x01);
                    return 8;
                case 0x44: // Test bit 0 of H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x01);
                    return 8;
                case 0x45: // Test bit 0 of L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x01);
                    return 8;
                case 0x46: // Test bit 0 of (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x01);
                    return 12;
                case 0x47: // Test bit 0 of A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x01);
                    return 8;
                case 0x48: // bit 1, B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x02);
                    return 8;
                case 0x49: // bit 1, C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x02);
                    return 8;
                case 0x4a: // bit 1, D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x02);
                    return 8;
                case 0x4b: // bit 1, E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x02);
                    return 8;
                case 0x4c: // bit 1, H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x02);
                    return 8;
                case 0x4d: // bit 1, L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x02);
                    return 8;
                case 0x4e: // bit 1, (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x02);
                    return 12;
                case 0x4f: // bit 1, A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x02);
                    return 8;
                case 0x50: // bit 2, B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x04);
                    return 8;
                case 0x51: // bit 2, C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x04);
                    return 8;
                case 0x52: // bit 2, D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x04);
                    return 8;
                case 0x53: // bit 2, E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x04);
                    return 8;
                case 0x54: // bit 2, H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x04);
                    return 8;
                case 0x55: // bit 2, L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x04);
                    return 8;
                case 0x56: // bit 2, (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x04);
                    return 12;
                case 0x57: // bit 2, A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x04);
                    return 8;
                case 0x58: // bit 3, B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x08);
                    return 8;
                case 0x59: // bit 3, C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x08);
                    return 8;
                case 0x5a: // bit 3, D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x08);
                    return 8;
                case 0x5b: // bit 3, E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x08);
                    return 8;
                case 0x5c: // bit 3, H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x08);
                    return 8;
                case 0x5d: // bit 3, L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x08);
                    return 8;
                case 0x5e: // bit 3, (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x08);
                    return 12;
                case 0x5f: // bit 3, A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x08);
                    return 8;
                case 0x60: // bit 4, B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x10);
                    return 8;
                case 0x61: // bit 4, C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x10);
                    return 8;
                case 0x62: // bit 4, D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x10);
                    return 8;
                case 0x63: // bit 4, E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x10);
                    return 8;
                case 0x64: // bit 4, H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x10);
                    return 8;
                case 0x65: // bit 4, L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x10);
                    return 8;
                case 0x66: // bit 4, (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x10);
                    return 12;
                case 0x67: // bit 4, A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x10);
                    return 8;
                case 0x68: // bit 5, B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x20);
                    return 8;
                case 0x69: // bit 5, C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x20);
                    return 8;
                case 0x6a: // bit 5, D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x20);
                    return 8;
                case 0x6b: // bit 5, E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x20);
                    return 8;
                case 0x6c: // bit 5, H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x20);
                    return 8;
                case 0x6d: // bit 5, L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x20);
                    return 8;
                case 0x6e: // bit 5, (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x20);
                    return 12;
                case 0x6f: // bit 5, A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x20);
                    return 8;
                case 0x70: // bit 6, B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x40);
                    return 8;
                case 0x71: // bit 6, C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x40);
                    return 8;
                case 0x72: // bit 6, D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x40);
                    return 8;
                case 0x73: // bit 6, E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x40);
                    return 8;
                case 0x74: // bit 6, H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x40);
                    return 8;
                case 0x75: // bit 6, L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x40);
                    return 8;
                case 0x76: // bit 6, (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x40);
                    return 12;
                case 0x77: // bit 6, A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x40);
                    return 8;
                case 0x78: // bit 7, B
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuB & 0x80);
                    return 8;
                case 0x79: // bit 7, C
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuC & 0x80);
                    return 8;
                case 0x7a: // bit 7, D
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuD & 0x80);
                    return 8;
                case 0x7b: // bit 7, E
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuE & 0x80);
                    return 8;
                case 0x7c: // bit 7, H
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuH & 0x80);
                    return 8;
                case 0x7d: // bit 7, L
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuL & 0x80);
                    return 8;
                case 0x7e: // bit 7, (HL)
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(R8_HL() & 0x80);
                    return 12;
                case 0x7f: // bit 7, A
                    cpuF &= 0x30;
                    cpuF |= 0x20;
                    SETZ_ON_ZERO(cpuA & 0x80);
                    return 8;
                case 0x80: // res 0, B
                    cpuB &= 0xfe;
                    return 8;
                case 0x81: // res 0, C
                    cpuC &= 0xfe;
                    return 8;
                case 0x82: // res 0, D
                    cpuD &= 0xfe;
                    return 8;
                case 0x83: // res 0, E
                    cpuE &= 0xfe;
                    return 8;
                case 0x84: // res 0, H
                    cpuH &= 0xfe;
                    return 8;
                case 0x85: // res 0, L
                    cpuL &= 0xfe;
                    return 8;
                case 0x86: // res 0, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xfe);
                }
                    return 16;
                case 0x87: // res 0, A
                    cpuA &= 0xfe;
                    return 8;
                case 0x88: // res 1, B
                    cpuB &= 0xfd;
                    return 8;
                case 0x89: // res 1, C
                    cpuC &= 0xfd;
                    return 8;
                case 0x8a: // res 1, D
                    cpuD &= 0xfd;
                    return 8;
                case 0x8b: // res 1, E
                    cpuE &= 0xfd;
                    return 8;
                case 0x8c: // res 1, H
                    cpuH &= 0xfd;
                    return 8;
                case 0x8d: // res 1, L
                    cpuL &= 0xfd;
                    return 8;
                case 0x8e: // res 1, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xfd);
                }
                    return 16;
                case 0x8f: // res 1, A
                    cpuA &= 0xfd;
                    return 8;
                case 0x90: // res 2, B
                    cpuB &= 0xfb;
                    return 8;
                case 0x91: // res 2, C
                    cpuC &= 0xfb;
                    return 8;
                case 0x92: // res 2, D
                    cpuD &= 0xfb;
                    return 8;
                case 0x93: // res 2, E
                    cpuE &= 0xfb;
                    return 8;
                case 0x94: // res 2, H
                    cpuH &= 0xfb;
                    return 8;
                case 0x95: // res 2, L
                    cpuL &= 0xfb;
                    return 8;
                case 0x96: // res 2, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xfb);
                }
                    return 16;
                case 0x97: // res 2, A
                    cpuA &= 0xfb;
                    return 8;
                case 0x98: // res 3, B
                    cpuB &= 0xf7;
                    return 8;
                case 0x99: // res 3, C
                    cpuC &= 0xf7;
                    return 8;
                case 0x9a: // res 3, D
                    cpuD &= 0xf7;
                    return 8;
                case 0x9b: // res 3, E
                    cpuE &= 0xf7;
                    return 8;
                case 0x9c: // res 3, H
                    cpuH &= 0xf7;
                    return 8;
                case 0x9d: // res 3, L
                    cpuL &= 0xf7;
                    return 8;
                case 0x9e: // res 3, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xf7);
                }
                    return 16;
                case 0x9f: // res 3, A
                    cpuA &= 0xf7;
                    return 8;
                case 0xa0: // res 4, B
                    cpuB &= 0xef;
                    return 8;
                case 0xa1: // res 4, C
                    cpuC &= 0xef;
                    return 8;
                case 0xa2: // res 4, D
                    cpuD &= 0xef;
                    return 8;
                case 0xa3: // res 4, E
                    cpuE &= 0xef;
                    return 8;
                case 0xa4: // res 4, H
                    cpuH &= 0xef;
                    return 8;
                case 0xa5: // res 4, L
                    cpuL &= 0xef;
                    return 8;
                case 0xa6: // res 4, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xef);
                }
                    return 16;
                case 0xa7: // res 4, A
                    cpuA &= 0xef;
                    return 8;
                case 0xa8: // res 5, B
                    cpuB &= 0xdf;
                    return 8;
                case 0xa9: // res 5, C
                    cpuC &= 0xdf;
                    return 8;
                case 0xaa: // res 5, D
                    cpuD &= 0xdf;
                    return 8;
                case 0xab: // res 5, E
                    cpuE &= 0xdf;
                    return 8;
                case 0xac: // res 5, H
                    cpuH &= 0xdf;
                    return 8;
                case 0xad: // res 5, L
                    cpuL &= 0xdf;
                    return 8;
                case 0xae: // res 5, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xdf);
                }
                    return 16;
                case 0xaf: // res 5, A
                    cpuA &= 0xdf;
                    return 8;
                case 0xb0: // res 6, B
                    cpuB &= 0xbf;
                    return 8;
                case 0xb1: // res 6, C
                    cpuC &= 0xbf;
                    return 8;
                case 0xb2: // res 6, D
                    cpuD &= 0xbf;
                    return 8;
                case 0xb3: // res 6, E
                    cpuE &= 0xbf;
                    return 8;
                case 0xb4: // res 6, H
                    cpuH &= 0xbf;
                    return 8;
                case 0xb5: // res 6, L
                    cpuL &= 0xbf;
                    return 8;
                case 0xb6: // res 6, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0xbf);
                }
                    return 16;
                case 0xb7: // res 6, A
                    cpuA &= 0xbf;
                    return 8;
                case 0xb8: // res 7, B
                    cpuB &= 0x7f;
                    return 8;
                case 0xb9: // res 7, C
                    cpuC &= 0x7f;
                    return 8;
                case 0xba: // res 7, D
                    cpuD &= 0x7f;
                    return 8;
                case 0xbb: // res 7, E
                    cpuE &= 0x7f;
                    return 8;
                case 0xbc: // res 7, H
                    cpuH &= 0x7f;
                    return 8;
                case 0xbd: // res 7, L
                    cpuL &= 0x7f;
                    return 8;
                case 0xbe: // res 7, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) & 0x7f);
                }
                    return 16;
                case 0xbf: // res 7, A
                    cpuA &= 0x7f;
                    return 8;
                case 0xc0: // set 0, B
                    cpuB |= 0x01;
                    return 8;
                case 0xc1: // set 0, C
                    cpuC |= 0x01;
                    return 8;
                case 0xc2: // set 0, D
                    cpuD |= 0x01;
                    return 8;
                case 0xc3: // set 0, E
                    cpuE |= 0x01;
                    return 8;
                case 0xc4: // set 0, H
                    cpuH |= 0x01;
                    return 8;
                case 0xc5: // set 0, L
                    cpuL |= 0x01;
                    return 8;
                case 0xc6: // set 0, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x01);
                }
                    return 16;
                case 0xc7: // set 0, A
                    cpuA |= 0x01;
                    return 8;
                case 0xc8: // set 1, B
                    cpuB |= 0x02;
                    return 8;
                case 0xc9: // set 1, C
                    cpuC |= 0x02;
                    return 8;
                case 0xca: // set 1, D
                    cpuD |= 0x02;
                    return 8;
                case 0xcb: // set 1, E
                    cpuE |= 0x02;
                    return 8;
                case 0xcc: // set 1, H
                    cpuH |= 0x02;
                    return 8;
                case 0xcd: // set 1, L
                    cpuL |= 0x02;
                    return 8;
                case 0xce: // set 1, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x02);
                }
                    return 16;
                case 0xcf: // set 1, A
                    cpuA |= 0x02;
                    return 8;
                case 0xd0: // set 2, B
                    cpuB |= 0x04;
                    return 8;
                case 0xd1: // set 2, C
                    cpuC |= 0x04;
                    return 8;
                case 0xd2: // set 2, D
                    cpuD |= 0x04;
                    return 8;
                case 0xd3: // set 2, E
                    cpuE |= 0x04;
                    return 8;
                case 0xd4: // set 2, H
                    cpuH |= 0x04;
                    return 8;
                case 0xd5: // set 2, L
                    cpuL |= 0x04;
                    return 8;
                case 0xd6: // set 2, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x04);
                }
                    return 16;
                case 0xd7: // set 2, A
                    cpuA |= 0x04;
                    return 8;
                case 0xd8: // set 3, B
                    cpuB |= 0x08;
                    return 8;
                case 0xd9: // set 3, C
                    cpuC |= 0x08;
                    return 8;
                case 0xda: // set 3, D
                    cpuD |= 0x08;
                    return 8;
                case 0xdb: // set 3, E
                    cpuE |= 0x08;
                    return 8;
                case 0xdc: // set 3, H
                    cpuH |= 0x08;
                    return 8;
                case 0xdd: // set 3, L
                    cpuL |= 0x08;
                    return 8;
                case 0xde: // set 3, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x08);
                }
                    return 16;
                case 0xdf: // set 3, A
                    cpuA |= 0x08;
                    return 8;
                case 0xe0: // set 4, B
                    cpuB |= 0x10;
                    return 8;
                case 0xe1: // set 4, C
                    cpuC |= 0x10;
                    return 8;
                case 0xe2: // set 4, D
                    cpuD |= 0x10;
                    return 8;
                case 0xe3: // set 4, E
                    cpuE |= 0x10;
                    return 8;
                case 0xe4: // set 4, H
                    cpuH |= 0x10;
                    return 8;
                case 0xe5: // set 4, L
                    cpuL |= 0x10;
                    return 8;
                case 0xe6: // set 4, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x10);
                }
                    return 16;
                case 0xe7: // set 4, A
                    cpuA |= 0x10;
                    return 8;
                case 0xe8: // set 5, B
                    cpuB |= 0x20;
                    return 8;
                case 0xe9: // set 5, C
                    cpuC |= 0x20;
                    return 8;
                case 0xea: // set 5, D
                    cpuD |= 0x20;
                    return 8;
                case 0xeb: // set 5, E
                    cpuE |= 0x20;
                    return 8;
                case 0xec: // set 5, H
                    cpuH |= 0x20;
                    return 8;
                case 0xed: // set 5, L
                    cpuL |= 0x20;
                    return 8;
                case 0xee: // set 5, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x20);
                }
                    return 16;
                case 0xef: // set 5, A
                    cpuA |= 0x20;
                    return 8;
                case 0xf0: // set 6, B
                    cpuB |= 0x40;
                    return 8;
                case 0xf1: // set 6, C
                    cpuC |= 0x40;
                    return 8;
                case 0xf2: // set 6, D
                    cpuD |= 0x40;
                    return 8;
                case 0xf3: // set 6, E
                    cpuE |= 0x40;
                    return 8;
                case 0xf4: // set 6, H
                    cpuH |= 0x40;
                    return 8;
                case 0xf5: // set 6, L
                    cpuL |= 0x40;
                    return 8;
                case 0xf6: // set 6, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x40);
                }
                    return 16;
                case 0xf7: // set 6, A
                    cpuA |= 0x40;
                    return 8;
                case 0xf8: // set 7, B
                    cpuB |= 0x80;
                    return 8;
                case 0xf9: // set 7, C
                    cpuC |= 0x80;
                    return 8;
                case 0xfa: // set 7, D
                    cpuD |= 0x80;
                    return 8;
                case 0xfb: // set 7, E
                    cpuE |= 0x80;
                    return 8;
                case 0xfc: // set 7, H
                    cpuH |= 0x80;
                    return 8;
                case 0xfd: // set 7, L
                    cpuL |= 0x80;
                    return 8;
                case 0xfe: // set 7, (HL)
                {
                    unsigned int tempAddr = HL();
                    write8(tempAddr, read8(tempAddr) | 0x80);
                }
                    return 16;
                case 0xff: // set 7, A
                    cpuA |= 0x80;
                    return 8;
            }
        case 0xcc: // call Z, nn
            if ((cpuF & 0x80) != 0x00)
            {
                unsigned char msb = read8(cpuPc + 1);
                unsigned char lsb = read8(cpuPc + 2);
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
                cpuSp -= 2;
                cpuPc += 3;
                write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 24;
            }
            else
            {
                cpuPc += 3;
                return 12;
            }
        case 0xcd: // call nn
        {
            unsigned char msb = read8(cpuPc + 1);
            unsigned char lsb = read8(cpuPc + 2);
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = ((unsigned int)lsb << 8) + (unsigned int)msb;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc += 3;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
        }
            return 24;
        case 0xce: // adc A, n
        {
            unsigned char tempByte = read8(cpuPc + 1);
            unsigned char tempByte2 = cpuF & 0x10;
            cpuF = 0x00;
            if (tempByte2 != 0x00)
            {
                SETC_ON_COND(tempByte == 0xff);
                tempByte++;
            }
            cpuA += tempByte;
            SETZ_ON_ZERO(cpuA);
            SETC_ON_COND(cpuA < tempByte);
            tempByte = tempByte & 0x0f;
            tempByte2 = cpuA & 0x0f;
            SETH_ON_COND(tempByte > tempByte2);
        }
            cpuPc += 2;
            return 8;
        case 0xcf: // rst 8 (call 0x0008)
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x8;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = 0x0008;
            return 16;
        case 0xd0: // ret NC
            if ((cpuF & 0x10) != 0x00)
            {
                cpuPc++;
                return 8;
            }
            else
            {
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallReturned = 1;
                }
                unsigned char msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 20;
            }
        case 0xd1: // pop DE
            read16(cpuSp, &cpuE, &cpuD);
            cpuSp += 2;
            cpuPc++;
            return 12;
        case 0xd2: // j NC, nn
            if ((cpuF & 0x10) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1);
                return 16;
            }
        case 0xd3: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xd4: // call NC, nn
            if ((cpuF & 0x10) != 0x00)
            {
                cpuPc += 3;
                return 12;
            }
            else
            {
                unsigned char msb = read8(cpuPc + 1);
                unsigned char lsb = read8(cpuPc + 2);
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
                cpuSp -= 2;
                cpuPc += 3;
                write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 24;
            }
        case 0xd5: // push DE
            cpuSp -= 2;
            write16(cpuSp, cpuE, cpuD);
            cpuPc++;
            return 16;
        case 0xd6: // sub A, n
        {
            unsigned char msb = read8(cpuPc + 1);
            cpuF = 0x40;
            SETC_ON_COND(msb > cpuA);
            SETH_ON_COND((msb & 0x0f) > (cpuA & 0x0f));
            cpuA -= msb;
            if (cpuA == 0x00)
            {
                cpuF = 0xc0;
            }
        }
            cpuPc += 2;
            return 8;
        case 0xd7: // rst 10
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x10;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = 0x0010;
            return 16;
        case 0xd8: // ret C
            if ((cpuF & 0x10) != 0x00)
            {
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallReturned = 1;
                }
                unsigned char msb, lsb;
                read16(cpuSp, &msb, &lsb);
                cpuSp += 2;
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 20;
            }
            else
            {
                cpuPc++;
                return 8;
            }
        case 0xd9: // reti
        {
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallReturned = 1;
            }
            unsigned char msb, lsb;
            read16(cpuSp, &msb, &lsb);
            cpuSp += 2;
            cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
            cpuIme = true;
        }
            return 16;
        case 0xda: // j C, nn (abs jump if carry)
            if ((cpuF & 0x10) != 0x00)
            {
                cpuPc = ((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1);
                return 16;
            }
            else
            {
                cpuPc += 3;
                return 12;
            }
        case 0xdb: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xdc: // call C, nn
            if ((cpuF & 0x10) != 0x00)
            {
                unsigned char msb = read8(cpuPc + 1);
                unsigned char lsb = read8(cpuPc + 2);
                if (debugger.totalBreakEnables > 0)
                {
                    debugger.breakLastCallAt = cpuPc;
                    debugger.breakLastCallTo = ((unsigned int)lsb << 8) + (unsigned int)msb;
                    debugger.breakLastCallReturned = 0;
                }
                cpuSp -= 2;
                cpuPc += 3;
                write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
                cpuPc = ((unsigned int)lsb << 8) + (unsigned int)msb;
                return 24;
            }
            else
            {
                cpuPc += 3;
                return 12;
            }
        case 0xdd: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xde: // sbc A, n
        {
            unsigned char tempByte = cpuA;
            unsigned char tempByte2 = cpuF & 0x10;
            cpuF = 0x40;
            cpuA -= read8(cpuPc + 1);
            if (tempByte2 != 0x00)
            {
                if (cpuA == 0x00)
                {
                    cpuF |= 0x30;
                }
                cpuA--;
            }
            SETC_ON_COND(cpuA > tempByte);
            SETZ_ON_ZERO(cpuA);
            tempByte2 = tempByte & 0x0f;
            tempByte = cpuA & 0x0f;
            SETH_ON_COND(tempByte > tempByte2);
        }
            cpuPc += 2;
            return 8;
        case 0xdf: // rst 18
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x18;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
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
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xe4: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xe5: // push HL
            cpuSp -= 2;
            write16(cpuSp, cpuL, cpuH);
            cpuPc++;
            return 16;
        case 0xe6: // and n
            cpuA = cpuA & read8(cpuPc + 1);
            cpuF = 0x20;
            SETZ_ON_ZERO(cpuA);
            cpuPc += 2;
            return 8;
        case 0xe7: // rst 20
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x20;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = 0x0020;
            return 16;
        case 0xe8: // add SP, d
        {
            unsigned char msb = read8(cpuPc + 1);
            cpuF = 0x00;
            if (msb >= 0x80)
            {
                unsigned int tempAddr = 256 - (unsigned int)msb;
                cpuSp -= tempAddr;
                SETC_ON_COND((cpuSp & 0x0000ffff) > (tempAddr & 0x0000ffff));
                SETH_ON_COND((cpuSp & 0x000000ff) > (tempAddr & 0x000000ff));
            }
            else
            {
                unsigned int tempAddr = (unsigned int)msb;
                cpuSp += tempAddr;
                SETC_ON_COND((cpuSp & 0x0000ffff) < (tempAddr & 0x0000ffff));
                SETH_ON_COND((cpuSp & 0x000000ff) < (tempAddr & 0x000000ff));
            }
        }
            cpuPc += 2;
            return 16;
        case 0xe9: // j HL
            cpuPc = HL();
            return 4;
        case 0xea: // ld (nn), A
            write8(((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1), cpuA);
            cpuPc += 3;
            return 16;
        case 0xeb: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xec: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xed: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xee: // xor n
            cpuA = cpuA ^ read8(cpuPc + 1);
            cpuF = 0x00;
            SETZ_ON_ZERO(cpuA);
            cpuPc += 2;
            return 8;
        case 0xef: // rst 28
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x28;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = 0x0028;
            return 16;
        case 0xf0: // ldh A, (n)
            cpuA = read8(0xff00 + (unsigned int)read8(cpuPc + 1));
            cpuPc += 2;
            return 12;
        case 0xf1: // pop AF
            read16(cpuSp, &cpuF, &cpuA);
            cpuF &= 0xf0;
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
            running = false;
            throwException(instr);
            return clocksAcc;
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
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x30;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = 0x0030;
            return 16;
        case 0xf8: // ld HL, SP+d
        {
            unsigned char msb = read8(cpuPc + 1);
            cpuF = 0x00;
            unsigned int tempAddr = cpuSp;
            if (msb >= 0x80)
            {
                tempAddr -= 256 - (unsigned int)msb;
                SETC_ON_COND(tempAddr > cpuSp);
                SETH_ON_COND((tempAddr & 0x00ffffff) > (cpuSp & 0x00ffffff));
            }
            else
            {
                tempAddr += (unsigned int)msb;
                SETC_ON_COND(cpuSp > tempAddr);
                SETH_ON_COND((cpuSp & 0x00ffffff) > (tempAddr & 0x00ffffff));
            }
            cpuH = (unsigned char)(tempAddr >> 8);
            cpuL = (unsigned char)(tempAddr & 0xff);
        }
            cpuPc += 2;
            return 12;
        case 0xf9: // ld SP, HL
            cpuSp = HL();
            cpuPc++;
            return 8;
        case 0xfa: // ld A, (nn)
            cpuA = read8(((unsigned int)read8(cpuPc + 2) << 8) + (unsigned int)read8(cpuPc + 1));
            cpuPc += 3;
            return 16;
        case 0xfb: // ei
            cpuPc++;
            cpuIme = true;
            return 4;
        case 0xfc: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xfd: // REMOVED INSTRUCTION
            running = false;
            throwException(instr);
            return clocksAcc;
        case 0xfe: // cp n
        {
            unsigned char msb = read8(cpuPc + 1);
            cpuF = 0x40;
            SETH_ON_COND((msb & 0x0f) > (cpuA & 0x0f));
            SETC_ON_COND(msb > cpuA);
            SETZ_ON_COND(cpuA == msb);
        }
            cpuPc += 2;
            return 8;
        case 0xff: // rst 38
            if (debugger.totalBreakEnables > 0)
            {
                debugger.breakLastCallAt = cpuPc;
                debugger.breakLastCallTo = 0x38;
                debugger.breakLastCallReturned = 0;
            }
            cpuSp -= 2;
            cpuPc++;
            write16(cpuSp, (unsigned char)(cpuPc & 0xff), (unsigned char)(cpuPc >> 8));
            cpuPc = 0x0038;
            return 16;
    }
    return 0;
}


