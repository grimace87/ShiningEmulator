#pragma once

#include <vector>

class Resource;

class Glyph {
public:
    float textureS;
    float textureT;
    float offsetX;
    float offsetY;
    float width;
    float height;
    float advanceX;
    Glyph();
};

class Font {
    Font(float baseHeight, float lineHeight, std::vector<Glyph>&& glyphs);
public:
    float baseHeight;
    float lineHeight;
    std::vector<Glyph> glyphs;
    Font();
    static Font fromResource(Resource* resource);
};
