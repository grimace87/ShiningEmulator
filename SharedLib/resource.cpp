#include "resource.h"

#include "font.h"
#include "fontdefs.h"

#define LODEPNG_NO_COMPILE_ENCODER
#define LODEPNG_NO_COMPILE_DISK
#include <lodepng/lodepng.h>

void copyIntoBitmap(const unsigned char* src, int srcWidth, int srcHeight, unsigned char* dst, int dstWidth, int dstX, int dstY);

Resource::~Resource() = default;

std::string Resource::getText() {
    if (rawStream) {
        return std::string((const char*)rawStream, rawDataLength);
    } else {
        return std::string();
    }
}

PngImage Resource::getPng() {
    if (rawStream) {
        // Use LodePNG library to decode
        unsigned int width, height;
        unsigned char* data;
        lodepng_decode32(&data, &width, &height, rawStream, rawDataLength);
        return { data, width, height };
    } else {
        return { nullptr, 0, 0 };
    }
}

void copyIntoBitmap(const unsigned char* src, int srcWidth, int srcHeight, unsigned char* dst, int dstWidth, int dstX, int dstY) {
    const unsigned char* srcPtr = src;
    unsigned char* dstPtr = dst + dstX + (dstY * dstWidth);
    for (int row = 0; row < srcHeight; row++) {
        memcpy(dstPtr, srcPtr, srcWidth);
        srcPtr += srcWidth;
        dstPtr += dstWidth;
    }
}

struct VertexPT {
    float x = 0.0f, y = 0.0f, z = 0.0f, s = 0.0f, t = 0.0f;
    inline void set(float _x, float _y, float _s, float _t) {
        x = _x;
        y = _y;
        s = _s;
        t = _t;
    }
};

struct QuadPT {
    VertexPT vertices[6];
} quadPT;

struct VertexPNT {
    float x = 0.0f, y = 0.0f, z = 0.0f, nx = 0.0f, ny = 0.0f, nz = 1.0f, s = 0.0f, t = 0.0f;
    inline void set(float _x, float _y, float _s, float _t) {
        x = _x;
        y = _y;
        s = _s;
        t = _t;
    }
};

struct QuadPNT {
    VertexPNT vertices[6];
} quadPNT;

std::vector<float> Resource::generateTextVbo(std::string& textToRender, Font& font, float left, float top, float boxWidth, float boxHeight, float lines, bool generateNormals, float scale) {
    // Assign buffer, with 6 vertices per character and 5 or 8 floats per vertex
    const size_t floatsPerVertex = generateNormals ? 8U : 5U;
    size_t floatCount = textToRender.length() * floatsPerVertex * 6;
    std::vector<float> floats;
    floats.resize(floatCount);

    // Find scaling factor
    const float lineHeightUnits = boxHeight / lines;
    const float unitsPerPixel = scale * lineHeightUnits / font.lineHeight;

    // Start building the buffer
    int charsRendered = 0;
    float penX = left;
    float penY = top - (float)font.baseHeight * unitsPerPixel;
    float xMin, xMax, yMin, yMax;
    float sMin, sMax, tMin, tMax;
    for (char c : textToRender) {
        Glyph& glyph = font.glyphs.at(c);

        xMin = penX + (float)glyph.offsetX * unitsPerPixel;
        xMax = xMin + (float)glyph.width * unitsPerPixel;
        yMax = penY + (float)(font.baseHeight - glyph.offsetY) * unitsPerPixel;
        yMin = yMax - (float)glyph.height * unitsPerPixel;

        sMin = glyph.textureS / FONT_TEXTURE_SIZE;
        sMax = sMin + glyph.width / FONT_TEXTURE_SIZE;
        tMin = glyph.textureT / FONT_TEXTURE_SIZE;
        tMax = tMin + glyph.height / FONT_TEXTURE_SIZE;

        if (generateNormals) {
            quadPNT.vertices[0].set(xMin, yMax, sMin, tMin);
            quadPNT.vertices[1].set(xMin, yMin, sMin, tMax);
            quadPNT.vertices[2].set(xMax, yMin, sMax, tMax);
            quadPNT.vertices[3].set(xMax, yMin, sMax, tMax);
            quadPNT.vertices[4].set(xMax, yMax, sMax, tMin);
            quadPNT.vertices[5].set(xMin, yMax, sMin, tMin);
            memcpy((void*)(&floats.front() + charsRendered * 48), (void*)&quadPNT, 48 * sizeof(float));

            penX += (float)glyph.advanceX * unitsPerPixel;
            if ((penX + (float)font.lineHeight * unitsPerPixel) > boxWidth) {
                penX = left;
                penY -= (float)font.lineHeight * unitsPerPixel;
            }
            charsRendered++;
        } else {
            quadPT.vertices[0].set(xMin, yMax, sMin, tMin);
            quadPT.vertices[1].set(xMin, yMin, sMin, tMax);
            quadPT.vertices[2].set(xMax, yMin, sMax, tMax);
            quadPT.vertices[3].set(xMax, yMin, sMax, tMax);
            quadPT.vertices[4].set(xMax, yMax, sMax, tMin);
            quadPT.vertices[5].set(xMin, yMax, sMin, tMin);
            memcpy((void*)(&floats.front() + charsRendered * 30), (void*)&quadPT, 30 * sizeof(float));

            penX += (float)glyph.advanceX * unitsPerPixel;
            if ((penX + (float)font.lineHeight * unitsPerPixel) > boxWidth) {
                penX = left;
                penY -= (float)font.lineHeight * unitsPerPixel;
            }
            charsRendered++;
        }

    }

    return floats;
}

PngImage::~PngImage() {
    delete pixelData;
}
