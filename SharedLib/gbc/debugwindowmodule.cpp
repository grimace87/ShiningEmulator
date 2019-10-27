#include "debugwindowmodule.h"

#ifdef _WIN32

#include "gbc.h"

#include <sstream>

DebugWindowModule debugger;

bool DebugWindowModule::classRegistered = false;
constexpr wchar_t debugWindowName[] = L"Shining Emulator - Debug Window";
constexpr wchar_t debugWindowClassName[] = L"ShDebug";

// Declare static pointer to emulator instance
Gbc* DebugWindowModule::gbc = nullptr;

// Declare private static class variables
HWND DebugWindowModule::debugWindow = nullptr;
HWND DebugWindowModule::debugStatusComponent = nullptr;
HWND DebugWindowModule::romTextComponent = nullptr;
HWND DebugWindowModule::debugTextComponent = nullptr;
HWND DebugWindowModule::staticTextComponent = nullptr;
HWND DebugWindowModule::pauseButtonComponent = nullptr;
HWND DebugWindowModule::refreshButtonComponent = nullptr;
HWND DebugWindowModule::dropdownBoxComponent = nullptr;
HWND DebugWindowModule::dropdownBankComponent = nullptr;
HWND DebugWindowModule::breakSramEnableComponent = nullptr;
HWND DebugWindowModule::breakSramDisableComponent = nullptr;
HWND DebugWindowModule::breakPcComponent = nullptr;
HWND DebugWindowModule::breakPcAddrComponent = nullptr;
HWND DebugWindowModule::breakWriteComponent = nullptr;
HWND DebugWindowModule::breakWriteAddrComponent = nullptr;
HWND DebugWindowModule::breakReadComponent = nullptr;
HWND DebugWindowModule::breakReadAddrComponent = nullptr;
HWND DebugWindowModule::generateStackTraceButtonComponent = nullptr;
WNDPROC DebugWindowModule::pauseButtonDefProc = nullptr;
WNDPROC DebugWindowModule::refreshButtonDefProc = nullptr;
WNDPROC DebugWindowModule::dropdownDefProc = nullptr;
WNDPROC DebugWindowModule::dropdownBankDefProc = nullptr;
WNDPROC DebugWindowModule::breakSramEnableDefProc = nullptr;
WNDPROC DebugWindowModule::breakSramDisableDefProc = nullptr;
WNDPROC DebugWindowModule::breakPcDefProc = nullptr;
WNDPROC DebugWindowModule::breakPcAddrDefProc = nullptr;
WNDPROC DebugWindowModule::breakWriteDefProc = nullptr;
WNDPROC DebugWindowModule::breakWriteAddrDefProc = nullptr;
WNDPROC DebugWindowModule::breakReadDefProc = nullptr;
WNDPROC DebugWindowModule::breakReadAddrDefProc = nullptr;
WNDPROC DebugWindowModule::generateStackTraceButtonDefProc = nullptr;

DebugWindowModule::DebugWindowModule() noexcept {
	// Reset public instance variables
    breakCode = BreakCode::NONE;
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

void DebugWindowModule::updateUiOnPause() {
    if (gbc->isPaused) {
        SendMessage(pauseButtonComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)L"Unpause Emulator");
        SendMessage(debugStatusComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)L"Emulation paused");
    } else {
        SendMessage(pauseButtonComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)L"Pause Emulator");
        SendMessage(debugStatusComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)L"Running");
    }
}

ATOM DebugWindowModule::registerWindowClass(HINSTANCE instance) {
	WNDCLASSW wcDebug = {};
	wcDebug.style = CS_OWNDC;
	wcDebug.lpfnWndProc = debugWndProc;
	wcDebug.cbClsExtra = 0;
	wcDebug.cbWndExtra = 0;
	wcDebug.hInstance = instance;
	wcDebug.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wcDebug.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcDebug.hbrBackground = (HBRUSH)COLOR_BACKGROUND;
	wcDebug.lpszMenuName = NULL;
	wcDebug.lpszClassName = debugWindowClassName;
	return RegisterClass(&wcDebug);
}

