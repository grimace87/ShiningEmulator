#pragma once

#include <vector>

class PlatformRenderer;
class Shader;
class FontShader;
class TextureShader;
class NormalTextureShader;

struct RenderConfig {
    unsigned int texture;
    unsigned int vbo;
    size_t strideBytes;
    unsigned int startVertex;
    unsigned int vertexCount;
    std::vector<int> uniform1is;
    std::vector<float> uniform3fs;
    std::vector<float> uniformMat4s;
    static RenderConfig makeTextConfig(
            FontShader& shader, PlatformRenderer& rendererPlatform,
            int texture, int vbo,
            int firstVertex, int vertexCount,
            float r, float g, float b);
    static RenderConfig makeStandardConfig(
            TextureShader& shader, PlatformRenderer& rendererPlatform,
            int texture, int vbo,
            int firstVertex, int vertexCount);
    static RenderConfig makeStandardNormalConfig(
            NormalTextureShader& shader, PlatformRenderer& rendererPlatform,
            int texture, int vbo,
            int firstVertex, int vertexCount);
};

struct FrameConfig {
    Shader* shader;
    std::vector<RenderConfig> renderConfigs;
};
