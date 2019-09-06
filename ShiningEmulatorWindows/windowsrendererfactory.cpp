#include "windowsrendererfactory.h"

#include "windowsrenderer.h"

WindowsRendererFactory::WindowsRendererFactory(HDC hDC, int width, int height)
        : hDC(hDC), canvasWidth(width), canvasHeight(height) { }

PlatformRenderer* WindowsRendererFactory::newPlatformRenderer() {
    return new WindowsRenderer(hDC, canvasWidth, canvasHeight);
}
