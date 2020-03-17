#include "app.h"

#include "appplatform.h"
#include "renderer.h"
#include "audiostreamer.h"

char* App::pendingFileToOpen = nullptr;

App::App(AppPlatform& platform) : menu(Menu::buildMain()), platform(platform) {
    platform.releaseAllInputs();
}

App::~App() = default;

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
