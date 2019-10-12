#include "debugutils.h"
#include "gbc.h"
#include <iomanip>

void DebugUtils::writeTraceFile(Gbc* gbc, FILE* file) {

    // Create string stream, set output flags
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');

    // Output current registers
    stream << "Registers:" << std::endl;
    stream << "PC=0x" << std::setw(4) << (int)gbc->cpuPc << std::endl;
    stream << "SP=0x" << std::setw(4) << (int)gbc->cpuSp << std::endl;
    stream << "AF=0x" << std::setw(2) << (int)gbc->cpuA << std::setw(2) << (int)gbc->cpuF << std::endl;
    stream << "BC=0x" << std::setw(2) << (int)gbc->cpuB << std::setw(2) << (int)gbc->cpuC << std::endl;
    stream << "DE=0x" << std::setw(2) << (int)gbc->cpuD << std::setw(2) << (int)gbc->cpuE << std::endl;
    stream << "HL=0x" << std::setw(2) << (int)gbc->cpuH << std::setw(2) << (int)gbc->cpuL << std::endl;
    stream << std::endl;

    // Print current ROM bank:
    stream << "Current ROM bank: " << (gbc->bankOffset / 0x4000) << std::endl << std::endl;

    // Print calling stack:
    stream << "Calling stack:" << std::endl;
    bool cameFromKnownCall = false;
    uint32_t stackAddress = gbc->cpuSp;
    do {
        if (stackAddress > 0x10000) {
            // Stack pointer may be going too high
            break;
        }
        uint8_t msb, lsb;
        uint32_t callingAddress;
        gbc->read16(stackAddress, &msb, &lsb);
        uint32_t returnAddress = ((unsigned int)lsb << 8U) + (unsigned int)msb;
        if (returnAddress < 3) {
            // Would not be able to look for a call instruction in this range
            break;
        }
        stackAddress += 2;
        cameFromKnownCall = addressFollowsCall(gbc, returnAddress, &callingAddress);
        if (cameFromKnownCall) {
            uint8_t opcode = gbc->read8(callingAddress);
            msb = gbc->read8(callingAddress + 1);
            lsb = gbc->read8(callingAddress + 2);
            printLinePrefix(stream, callingAddress);
            printOperation(stream, opcode, msb, lsb);
        }
    } while (cameFromKnownCall);
    stream << std::endl;

    // Print a bunch of operations from this point on
    // TODO

    // Print bodies of functions encountered
    // TODO

    // Write to file - leave closing the file up to the caller
    std::string result = stream.str();
    fwrite(result.c_str(), sizeof(char), result.length(), file);
}

bool DebugUtils::addressFollowsCall(Gbc* gbc, uint32_t address, uint32_t* callingAddress) {
    // Check for call instructions
    uint32_t testAddress = address - 3;
    uint8_t opcode = gbc->read8(testAddress);
    switch (opcode) {
        case 0xcc:
        case 0xc4:
        case 0xcd:
        case 0xd4:
        case 0xdc:
            *callingAddress = testAddress;
            return true;
    }

    // Check for rst instruction
    testAddress = address - 1;
    opcode = gbc->read8(testAddress);
    switch (opcode) {
        case 0xc7:
        case 0xcf:
        case 0xd7:
        case 0xdf:
        case 0xe7:
        case 0xef:
        case 0xf7:
        case 0xff:
            *callingAddress = testAddress;
            return true;
    }

    // Not a calling function
    return false;
}

void DebugUtils::printLinePrefix(std::ostringstream& stream, uint32_t address) {
    stream << "0x" << std::setw(4) << address << ' ';
}

