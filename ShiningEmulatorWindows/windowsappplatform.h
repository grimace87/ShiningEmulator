#pragma once

#include "../SharedLib/appplatform.h"

#include <Windows.h>

class Menu;

#define MAX_LOADSTRING 100

class WindowsAppPlatform : public AppPlatform {
    bool onAppThreadStarted(App* app) override;
    HWND hWnd;

protected:
    uint64_t getUptimeMillis() override;

public:
    explicit WindowsAppPlatform(HWND hWnd);
    Menu* mainMenu;
    Resource* getResource(const char* fileName, bool isAsset, bool isGlShader) override;
    Resource* chooseFile(std::string fileTypeDescr, std::vector<std::string> fileTypes) override;
    FILE* openFileInAppDir(std::string fileName, const char* mode) override;
    void withCurrentTime(std::function<void(struct tm*)> func) override;
    void pollGamepad() override;
};