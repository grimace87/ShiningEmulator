#pragma once

#include "inputdefs.h"

#include <mutex>
#include <vector>
#include <map>
#include <functional>
#include <fstream>

class PlatformRenderer;
class AudioStreamer;
class Resource;
class Thread;
class Gbc;

enum class FileOpenMode {
    READ_ONLY_BINARY,
    WRITE_NEW_FILE_BINARY,
    RANDOM_READ_WRITE_BINARY
};

class AppPlatform {
protected:
    virtual std::string getAppDir() = 0;
    virtual char getSeparator() = 0;
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
    bool usesTouch = false;
    virtual bool onAppThreadStarted(Thread* app) = 0;
    virtual PlatformRenderer* newPlatformRenderer() = 0;
    virtual AudioStreamer* newAudioStreamer(Gbc* gbc) = 0;
    virtual Resource* getResource(const char* fileName, bool isAsset, bool isGlShader) = 0;
    virtual Resource* chooseFile(std::string fileTypeDescr, std::vector<std::string> fileTypes) = 0;
	virtual void openDebugWindow(Gbc* gbc) = 0;
    virtual void withCurrentTime(std::function<void(struct tm*)> func) = 0;
    virtual void pollGamepad() = 0;
    virtual uint64_t getUptimeMillis() = 0;
    std::string stripPath(std::string& fullPathedName);
    std::string appendFileNameToAppDir(std::string& fileName);
    std::string replaceExtension(std::string& originalFileName, std::string& extensionLetters);
    std::fstream openFile(std::string& fullPathedName, FileOpenMode mode);

    static inline std::ios_base::openmode makeOpenFlags(FileOpenMode mode) {
        switch (mode) {
            case FileOpenMode::READ_ONLY_BINARY: return std::ios::in | std::ios::binary;
            case FileOpenMode::WRITE_NEW_FILE_BINARY: return std::ios::out | std::ios::binary | std::ios::trunc;
            case FileOpenMode::RANDOM_READ_WRITE_BINARY: return std::ios::in | std::ios::out | std::ios::binary;
        }
    }
};