#pragma once

#include "thread.h"
#include "menu.h"

class AppState;
class AppPlatform;
class Renderer;
class Message;

class App : public Thread {
public:
    App(AppPlatform& platform);
    virtual ~App();
    Menu menu;
    AppPlatform& platform;
    static AppState* savedState;
    static char* pendingFileToOpen;
    bool initObject() final;
    void killObject() final;
    virtual void persistState() = 0;
    virtual void loadPersistentState() = 0;
    void doWork() override = 0;
    void requestWindowResize(int width, int height);
    // Cursor modifiers merely call the equivalents on the AppPlatform object
    void addCursor(int id, float xPixels, float yPixels);
    void updateCursor(int id, float xPixels, float yPixels);
    void removeCursor(int id);
    void removeAllCursors();
protected:
    Renderer* renderer;
    void processMsg(const Message& msg) override = 0;
    virtual bool createRenderer() = 0;
};