void DebugWindowModule::showWindow(HINSTANCE instance, HWND mainWindow, Gbc* gbcInstance) {

    if (debugWindow) {
        return;
    }

	// Connect GBC instance
	DebugWindowModule::gbc = gbcInstance;

	// Create window class if need be
	if (!classRegistered) {
		ATOM classAtom = registerWindowClass(instance);
		if (classAtom == 0) {
			return;
		}
		classRegistered = true;
	}

    // New window
    debugWindow = CreateWindow(
		debugWindowClassName, debugWindowName,
        WS_CAPTION | WS_VISIBLE | WS_POPUPWINDOW | WS_CHILD | WS_TABSTOP,
        5, 5, 638, 734,
        mainWindow, nullptr, instance, nullptr
    );
	
    // Status bar
    debugStatusComponent = CreateWindowEx(
        WS_EX_WINDOWEDGE | WS_EX_TRANSPARENT,
        L"STATIC", (LPCWSTR)nullptr,
        WS_DLGFRAME | WS_VISIBLE | WS_CHILD,
        10, 672, 610, 32,
        debugWindow, nullptr, instance, nullptr
    );

    // ROM details box
    romTextComponent = CreateWindowEx(
        WS_EX_WINDOWEDGE | WS_EX_TRANSPARENT,
        L"STATIC", (LPCWSTR)nullptr,
        WS_DLGFRAME | WS_VISIBLE | WS_CHILD,
        10, 10, 300, 146,
        debugWindow, nullptr, instance, nullptr
    );

    // Debug text ouput box
    debugTextComponent = CreateWindowEx(
        WS_EX_WINDOWEDGE | WS_EX_TRANSPARENT,
        L"STATIC", (LPCWSTR)nullptr,
        WS_DLGFRAME | WS_VISIBLE | WS_CHILD,
        320, 10, 300, 146,
        debugWindow, nullptr, instance, nullptr
    );
	
    // Pause button
    pauseButtonComponent = CreateWindow(
        L"BUTTON", L"Pause Emulator",
        WS_DLGFRAME | WS_VISIBLE | WS_CHILD,
        10, 166, 145, 32,
        debugWindow, nullptr, instance, nullptr
    );
    pauseButtonDefProc = (WNDPROC)SetWindowLongPtr(pauseButtonComponent, GWLP_WNDPROC, (LONG_PTR)pauseButtonWndProc);

    // Refresh button
    refreshButtonComponent = CreateWindow(
        L"BUTTON", L"Refresh Data",
        WS_DLGFRAME | WS_VISIBLE | WS_CHILD,
        165, 166, 145, 32,
        debugWindow, nullptr, instance, nullptr
    );
    refreshButtonDefProc = (WNDPROC)SetWindowLongPtr(refreshButtonComponent, GWLP_WNDPROC, (LONG_PTR)refreshButtonWndProc);
	
    // Memory range dropdown box
    dropdownBoxComponent = CreateWindow(
        L"COMBOBOX", (LPCWSTR)nullptr,
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_TABSTOP,
        320, 166, 145, 200,
        debugWindow, nullptr, instance, nullptr
    );
    dropdownDefProc = (WNDPROC)SetWindowLongPtr(dropdownBoxComponent, GWLP_WNDPROC, (LONG_PTR)dropdownWndProc);
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"ROM");
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"VRAM");
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"External RAM");
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"Working RAM");
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"Sprite table");
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"IO ports");
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"Hi RAM");
    SendMessage(dropdownBoxComponent, CB_ADDSTRING, 0, (LPARAM)L"Misc");
    SendMessage(dropdownBoxComponent, CB_SETCURSEL, 5, 0);
	
    // Memory bank dropdown box
    dropdownBankComponent = CreateWindow(
        L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_TABSTOP,
        475, 166, 145, 200,
        debugWindow, nullptr, instance, nullptr
    );
    dropdownBankDefProc = (WNDPROC)SetWindowLongPtr(dropdownBankComponent, GWLP_WNDPROC, (LONG_PTR)dropdownBankWndProc);
    SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 0");
    SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);

    // Text window
    staticTextComponent = CreateWindowEx(
        WS_EX_WINDOWEDGE | WS_EX_TRANSPARENT,
        L"EDIT", L"OK",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_TABSTOP | ES_READONLY | ES_AUTOVSCROLL | ES_MULTILINE,
        10, 208, 610, 292,
        debugWindow, nullptr, instance, nullptr
    );
    HFONT hf = (HFONT)GetStockObject(OEM_FIXED_FONT);
    SendMessage(staticTextComponent, WM_SETFONT, (WPARAM)hf, 0);
    EnableScrollBar(staticTextComponent, SB_HORZ, ESB_ENABLE_BOTH);

    // Breakpoint type checkboxes
    breakSramEnableComponent = CreateWindow(
        L"BUTTON", L"Break on SRAM enable",
        BS_CHECKBOX | WS_VISIBLE | WS_CHILD,
        10, 510, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakSramEnableDefProc = (WNDPROC)SetWindowLongPtr(breakSramEnableComponent, GWLP_WNDPROC, (LONG_PTR)checkBoxWndProc);

    breakSramDisableComponent = CreateWindow(
        L"BUTTON", L"Break on SRAM disable",
        BS_CHECKBOX | WS_VISIBLE | WS_CHILD,
        215, 510, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakSramDisableDefProc = (WNDPROC)SetWindowLongPtr(breakSramDisableComponent, GWLP_WNDPROC, (LONG_PTR)checkBoxWndProc);

    breakPcComponent = CreateWindow(
        L"BUTTON", L"Break on PC address",
        BS_CHECKBOX | WS_VISIBLE | WS_CHILD,
        10, 540, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakPcDefProc = (WNDPROC)SetWindowLongPtr(breakPcComponent, GWLP_WNDPROC, (LONG_PTR)checkBoxWndProc);

    breakWriteComponent = CreateWindow(
        L"BUTTON", L"Break on write to address",
        BS_CHECKBOX | WS_VISIBLE | WS_CHILD,
        10, 570, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakWriteDefProc = (WNDPROC)SetWindowLongPtr(breakWriteComponent, GWLP_WNDPROC, (LONG_PTR)checkBoxWndProc);

    breakReadComponent = CreateWindow(
        L"BUTTON", L"Break on read from address",
        BS_CHECKBOX | WS_VISIBLE | WS_CHILD,
        10, 600, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakReadDefProc = (WNDPROC)SetWindowLongPtr(breakReadComponent, GWLP_WNDPROC, (LONG_PTR)checkBoxWndProc);

    // The address typing field for PC breaks
    breakPcAddrComponent = CreateWindow(
        L"EDIT", L"0100",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_LOWERCASE | ES_READONLY,
        215, 540, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakPcAddrDefProc = (WNDPROC)SetWindowLongPtr(breakPcAddrComponent, GWLP_WNDPROC, (LONG_PTR)breakPcAddrWndProc);

    // The address typing field for write breaks
    breakWriteAddrComponent = CreateWindow(
        L"EDIT", L"0000",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_LOWERCASE | ES_READONLY,
        215, 570, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakWriteAddrDefProc = (WNDPROC)SetWindowLongPtr(breakWriteAddrComponent, GWLP_WNDPROC, (LONG_PTR)breakWriteAddrWndProc);

    // The address typing field for read breaks
    breakReadAddrComponent = CreateWindow(
        L"EDIT", L"0000",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_LOWERCASE | ES_READONLY,
        215, 600, 195, 20,
        debugWindow, nullptr, instance, nullptr
    );
    breakReadAddrDefProc = (WNDPROC)SetWindowLongPtr(breakReadAddrComponent, GWLP_WNDPROC, (LONG_PTR)breakReadAddrWndProc);

    generateStackTraceButtonComponent = CreateWindow(
        L"BUTTON", L"Generate stack trace file",
        WS_DLGFRAME | WS_VISIBLE | WS_CHILD,
        10, 630, 400, 32,
        debugWindow, nullptr, instance, nullptr
    );
    generateStackTraceButtonDefProc = (WNDPROC)SetWindowLongPtr(generateStackTraceButtonComponent, GWLP_WNDPROC, (LONG_PTR)generateStackTraceButtonWndProc);

    // Fill data in text boxes
    loadROMDetails();
    loadMemoryDetails(1);
    SendMessage(debugStatusComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)L"Running");
    SendMessage(debugTextComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)L"Awaiting breakpoint for info display");
}

void DebugWindowModule::setBreakCode(BreakCode code) {
    if (breakCode == code) {
        return;
    }

    breakCode = code;
    gbc->isPaused = breakCode != BreakCode::NONE;
    updateUiOnPause();
}

bool DebugWindowModule::breakCodeIsSet() {
    return breakCode != BreakCode::NONE;
}

/******************
 * Load ROM details
 * into debug window
 ******************/

void DebugWindowModule::loadROMDetails() {

	if (debugWindow == nullptr) {
		return;
	}

	std::stringstream text;
	text << "Internal name: " << gbc->romProperties.title << "\n";
	text << "ROM size: ";
	if (gbc->romProperties.sizeBytes >= 1048576) text << (gbc->romProperties.sizeBytes / 1048576) << " MB\n";
	else text << (gbc->romProperties.sizeBytes / 1024) << " KB\n";
	text << "Internal RAM size: ";
	if (gbc->sram.sizeBytes >= 1024) text << (gbc->sram.sizeBytes / 1024) << " KB\n";
	else text << gbc->sram.sizeBytes << " bytes\n";
	text << "ROM type: ";
	if (gbc->romProperties.cgbFlag) text << "Color game\n";
	else if (gbc->romProperties.sgbFlag) text << "Super game\n";
	else text << "Standard game\n";
	text << "Has battery backup: ";
	if (gbc->sram.hasBattery) text << "Yes\n";
	else text << "No\n";
	text << "Has internal timer: ";
	if (gbc->sram.hasTimer) text << "Yes\n";
	else text << "No\n";
	text << "Memory controller type: ";
	if (gbc->romProperties.mbc == MBC_NONE) text << "None\n";
	else if (gbc->romProperties.mbc == MBC1) text << "MBC1\n";
	else if (gbc->romProperties.mbc == MBC2) text << "MBC2\n";
	else if (gbc->romProperties.mbc == MBC3) text << "MBC3\n";
	else if (gbc->romProperties.mbc == MBC5) text << "MBC5\n";
	else text << "Unknown\n";
	std::string fullString = text.str();
	SendMessageA(romTextComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)fullString.data());
}

/******************
 * Load memory details
 * into debug window
 ******************/

void DebugWindowModule::loadMemoryDetails(int statusChanged) {

	if (debugWindow == nullptr) {
		return;
	}

	// Pass StatusChanged = 1 when the game changed, of the memory range changed

	int Item = SendMessage((HWND)dropdownBoxComponent, CB_GETCURSEL, 0, 0);
	int Bank;

	std::stringstream text;
	std::string fullOutput;

	int col;
	int line;

	int Insert;
	int Size;

	switch (Item) {
	case 0:
		if (statusChanged) {
			SendMessage(dropdownBankComponent, CB_RESETCONTENT, 0, 0);
			Size = (unsigned int)gbc->romProperties.bankSelectMask + 1;
			if (Size == 1) Size = 2;
			for (line = 0; line < Size; line++) {
				text << "Bank ";
				text << line;
				fullOutput = text.str();
				SendMessageA(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)fullOutput.data());
				text.str(std::string());
			}
			EnableWindow(dropdownBankComponent, TRUE);
			SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
		}
		Bank = SendMessage(dropdownBankComponent, CB_GETCURSEL, 0, 0);
		if (Bank == 0) text << "0x0000 ";
		else text << "0x4000 ";
		for (line = 0; line < 1024; line++) {
			for (col = 0; col < 16; col++) {
				Insert = (int)(gbc->rom[0x4000 * Bank + line * 16 + col]);
				text << Insert;
				if (Insert < 10) text << "   ";
				else if (Insert < 100) text << "  ";
				else text << " ";
			}
			if ((line % 4) == 3) {
				line++;
				if (line == 1024) break;
				if (Bank == 0)
					text << "\r\n0x" << (line / 256);
				else
					text << "\r\n0x" << (line / 256) + 4;
				Insert = (line & 0x00f0) / 16;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				Insert = line & 0x000f;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				text << "0 ";
				line--;
			}
			else
				text << "\r\n       ";
		}
		break;
	case 1:
		if (statusChanged) {
			if (gbc->romProperties.cgbFlag) {
				EnableWindow(dropdownBankComponent, TRUE);
				SendMessage(dropdownBankComponent, CB_RESETCONTENT, 0, 0);
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 0");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 1");
				SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			} else {
				SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
				EnableWindow(dropdownBankComponent, FALSE);
			}
		}
		text << "0x8000 ";
		Bank = SendMessage(dropdownBankComponent, CB_GETCURSEL, 0, 0);
		for (line = 0; line < 512; line++) {
			for (col = 0; col < 16; col++) {
				Insert = (int)(gbc->vram[0x2000 * Bank + line * 16 + col]);
				text << Insert;
				if (Insert < 10) text << "   ";
				else if (Insert < 100) text << "  ";
				else text << " ";
			}
			if ((line % 4) == 3) {
				line++;
				if (line == 512) break;
				if (line < 256)
					text << "\r\n0x8";
				else
					text << "\r\n0x9";
				Insert = (line & 0x00f0) / 16;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				Insert = line & 0x000f;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				text << "0 ";
				line--;
			}
			else
				text << "\r\n       ";
		}
		break;
	case 2:
		if (statusChanged) {
			if ((gbc->romProperties.hasSram) && (gbc->sram.sizeBytes > 8192)) {
				EnableWindow(dropdownBankComponent, TRUE);
				SendMessage(dropdownBankComponent, CB_RESETCONTENT, 0, 0);
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 0");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 1");
				if (gbc->sram.sizeBytes > 16384) {
					SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 2");
					SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 3");
				}
				SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			}
			else {
				SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
				EnableWindow(dropdownBankComponent, FALSE);
			}
		}
		if (gbc->sram.sizeBytes == 0)
			text << "No external RAM exists.";
		else {
			Size = gbc->sram.sizeBytes;
			text << "0xa000 ";
			if (Size <= 8192) {
				for (line = 0; line < Size / 16; line++) {
					for (col = 0; col < 16; col++) {
						Insert = (int)(gbc->sram.data[line * 16 + col]);
						text << Insert;
						if (Insert < 10) text << "   ";
						else if (Insert < 100) text << "  ";
						else text << " ";
					}
					if ((line % 4) == 3) {
						line++;
						if ((line * 16) == gbc->sram.sizeBytes) break;
						if (line == 512) break;
						if (line < 256)
							text << "\r\n0xa";
						else
							text << "\r\n0xb";
						Insert = (line & 0x00f0) / 16;
						if (Insert < 10)
							text << Insert;
						else
							text << (char)(Insert + 87);
						Insert = line & 0x000f;
						if (Insert < 10)
							text << Insert;
						else
							text << (char)(Insert + 87);
						text << "0 ";
						line--;
					}
					else
						text << "\r\n       ";
				}
			}
			else {
				Bank = SendMessage(dropdownBankComponent, CB_GETCURSEL, 0, 0);
				for (line = 0; line < 512; line++) {
					for (col = 0; col < 16; col++) {
						Insert = (int)(gbc->sram.data[0x1000 * Bank + line * 16 + col]);
						text << Insert;
						if (Insert < 10) text << "   ";
						else if (Insert < 100) text << "  ";
						else text << " ";
					}
					if ((line % 4) == 3) {
						line++;
						if (line == 512) break;
						if (line < 256)
							text << "\r\n0xa";
						else
							text << "\r\n0xb";
						Insert = (line & 0x00f0) / 16;
						if (Insert < 10)
							text << Insert;
						else
							text << (char)(Insert + 87);
						Insert = line & 0x000f;
						if (Insert < 10)
							text << Insert;
						else
							text << (char)(Insert + 87);
						text << "0 ";
						line--;
					}
					else
						text << "\r\n       ";
				}
			}
		}
		break;
	case 3:
		if (statusChanged) {
			EnableWindow(dropdownBankComponent, TRUE);
			if (gbc->romProperties.cgbFlag) {
				SendMessage(dropdownBankComponent, CB_RESETCONTENT, 0, 0);
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 0");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 1");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 2");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 3");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 4");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 5");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 6");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 7");
				SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			} else {
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 0");
				SendMessage(dropdownBankComponent, CB_ADDSTRING, 0, (LPARAM)L"Bank 1");
				SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			}
		}
		Bank = SendMessage(dropdownBankComponent, CB_GETCURSEL, 0, 0);
		if (Bank == 0) text << "0xc000 ";
		else text << "0xd000 ";
		for (line = 0; line < 256; line++) {
			for (col = 0; col < 16; col++) {
				Insert = (int)(gbc->sram.data[0x1000 * Bank + line * 16 + col]);
				text << Insert;
				if (Insert < 10) text << "   ";
				else if (Insert < 100) text << "  ";
				else text << " ";
			}
			if ((line % 4) == 3) {
				line++;
				if (line == 256) break;
				if (Bank == 0) text << "\r\n0xc";
				else text << "\r\n0xd";
				Insert = (line & 0x00f0) / 16;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				Insert = line & 0x000f;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				text << "0 ";
				line--;
			}
			else
				text << "\r\n       ";
		}
		break;
	case 4:
		if (statusChanged) {
			SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			EnableWindow(dropdownBankComponent, FALSE);
		}
		text << "0xe000 ";
		for (line = 0; line < 10; line++) {
			for (col = 0; col < 16; col++) {
				Insert = (int)(gbc->oam[line * 16 + col]);
				text << Insert;
				if (Insert < 10) text << "   ";
				else if (Insert < 100) text << "  ";
				else text << " ";
			}
			if ((line % 4) == 3) {
				line++;
				if (line == 10) break;
				text << "\r\n0xe";
				Insert = (line & 0x00f0) / 16;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				Insert = line & 0x000f;
				if (Insert < 10)
					text << Insert;
				else
					text << (char)(Insert + 87);
				text << "0 ";
				line--;
			}
			else
				text << "\r\n       ";
		}
		break;
	case 5:
		if (statusChanged) {
			SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			EnableWindow(dropdownBankComponent, FALSE);
		}
		text << "0xff00 ";
		for (line = 0; line < 16; line++) {
			for (col = 0; col < 8; col++) {
				Insert = (int)(gbc->ioPorts[line * 8 + col]);
				text << Insert;
				if (Insert < 10) text << "   ";
				else if (Insert < 100) text << "  ";
				else text << " ";
			}
			if (line == 15) break;
			if (line == 3)
				text << "\r\n0xff20 ";
			else if (line == 7)
				text << "\r\n0xff40 ";
			else if (line == 11)
				text << "\r\n0xff60 ";
			else
				text << "\r\n       ";
		}
		break;
	case 6:
		if (statusChanged) {
			SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			EnableWindow(dropdownBankComponent, FALSE);
		}
		text << "0xff80 ";
		for (line = 0; line < 16; line++) {
			for (col = 0; col < 8; col++) {
				if (line + col == 22) break;
				Insert = (int)(gbc->ioPorts[0x80 + line * 8 + col]);
				text << Insert;
				if (Insert < 10) text << "   ";
				else if (Insert < 100) text << "  ";
				else text << " ";
			}
			if (line == 15) break;
			if (line == 3)
				text << "\r\n0xffa0 ";
			else if (line == 7)
				text << "\r\n0xffc0 ";
			else if (line == 11)
				text << "\r\n0xffe0 ";
			else
				text << "\r\n       ";
		}
		text << (int)(gbc->ioPorts[0xff]);
		break;
	case 7:
		if (statusChanged) {
			SendMessage(dropdownBankComponent, CB_SETCURSEL, 0, 0);
			EnableWindow(dropdownBankComponent, FALSE);
		}
		text << "CPU regs AFBCDEHL ";
		Insert = (int)(gbc->cpuA); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		Insert = (int)(gbc->cpuF); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		Insert = (int)(gbc->cpuB); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		Insert = (int)(gbc->cpuC); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		Insert = (int)(gbc->cpuD); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		Insert = (int)(gbc->cpuE); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		Insert = (int)(gbc->cpuH); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		Insert = (int)(gbc->cpuL); text << Insert;
		if (Insert < 10) text << "   "; else if (Insert < 100) text << "  "; else text << " ";
		text << "\r\nPC " << gbc->cpuPc;
		text << "\r\nSP " << gbc->cpuSp;
		text << "\r\nROM bank " << (gbc->bankOffset / 0x4000U);
		text << "\r\nInterrupt master enable: " << gbc->cpuIme;
		text << "\r\n\r\nBG color palettes (8x4xRGB):\r\n";
		for (line = 0; line < 8; line++) {
			text << " " << line << ": ";
			text << gbc->cgbBgPalette[line * 4 + 0] << ", ";
			text << gbc->cgbBgPalette[line * 4 + 1] << ", ";
			text << gbc->cgbBgPalette[line * 4 + 2] << ", ";
			text << gbc->cgbBgPalette[line * 4 + 3] << "\r\n";
		}
		text << "Sprite color palettes (8x4xRGB):\r\n";
		for (line = 0; line < 8; line++) {
			text << " " << line << ": ";
			text << gbc->cgbObjPalette[line * 4 + 0] << ", ";
			text << gbc->cgbObjPalette[line * 4 + 1] << ", ";
			text << gbc->cgbObjPalette[line * 4 + 2] << ", ";
			text << gbc->cgbObjPalette[line * 4 + 3] << "\r\n";
		}
		break;
	}

	fullOutput = text.str();
	SetWindowTextA(staticTextComponent, (LPCSTR)fullOutput.data());
}

