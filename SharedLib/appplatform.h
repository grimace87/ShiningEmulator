#pragma once

#include "inputdefs.h"

#include <mutex>
#include <vector>
#include <map>
#include <functional>

class Resource;
class App;

class AppPlatform {
public:
    void signalKey(unsigned int code, bool down);
    void addCursor(std::mutex& threadMutex, int id, float xPixels, float yPixels);
    void updateCursor(std::mutex& threadMutex, int id, float xPixels, float yPixels);
    void removeCursor(std::mutex& threadMutex, int id);
    void removeAllCursors(std::mutex& threadMutex);
    void releaseAllInputs();
    std::map<int,Cursor> cursors;
    bool inputsChanged = false;
    bool keyboardInputs[256];
    GamepadInputs gamepadInputs;
    virtual bool onAppThreadStarted(App* app) = 0;
    virtual Resource* getResource(const char* fileName, bool isAsset, bool isGlShader) = 0;
    virtual Resource* chooseFile(std::string fileTypeDescr, std::vector<std::string> fileTypes) = 0;
    virtual FILE* openFileInAppDir(std::string fileName, const char* mode) = 0;
    virtual void withCurrentTime(std::function<void(struct tm*)> func) = 0;
    virtual void pollGamepad() = 0;
    virtual uint64_t getUptimeMillis() = 0;
};