#pragma once

#include "../SharedLib/appplatform.h"

#include "../SharedLib/menu.h"

#include <Windows.h>

class WindowsAppPlatform : public AppPlatform {
    bool onAppThreadStarted(Thread* app) override;
    HINSTANCE hInstance;
    HWND hWnd;
    HDC hDC;
    int canvasWidth;
    int canvasHeight;

protected:
    uint64_t getUptimeMillis() override;

public:
    WindowsAppPlatform(HINSTANCE hInstance, HWND hWnd, HDC hDC, int width, int height);
    Menu mainMenu;
    PlatformRenderer* newPlatformRenderer() final;
    AudioStreamer* newAudioStreamer(Gbc* gbc) final;
    Resource* getResource(const char* fileName, bool isAsset, bool isGlShader) override;
    Resource* chooseFile(std::string fileTypeDescr, std::vector<std::string> fileTypes) override;
    std::string getAppDir() override;
    char getSeparator() override;
	void openDebugWindow(Gbc* gbc) override;
    void withCurrentTime(std::function<void(struct tm*)> func) override;
    void pollGamepad() override;
};