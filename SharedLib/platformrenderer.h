#pragma once

#include <vector>

struct FrameConfig;

class PlatformRenderer {
public:
    int canvasWidth = 0;
    int canvasHeight = 0;
    virtual bool createContext() = 0;
    virtual void destroyContext() = 0;
    virtual void verifyNoError() = 0;
    virtual unsigned int createShader(const char* vertexShaderSource, int vertexSourceLength, const char* fragmentShaderSource, int fragmentSourceLength) = 0;
    virtual int queryAttribLocation(unsigned int program, const char* attribName) = 0;
    virtual int queryUniformLocation(unsigned int program, const char* uniformName) = 0;
    virtual unsigned int createTexture(int textureFormat, const unsigned char* rawData, size_t imageWidth, size_t imageHeight, bool useLinearMagFilter) = 0;
    virtual unsigned int createVbo(float* data, size_t length) = 0;
    virtual void configureAttribute(int attributeLayout, int strideElements, int index, int size) = 0;
    virtual void setUniform1i(int layout, int value) = 0;
    virtual void setUniform3f(int layout, float* values) = 0;
    virtual void setUniformMatrix4f(int layout, float* values) = 0;
    virtual void subTexture(unsigned int texture, int textureFormat, int x, int y, int width, int height, const unsigned char* rawData) = 0;
    virtual void renderPass(std::vector<FrameConfig>& configs, unsigned int firstIndex, unsigned int count) = 0;
    virtual void resizeDisplay(int width, int height) = 0;
};
