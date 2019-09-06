#include "androidrendererfactory.h"

#include "androidrenderer.h"

#include <android/native_window.h>

AndroidRendererFactory::AndroidRendererFactory(ANativeWindow* window) {
    this->window = window;
}

PlatformRenderer* AndroidRendererFactory::newPlatformRenderer() {
	return new AndroidRenderer(window);
}