void DebugWindowModule::loadBreakDetails() {

	if (debugWindow == nullptr) {
		return;
	}

	char BreakPointText[256];
	char Spare;

	strcpy_s(BreakPointText, "At last breakpoint:\n                        \nPC=0x    \nSP=0x    \nROM bank   \nSRAM bank  \nLast call was to 0x     at 0x    \n");
	strcpy_s(&BreakPointText[20], 24, breakMsg);
	BreakPointText[44] = '\n';
	// Write PC
	Spare = (char)((gbc->cpuPc & 0xf000) / 0x1000); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[50] = Spare;
	Spare = (char)((gbc->cpuPc & 0xf00) / 0x100); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[51] = Spare;
	Spare = (char)((gbc->cpuPc & 0xf0) / 0x10); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[52] = Spare;
	Spare = (char)((gbc->cpuPc & 0xf) / 0x1); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[53] = Spare;
	// Write SP
	Spare = (char)((gbc->cpuSp & 0xf000) / 0x1000); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[60] = Spare;
	Spare = (char)((gbc->cpuSp & 0xf00) / 0x100); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[61] = Spare;
	Spare = (char)((gbc->cpuSp & 0xf0) / 0x10); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[62] = Spare;
	Spare = (char)((gbc->cpuSp & 0xf) / 0x1); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[63] = Spare;
	// Write ROM bank
	Spare = (char)((gbc->bankOffset / 0x4000) / 10); Spare += 0x30; BreakPointText[74] = Spare;
	Spare = (char)((gbc->bankOffset / 0x4000) % 10); Spare += 0x30; BreakPointText[75] = Spare;
	// Write SRAM bank
	Spare = (char)((gbc->sram.bankOffset / 0x1000)); Spare += 0x30; BreakPointText[87] = Spare;
	// Address of most recently called function
	Spare = (char)((breakLastCallTo & 0xf000) / 0x1000); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[108] = Spare;
	Spare = (char)((breakLastCallTo & 0xf00) / 0x100); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[109] = Spare;
	Spare = (char)((breakLastCallTo & 0xf0) / 0x10); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[110] = Spare;
	Spare = (char)((breakLastCallTo & 0xf) / 0x1); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[111] = Spare;
	// Address of most recent call to a function
	Spare = (char)((breakLastCallAt & 0xf000) / 0x1000); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[118] = Spare;
	Spare = (char)((breakLastCallAt & 0xf00) / 0x100); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[119] = Spare;
	Spare = (char)((breakLastCallAt & 0xf0) / 0x10); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[120] = Spare;
	Spare = (char)((breakLastCallAt & 0xf) / 0x1); if (Spare < 0xa) Spare += 0x30; else Spare += 0x57; BreakPointText[121] = Spare;
	// Whether most recent function call returned
	if (breakLastCallReturned) strcpy_s(&BreakPointText[123], 13, "No ret since");
	else strcpy_s(&BreakPointText[123], 15, "Since returned");
	// Show this message in the static text box
	SendMessage(debugTextComponent, WM_SETTEXT, (WPARAM)NULL, (LPARAM)BreakPointText);

}

