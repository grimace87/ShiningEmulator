#include "app.h"

#include "appplatform.h"
#include "renderer.h"

AppState* App::savedState = nullptr;
char* App::pendingFileToOpen = nullptr;

App::App(AppPlatform& platform, RendererFactory& rendererFactory) : menu(Menu::buildMain()), platform(platform), rendererFactory(rendererFactory) {
    savedState = nullptr;
    renderer = nullptr;
    platform.releaseAllInputs();
}

App::~App() = default;

bool App::initObject() {
    platform.onAppThreadStarted(this);
    return createRenderer();
}

void App::killObject() {
    if (renderer) {
        renderer->stopThread();
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
