#pragma once

#include "../renderer.h"

class Gbc;
class TextButton;
class Image;

namespace FCT {
    const int RECT = 0;
    const int TEXT = 1;
    const int GAME = 2;
}

namespace RCT {
    const int RECT_MENU_BG = 0;
    const int RECT_MENU_BTN_OUTLINE = 1;

    const int TEXT_MENU_BTN = 0;

    const int GAME_HUD = 0;
    const int GAME_WINDOW = 1;
}

class GbcRenderer : public Renderer {
    unsigned int windowTextureHandle;
    Gbc* gbc;
protected:
    bool initObject() override;
    void doWork() override;
public:
    GbcRenderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer, Gbc* ggbc);
    ~GbcRenderer() override;
    std::vector<TextButton> uiButtons;
    std::vector<Image> gameplayButtons;
};