void DebugWindowModule::decodeBreakPCAddress() {

	if (debugWindow == nullptr) {
		return;
	}

	TCHAR GetText[5];
	((WORD*)GetText)[0] = 5;
	SendMessage(breakPcAddrComponent, EM_GETLINE, (WPARAM)0, (LPARAM)GetText);

	unsigned int GetInts[4];
	if (GetText[0] < 0x40) GetInts[0] = (unsigned int)(GetText[0] - 0x30) * 0x1000; else GetInts[0] = (unsigned int)(GetText[0] - 0x57) * 0x1000;
	if (GetText[1] < 0x40) GetInts[1] = (unsigned int)(GetText[1] - 0x30) * 0x100; else GetInts[1] = (unsigned int)(GetText[1] - 0x57) * 0x100;
	if (GetText[2] < 0x40) GetInts[2] = (unsigned int)(GetText[2] - 0x30) * 0x10; else GetInts[2] = (unsigned int)(GetText[2] - 0x57) * 0x10;
	if (GetText[3] < 0x40) GetInts[3] = (unsigned int)(GetText[3] - 0x30) * 0x1; else GetInts[3] = (unsigned int)(GetText[3] - 0x57) * 0x1;
	breakPcAddr = GetInts[0] + GetInts[1] + GetInts[2] + GetInts[3];

}

