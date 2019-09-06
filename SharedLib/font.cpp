#include "font.h"

#include "fontdefs.h"
#include "resource.h"
#include <sstream>

Glyph::Glyph() {
    textureS = 0.0f;
    textureT = 0.0f;
    offsetX = 0.0f;
    offsetY = 0.0f;
    width = 0.0f;
    height = 0.0f;
    advanceX = 0.0f;
}

Font::Font(float baseHeight, float lineHeight, std::vector<Glyph>&& glyphs) :
        baseHeight(baseHeight), lineHeight(lineHeight), glyphs(glyphs) { }

Font::Font() :
        baseHeight(1.0f), lineHeight(1.0f), glyphs() { }

Font Font::fromResource(Resource* resource) {
    // Create the buffer
    auto glyphSet = std::vector<Glyph>();
    glyphSet.resize(FONT_TEXTURE_GLYPH_COUNT);

    // Create a stream around this resource
    auto inputString = std::string((char*)resource->rawStream, resource->rawDataLength);
    std::istringstream stream(inputString);

    // Search for "base=XX" and "lineHeight=XX" components before "chars count=XX"
    std::string keyBase = "base";
    std::string keyLineHeight = "lineHeight";
    std::string keyCharCount = "count";
    int valBase = 0, valLineHeight = 0;
    while (!stream.eof()) {
        // Read each component of the format "key=xx"
        std::string item;
        stream >> item;

        // Look for an equals sign ignore items not containing it
        auto equalsPos = item.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }
        std::string key = item.substr(0, equalsPos);

        // Check for needed items
        if (key == keyBase) {
            valBase = std::stoi(item.substr(equalsPos + 1));
        } else if (key == keyLineHeight) {
            valLineHeight = std::stoi(item.substr(equalsPos + 1));
        } else if (key == keyCharCount) {
            break;
        }
    }

    // Parse each character line
    std::string keyNewChar = "char";
    std::string keyId = "id";
    std::string keyX = "x";
    std::string keyY = "y";
    std::string keyWidth = "width";
    std::string keyHeight = "height";
    std::string keyOffsetX = "xoffset";
    std::string keyOffsetY = "yoffset";
    std::string keyAdvance = "xadvance";
    int valId = -1;
    int valTextureS = 0, valTextureT = 0, valWidth = 0, valHeight = 0, valOffsetX = 0, valOffsetY = 0, valAdvance = 0;
    while (!stream.eof()) {
        // Read each component of the format "key=xx"
        std::string item;
        stream >> item;

        // Save where a new "char" identifier is encountered
        if (item == keyNewChar) {
            if (valId >= 0 && valId < FONT_TEXTURE_GLYPH_COUNT) {
                Glyph glyph;
                glyph.textureS = valTextureS;
                glyph.textureT = valTextureT;
                glyph.width = valWidth;
                glyph.height = valHeight;
                glyph.offsetX = valOffsetX;
                glyph.offsetY = valOffsetY;
                glyph.advanceX = valAdvance;
                glyphSet[valId] = glyph;
            }
            continue;
        }

        // Read the value of any other item
        auto equalsPos = item.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }
        std::string key = item.substr(0, equalsPos);
        int value = std::stoi(item.substr(equalsPos + 1));
        if (key == keyId) {
            valId = value;
        } else if (key == keyX) {
            valTextureS = value;
        } else if (key == keyY) {
            valTextureT = value;
        } else if (key == keyWidth) {
            valWidth = value;
        } else if (key == keyHeight) {
            valHeight = value;
        } else if (key == keyOffsetX) {
            valOffsetX = value;
        } else if (key == keyOffsetY) {
            valOffsetY = value;
        } else if (key == keyAdvance) {
            valAdvance = value;
        }
    }

    // Return set
    return Font((float)valBase, (float)valLineHeight, std::move(glyphSet));
}
