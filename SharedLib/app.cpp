#include "app.h"

#include "appplatform.h"
#include "renderer.h"
#include "audiostreamer.h"

AppState* App::savedState = nullptr;
char* App::pendingFileToOpen = nullptr;

App::App(AppPlatform& platform) : menu(Menu::buildMain()), platform(platform) {
    savedState = nullptr;
    renderer = nullptr;
    audioStreamer = nullptr;
    platform.releaseAllInputs();
}

App::~App() = default;

bool App::initObject() {
    platform.onAppThreadStarted(this);
    return createRenderer() && createAudioStreamer();
}

void App::killObject() {
    if (renderer) {
        renderer->stopThread();
    }
    if (audioStreamer) {
        audioStreamer->stop();
    }
}

void App::requestWindowResize(int width, int height) {
    if (renderer) {
        renderer->requestWindowResize(width, height);
    }
}

void App::addCursor(int id, float xPixels, float yPixels) {
    platform.addCursor(threadMutex, id, xPixels, yPixels);
}

void App::updateCursor(int id, float xPixels, float yPixels) {
    platform.updateCursor(threadMutex, id, xPixels, yPixels);
}

void App::removeCursor(int id) {
    platform.removeCursor(threadMutex, id);
}

void App::removeAllCursors() {
    platform.removeAllCursors(threadMutex);
}
