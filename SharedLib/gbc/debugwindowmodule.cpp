#include "debugwindowmodule.h"

DebugWindowModule::DebugWindowModule() {
    breakCode = 0;
    totalBreakEnables = 0;
    breakOnSramEnable = 0;
    breakOnSramDisable = 0;
    breakOnPc = 0;
    breakOnWrite = 0;
    breakOnRead = 0;
    breakLastCallTo = 0;
    breakLastCallAt = 0;
    breakLastCallReturned = 0;
    breakPcAddr = 0x0100;
    breakWriteAddr = 0x0000;
    breakWriteByte = 0;
    breakReadAddr = 0x0000;
    breakReadByte = 0;
}
