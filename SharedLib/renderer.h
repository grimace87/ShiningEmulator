#pragma once

#include "thread.h"
#include "renderconfig.h"

class AppState;
class AppPlatform;
class Message;

class Renderer : public Thread {
public:
    int requestedWidth, requestedHeight;
    bool initObject() override = 0;
    void doWork() override = 0;
    void killObject() override;
    void stopThread() override;
    Renderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer);
    virtual ~Renderer();
    bool signalFrameReady(uint64_t timeDiffMillis, uint32_t lockedAppState);
    void requestWindowResize(int width, int height);
    void queryCanvasSize(int* outWidth, int* outHeight);
protected:
    void processMsg(const Message& msg) override;
    AppPlatform* appPlatform;
    PlatformRenderer* platformRenderer;
    std::vector<FrameConfig> frameConfigs;
    bool frameQueued;
    uint32_t frameState;
    uint64_t frameTimeDiffMillis;
};
