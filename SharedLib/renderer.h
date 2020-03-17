#pragma once

#include "thread.h"
#include "renderconfig.h"

class AppPlatform;

class Renderer : public Thread {
public:
    int requestedWidth, requestedHeight;
    bool initObject() override = 0;
    void doWork() override = 0;
    void killObject() override;
    Renderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer);
    virtual ~Renderer();
    bool signalFrameReady(uint64_t timeDiffMillis, uint32_t lockedAppState);
    void requestWindowResize(int width, int height);
    void queryCanvasSize(int* outWidth, int* outHeight);
protected:
    void processMsg(const Message& msg) override;
    std::condition_variable frameConditionVariable;
    AppPlatform* appPlatform;
    PlatformRenderer* platformRenderer;
    std::vector<FrameConfig> frameConfigs;
    bool frameQueued;
    uint32_t frameState;
    uint64_t frameTimeDiffMillis;
};