void DebugUtils::printOperation(std::ostringstream& stream, uint8_t opcode, uint8_t imm1, uint8_t imm2) {
    if (opcode == 0xcb) {
        printExtendedOperation(stream, imm1, imm2);
        return;
    }
    switch (opcode) {
        case 0x00: stream << "nop"; break;
        case 0x01: stream << "ld BC, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0x02: stream << "ld (BC), A"; break;
        case 0x03: stream << "inc BC"; break;
        case 0x04: stream << "inc B"; break;
        case 0x05: stream << "dec B"; break;
        case 0x06: stream << "ld B, 0x" << std::setw(2) << (int)imm1; break;
        case 0x07: stream << "rlc A"; break;
        case 0x08: stream << "ld (0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1 << "), SP"; break;
        case 0x09: stream << "add HL, BC"; break;
        case 0x0a: stream << "ld A, (BC)"; break;
        case 0x0b: stream << "dec BC"; break;
        case 0x0c: stream << "inc C"; break;
        case 0x0d: stream << "dec C"; break;
        case 0x0e: stream << "ld C, 0x" << std::setw(2) << (int)imm1; break;
        case 0x0f: stream << "rrc A"; break;
        case 0x10: stream << "stop"; break;
        case 0x11: stream << "ld DE, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0x12: stream << "ld (DE), A"; break;
        case 0x13: stream << "inc DE"; break;
        case 0x14: stream << "inc D"; break;
        case 0x15: stream << "dec D"; break;
        case 0x16: stream << "ld D, 0x" << std::setw(2) << (int)imm1; break;
        case 0x17: stream << "rl A"; break;
        case 0x18: { if (imm1 < 0x80) stream << "jr 0x" << std::setw(2) << (int)imm1; else stream << "jr -0x" << (0x100 - imm1); } break;
        case 0x19: stream << "add HL, DE"; break;
        case 0x1a: stream << "ld A, (DE)"; break;
        case 0x1b: stream << "dec DE"; break;
        case 0x1c: stream << "inc E"; break;
        case 0x1d: stream << "dec E"; break;
        case 0x1e: stream << "ld E, 0x" << std::setw(2) << (int)imm1; break;
        case 0x1f: stream << "rr A"; break;
        case 0x20: { if (imm1 < 0x80) stream << "jr NZ, 0x" << std::setw(2) << (int)imm1; else stream << "jr NZ, -0x" << (0x100 - imm1); } break;
        case 0x21: stream << "ld HL, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0x22: stream << "ldi (HL), A"; break;
        case 0x23: stream << "inc HL"; break;
        case 0x24: stream << "inc H"; break;
        case 0x25: stream << "dec H"; break;
        case 0x26: stream << "ld H, 0x" << std::setw(2) << (int)imm1; break;
        case 0x27: stream << "daa"; break;
        case 0x28: { if (imm1 < 0x80) stream << "jr Z, 0x" << std::setw(2) << (int)imm1; else stream << "jr Z, -0x" << (0x100 - imm1); } break;
        case 0x29: stream << "add HL, HL"; break;
        case 0x2a: stream << "ldi A, (HL)"; break;
        case 0x2b: stream << "dec HL"; break;
        case 0x2c: stream << "inc L"; break;
        case 0x2d: stream << "dec L"; break;
        case 0x2e: stream << "ld L, 0x" << std::setw(2) << (int)imm1; break;
        case 0x2f: stream << "cpl A"; break;
        case 0x30: { if (imm1 < 0x80) stream << "jr NC, 0x" << std::setw(2) << (int)imm1; else stream << "jr NC, -0x" << (0x100 - imm1); } break;
        case 0x31: stream << "ld SP, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0x32: stream << "ldd (HL), A"; break;
        case 0x33: stream << "inc SP"; break;
        case 0x34: stream << "inc (HL)"; break;
        case 0x35: stream << "dec (HL)"; break;
        case 0x36: stream << "ld (HL), 0x" << std::setw(2) << (int)imm1; break;
        case 0x37: stream << "scf"; break;
        case 0x38: { if (imm1 < 0x80) stream << "jr C, 0x" << std::setw(2) << (int)imm1; else stream << "jr C, -0x" << (0x100 - imm1); } break;
        case 0x39: stream << "add HL, SP"; break;
        case 0x3a: stream << "ldd A, (HL)"; break;
        case 0x3b: stream << "dec SP"; break;
        case 0x3c: stream << "inc A"; break;
        case 0x3d: stream << "dec A"; break;
        case 0x3e: stream << "ld A, 0x" << std::setw(2) << (int)imm1; break;
        case 0x3f: stream << "ccf"; break;
        case 0x40: stream << "ld B, B"; break;
        case 0x41: stream << "ld B, C"; break;
        case 0x42: stream << "ld B, D"; break;
        case 0x43: stream << "ld B, E"; break;
        case 0x44: stream << "ld B, H"; break;
        case 0x45: stream << "ld B, L"; break;
        case 0x46: stream << "ld B, (HL)"; break;
        case 0x47: stream << "ld B, A"; break;
        case 0x48: stream << "ld C, B"; break;
        case 0x49: stream << "ld C, C"; break;
        case 0x4a: stream << "ld C, D"; break;
        case 0x4b: stream << "ld C, E"; break;
        case 0x4c: stream << "ld C, H"; break;
        case 0x4d: stream << "ld C, L"; break;
        case 0x4e: stream << "ld C, (HL)"; break;
        case 0x4f: stream << "ld C, A"; break;
        case 0x50: stream << "ld D, B"; break;
        case 0x51: stream << "ld D, C"; break;
        case 0x52: stream << "ld D, D"; break;
        case 0x53: stream << "ld D, E"; break;
        case 0x54: stream << "ld D, H"; break;
        case 0x55: stream << "ld D, L"; break;
        case 0x56: stream << "ld D, (HL)"; break;
        case 0x57: stream << "ld D, A"; break;
        case 0x58: stream << "ld E, B"; break;
        case 0x59: stream << "ld E, C"; break;
        case 0x5a: stream << "ld E, D"; break;
        case 0x5b: stream << "ld E, E"; break;
        case 0x5c: stream << "ld E, H"; break;
        case 0x5d: stream << "ld E, L"; break;
        case 0x5e: stream << "ld E, (HL)"; break;
        case 0x5f: stream << "ld E, A"; break;
        case 0x60: stream << "ld H, B"; break;
        case 0x61: stream << "ld H, C"; break;
        case 0x62: stream << "ld H, D"; break;
        case 0x63: stream << "ld H, E"; break;
        case 0x64: stream << "ld H, H"; break;
        case 0x65: stream << "ld H, L"; break;
        case 0x66: stream << "ld H, (HL)"; break;
        case 0x67: stream << "ld H, A"; break;
        case 0x68: stream << "ld L, B"; break;
        case 0x69: stream << "ld L, C"; break;
        case 0x6a: stream << "ld L, D"; break;
        case 0x6b: stream << "ld L, E"; break;
        case 0x6c: stream << "ld L, H"; break;
        case 0x6d: stream << "ld L, L"; break;
        case 0x6e: stream << "ld L, (HL)"; break;
        case 0x6f: stream << "ld L, A"; break;
        case 0x70: stream << "ld (HL), B"; break;
        case 0x71: stream << "ld (HL), C"; break;
        case 0x72: stream << "ld (HL), D"; break;
        case 0x73: stream << "ld (HL), E"; break;
        case 0x74: stream << "ld (HL), H"; break;
        case 0x75: stream << "ld (HL), L"; break;
        case 0x76: stream << "halt"; break;
        case 0x77: stream << "ld (HL), A"; break;
        case 0x78: stream << "ld A, B"; break;
        case 0x79: stream << "ld A, C"; break;
        case 0x7a: stream << "ld A, D"; break;
        case 0x7b: stream << "ld A, E"; break;
        case 0x7c: stream << "ld A, H"; break;
        case 0x7d: stream << "ld A, L"; break;
        case 0x7e: stream << "ld A, (HL)"; break;
        case 0x7f: stream << "ld A, A"; break;
        case 0x80: stream << "add A, B"; break;
        case 0x81: stream << "add A, C"; break;
        case 0x82: stream << "add A, D"; break;
        case 0x83: stream << "add A, E"; break;
        case 0x84: stream << "add A, H"; break;
        case 0x85: stream << "add A, L"; break;
        case 0x86: stream << "add A, (HL)"; break;
        case 0x87: stream << "add A, A"; break;
        case 0x88: stream << "adc A, B"; break;
        case 0x89: stream << "adc A, C"; break;
        case 0x8a: stream << "adc A, D"; break;
        case 0x8b: stream << "adc A, E"; break;
        case 0x8c: stream << "adc A, H"; break;
        case 0x8d: stream << "adc A, L"; break;
        case 0x8e: stream << "adc A, (HL)"; break;
        case 0x8f: stream << "adc A, A"; break;
        case 0x90: stream << "sub A, B"; break;
        case 0x91: stream << "sub A, C"; break;
        case 0x92: stream << "sub A, D"; break;
        case 0x93: stream << "sub A, E"; break;
        case 0x94: stream << "sub A, H"; break;
        case 0x95: stream << "sub A, L"; break;
        case 0x96: stream << "sub A, (HL)"; break;
        case 0x97: stream << "sub A, A"; break;
        case 0x98: stream << "sbc A, B"; break;
        case 0x99: stream << "sbc A, C"; break;
        case 0x9a: stream << "sbc A, D"; break;
        case 0x9b: stream << "sbc A, E"; break;
        case 0x9c: stream << "sbc A, H"; break;
        case 0x9d: stream << "sbc A, L"; break;
        case 0x9e: stream << "sbc A, (HL)"; break;
        case 0x9f: stream << "sbc A, A"; break;
        case 0xa0: stream << "and A, B"; break;
        case 0xa1: stream << "and A, C"; break;
        case 0xa2: stream << "and A, D"; break;
        case 0xa3: stream << "and A, E"; break;
        case 0xa4: stream << "and A, H"; break;
        case 0xa5: stream << "and A, L"; break;
        case 0xa6: stream << "and A, (HL)"; break;
        case 0xa7: stream << "and A, A"; break;
        case 0xa8: stream << "xor A, B"; break;
        case 0xa9: stream << "xor A, C"; break;
        case 0xaa: stream << "xor A, D"; break;
        case 0xab: stream << "xor A, E"; break;
        case 0xac: stream << "xor A, H"; break;
        case 0xad: stream << "xor A, L"; break;
        case 0xae: stream << "xor A, (HL)"; break;
        case 0xaf: stream << "xor A, A"; break;
        case 0xb0: stream << "or A, B"; break;
        case 0xb1: stream << "or A, C"; break;
        case 0xb2: stream << "or A, D"; break;
        case 0xb3: stream << "or A, E"; break;
        case 0xb4: stream << "or A, H"; break;
        case 0xb5: stream << "or A, L"; break;
        case 0xb6: stream << "or A, (HL)"; break;
        case 0xb7: stream << "or A, A"; break;
        case 0xb8: stream << "cp A, B"; break;
        case 0xb9: stream << "cp A, C"; break;
        case 0xba: stream << "cp A, D"; break;
        case 0xbb: stream << "cp A, E"; break;
        case 0xbc: stream << "cp A, H"; break;
        case 0xbd: stream << "cp A, L"; break;
        case 0xbe: stream << "cp A, (HL)"; break;
        case 0xbf: stream << "cp A, A"; break;
        case 0xc0: stream << "ret NZ"; break;
        case 0xc1: stream << "pop BC"; break;
        case 0xc2: stream << "j NZ, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xc3: stream << "j 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xc4: stream << "call NZ, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xc5: stream << "push BC"; break;
        case 0xc6: stream << "add A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xc7: stream << "rst 0x0000"; break;
        case 0xc8: stream << "ret Z"; break;
        case 0xc9: stream << "ret"; break;
        case 0xca: stream << "j Z, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xcc: stream << "call Z, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xcd: stream << "call 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xce: stream << "adc A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xcf: stream << "rst 0x0008"; break;
        case 0xd0: stream << "ret NC"; break;
        case 0xd1: stream << "pop DE"; break;
        case 0xd2: stream << "j NC, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xd3: stream << "REMOVED (0xd3)"; break;
        case 0xd4: stream << "call NC, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xd5: stream << "push DE"; break;
        case 0xd6: stream << "sub A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xd7: stream << "rst 0x0010"; break;
        case 0xd8: stream << "ret C"; break;
        case 0xd9: stream << "reti"; break;
        case 0xda: stream << "j C, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xdb: stream << "REMOVED (0xdb)"; break;
        case 0xdc: stream << "call C, 0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1; break;
        case 0xdd: stream << "REMOVED (0xdd)"; break;
        case 0xde: stream << "sbc A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xdf: stream << "rst 0x0018"; break;
        case 0xe0: stream << "ldh (0x" << std::setw(2) << (int)imm1 << "), A"; break;
        case 0xe1: stream << "pop HL"; break;
        case 0xe2: stream << "ldh (C), A"; break;
        case 0xe3: stream << "REMOVED (0xe3)"; break;
        case 0xe4: stream << "REMOVED (0xe4)"; break;
        case 0xe5: stream << "push HL"; break;
        case 0xe6: stream << "and A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xe7: stream << "rst 0x0020"; break;
        case 0xe8: { if (imm1 < 0x80) stream << "add SP, 0x" << std::setw(2) << (int)imm1; else stream << "add SP, -0x" << (0x100 - imm1); } break;
        case 0xe9: stream << "j HL"; break;
        case 0xea: stream << "ld (0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1 << "), A"; break;
        case 0xeb: stream << "REMOVED (0xeb)"; break;
        case 0xec: stream << "REMOVED (0xec)"; break;
        case 0xed: stream << "REMOVED (0xed)"; break;
        case 0xee: stream << "xor A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xef: stream << "rst 0x0020"; break;
        case 0xf0: stream << "ldh A, (0x" << std::setw(2) << (int)imm1 << ")"; break;
        case 0xf1: stream << "pop AF"; break;
        case 0xf2: stream << "ldh A, (C)"; break;
        case 0xf3: stream << "di"; break;
        case 0xf4: stream << "REMOVED (0xf4)"; break;
        case 0xf5: stream << "push AF"; break;
        case 0xf6: stream << "or A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xf7: stream << "rst 0x0030"; break;
        case 0xf8: { if (imm1 < 0x80) stream << "ld HL, SP+0x" << std::setw(2) << (int)imm1; else stream << "ld HL, SP-0x" << (0x100 - imm1); } break;
        case 0xf9: stream << "ld SP, HL"; break;
        case 0xfa: stream << "ld A, (0x" << std::setw(2) << (int)imm2 << std::setw(2) << (int)imm1 << ")"; break;
        case 0xfb: stream << "ei"; break;
        case 0xfc: stream << "REMOVED (0xfc)"; break;
        case 0xfd: stream << "REMOVED (0xfd)"; break;
        case 0xfe: stream << "cp A, 0x" << std::setw(2) << (int)imm1; break;
        case 0xff: stream << "rst 0x0038"; break;
    }
    stream << std::endl;
}

