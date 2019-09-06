#pragma once

#include <android/native_window.h>
#include "../../../../../SharedLib/rendererfactory.h"

class AndroidRendererFactory : public RendererFactory {
	ANativeWindow* window;

public:
	AndroidRendererFactory(ANativeWindow* window);
	PlatformRenderer* newPlatformRenderer() final;
};