void DebugWindowModule::decodeBreakWriteAddress() {

	if (debugWindow == nullptr) {
		return;
	}

	TCHAR GetText[5];
	((WORD*)GetText)[0] = 5;
	SendMessage(breakWriteAddrComponent, EM_GETLINE, (WPARAM)0, (LPARAM)GetText);

	unsigned int GetInts[4];
	if (GetText[0] < 0x40) GetInts[0] = (unsigned int)(GetText[0] - 0x30) * 0x1000; else GetInts[0] = (unsigned int)(GetText[0] - 0x57) * 0x1000;
	if (GetText[1] < 0x40) GetInts[1] = (unsigned int)(GetText[1] - 0x30) * 0x100; else GetInts[1] = (unsigned int)(GetText[1] - 0x57) * 0x100;
	if (GetText[2] < 0x40) GetInts[2] = (unsigned int)(GetText[2] - 0x30) * 0x10; else GetInts[2] = (unsigned int)(GetText[2] - 0x57) * 0x10;
	if (GetText[3] < 0x40) GetInts[3] = (unsigned int)(GetText[3] - 0x30) * 0x1; else GetInts[3] = (unsigned int)(GetText[3] - 0x57) * 0x1;
	breakWriteAddr = GetInts[0] + GetInts[1] + GetInts[2] + GetInts[3];

}