void DebugUtils::printExtendedOperation(std::ostringstream& stream, uint8_t opcode, uint8_t imm1) {
    switch (opcode) {
        case 0x00: stream << "rlc B"; break;
        case 0x01: stream << "rlc C"; break;
        case 0x02: stream << "rlc D"; break;
        case 0x03: stream << "rlc E"; break;
        case 0x04: stream << "rlc H"; break;
        case 0x05: stream << "rlc L"; break;
        case 0x06: stream << "rlc (HL)"; break;
        case 0x07: stream << "rlc A"; break;
        case 0x08: stream << "rrc B"; break;
        case 0x09: stream << "rrc C"; break;
        case 0x0a: stream << "rrc D"; break;
        case 0x0b: stream << "rrc E"; break;
        case 0x0c: stream << "rrc H"; break;
        case 0x0d: stream << "rrc L"; break;
        case 0x0e: stream << "rrc (HL)"; break;
        case 0x0f: stream << "rrc A"; break;
        case 0x10: stream << "rl B"; break;
        case 0x11: stream << "rl C"; break;
        case 0x12: stream << "rl D"; break;
        case 0x13: stream << "rl E"; break;
        case 0x14: stream << "rl H"; break;
        case 0x15: stream << "rl L"; break;
        case 0x16: stream << "rl (HL)"; break;
        case 0x17: stream << "rl A"; break;
        case 0x18: stream << "rr B"; break;
        case 0x19: stream << "rr C"; break;
        case 0x1a: stream << "rr D"; break;
        case 0x1b: stream << "rr E"; break;
        case 0x1c: stream << "rr H"; break;
        case 0x1d: stream << "rr L"; break;
        case 0x1e: stream << "rr (HL)"; break;
        case 0x1f: stream << "rr A"; break;
        case 0x20: stream << "sla B"; break;
        case 0x21: stream << "sla C"; break;
        case 0x22: stream << "sla D"; break;
        case 0x23: stream << "sla E"; break;
        case 0x24: stream << "sla H"; break;
        case 0x25: stream << "sla L"; break;
        case 0x26: stream << "sla (HL)"; break;
        case 0x27: stream << "sla A"; break;
        case 0x28: stream << "sra B"; break;
        case 0x29: stream << "sra C"; break;
        case 0x2a: stream << "sra D"; break;
        case 0x2b: stream << "sra E"; break;
        case 0x2c: stream << "sra H"; break;
        case 0x2d: stream << "sra L"; break;
        case 0x2e: stream << "sra (HL)"; break;
        case 0x2f: stream << "sra A"; break;
        case 0x30: stream << "swap B"; break;
        case 0x31: stream << "swap C"; break;
        case 0x32: stream << "swap D"; break;
        case 0x33: stream << "swap E"; break;
        case 0x34: stream << "swap H"; break;
        case 0x35: stream << "swap L"; break;
        case 0x36: stream << "swap (HL)"; break;
        case 0x37: stream << "swap A"; break;
        case 0x38: stream << "srl B"; break;
        case 0x39: stream << "srl C"; break;
        case 0x3a: stream << "srl D"; break;
        case 0x3b: stream << "srl E"; break;
        case 0x3c: stream << "srl H"; break;
        case 0x3d: stream << "srl L"; break;
        case 0x3e: stream << "srl (HL)"; break;
        case 0x3f: stream << "srl A"; break;
        case 0x40: stream << "bit 0, B"; break;
        case 0x41: stream << "bit 0, C"; break;
        case 0x42: stream << "bit 0, D"; break;
        case 0x43: stream << "bit 0, E"; break;
        case 0x44: stream << "bit 0, H"; break;
        case 0x45: stream << "bit 0, L"; break;
        case 0x46: stream << "bit 0, (HL)"; break;
        case 0x47: stream << "bit 0, A"; break;
        case 0x48: stream << "bit 1, B"; break;
        case 0x49: stream << "bit 1, C"; break;
        case 0x4a: stream << "bit 1, D"; break;
        case 0x4b: stream << "bit 1, E"; break;
        case 0x4c: stream << "bit 1, H"; break;
        case 0x4d: stream << "bit 1, L"; break;
        case 0x4e: stream << "bit 1, (HL)"; break;
        case 0x4f: stream << "bit 1, A"; break;
        case 0x50: stream << "bit 2, B"; break;
        case 0x51: stream << "bit 2, C"; break;
        case 0x52: stream << "bit 2, D"; break;
        case 0x53: stream << "bit 2, E"; break;
        case 0x54: stream << "bit 2, H"; break;
        case 0x55: stream << "bit 2, L"; break;
        case 0x56: stream << "bit 2, (HL)"; break;
        case 0x57: stream << "bit 2, A"; break;
        case 0x58: stream << "bit 3, B"; break;
        case 0x59: stream << "bit 3, C"; break;
        case 0x5a: stream << "bit 3, D"; break;
        case 0x5b: stream << "bit 3, E"; break;
        case 0x5c: stream << "bit 3, H"; break;
        case 0x5d: stream << "bit 3, L"; break;
        case 0x5e: stream << "bit 3, (HL)"; break;
        case 0x5f: stream << "bit 3, A"; break;
        case 0x60: stream << "bit 4, B"; break;
        case 0x61: stream << "bit 4, C"; break;
        case 0x62: stream << "bit 4, D"; break;
        case 0x63: stream << "bit 4, E"; break;
        case 0x64: stream << "bit 4, H"; break;
        case 0x65: stream << "bit 4, L"; break;
        case 0x66: stream << "bit 4, (HL)"; break;
        case 0x67: stream << "bit 4, A"; break;
        case 0x68: stream << "bit 5, B"; break;
        case 0x69: stream << "bit 5, C"; break;
        case 0x6a: stream << "bit 5, D"; break;
        case 0x6b: stream << "bit 5, E"; break;
        case 0x6c: stream << "bit 5, H"; break;
        case 0x6d: stream << "bit 5, L"; break;
        case 0x6e: stream << "bit 5, (HL)"; break;
        case 0x6f: stream << "bit 5, A"; break;
        case 0x70: stream << "bit 6, B"; break;
        case 0x71: stream << "bit 6, C"; break;
        case 0x72: stream << "bit 6, D"; break;
        case 0x73: stream << "bit 6, E"; break;
        case 0x74: stream << "bit 6, H"; break;
        case 0x75: stream << "bit 6, L"; break;
        case 0x76: stream << "bit 6, (HL)"; break;
        case 0x77: stream << "bit 6, A"; break;
        case 0x78: stream << "bit 7, B"; break;
        case 0x79: stream << "bit 7, C"; break;
        case 0x7a: stream << "bit 7, D"; break;
        case 0x7b: stream << "bit 7, E"; break;
        case 0x7c: stream << "bit 7, H"; break;
        case 0x7d: stream << "bit 7, L"; break;
        case 0x7e: stream << "bit 7, (HL)"; break;
        case 0x7f: stream << "bit 7, A"; break;
        case 0x80: stream << "res 0, B"; break;
        case 0x81: stream << "res 0, C"; break;
        case 0x82: stream << "res 0, D"; break;
        case 0x83: stream << "res 0, E"; break;
        case 0x84: stream << "res 0, H"; break;
        case 0x85: stream << "res 0, L"; break;
        case 0x86: stream << "res 0, (HL)"; break;
        case 0x87: stream << "res 0, A"; break;
        case 0x88: stream << "res 1, B"; break;
        case 0x89: stream << "res 1, C"; break;
        case 0x8a: stream << "res 1, D"; break;
        case 0x8b: stream << "res 1, E"; break;
        case 0x8c: stream << "res 1, H"; break;
        case 0x8d: stream << "res 1, L"; break;
        case 0x8e: stream << "res 1, (HL)"; break;
        case 0x8f: stream << "res 1, A"; break;
        case 0x90: stream << "res 2, B"; break;
        case 0x91: stream << "res 2, C"; break;
        case 0x92: stream << "res 2, D"; break;
        case 0x93: stream << "res 2, E"; break;
        case 0x94: stream << "res 2, H"; break;
        case 0x95: stream << "res 2, L"; break;
        case 0x96: stream << "res 2, (HL)"; break;
        case 0x97: stream << "res 2, A"; break;
        case 0x98: stream << "res 3, B"; break;
        case 0x99: stream << "res 3, C"; break;
        case 0x9a: stream << "res 3, D"; break;
        case 0x9b: stream << "res 3, E"; break;
        case 0x9c: stream << "res 3, H"; break;
        case 0x9d: stream << "res 3, L"; break;
        case 0x9e: stream << "res 3, (HL)"; break;
        case 0x9f: stream << "res 3, A"; break;
        case 0xa0: stream << "res 4, B"; break;
        case 0xa1: stream << "res 4, C"; break;
        case 0xa2: stream << "res 4, D"; break;
        case 0xa3: stream << "res 4, E"; break;
        case 0xa4: stream << "res 4, H"; break;
        case 0xa5: stream << "res 4, L"; break;
        case 0xa6: stream << "res 4, (HL)"; break;
        case 0xa7: stream << "res 4, A"; break;
        case 0xa8: stream << "res 5, B"; break;
        case 0xa9: stream << "res 5, C"; break;
        case 0xaa: stream << "res 5, D"; break;
        case 0xab: stream << "res 5, E"; break;
        case 0xac: stream << "res 5, H"; break;
        case 0xad: stream << "res 5, L"; break;
        case 0xae: stream << "res 5, (HL)"; break;
        case 0xaf: stream << "res 5, A"; break;
        case 0xb0: stream << "res 6, B"; break;
        case 0xb1: stream << "res 6, C"; break;
        case 0xb2: stream << "res 6, D"; break;
        case 0xb3: stream << "res 6, E"; break;
        case 0xb4: stream << "res 6, H"; break;
        case 0xb5: stream << "res 6, L"; break;
        case 0xb6: stream << "res 6, (HL)"; break;
        case 0xb7: stream << "res 6, A"; break;
        case 0xb8: stream << "res 7, B"; break;
        case 0xb9: stream << "res 7, C"; break;
        case 0xba: stream << "res 7, D"; break;
        case 0xbb: stream << "res 7, E"; break;
        case 0xbc: stream << "res 7, H"; break;
        case 0xbd: stream << "res 7, L"; break;
        case 0xbe: stream << "res 7, (HL)"; break;
        case 0xbf: stream << "res 7, A"; break;
        case 0xc0: stream << "set 0, B"; break;
        case 0xc1: stream << "set 0, C"; break;
        case 0xc2: stream << "set 0, D"; break;
        case 0xc3: stream << "set 0, E"; break;
        case 0xc4: stream << "set 0, H"; break;
        case 0xc5: stream << "set 0, L"; break;
        case 0xc6: stream << "set 0, (HL)"; break;
        case 0xc7: stream << "set 0, A"; break;
        case 0xc8: stream << "set 1, B"; break;
        case 0xc9: stream << "set 1, C"; break;
        case 0xca: stream << "set 1, D"; break;
        case 0xcb: stream << "set 1, E"; break;
        case 0xcc: stream << "set 1, H"; break;
        case 0xcd: stream << "set 1, L"; break;
        case 0xce: stream << "set 1, (HL)"; break;
        case 0xcf: stream << "set 1, A"; break;
        case 0xd0: stream << "set 2, B"; break;
        case 0xd1: stream << "set 2, C"; break;
        case 0xd2: stream << "set 2, D"; break;
        case 0xd3: stream << "set 2, E"; break;
        case 0xd4: stream << "set 2, H"; break;
        case 0xd5: stream << "set 2, L"; break;
        case 0xd6: stream << "set 2, (HL)"; break;
        case 0xd7: stream << "set 2, A"; break;
        case 0xd8: stream << "set 3, B"; break;
        case 0xd9: stream << "set 3, C"; break;
        case 0xda: stream << "set 3, D"; break;
        case 0xdb: stream << "set 3, E"; break;
        case 0xdc: stream << "set 3, H"; break;
        case 0xdd: stream << "set 3, L"; break;
        case 0xde: stream << "set 3, (HL)"; break;
        case 0xdf: stream << "set 3, A"; break;
        case 0xe0: stream << "set 4, B"; break;
        case 0xe1: stream << "set 4, C"; break;
        case 0xe2: stream << "set 4, D"; break;
        case 0xe3: stream << "set 4, E"; break;
        case 0xe4: stream << "set 4, H"; break;
        case 0xe5: stream << "set 4, L"; break;
        case 0xe6: stream << "set 4, (HL)"; break;
        case 0xe7: stream << "set 4, A"; break;
        case 0xe8: stream << "set 5, B"; break;
        case 0xe9: stream << "set 5, C"; break;
        case 0xea: stream << "set 5, D"; break;
        case 0xeb: stream << "set 5, E"; break;
        case 0xec: stream << "set 5, H"; break;
        case 0xed: stream << "set 5, L"; break;
        case 0xee: stream << "set 5, (HL)"; break;
        case 0xef: stream << "set 5, A"; break;
        case 0xf0: stream << "set 6, B"; break;
        case 0xf1: stream << "set 6, C"; break;
        case 0xf2: stream << "set 6, D"; break;
        case 0xf3: stream << "set 6, E"; break;
        case 0xf4: stream << "set 6, H"; break;
        case 0xf5: stream << "set 6, L"; break;
        case 0xf6: stream << "set 6, (HL)"; break;
        case 0xf7: stream << "set 6, A"; break;
        case 0xf8: stream << "set 7, B"; break;
        case 0xf9: stream << "set 7, C"; break;
        case 0xfa: stream << "set 7, D"; break;
        case 0xfb: stream << "set 7, E"; break;
        case 0xfc: stream << "set 7, H"; break;
        case 0xfd: stream << "set 7, L"; break;
        case 0xfe: stream << "set 7, (HL)"; break;
        case 0xff: stream << "set 7, A"; break;
    }
    stream << std::endl;
}
