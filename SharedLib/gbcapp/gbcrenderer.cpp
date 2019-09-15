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

GbcRenderer::GbcRenderer(AppPlatform* appPlatform, PlatformRenderer* platformRenderer, Gbc* gbc) :
        Renderer(appPlatform, platformRenderer), gbc(gbc), windowTextureHandle(0) {
    this->frameState = new GbcAppState();
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
    TextureShader* mainShader = new TextureShader(*appPlatform, *platformRenderer);
    FontShader* fontShader = new FontShader(*appPlatform, *platformRenderer);

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

    // Create gameplay buttons
    buttonSize = { 0.75f, 0.75f };
    buttonMargins = { 0.125f, 0.0f, 0.0f, 0.1875f };
    buttonBgTextureBounds = { 0.375f, 0.375f, 0.5f, 0.5f };
    auto dpadImage = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::START);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.5f, 0.3125f };
    buttonBgTextureBounds = { 0.875f, 0.25f, 1.0f, 0.375f };
    auto bButtonBackground = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.5f, 0.3125f };
    buttonBgTextureBounds = { 0.625f, 0.375f, 0.75f, 0.5f };
    auto bButton = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.125f, 0.1875f };
    buttonBgTextureBounds = { 0.875f, 0.25f, 1.0f, 0.375f };
    auto aButtonBackground = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.125f, 0.1875f };
    buttonBgTextureBounds = { 0.5f, 0.375f, 0.625f, 0.5f };
    auto aButton = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.1875f, 0.125f };
    buttonBgTextureBounds = { 0.875f, 0.375f, 1.0f, 0.4375f };
    auto selectButtonBackground = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.1875f, 0.125f };
    buttonBgTextureBounds = { 0.75f, 0.375f, 0.875f, 0.4375f };
    auto selectButton = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.1875f, 0.0f, 0.0f, 0.125f };
    buttonBgTextureBounds = { 0.875f, 0.375f, 1.0f, 0.4375f };
    auto startButtonBackground = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.1875f, 0.0f, 0.0f, 0.125f };
    buttonBgTextureBounds = { 0.75f, 0.4375f, 0.875f, 0.5f };
    auto startButton = Image(mainTexture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);

    this->gameplayButtons.push_back(dpadImage);
    this->gameplayButtons.push_back(bButtonBackground);
    this->gameplayButtons.push_back(bButton);
    this->gameplayButtons.push_back(aButtonBackground);
    this->gameplayButtons.push_back(aButton);
    this->gameplayButtons.push_back(selectButtonBackground);
    this->gameplayButtons.push_back(selectButton);
    this->gameplayButtons.push_back(startButtonBackground);
    this->gameplayButtons.push_back(startButton);

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

    // Create the frame config objects
    FrameConfig mainConfig;
    mainConfig.shader = mainShader;
    mainConfig.renderConfigs.push_back(config1);
    mainConfig.renderConfigs.push_back(config2);
    FrameConfig textConfig;
    textConfig.shader = fontShader;
    textConfig.renderConfigs.push_back(config3);
    FrameConfig gameplayConfig;
    gameplayConfig.shader = mainShader;
    gameplayConfig.renderConfigs.push_back(config5);
    gameplayConfig.renderConfigs.push_back(config4);

    // Collect the frame configs
    frameConfigs.push_back(mainConfig);
    frameConfigs.push_back(textConfig);
    frameConfigs.push_back(gameplayConfig);

    // Finish with resources
    delete mainTextureResource;

    return contextCreated;
}

void GbcRenderer::doWork() {
    // Wait for frame signal (this should be invoked when stopping the thread also since frames will
    // no longer be generated)
    std::unique_lock<std::mutex> lock(threadMutex);
    cond.wait(lock);

    // Make sure thread is still in a running state
    if (!running)
    {
        return;
    }

    // Make sure no errors have been generated
    platformRenderer->verifyNoError();

    bool framePrepared = false;
    int firstConfig, configCount;
    if (frameQueued)
    {
        // Cast app state, check mode, prepare frame configs and indicate which to render
        GbcAppState* state = (GbcAppState*) frameState;
        if (state->appMode == AppMode::MAIN_MENU)
        {
            // Construct transformation matrices
            glm::mat4 mainMvpMatrix(1.0f);

            // Set uniforms for the main menu UI background/button rectangles
            auto& rectConfig = frameConfigs[FCT::RECT];
            ((TextureShader*)rectConfig.shader)->prepareConfig(rectConfig.renderConfigs[RCT::RECT_MENU_BG], 0, glm::value_ptr(mainMvpMatrix));
            ((TextureShader*)rectConfig.shader)->prepareConfig(rectConfig.renderConfigs[RCT::RECT_MENU_BTN_OUTLINE], 0, glm::value_ptr(mainMvpMatrix));

            // Set uniforms for the main menu UI button text
            float r = 0.5f;
            float g = 0.2f;
            float b = 0.1f;
            auto& textConfig = frameConfigs[FCT::TEXT];
            ((FontShader*)textConfig.shader)->prepareConfig(textConfig.renderConfigs[RCT::TEXT_MENU_BTN], 0, glm::value_ptr(mainMvpMatrix), r, g, b);

            firstConfig = 0;
            configCount = 2;
            framePrepared = true;
        }
        else if (state->appMode == AppMode::PLAYING)
        {
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
                auto& playConfig = frameConfigs[FCT::GAME];
                ((TextureShader*)playConfig.shader)->prepareConfig(playConfig.renderConfigs[RCT::GAME_HUD], 0, glm::value_ptr(mainMvpMatrix));
                ((TextureShader*)playConfig.shader)->prepareConfig(playConfig.renderConfigs[RCT::GAME_WINDOW], 0, glm::value_ptr(mainMvpMatrix));

                firstConfig = 2;
                configCount = 1;
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
    if (framePrepared)
    {
        platformRenderer->renderPass(frameConfigs, firstConfig, configCount);
    }

    // Make sure rendering functions didn't generate an error
    platformRenderer->verifyNoError();

}