void DebugWindowModule::decodeBreakReadAddress() {

	if (debugWindow == nullptr) {
		return;
	}

	TCHAR GetText[5];
	((WORD*)GetText)[0] = 5;
	SendMessage(breakReadAddrComponent, EM_GETLINE, (WPARAM)0, (LPARAM)GetText);

	unsigned int GetInts[4];
	if (GetText[0] < 0x40) GetInts[0] = (unsigned int)(GetText[0] - 0x30) * 0x1000; else GetInts[0] = (unsigned int)(GetText[0] - 0x57) * 0x1000;
	if (GetText[1] < 0x40) GetInts[1] = (unsigned int)(GetText[1] - 0x30) * 0x100; else GetInts[1] = (unsigned int)(GetText[1] - 0x57) * 0x100;
	if (GetText[2] < 0x40) GetInts[2] = (unsigned int)(GetText[2] - 0x30) * 0x10; else GetInts[2] = (unsigned int)(GetText[2] - 0x57) * 0x10;
	if (GetText[3] < 0x40) GetInts[3] = (unsigned int)(GetText[3] - 0x30) * 0x1; else GetInts[3] = (unsigned int)(GetText[3] - 0x57) * 0x1;
	breakReadAddr = GetInts[0] + GetInts[1] + GetInts[2] + GetInts[3];

}

