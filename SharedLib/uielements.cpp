#include <math.h>
#include "uielements.h"

#include "font.h"
#include "resource.h"

void putVertex(float* buffer, off_t index, float x, float y, float s, float t, bool includeNormals) {
    buffer[index] = x;
    buffer[index + 1] = y;
    buffer[index + 2] = 0.0f;
    if (includeNormals) {
        buffer[index + 3] = 0.0f;
        buffer[index + 4] = 0.0f;
        buffer[index + 5] = 1.0f;
        buffer[index + 6] = s;
        buffer[index + 7] = t;
    } else {
        buffer[index + 3] = s;
        buffer[index + 4] = t;
    }
}

UIBase::UIBase(Size& size, Rect& margins, int backgroundTexture, Rect& textureBounds, Gravity verticalGravity, Gravity horizontalGravity):
        size { size },
        margins { margins },
        backgroundTexture { backgroundTexture },
        textureBounds { textureBounds },
        verticalGravity { verticalGravity },
        horizontalGravity { horizontalGravity },
        coordsAreSet { false },
        fittingScaleFactor { 1.0f },
        screenAspectUsed { 1.0f },
        actualCoords { } {
}

void UIBase::setActualCoords(float left, float right, float top, float bottom) {
    this->actualCoords.left = left;
    this->actualCoords.right = right;
    this->actualCoords.top = top;
    this->actualCoords.bottom = bottom;
    this->coordsAreSet = true;
}

bool UIBase::containsCoords(float xUnits, float yUnits) {
    float transformedX = xUnits * screenAspectUsed;
    if (coordsAreSet) {
        return (transformedX > actualCoords.left) && (transformedX < actualCoords.right) && (yUnits > actualCoords.bottom) && (yUnits < actualCoords.top);
    } else {
        return false;
    }
}

unsigned int UIBase::compassFromCentre(float xUnits, float yUnits) {
    float transformedX = xUnits * screenAspectUsed;
    const float PI_6 = 0.52359877559f;
    const float PI_3 = 1.0471975512f;
    const float PI_2_3 = 2.0943951023f;
    const float PI_5_6 = 2.61799387799f;
    float centreX = actualCoords.left + 0.5f * (actualCoords.right - actualCoords.left);
    float centreY = actualCoords.bottom + 0.5f * (actualCoords.top - actualCoords.bottom);
    float angle = atan2f(yUnits - centreY, transformedX - centreX);

    unsigned int mask = 0;
    if (angle > PI_6 && angle < PI_5_6) {
        mask |= (unsigned int)CompassMask::NORTH;
    }
    if (angle < PI_3 && angle > -PI_3) {
        mask |= (unsigned int)CompassMask::EAST;
    }
    if (angle < -PI_6 && angle > -PI_5_6) {
        mask |= (unsigned int)CompassMask::SOUTH;
    }
    if (angle > PI_2_3 || angle < -PI_2_3) {
        mask |= (unsigned int)CompassMask::WEST;
    }
    return mask;
}

TextButton::TextButton(
        int backgroundTexture, int fontTexture,
        Font* font, const char* label,
        float textHeightFraction, Size& size,
        Rect& margins, Rect& textureBounds,
        Gravity verticalGravity, Gravity horizontalGravity
) : UIBase(size, margins, backgroundTexture, textureBounds, verticalGravity, horizontalGravity),
        fontTexture { fontTexture },
        font { font },
        label { label },
        textHeightFraction { textHeightFraction } {
}

