#pragma once

class PlatformRenderer;

class RendererFactory {
public:
    virtual PlatformRenderer* newPlatformRenderer() = 0;
};