////////////////////////////
// WndProc overrides
////////////////////////////

LRESULT APIENTRY DebugWindowModule::debugWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_DESTROY) {
        DebugWindowModule::debugWindow = nullptr;
        return 0;
    } else {
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}

LRESULT APIENTRY DebugWindowModule::pauseButtonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_LBUTTONDOWN) {
        BreakCode code = gbc->isPaused ? BreakCode::NONE : BreakCode::MANUAL_PAUSE;
        debugger.setBreakCode(code);
	}
	return CallWindowProc(pauseButtonDefProc, pauseButtonComponent, message, wParam, lParam);
}

LRESULT APIENTRY DebugWindowModule::refreshButtonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_LBUTTONDOWN) {
		debugger.loadMemoryDetails(0);
	}
	return CallWindowProc(refreshButtonDefProc, refreshButtonComponent, message, wParam, lParam);
}

LRESULT APIENTRY DebugWindowModule::generateStackTraceButtonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_LBUTTONDOWN) {
        if (gbc->isRunning || gbc->isPaused) {
            debugger.writeStackTraceFile();
        } else {
            MessageBoxW(hWnd, L"Game must be running or paused to generate a stack trace", L"Action failed", MB_OK);
        }
        BreakCode code = gbc->isPaused ? BreakCode::NONE : BreakCode::MANUAL_PAUSE;
    }
    return CallWindowProc(generateStackTraceButtonDefProc, generateStackTraceButtonComponent, message, wParam, lParam);
}

LRESULT APIENTRY DebugWindowModule::dropdownWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_COMMAND) {
		if (HIWORD(wParam) == CBN_SELCHANGE) {
            debugger.loadMemoryDetails(1);
        }
	}
	return CallWindowProc(dropdownDefProc, dropdownBoxComponent, message, wParam, lParam);
}

LRESULT APIENTRY DebugWindowModule::dropdownBankWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_COMMAND) {
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            debugger.loadMemoryDetails(0);
        }
	}
	return CallWindowProc(dropdownBankDefProc, dropdownBankComponent, message, wParam, lParam);
}

