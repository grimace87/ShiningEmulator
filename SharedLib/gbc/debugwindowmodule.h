#pragma once

#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif

#include <Windows.h>

#define LAUNCH_DEBUG_MSG WM_USER + 1

class Gbc;

class DebugWindowModule {

	// Current GBC instance
	static Gbc* gbc;

	// Register class
	static ATOM registerWindowClass(HINSTANCE instance);
	static bool classRegistered;

	// Handle to window
	static HWND debugWindow;

	// Handles to component 'windows'
	static HWND debugStatusComponent;
	static HWND romTextComponent;
	static HWND debugTextComponent;
	static HWND staticTextComponent;
	static HWND pauseButtonComponent;
	static HWND refreshButtonComponent;
	static HWND dropdownBoxComponent;
	static HWND dropdownBankComponent;
	static HWND breakSramEnableComponent;
	static HWND breakSramDisableComponent;
	static HWND breakPcComponent;
	static HWND breakPcAddrComponent;
	static HWND breakWriteComponent;
	static HWND breakWriteAddrComponent;
	static HWND breakReadComponent;
	static HWND breakReadAddrComponent;

	// Custom window procedures for the window and components
	static LRESULT APIENTRY debugWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY pauseButtonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY refreshButtonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY dropdownWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY dropdownBankWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY checkBoxWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY breakPcAddrWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY breakWriteAddrWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT APIENTRY breakReadAddrWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// Handles to default window procedures
	static WNDPROC pauseButtonDefProc;
	static WNDPROC refreshButtonDefProc;
	static WNDPROC dropdownDefProc;
	static WNDPROC dropdownBankDefProc;
	static WNDPROC breakSramEnableDefProc;
	static WNDPROC breakSramDisableDefProc;
	static WNDPROC breakPcDefProc;
	static WNDPROC breakPcAddrDefProc;
	static WNDPROC breakWriteDefProc;
	static WNDPROC breakWriteAddrDefProc;
	static WNDPROC breakReadDefProc;
	static WNDPROC breakReadAddrDefProc;

	// Configure UI on pause/unpause
	static void updateUiOnPause();

public:
    // Break code - indicates that the emulation has been paused by the debugger,
    // and the type of the break
    enum class BreakCode {
        NONE,
        ENABLED_SRAM,
        DISABLED_SRAM,
        REACHED_ADDRESS,
        WROTE_TO_ADDRESS,
        READ_FROM_ADDRESS
    };
    BreakCode breakCode;

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
    DebugWindowModule() noexcept;
    void showWindow(HINSTANCE instance, HWND mainWindow, Gbc* gbcInstance);
    void setBreakCode(BreakCode code);
    void loadROMDetails();
    void loadMemoryDetails(int statusChanged);
	void loadBreakDetails();
	void decodeBreakPCAddress();
	void decodeBreakWriteAddress();
	void decodeBreakReadAddress();
};

#endif
