#pragma once

#include <cstdio>
#include <cstdint>
#include <sstream>

class Gbc;

class DebugUtils {
    static bool addressFollowsCall(Gbc* gbc, uint32_t address, uint32_t* callingAddress);
    static bool isCallOp(uint8_t opcode);
    static bool isRstOp(uint8_t opcode);
    static void printLinePrefix(std::ostringstream& stream, uint32_t address);
    static void printFunctionContents(Gbc* gbc, std::ostringstream& stream, uint32_t fromAddress, bool printSubFunctions);
    static uint32_t getAdjustedPcFollowingOperation(uint32_t currentPc, uint8_t opcode, uint8_t imm1, uint8_t imm2);
    static void printOperation(std::ostringstream& stream, uint8_t opcode, uint8_t imm1, uint8_t imm2);
    static void printExtendedOperation(std::ostringstream& stream, uint8_t opcode, uint8_t imm1);
public:
    void writeTraceFile(Gbc* gbc, FILE* file);
};