LRESULT APIENTRY DebugWindowModule::checkBoxWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (hWnd == breakSramEnableComponent) {
		if (message == WM_LBUTTONDOWN) {
			if (SendMessage(hWnd, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED) {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_UNCHECKED, (LPARAM)0);
				debugger.breakOnSramEnable = 0;
			} else {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);
				debugger.breakOnSramEnable = 1;
			}
			debugger.totalBreakEnables = debugger.breakOnSramEnable + debugger.breakOnSramDisable + debugger.breakOnPc + debugger.breakOnWrite + debugger.breakOnRead;
			return 0;
		} else {
            return CallWindowProc(breakSramEnableDefProc, hWnd, message, wParam, lParam);
        }
	} else if (hWnd == breakSramDisableComponent) {
		if (message == WM_LBUTTONDOWN) {
			if (SendMessage(hWnd, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED) {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_UNCHECKED, (LPARAM)0);
				debugger.breakOnSramDisable = 0;
			}
			else {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);
				debugger.breakOnSramDisable = 1;
			}
			debugger.totalBreakEnables = debugger.breakOnSramEnable + debugger.breakOnSramDisable + debugger.breakOnPc + debugger.breakOnWrite + debugger.breakOnRead;
            return 0;
		} else {
            return CallWindowProc(breakSramEnableDefProc, hWnd, message, wParam, lParam);
        }
	} else if (hWnd == breakPcComponent) {
		if (message == WM_LBUTTONDOWN) {
			if (SendMessage(hWnd, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED) {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_UNCHECKED, (LPARAM)0);
				debugger.breakOnPc = 0;
			} else {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);
				debugger.breakOnPc = 1;
			}
			debugger.totalBreakEnables = debugger.breakOnSramEnable + debugger.breakOnSramDisable + debugger.breakOnPc + debugger.breakOnWrite + debugger.breakOnRead;
            return 0;
		} else {
            return CallWindowProc(breakPcDefProc, hWnd, message, wParam, lParam);
        }
	} else if (hWnd == breakWriteComponent) {
		if (message == WM_LBUTTONDOWN) {
			if (SendMessage(hWnd, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED) {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_UNCHECKED, (LPARAM)0);
				debugger.breakOnWrite = 0;
			} else {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);
				debugger.breakOnWrite = 1;
			}
			debugger.totalBreakEnables = debugger.breakOnSramEnable + debugger.breakOnSramDisable + debugger.breakOnPc + debugger.breakOnWrite + debugger.breakOnRead;
            return 0;
		} else {
            return CallWindowProc(breakWriteDefProc, hWnd, message, wParam, lParam);
        }
	} else if (hWnd == breakReadComponent) {
		if (message == WM_LBUTTONDOWN) {
			if (SendMessage(hWnd, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED) {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_UNCHECKED, (LPARAM)0);
				debugger.breakOnRead = 0;
			}
			else {
				SendMessage(hWnd, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);
				debugger.breakOnRead = 1;
			}
			debugger.totalBreakEnables = debugger.breakOnSramEnable + debugger.breakOnSramDisable + debugger.breakOnPc + debugger.breakOnWrite + debugger.breakOnRead;
            return 0;
		} else {
            return CallWindowProc(breakReadDefProc, hWnd, message, wParam, lParam);
        }
	}

	// Unknown window handle, should never be reached
    return 0;
}

LRESULT APIENTRY DebugWindowModule::breakPcAddrWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

	wchar_t replacementText[2];
	DWORD dwStartPos, dwEndPos;

	// Capture keys pressed
	if (message == WM_KEYDOWN) {
		if ((wParam >= 0x30 && wParam <= 0x39) || (wParam >= 0x41 && wParam <= 0x46)) {
			SendMessage(hWnd, EM_GETSEL, (WPARAM)& dwStartPos, (LPARAM)& dwEndPos);
			if ((dwStartPos == dwEndPos) && (dwStartPos < 4)) {
                replacementText[0] = (wchar_t)wParam;
                replacementText[1] = L'\0';
				SendMessage(hWnd, EM_SETSEL, (WPARAM)dwStartPos, (LPARAM)(dwStartPos + 1));
				SendMessage(hWnd, EM_REPLACESEL, FALSE, (LPARAM)replacementText);
				debugger.decodeBreakPCAddress();
			}
		}
	}

	return CallWindowProc(breakPcAddrDefProc, hWnd, message, wParam, lParam);
}

LRESULT APIENTRY DebugWindowModule::breakWriteAddrWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

	wchar_t replacementText[2];
	DWORD dwStartPos, dwEndPos;

	// Capture keys pressed
	if (message == WM_KEYDOWN) {
		if ((wParam >= 0x30 && wParam <= 0x39) || (wParam >= 0x41 && wParam <= 0x46)) {
			SendMessage(hWnd, EM_GETSEL, (WPARAM)& dwStartPos, (LPARAM)& dwEndPos);
			if ((dwStartPos == dwEndPos) && (dwStartPos < 4)) {
                replacementText[0] = (wchar_t)wParam;
                replacementText[1] = L'\0';
				SendMessage(hWnd, EM_SETSEL, (WPARAM)dwStartPos, (LPARAM)(dwStartPos + 1));
				SendMessage(hWnd, EM_REPLACESEL, FALSE, (LPARAM)replacementText);
				debugger.decodeBreakWriteAddress();
			}
		}
	}

	return CallWindowProc(breakWriteAddrDefProc, hWnd, message, wParam, lParam);
}

LRESULT APIENTRY DebugWindowModule::breakReadAddrWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

	wchar_t replacementText[2];
	DWORD dwStartPos, dwEndPos;

	// Capture keys pressed
	if (message == WM_KEYDOWN) {
		if ((wParam >= 0x30 && wParam <= 0x39) || (wParam >= 0x41 && wParam <= 0x46)) {
			SendMessage(hWnd, EM_GETSEL, (WPARAM)& dwStartPos, (LPARAM)& dwEndPos);
			if ((dwStartPos == dwEndPos) && (dwStartPos < 4)) {
                replacementText[0] = (wchar_t)wParam;
                replacementText[1] = L'\0';
				SendMessage(hWnd, EM_SETSEL, (WPARAM)dwStartPos, (LPARAM)(dwStartPos + 1));
				SendMessage(hWnd, EM_REPLACESEL, FALSE, (LPARAM)replacementText);
				debugger.decodeBreakReadAddress();
			}
		}
	}

	return CallWindowProc(breakReadAddrDefProc, hWnd, message, wParam, lParam);
}

void DebugWindowModule::writeStackTraceFile() {
    FILE* outputFile = fopen("stacktrace.txt", "w");
    if (outputFile) {
        debugger.utils.writeTraceFile(gbc, outputFile);
        fclose(outputFile);
    }
}

#endif
