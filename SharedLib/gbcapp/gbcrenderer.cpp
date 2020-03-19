#include "gbcrenderer.h"

#define GLM_PRECISION_MEDIUMP_FLOAT
#include "../lib/glm/matrix.hpp"
#include "../lib/glm/gtc/matrix_transform.hpp"
#include "../lib/glm/gtc/type_ptr.hpp"

#include "uibuffers.h"
#include "gbcappstate.h"
#include "../gbc/gbc.h"
#include "../resource.h"
#include "../font.h"
#include "../appplatform.h"
#include "../platformrenderer.h"
#include "../uielements.h"
#include "../shader.h"
#include "gbcui.h"

GbcRenderer::GbcRenderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer, Gbc* gbc) :
        appPlatform(appPlatform),
        platformRenderer(platformRenderer),
        gbc(gbc),
        windowTextureHandle(0) {
    showFullUi = appPlatform->usesTouch;
    requestedWidth = 0;
    requestedHeight = 0;
    frameQueued = false;
    frameTimeDiffMillis = 0;
    frameState = 0;
}

GbcRenderer::~GbcRenderer() = default;

bool GbcRenderer::initObject() {
    bool contextCreated = platformRenderer->createContext();
    if (!contextCreated) {
        return false;
    }

    // Get shader, textures and VBOs
    unsigned int mainTexture, fontTexture, mainVbo, gameHudVbo, buttonOutlinesVbo, buttonLabelsVbo, windowVbo;
    Resource* mainTextureResource;
    Font font;
    {
        mainTextureResource = appPlatform->getResource("tex_main.png", true, false);
        PngImage imageDef = mainTextureResource->getPng();
        mainTexture = platformRenderer->createTexture(TEXTURE_FORMAT_RGBA, imageDef.pixelData, imageDef.width, imageDef.height, true);
    }
    {
        Resource* fontTextureResource = appPlatform->getResource("Musica.png", true, false);
        PngImage imageDef = fontTextureResource->getPng();
        fontTexture = platformRenderer->createTexture(TEXTURE_FORMAT_RGBA, imageDef.pixelData, imageDef.width, imageDef.height, true);
        delete fontTextureResource;
    }
    {
        Resource* fontDescriptionResource = appPlatform->getResource("Musica.fnt", true, false);
        font = Font::fromResource(fontDescriptionResource);
        delete fontDescriptionResource;
        if (font.lineHeight == 0)
        {
            return false;
        }
    }
    mainVbo = platformRenderer->createVbo(mainMenuUiFloats, mainMenuFloatCount);

    platformRenderer->verifyNoError();

    // Create shaders
    auto mainShader = new TextureShader(*appPlatform, *platformRenderer);
    auto fontShader = new FontShader(*appPlatform, *platformRenderer);

    platformRenderer->verifyNoError();

    // Create main menu buttons
    Size buttonSize = { 0.2f, 0.8f };
    Rect buttonMargins = { 0.0f, 0.0f, 0.0f, 0.0f };
    Rect buttonBgTextureBounds = { 0.375f, 0.25f, 0.875f, 0.375f };
    auto openRomButton = TextButton(
            mainTexture, fontTexture, &font,
            "Open ROM", 0.5f,
            buttonSize, buttonMargins, buttonBgTextureBounds,
            Gravity::MIDDLE, Gravity::MIDDLE
    );
    this->uiButtons.push_back(openRomButton);

    // Generate float buffers for main menu buttons
    std::vector<float> buttonFloats = TextButton::makeAllFloats(this->uiButtons, mainTextureResource, 1.0f, false);
    size_t floatsForButtonOutlines = 30 * this->uiButtons.size();
    size_t floatsForButtonLabels = buttonFloats.size() - floatsForButtonOutlines;
    buttonOutlinesVbo = platformRenderer->createVbo(&buttonFloats.front(), floatsForButtonOutlines);
    buttonLabelsVbo = platformRenderer->createVbo(&buttonFloats.front() + floatsForButtonOutlines, floatsForButtonLabels);

    // Fill vector of gameplay button images
    bool wide = platformRenderer->canvasWidth > platformRenderer->canvasHeight;
    GbcUi::populateGameplayButtonsVector(this->gameplayButtons, wide, (int)mainTexture);

    // Generate float buffer for gameplay buttons
    int width;
    int height;
    queryCanvasSize(&width, &height);
    float aspect = (float)width / (float)height;
    std::vector<float> gameHudFloats = Image::makeAllFloats(this->gameplayButtons, aspect, false);
    size_t floatsForGameHud = 30 * this->gameplayButtons.size();
    gameHudVbo = platformRenderer->createVbo(&gameHudFloats.front(), floatsForGameHud);

    // Create window assets
    float* scaledWindowFloats = generateWindowFloats(0.8f, aspect);
    windowVbo = platformRenderer->createVbo(scaledWindowFloats, windowFloatCount);
    this->windowTextureHandle = platformRenderer->createTexture(TEXTURE_FORMAT_RGBA, nullptr, 160 * 4, 144 * 4, false);

    // Make sure rendering functions didn't generate an error
    platformRenderer->verifyNoError();

    // Create the rendering config objects
    RenderConfig config1 = RenderConfig::makeStandardConfig(*mainShader, *platformRenderer, mainTexture, mainVbo, 0, mainMenuFloatCount / 5);
    RenderConfig config2 = RenderConfig::makeStandardConfig(*mainShader, *platformRenderer, mainTexture, buttonOutlinesVbo, 0, floatsForButtonOutlines / 5);
    RenderConfig config3 = RenderConfig::makeTextConfig(*fontShader, *platformRenderer, fontTexture, buttonLabelsVbo, 0, floatsForButtonLabels / 5, 1.0f, 0.0f, 0.0f);
    //RenderConfig config4 = RenderConfig::makeStandardConfig(*mainShader, *platformRenderer, mainTexture, gameHudVbo, 0, gameHudFloatCount / 5);
    RenderConfig config4 = RenderConfig::makeStandardConfig(*mainShader, *platformRenderer, mainTexture, gameHudVbo, 0, floatsForGameHud / 5);
    RenderConfig config5 = RenderConfig::makeStandardConfig(*mainShader, *platformRenderer, windowTextureHandle, windowVbo, 0, windowFloatCount / 5);

    // Modify the HUD config according to how much ought to be shown
    if (showFullUi) {
        config4.startVertex = 0;
        config4.vertexCount = 102;
    } else {
        config4.startVertex = 54;
        config4.vertexCount = 48;
    }

    // Create the frame config objects
    FrameConfig mainConfig;
    mainConfig.shader = mainShader;
    mainConfig.renderConfigs.push_back(config1);
    mainConfig.renderConfigs.push_back(config2);
    FrameConfig textConfig;
    textConfig.shader = fontShader;
    textConfig.renderConfigs.push_back(config3);
    FrameConfig gameplayWindowConfig;
    gameplayWindowConfig.shader = mainShader;
    gameplayWindowConfig.renderConfigs.push_back(config5);
    FrameConfig gameplayHudConfig;
    gameplayHudConfig.shader = mainShader;
    gameplayHudConfig.renderConfigs.push_back(config4);

    // Collect the frame configs
    frameConfigs.push_back(mainConfig);
    frameConfigs.push_back(textConfig);
    frameConfigs.push_back(gameplayWindowConfig);
    frameConfigs.push_back(gameplayHudConfig);

    // Finish with resources
    delete mainTextureResource;

    return contextCreated;
}

