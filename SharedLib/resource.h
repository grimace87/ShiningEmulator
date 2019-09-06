#pragma once

#include <string>
#include <vector>

class Font;

#define TEXTURE_FORMAT_RGBA 1
#define TEXTURE_FORMAT_GREYSCALE 2
#define TEXTURE_FORMAT_BGRA_IF_AVAIL 3

class PngImage {
public:
    unsigned char* pixelData;
    size_t width;
    size_t height;
    ~PngImage();
};

class Resource {
public:
    std::string getText();
    PngImage getPng();
    static std::vector<float> generateTextVbo(std::string& textToRender, Font& font, float left, float top, float boxWidth, float boxHeight, float lines, bool generateNormals, float scale);
    virtual ~Resource();
    std::string fileName;
    size_t rawDataLength;
    const unsigned char* rawStream = nullptr;
};
