#include "renderer.h"

#include "platformrenderer.h"

Renderer::Renderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer) :
        appPlatform(appPlatform),
        platformRenderer(platformRenderer) {

    frameQueued = false;
    requestedWidth = 0;
    requestedHeight = 0;
    frameTimeDiffMillis = 0;
    frameState = 0;
}

Renderer::~Renderer() = default;

bool Renderer::signalFrameReady(uint64_t timeDiffMillis, uint32_t appState) {
    bool frameWasQueued = false;
    if (!frameQueued) {
        threadMutex.lock();
        frameTimeDiffMillis = timeDiffMillis;
        frameState = appState;
        frameQueued = true;
        frameWasQueued = true;
        threadMutex.unlock();
    }
    frameConditionVariable.notify_all();
    return frameWasQueued;
}

void Renderer::processMsg(const Message& msg) {
    Thread::processMsg(msg);
    switch (msg.msg) {
        case Action::MSG_UPDATE_SIZE:
            platformRenderer->resizeDisplay(requestedWidth, requestedHeight);
            requestedWidth = 0;
            requestedHeight = 0;
            break;
        default: ;
    }
}

void Renderer::requestWindowResize(int width, int height) {
    requestedWidth = width;
    requestedHeight = height;
    postMessage({ Action::MSG_UPDATE_SIZE, 0 });
}

void Renderer::queryCanvasSize(int* outWidth, int* outHeight) {
    if (platformRenderer) {
        *outWidth = platformRenderer->canvasWidth;
        *outHeight = platformRenderer->canvasHeight;
    } else {
        *outWidth = 0;
        *outHeight = 0;
    }
}

void Renderer::killObject() {
    platformRenderer->destroyContext();
}
