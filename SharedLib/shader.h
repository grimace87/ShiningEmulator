#pragma once

#include <cstddef>

class RenderConfig;
class PlatformRenderer;
class AppPlatform;

class Shader {
public:
    unsigned int programObject = 0;
    virtual void prepareFrame(PlatformRenderer& rendererPlatform, RenderConfig& config) = 0;
    virtual size_t getStrideElements() = 0;
};

class TextureShader : public Shader {
public:
    int attPosition;
    int attTextureCoords;
    int uniMvpMatrix;
    int uniTexture;
    static void prepareConfig(RenderConfig& config, int activeTexture, const float* mvpMatrix);
    void prepareFrame(PlatformRenderer& rendererPlatform, RenderConfig& config) override;
    size_t getStrideElements() override;
    TextureShader(AppPlatform& platform, PlatformRenderer& rendererPlatform);
};

class NormalTextureShader : public TextureShader {
public:
    int attNormal;
    int uniMvMatrix;
    static void prepareConfig(RenderConfig& config, int activeTexture, const float* mvpMatrix, const float* mvMatrix);
    void prepareFrame(PlatformRenderer& rendererPlatform, RenderConfig& config) override;
    size_t getStrideElements() override;
    NormalTextureShader(AppPlatform& platform, PlatformRenderer& rendererPlatform);
};

class FontShader : public TextureShader {
public:
    int uniTextColor;
    static void prepareConfig(RenderConfig& config, int activeTexture, const float* mvpMatrix, float r, float g, float b);
    void prepareFrame(PlatformRenderer& rendererPlatform, RenderConfig& config) override;
    size_t getStrideElements() override;
    FontShader(AppPlatform& platform, PlatformRenderer& rendererPlatform);
};
