#pragma once

#include "../thread.h"
#include "../renderconfig.h"

class Gbc;
class TextButton;
class Image;
class AppPlatform;

namespace FCT {
    const int RECT = 0;
    const int TEXT = 1;
    const int GAME_WINDOW = 2;
    const int GAME_HUD = 3;
}

namespace RCT {
    const int RECT_MENU_BG = 0;
    const int RECT_MENU_BTN_OUTLINE = 1;

    const int TEXT_MENU_BTN = 0;

    const int GAME_WINDOW = 0;

    const int GAME_HUD = 0;
}

class GbcRenderer : public Thread {
    unsigned int windowTextureHandle;
    Gbc* gbc;
    bool showFullUi;
    bool frameQueued;
    uint32_t frameState;
    uint64_t frameTimeDiffMillis;
protected:
    bool initObject() override;
    void doWork() override;
    void preCleanup() final;
public:
    GbcRenderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer, Gbc* gbc);
    ~GbcRenderer();
    void processMsg(const Message& msg) override;
    void killObject() override;
    bool signalFrameReady(uint64_t timeDiffMillis, uint32_t lockedAppState);
    void requestWindowResize(int width, int height);
    void queryCanvasSize(int* outWidth, int* outHeight);

    std::vector<TextButton> uiButtons;
    std::vector<Image> gameplayButtons;
    int requestedWidth;
    int requestedHeight;
    std::condition_variable frameConditionVariable;
    AppPlatform* appPlatform;
    PlatformRenderer* platformRenderer;
    std::vector<FrameConfig> frameConfigs;
};

