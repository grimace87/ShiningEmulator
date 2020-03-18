#include "appplatform.h"

void AppPlatform::signalKey(unsigned int code, bool down) {
    if (code < 128) {
        keyboardInputs[code] = down;
    }
}

void AppPlatform::addCursor(std::mutex& threadMutex, int id, float xPixels, float yPixels) {
    Cursor newCursor{ false, xPixels, yPixels, xPixels, yPixels };
    threadMutex.lock();
    cursors.emplace(id, newCursor);
    threadMutex.unlock();
}

void AppPlatform::updateCursor(std::mutex& threadMutex, int id, float xPixels, float yPixels) {
    threadMutex.lock();
    auto elementIter = cursors.find(id);
    if (elementIter != cursors.end()) {
        elementIter->second.xPixels = xPixels;
        elementIter->second.yPixels = yPixels;
    }
    threadMutex.unlock();
}

void AppPlatform::removeCursor(std::mutex& threadMutex, int id) {
    threadMutex.lock();
    cursors.erase(id);
    threadMutex.unlock();
}

void AppPlatform::removeAllCursors(std::mutex& threadMutex) {
    threadMutex.lock();
    cursors.clear();
    threadMutex.unlock();
}

void AppPlatform::releaseAllInputs() {
    cursors.clear();
    for (unsigned char c = 0; c < 128; c++) {
        keyboardInputs[c] = false;
    }
}

std::string AppPlatform::stripPath(std::string& fullPathedName) {
    size_t lastSlash = fullPathedName.find_last_of(getSeparator());
    if (lastSlash == std::string::npos) {
        return fullPathedName;
    }
    if ((lastSlash + 1) == fullPathedName.length()) {
        return "";
    }
    return fullPathedName.substr(lastSlash + 1);
}

std::string AppPlatform::appendFileNameToAppDir(std::string& fileName) {
    return getAppDir() + getSeparator() + fileName;
}

std::string AppPlatform::replaceExtension(std::string& originalFileName, std::string& extensionLetters) {
    size_t lastPeriod = originalFileName.find_last_of('.');
    if (lastPeriod == std::string::npos) {
        return originalFileName;
    }
    return originalFileName.substr(0, lastPeriod) + '.' + extensionLetters;
}

std::fstream AppPlatform::openFile(std::string& fullPathedName, FileOpenMode mode) {
    std::fstream stream;
    stream.open(fullPathedName, AppPlatform::makeOpenFlags(mode));
    return stream;
}
