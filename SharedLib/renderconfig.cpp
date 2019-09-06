#include "renderconfig.h"

#include "shader.h"

RenderConfig RenderConfig::makeTextConfig(
        FontShader& shader, PlatformRenderer& rendererPlatform, int texture, int vbo,
        int firstVertex, int vertexCount,
        float r, float g, float b) {
    RenderConfig renderConfig;
    renderConfig.texture = texture;
    renderConfig.vbo = vbo;
    renderConfig.startVertex = firstVertex;
    renderConfig.vertexCount = vertexCount;
    renderConfig.strideBytes = shader.getStrideElements() * sizeof(float);
    renderConfig.uniform1is.push_back(0);
    renderConfig.uniform3fs.push_back(r);
    renderConfig.uniform3fs.push_back(g);
    renderConfig.uniform3fs.push_back(b);
    renderConfig.uniformMat4s.resize(16);
    return renderConfig;
}

RenderConfig RenderConfig::makeStandardConfig(
        TextureShader& shader, PlatformRenderer& rendererPlatform, int texture, int vbo,
        int firstVertex, int vertexCount) {
    RenderConfig renderConfig;
    renderConfig.texture = texture;
    renderConfig.vbo = vbo;
    renderConfig.startVertex = firstVertex;
    renderConfig.vertexCount = vertexCount;
    renderConfig.strideBytes = shader.getStrideElements() * sizeof(float);
    renderConfig.uniform1is.push_back(0);
    renderConfig.uniformMat4s.resize(16);
    return renderConfig;
}

RenderConfig RenderConfig::makeStandardNormalConfig(
        NormalTextureShader& shader, PlatformRenderer& rendererPlatform, int texture, int vbo,
        int firstVertex, int vertexCount) {
    RenderConfig renderConfig;
    renderConfig.texture = texture;
    renderConfig.vbo = vbo;
    renderConfig.startVertex = firstVertex;
    renderConfig.vertexCount = vertexCount;
    renderConfig.strideBytes = shader.getStrideElements() * sizeof(float);
    renderConfig.uniform1is.push_back(0);
    renderConfig.uniformMat4s.resize(32);
    return renderConfig;
}
