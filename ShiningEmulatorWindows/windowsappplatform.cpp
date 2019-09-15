#include "windowsappplatform.h"

#include "windowsresource.h"
#include "windowsfilehelper.h"
#include "../SharedLib/menu.h"

#include <Xinput.h>
#include <shtypes.h>
#include <ShObjIdl_core.h>

#include "../SharedLib/gbc/debugwindowmodule.h"

WindowsAppPlatform::WindowsAppPlatform(HINSTANCE hInstance, HWND hWnd) : hInstance(hInstance), hWnd(hWnd), mainMenu(Menu::buildMain()) {
	this->usesTouch = false;
}

bool WindowsAppPlatform::onAppThreadStarted(App* app) {
    HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    return result == S_OK;
}

Resource* WindowsAppPlatform::getResource(const char* fileName, bool isAsset, bool isGlShader) {
    return new WindowsResource(fileName, isAsset);
}

Resource* WindowsAppPlatform::chooseFile(std::string fileTypeDescr, std::vector<std::string> fileTypes) {
    // Convert file type description to wide characters
    auto wideStringDescr = new wchar_t[128];
    size_t charsConverted;
    mbstowcs_s(&charsConverted, wideStringDescr, 128, fileTypeDescr.c_str(), fileTypeDescr.size());

    // Form colon-separated file type list as wide character string
    auto wideStringTypes = new wchar_t[128];
    size_t index = 0;
    for (auto& str : fileTypes) {
        wideStringTypes[index] = L'*';
        mbstowcs_s(&charsConverted, wideStringTypes + index + 1, 128 - index, str.c_str(), str.size());
        wideStringTypes[index + 1 + str.size()] = L';';
        wideStringTypes[index + 1 + str.size() + 1] = L'\0';
        index += str.size() + 2;
    }
    if (index > 0) {
        wideStringTypes[index - 1] = L'\0';
    }

    // Form the filter spec
    COMDLG_FILTERSPEC fileSpecs[] =
            {
                    { wideStringDescr, wideStringTypes }
            };

    // Create a file dialog instance
    IFileDialog* dialog = NULL;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(result)) {
        // Create the event handler and assign it to the dialog
        WindowsFileHelper eventHelper;
        DWORD dwCookie;
        result = dialog->Advise(&eventHelper, &dwCookie);
        if (SUCCEEDED(result)) {
            // Get existing options
            DWORD dwFlags;
            result = dialog->GetOptions(&dwFlags);
            if (SUCCEEDED(result)) {
                // Set options, to use only filesystem items
                result = dialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
                if (SUCCEEDED(result)) {
                    // Set allowable file types
                    result = dialog->SetFileTypes(ARRAYSIZE(fileSpecs), fileSpecs);
                    if (SUCCEEDED(result)) {
                        // Set selected file type index (note this is a 1-based index)
                        result = dialog->SetFileTypeIndex(1);
                        if (SUCCEEDED(result)) {
                            // Show the dialog
                            result = dialog->Show(NULL);
                            if (SUCCEEDED(result)) {
                                // Get the result from the user pressing the Open button
                                IShellItem* fileItem;
                                result = dialog->GetResult(&fileItem);
                                if (SUCCEEDED(result)) {
                                    // Get file name and return it
                                    PWSTR filePath = NULL;
                                    result = fileItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
                                    if (SUCCEEDED(result)) {
                                        const size_t maxSize = 512;
                                        char mbString[maxSize];
                                        ZeroMemory(mbString, maxSize);
                                        size_t converted;
                                        wcstombs_s(&converted, mbString, maxSize, filePath, maxSize);
                                        if (converted < maxSize) {
                                            dialog->Unadvise(dwCookie);
                                            return getResource(mbString, false, false);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        dialog->Unadvise(dwCookie);
    }

    return nullptr;
}


FILE* WindowsAppPlatform::openFileInAppDir(std::string fileName, const char* mode) {
    // Get path and filename of running app
    char path[512];
    int bytesCopied = GetModuleFileNameA(NULL, (LPSTR)&path, 512);
    if (bytesCopied == 0) {
        return nullptr;
    }
    std::string pathOfRunningApp = path;

    // Get directory of the running app (including the final slash at the end)
    size_t lastSlash = pathOfRunningApp.find_last_of("/\\");
    if (lastSlash == std::string::npos) {
        return nullptr;
    }
    std::string runningAppDirectory = pathOfRunningApp.substr(0, lastSlash + 1);

    // Extract the file name and extension of the requested file
    size_t lastSlashInRequestedFilePath = fileName.find_last_of("/\\");
    std::string useFileName;
    if (lastSlashInRequestedFilePath == std::string::npos) {
        useFileName = fileName;
    } else {
        useFileName = fileName.substr(lastSlashInRequestedFilePath + 1);
    }

    // Form the full path of a new file in this same directory
    std::string fullPathOfNewFile = runningAppDirectory + useFileName;

    FILE* file;
    errno_t result = fopen_s(&file, fullPathOfNewFile.c_str(), mode);
    if (result == 0) {
        return file;
    } else {
        if (result == 13) {
            MessageBoxW(NULL, L"Could not access app location to save game data.", L"Permission denied", MB_OK);
        }
        return nullptr;
    }
}

void WindowsAppPlatform::openDebugWindow(Gbc* gbc) {
	SendMessageW(hWnd, LAUNCH_DEBUG_MSG, 0, 0);
}

void WindowsAppPlatform::withCurrentTime(std::function<void(struct tm*)> func) {
    time_t timestamp = time(nullptr);
    struct tm localTime;
    localtime_s(&localTime, &timestamp);
    func(&localTime);
}

uint64_t WindowsAppPlatform::getUptimeMillis() {
    return (uint64_t)GetTickCount64();
}

void WindowsAppPlatform::pollGamepad() {
    XINPUT_STATE state;
    DWORD res = XInputGetState(0, &state);
    if (res == ERROR_SUCCESS) {
        gamepadInputs.isConnected = true;
        WORD digitalButtons = state.Gamepad.wButtons;
        gamepadInputs.left = digitalButtons & XINPUT_GAMEPAD_DPAD_LEFT;
        gamepadInputs.right = digitalButtons & XINPUT_GAMEPAD_DPAD_RIGHT;
        gamepadInputs.up = digitalButtons & XINPUT_GAMEPAD_DPAD_UP;
        gamepadInputs.down = digitalButtons & XINPUT_GAMEPAD_DPAD_DOWN;
        gamepadInputs.select = digitalButtons & XINPUT_GAMEPAD_BACK;
        gamepadInputs.start = digitalButtons & XINPUT_GAMEPAD_START;
        gamepadInputs.actionLeft = digitalButtons & XINPUT_GAMEPAD_X;
        gamepadInputs.actionTop = digitalButtons & XINPUT_GAMEPAD_Y;
        gamepadInputs.actionRight = digitalButtons & XINPUT_GAMEPAD_B;
        gamepadInputs.actionBottom = digitalButtons & XINPUT_GAMEPAD_A;
        if (state.Gamepad.sThumbLX > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && state.Gamepad.sThumbLX < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
            gamepadInputs.xClamped = 0.0f;
        } else {
            gamepadInputs.xClamped = (float)state.Gamepad.sThumbLX / 32768.0f;
        }
        if (state.Gamepad.sThumbLY > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && state.Gamepad.sThumbLY < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
            gamepadInputs.yClamped = 0.0f;
        } else {
            gamepadInputs.yClamped = (float)state.Gamepad.sThumbLY / 32768.0f;
        }
    } else {
        gamepadInputs.isConnected = false;
    }
}
