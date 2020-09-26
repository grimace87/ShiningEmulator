#pragma once

#include "../SharedLib/platformrenderer.h"

#include <Windows.h>
#include <wincodec.h>

class WindowsRenderer : public PlatformRenderer {
    HDC hDC;
    HGLRC hRC;
    IWICImagingFactory* imageFactory;
    bool initOpenGL();

protected:
    // Renderer
    bool createContext() override;
    void destroyContext() override;
    void verifyNoError() override;
    unsigned int createShader(const char* vertexShaderSource, int vertexSourceLength, const char* fragmentShaderSource, int fragmentSourceLength) override;
    int queryAttribLocation(unsigned int program, const char* attribName) override;
    int queryUniformLocation(unsigned int program, const char* uniformName) override;
    unsigned int createTexture(int textureFormat, const unsigned char* rawData, size_t imageWidth, size_t imageHeight, bool useLinearMagFilter) override;
    unsigned int createVbo(float* data, size_t length) override;
    void configureAttribute(int attributeLayout, int strideElements, int index, int size) override;
    void setUniform1i(int layout, int value) override;
    void setUniform3f(int layout, float* values) override;
    void setUniformMatrix4f(int layout, float* values) override;
    void subTexture(unsigned int texture, int textureFormat, int x, int y, int width, int height, const unsigned char* rawData) override;
    void renderPass(std::vector<FrameConfig>& configs, unsigned int firstIndex, unsigned int count) override;
    void resizeDisplay(int width, int height) override;

public:
    WindowsRenderer(HDC hDC, int width, int height);
};
