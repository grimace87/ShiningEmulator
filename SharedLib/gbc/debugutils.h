#pragma once

#include <cstdio>
#include <cstdint>
#include <sstream>

class Gbc;

class DebugUtils {
    static bool addressFollowsCall(Gbc* gbc, uint32_t address, uint32_t* callingAddress);
    static void printLinePrefix(std::ostringstream& stream, uint32_t address);
    static void printOperation(std::ostringstream& stream, uint8_t opcode, uint8_t imm1, uint8_t imm2);
    static void printExtendedOperation(std::ostringstream& stream, uint8_t opcode, uint8_t imm1);
public:
    void writeTraceFile(Gbc* gbc, FILE* file);
};
