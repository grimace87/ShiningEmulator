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
