#pragma once

class DebugWindowModule {
public:
    int breakCode;
    int totalBreakEnables;
    int breakOnSramEnable;
    int breakOnSramDisable;
    int breakOnPc;
    int breakOnWrite;
    int breakOnRead;
    char breakMsg[25] = {};
    unsigned int breakLastCallTo;
    unsigned int breakLastCallAt;
    unsigned int breakLastCallReturned;
    unsigned int breakPcAddr;
    unsigned int breakWriteAddr;
    unsigned int breakWriteByte;
    unsigned int breakReadAddr;
    unsigned int breakReadByte;
    DebugWindowModule();
};
