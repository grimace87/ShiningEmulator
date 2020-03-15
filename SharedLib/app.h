#pragma once

#include "thread.h"
#include "menu.h"

#include <iostream>

class AppPlatform;
class Renderer;
class AudioStreamer;
struct Message;

class App : public Thread {
public:
    App(AppPlatform& platform);
    virtual ~App();
    Menu menu;
    AppPlatform& platform;
    static char* pendingFileToOpen;
    bool initObject() final;
    void killObject() final;
    virtual void persistState(std::ostream& stream) = 0;
    virtual void loadPersistentState(std::istream& stream) = 0;
    void doWork() override = 0;
    void requestWindowResize(int width, int height);
    // Cursor modifiers merely call the equivalents on the AppPlatform object
    void addCursor(int id, float xPixels, float yPixels);
    void updateCursor(int id, float xPixels, float yPixels);
    void removeCursor(int id);
    void removeAllCursors();
protected:
    Renderer* renderer;
    AudioStreamer* audioStreamer;
    void processMsg(const Message& msg) override = 0;
    virtual bool createRenderer() = 0;
    virtual bool createAudioStreamer() = 0;
};