void GbcRenderer::preCleanup() {
    isValid = false;
    frameConditionVariable.notify_one();
}

void GbcRenderer::killObject() {
    platformRenderer->destroyContext();
}

void GbcRenderer::processMsg(const Message& msg) {
    Thread::processMsg(msg);
    switch (msg.msg) {
        case Action::MSG_UPDATE_SIZE:
            platformRenderer->resizeDisplay(requestedWidth, requestedHeight);
            requestedWidth = 0;
            requestedHeight = 0;
            break;
        default: ;
    }
}

bool GbcRenderer::signalFrameReady(uint64_t timeDiffMillis, uint32_t appState) {
    bool frameWasQueued = false;
    if (!frameQueued) {
        threadMutex.lock();
        frameTimeDiffMillis = timeDiffMillis;
        frameState = appState;
        frameQueued = true;
        frameWasQueued = true;
        threadMutex.unlock();
    }
    frameConditionVariable.notify_all();
    return frameWasQueued;
}

void GbcRenderer::requestWindowResize(int width, int height) {
    requestedWidth = width;
    requestedHeight = height;
    postMessage({ Action::MSG_UPDATE_SIZE, 0 });
}

void GbcRenderer::queryCanvasSize(int* outWidth, int* outHeight) {
    if (platformRenderer) {
        *outWidth = platformRenderer->canvasWidth;
        *outHeight = platformRenderer->canvasHeight;
    } else {
        *outWidth = 0;
        *outHeight = 0;
    }
}