std::vector<float> TextButton::makeFloatData(Resource* fontResource, float screenAspect, bool includeNormals) {
    // Allocate space
    const size_t floatsPerVertex = includeNormals ? 8U : 5U;
    const size_t noOfFloats = 6 * floatsPerVertex * (1 + label.size());
    std::vector<float> buffer;
    buffer.resize(noOfFloats);

    // Scale all sizes to fit the smaller screen dimension across the range of (-1, 1)
    screenAspectUsed = screenAspect;
    if (screenAspect > 1.0f) {
        screenAspect = 1.0f / screenAspect;
    }

    // Get top and bottom edges
    float top, bottom;
    switch (verticalGravity) {
        case Gravity::START:
            top = 1.0f - fittingScaleFactor * margins.top;
            bottom = top - fittingScaleFactor * size.height;
            break;
        case Gravity::MIDDLE:
            top = 0.0f + fittingScaleFactor * (0.5f * size.height - margins.top + margins.bottom);
            bottom = top - fittingScaleFactor * size.height;
            break;
        case Gravity::END:
            bottom = -1.0f + fittingScaleFactor * margins.bottom;
            top = bottom + fittingScaleFactor * size.height;
            break;
    }

    // Get left and right edges
    float left, right;
    switch (horizontalGravity) {
        case Gravity::START:
            left = -screenAspect + fittingScaleFactor * margins.left;
            right = left + fittingScaleFactor * size.width;
            break;
        case Gravity::MIDDLE:
            left = 0.0f + fittingScaleFactor * (-0.5f * size.width + margins.left - margins.right);
            right = left + fittingScaleFactor * size.width;
            break;
        case Gravity::END:
            right = screenAspect - fittingScaleFactor * margins.right;
            left = right - fittingScaleFactor * size.width;
            break;
    }

    // Save actual coordinates
    setActualCoords(left, right, top, bottom);

    // Add data for the outline
    off_t offset = 0;
    float *const bufferPtr = &buffer.front();
    putVertex(bufferPtr, offset, left, bottom, textureBounds.left, textureBounds.bottom, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, right, bottom, textureBounds.right, textureBounds.bottom, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, right, top, textureBounds.right, textureBounds.top, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, right, top, textureBounds.right, textureBounds.top, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, left, top, textureBounds.left, textureBounds.top, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, left, bottom, textureBounds.left, textureBounds.bottom, includeNormals);

    // Figure out how wide the text ought to be
    const float maxGlyphHeight = textHeightFraction * size.height;
    const float glyphScalingFactor = maxGlyphHeight / (float)font->lineHeight;
    float labelWidthUnits = 0.0f;
    for (char c : label) {
        Glyph& glyph = font->glyphs.at(c);
        labelWidthUnits += glyphScalingFactor * (float)glyph.advanceX;
    }

    // Add a rectangle for each character in the label string, add to the complete buffer
    float padSides = size.width / 16.0f;
    const float padToppom = size.height * (1.0f - textHeightFraction) * 0.5f;
    float drawnWidth, scaleFactor;
    if (size.width - 2 * padSides < labelWidthUnits) {
        drawnWidth = size.width - 2 * padSides;
        scaleFactor = drawnWidth / labelWidthUnits;
    } else {
        drawnWidth = labelWidthUnits;
        padSides = (size.width - drawnWidth) / 2.0f;
        scaleFactor = 1.0f;
    }
    std::vector<float> textFloats = fontResource->generateTextVbo(
            label,
            *font,
            left + padSides,
            top - padToppom,
            drawnWidth,
            size.height - 2 * padToppom,
            1,
            includeNormals,
            scaleFactor
    );
    const size_t textFloatCount = textFloats.size();
    const size_t buttonOutlineFloats = 6 * floatsPerVertex;
    for (size_t i = 0; i < textFloatCount; i++) {
        buffer[i + buttonOutlineFloats] = textFloats[i];
    }

    // Return all data
    return buffer;
}

std::vector<float> TextButton::makeAllFloats(std::vector<TextButton>& buttons, Resource* fontResource, float screenAspect, bool includeNormals) {
    std::vector<float> allFloats;
    std::vector<float> textFloats;
    const int floatsPerVertex = includeNormals ? 8 : 5;
    for (TextButton& button : buttons) {
        std::vector<float> buttonFloats = button.makeFloatData(fontResource, screenAspect, includeNormals);
        // Accumulate all text floats for now, collect button outline floats separately to be appended later
        allFloats.insert(allFloats.end(), buttonFloats.begin(), buttonFloats.begin() + 6 * floatsPerVertex);
        textFloats.insert(textFloats.end(), buttonFloats.begin() + 6 * floatsPerVertex, buttonFloats.end());
    }
    allFloats.insert(allFloats.end(), textFloats.begin(), textFloats.end());
    return allFloats;
}

