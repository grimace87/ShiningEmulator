#pragma once

#include "../SharedLib/rendererfactory.h"

#include <Windows.h>

class WindowsRendererFactory : public RendererFactory {
    HDC hDC;
    int canvasWidth;
    int canvasHeight;

public:
    WindowsRendererFactory(HDC hDC, int width, int height);
    PlatformRenderer* newPlatformRenderer() final;
};
