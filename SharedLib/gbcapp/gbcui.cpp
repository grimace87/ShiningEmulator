#include "gbcui.h"

void GbcUi::populateGameplayButtonsVector(std::vector<Image>& buttons, bool widescreen, int texture) {

    Size buttonSize {};
    Rect buttonMargins {};
    Rect buttonBgTextureBounds {};
    float lowMargin = widescreen ? 0.0F : 0.15F;
    float sideButtonLowMargin = widescreen ? 0.0F : 0.15F;
    float sideMargin = widescreen ? 0.1F : 0.0F;

    // Create gameplay buttons
    buttonSize = { 0.75f, 0.75f };
    buttonMargins = { 0.125f, 0.0f, 0.0f, 0.1875f + lowMargin + sideButtonLowMargin };
    buttonBgTextureBounds = { 0.375f, 0.375f, 0.5f, 0.5f };
    auto dpadImage = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::START);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.5f + sideMargin, 0.3125f + lowMargin + sideButtonLowMargin };
    buttonBgTextureBounds = { 0.875f, 0.25f, 1.0f, 0.375f };
    auto bButtonBackground = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.5f + sideMargin, 0.3125f + lowMargin + sideButtonLowMargin };
    buttonBgTextureBounds = { 0.625f, 0.375f, 0.75f, 0.5f };
    auto bButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.125f + sideMargin, 0.1875f + lowMargin + sideButtonLowMargin };
    buttonBgTextureBounds = { 0.875f, 0.25f, 1.0f, 0.375f };
    auto aButtonBackground = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.375f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.125f + sideMargin, 0.1875f + lowMargin + sideButtonLowMargin };
    buttonBgTextureBounds = { 0.5f, 0.375f, 0.625f, 0.5f };
    auto aButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::END);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.1875f, 0.125f + lowMargin };
    buttonBgTextureBounds = { 0.875f, 0.375f, 1.0f, 0.4375f };
    auto selectButtonBackground = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.0f, 0.0f, 0.1875f, 0.125f + lowMargin };
    buttonBgTextureBounds = { 0.75f, 0.375f, 0.875f, 0.4375f };
    auto selectButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.1875f, 0.0f, 0.0f, 0.125f + lowMargin };
    buttonBgTextureBounds = { 0.875f, 0.375f, 1.0f, 0.4375f };
    auto startButtonBackground = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);
    buttonSize = { 0.1875f, 0.375f };
    buttonMargins = { 0.1875f, 0.0f, 0.0f, 0.125f + lowMargin };
    buttonBgTextureBounds = { 0.75f, 0.4375f, 0.875f, 0.5f };
    auto startButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::END, Gravity::MIDDLE);
    buttonSize = { 0.125f, 0.125f };
    buttonMargins = { 0.125f, widescreen ? 0.125f : 0.25f, 0.0f, 0.0f };
    buttonBgTextureBounds = { 0.125f, 0.5f, 0.25f, 0.625f };
    auto slowerButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::START, Gravity::START);
    buttonSize = { 0.125f, 0.125f };
    buttonMargins = { 0.3125f, widescreen ? 0.125f : 0.25f, 0.0f, 0.0f };
    buttonBgTextureBounds = { 0.0f, 0.5f, 0.125f, 0.625f };
    auto fasterButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::START, Gravity::START);
    buttonSize = { 0.125f, 0.125f };
    buttonMargins = { 0.0f, widescreen ? 0.125f : 0.25f, 0.3125f, 0.0f };
    buttonBgTextureBounds = { 0.25f, 0.5f, 0.375f, 0.625f };
    auto loadSaveStateButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::START, Gravity::END);
    buttonSize = { 0.125f, 0.125f };
    buttonMargins = { 0.0f, widescreen ? 0.125f : 0.25f, 0.125f, 0.0f };
    buttonBgTextureBounds = { 0.375f, 0.5f, 0.5f, 0.625f };
    auto saveSaveStateButton = Image(texture, buttonSize, buttonMargins, buttonBgTextureBounds, Gravity::START, Gravity::END);

    // Copy these buttons into the supplied list
    buttons.clear();
    buttons.push_back(dpadImage);
    buttons.push_back(bButtonBackground);
    buttons.push_back(bButton);
    buttons.push_back(aButtonBackground);
    buttons.push_back(aButton);
    buttons.push_back(selectButtonBackground);
    buttons.push_back(selectButton);
    buttons.push_back(startButtonBackground);
    buttons.push_back(startButton);
    buttons.push_back(slowerButton);
    buttons.push_back(fasterButton);
    buttons.push_back(loadSaveStateButton);
    buttons.push_back(saveSaveStateButton);
}