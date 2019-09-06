#include "renderer.h"

#include "platformrenderer.h"

Renderer::Renderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer) :
        appPlatform(appPlatform),
        platformRenderer(platformRenderer) {

    running = false;
    frameQueued = false;
    requestedWidth = 0;
    requestedHeight = 0;
    frameTimeDiffMillis = 0;
    frameState = nullptr;
}

Renderer::~Renderer() = default;

bool Renderer::signalFrameReady(uint64_t timeDiffMillis, AppState* lockedAppState) {
    bool frameWasQueued = false;
    if (!frameQueued) {
        threadMutex.lock();
        frameTimeDiffMillis = timeDiffMillis;
        frameState = lockedAppState;
        frameQueued = true;
        frameWasQueued = true;
        threadMutex.unlock();
    }
    cond.notify_all();
    return frameWasQueued;
}

void Renderer::processMsg(const Message& msg) {
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

void Renderer::stopThread() {
    // Signal frame ready to prevent waiting for a frame that will never come
    if (running) {
        running = false;
        cond.notify_all();
        std::unique_lock<std::mutex> lock(threadMutex);
        cond.wait(lock, [&]() { return !valid; });
        lock.unlock();
    }
}