void GbcRenderer::doWork() {
    // Wait for frame signal (this should be invoked when stopping the thread also since frames will
    // no longer be generated)
    std::unique_lock<std::mutex> lock(threadMutex);
    frameConditionVariable.wait(lock);

    // Make sure thread is still in a running state
    if (!isValid) {
        return;
    }

    // Make sure no errors have been generated
    platformRenderer->verifyNoError();

    bool framePrepared = false;
    unsigned int firstConfig = 0;
    unsigned int configCount = 0;
    if (frameQueued) {
        // Check mode, prepare frame configs and indicate which to render
        if (frameState == (uint32_t)GbcAppState::MAIN_MENU) {
            // Construct transformation matrices
            glm::mat4 mainMvpMatrix(1.0f);

            // Set uniforms for the main menu UI background/button rectangles
            auto& rectConfig = frameConfigs[FCT::RECT];
            TextureShader::prepareConfig(rectConfig.renderConfigs[RCT::RECT_MENU_BG], 0, glm::value_ptr(mainMvpMatrix));
            TextureShader::prepareConfig(rectConfig.renderConfigs[RCT::RECT_MENU_BTN_OUTLINE], 0, glm::value_ptr(mainMvpMatrix));

            // Set uniforms for the main menu UI button text
            float r = 0.5f;
            float g = 0.2f;
            float b = 0.1f;
            auto& textConfig = frameConfigs[FCT::TEXT];
            FontShader::prepareConfig(textConfig.renderConfigs[RCT::TEXT_MENU_BTN], 0, glm::value_ptr(mainMvpMatrix), r, g, b);

            firstConfig = 0;
            configCount = 2;
            framePrepared = true;
        } else if (frameState == (uint32_t)GbcAppState::PLAYING) {
            uint32_t* upscaledFrameBuffer = gbc->frameManager.getRenderableFrameBuffer();
            if (upscaledFrameBuffer != nullptr) {
                // Construct transformation matrix
                int width, height;
                queryCanvasSize(&width, &height);
                float aspect = (float)width / (float)height;
                glm::mat4 mainMvpMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / aspect, 1.0f, 1.0f));

                platformRenderer->subTexture(this->windowTextureHandle, TEXTURE_FORMAT_RGBA, 0, 0, 160 * 4, 144 * 4, (unsigned char*)upscaledFrameBuffer);
                gbc->frameManager.freeFrame(upscaledFrameBuffer);

                // Set uniforms for the heads-up display rectangles
                firstConfig = 2;
                configCount = 2;
                auto& windowConfig = frameConfigs[FCT::GAME_WINDOW];
                TextureShader::prepareConfig(windowConfig.renderConfigs[RCT::GAME_WINDOW], 0, glm::value_ptr(mainMvpMatrix));
                auto &hudConfig = frameConfigs[FCT::GAME_HUD];
                TextureShader::prepareConfig(hudConfig.renderConfigs[RCT::GAME_HUD], 0, glm::value_ptr(mainMvpMatrix));

                framePrepared = true;
            }
        } else {
            //auto msg = std::string("Invald game state: ") + std::to_string(static_cast<int>(state->appMode));
            //throw std::runtime_error(msg);
        }
    }

    // New frame can be queued
    frameQueued = false;
    lock.unlock();

    // Render the queued frame
    if (framePrepared) {
        platformRenderer->renderPass(frameConfigs, firstConfig, configCount);
    }

    // Make sure rendering functions didn't generate an error
    platformRenderer->verifyNoError();

}