TextButtonSetRenderConfigs TextButton::createFrameConfigs(
        std::vector<TextButton>& buttons, TextureShader& basicTextureShader, FontShader& fontShader, PlatformRenderer& rendererPlatform,
        int bgTexture, int fontTexture, int bgVbo, int fontVbo,
        float r, float g, float b) {
    // Make the frame configs
    const size_t textVertexCount = TextButton::getTextVertexCount(buttons);
    const size_t buttonOutlineVertexCount = 6U * buttons.size();
    return {
            RenderConfig::makeTextConfig(fontShader, rendererPlatform, fontTexture, fontVbo, 0, textVertexCount, r, g, b),
            RenderConfig::makeStandardConfig(basicTextureShader, rendererPlatform, bgTexture, bgVbo, 0, buttonOutlineVertexCount)
    };
}

size_t TextButton::getTextVertexCount(std::vector<TextButton>& buttons) {
    size_t textFloats = 0;
    for (auto& button : buttons) {
        textFloats += 6U * button.label.size();
    }
    return textFloats;
}

Image::Image(
        int backgroundTexture, Size& size,
        Rect& margins, Rect& textureBounds,
        Gravity verticalGravity, Gravity horizontalGravity
) : UIBase(size, margins, backgroundTexture, textureBounds, verticalGravity, horizontalGravity) { }

std::vector<float> Image::makeFloatData(float screenAspect, bool includeNormals) {
    // Allocate space
    const size_t floatsPerVertex = includeNormals ? 8U : 5U;
    const size_t noOfFloats = 6U * floatsPerVertex;
    std::vector<float> buffer;
    buffer.resize(noOfFloats);

    // Scale all sizes to fit the smaller screen dimension across the range of (-1, 1)
    screenAspectUsed = screenAspect;
    fittingScaleFactor = screenAspect < 1.0f ? screenAspect : fittingScaleFactor = 1.0f;

    // Get top and bottom edges
    float top, bottom;
    switch (verticalGravity) {
        case Gravity::START:
            top = 1.0f - fittingScaleFactor * margins.top;
            bottom = top - fittingScaleFactor * size.height;
            break;
        case Gravity::MIDDLE:
            top = 0.0f + fittingScaleFactor * (0.5f * size.height - margins.top + margins.bottom);
            bottom = top - fittingScaleFactor * size.height;
            break;
        case Gravity::END:
            bottom = -1.0f + fittingScaleFactor * margins.bottom;
            top = bottom + fittingScaleFactor * size.height;
            break;
    }

    // Get left and right edges
    float left, right;
    switch (horizontalGravity) {
        case Gravity::START:
            left = -screenAspect + fittingScaleFactor * margins.left;
            right = left + fittingScaleFactor * size.width;
            break;
        case Gravity::MIDDLE:
            left = 0.0f + fittingScaleFactor * (-0.5f * size.width + margins.left - margins.right);
            right = left + fittingScaleFactor * size.width;
            break;
        case Gravity::END:
            right = screenAspect - fittingScaleFactor * margins.right;
            left = right - fittingScaleFactor * size.width;
            break;
    }

    // Save actual coordinates
    setActualCoords(left, right, top, bottom);

    // Add data for the image
    off_t offset = 0;
    float *const bufferPtr = &buffer.front();
    putVertex(bufferPtr, offset, left, bottom, textureBounds.left, textureBounds.bottom, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, right, bottom, textureBounds.right, textureBounds.bottom, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, right, top, textureBounds.right, textureBounds.top, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, right, top, textureBounds.right, textureBounds.top, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, left, top, textureBounds.left, textureBounds.top, includeNormals);
    offset += floatsPerVertex;
    putVertex(bufferPtr, offset, left, bottom, textureBounds.left, textureBounds.bottom, includeNormals);

    // Return all data
    return buffer;
}

std::vector<float> Image::makeAllFloats(std::vector<Image>& images, float screenAspect, bool includeNormals) {
    std::vector<float> allFloats;
    const size_t floatsPerVertex = includeNormals ? 8U : 5U;
    for (Image& image : images) {
        std::vector<float> thisImageFloats = image.makeFloatData(screenAspect, includeNormals);
        allFloats.insert(allFloats.end(), thisImageFloats.begin(), thisImageFloats.begin() + 6U * floatsPerVertex);
    }
    return allFloats;
}

RenderConfig Image::createFrameConfig(std::vector<Image>& images, TextureShader& shader, PlatformRenderer& rendererPlatform, int bgTexture, int vbo) {
    // Make the frame configs
    const size_t buttonOutlineVertexCount = 6U * images.size();
    return RenderConfig::makeStandardConfig(shader, rendererPlatform, bgTexture, vbo, 0, buttonOutlineVertexCount);
}
