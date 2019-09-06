#pragma once

#include "uidefs.h"
#include "renderconfig.h"

#include <vector>
#include <string>

class Font;
class TextureShader;
class FontShader;
class Resource;
class PlatformRenderer;

// Base UI element
class UIBase {
    bool coordsAreSet;
    Rect actualCoords;

protected:
    UIBase(Size& size, Rect& margins, int backgroundTexture, Rect& textureBounds, Gravity verticalGravity, Gravity horizontalGravity);
    void setActualCoords(float left, float right, float top, float bottom);

public:
    Size size{};
    Rect margins{};
    int backgroundTexture;
    Rect textureBounds{};
    Gravity verticalGravity, horizontalGravity;
    float fittingScaleFactor;
    float screenAspectUsed;

    bool containsCoords(float xUnits, float yUnits);
    unsigned int compassFromCentre(float xUnits, float yUnits);
};

// Text buttons
class TextButtonSetRenderConfigs {
public:
    RenderConfig textConfig;
    RenderConfig outlineConfig;
};

class TextButton : public UIBase {
    std::vector<float> makeFloatData(Resource* fontResource, float screenAspect, bool includeNormals);
public:
    TextButton(
            int backgroundTexture, int fontTexture,
            Font* font, const char* label,
            float textHeightFraction, Size& size,
            Rect& margins, Rect& textureBounds,
            Gravity verticalGravity, Gravity horizontalGravity);
    static TextButtonSetRenderConfigs createFrameConfigs(
            std::vector<TextButton>& buttons, TextureShader& basicTextureShader, FontShader& fontShader, PlatformRenderer& rendererPlatform,
            int bgTexture, int fontTexture, int bgVbo, int fontVbo,
            float r, float g, float b);
    int fontTexture;
    Font* font;
    std::string label;
    float textHeightFraction;
    static std::vector<float> makeAllFloats(std::vector<TextButton>& buttons, Resource* fontResource, float screenAspect, bool includeNormals);
    static size_t getTextVertexCount(std::vector<TextButton>& buttons);
};

// Images
class Image : public UIBase {
    std::vector<float> makeFloatData(float screenAspect, bool includeNormals);
public:
    Image(
            int backgroundTexture, Size& size,
            Rect& margins, Rect& textureBounds,
            Gravity verticalGravity, Gravity horizontalGravity);
    static RenderConfig createFrameConfig(std::vector<Image>& images, TextureShader& shader, PlatformRenderer& rendererPlatform, int bgTexture, int vbo);
    static std::vector<float> makeAllFloats(std::vector<Image>& images, float screenAspect, bool includeNormals);
};
