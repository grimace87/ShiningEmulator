#include "shader.h"

#include <memory>

#include "renderconfig.h"
#include "appplatform.h"
#include "platformrenderer.h"
#include "resource.h"

size_t TextureShader::getStrideElements() {
    return 5;
}

TextureShader::TextureShader(AppPlatform& platform, PlatformRenderer& rendererPlatform) {
    // Compile the shader
    std::unique_ptr<Resource> vertexResource(platform.getResource("vs_main.glsl", true, true));
    std::string vertexSource = vertexResource->getText();
    std::unique_ptr<Resource> fragResource(platform.getResource("fs_main.glsl", true, true));
    std::string fragSource = fragResource->getText();
    programObject = rendererPlatform.createShader(vertexSource.c_str(), vertexSource.length(), fragSource.c_str(), fragSource.length());

    // Assign attributes and uniforms
    attPosition = rendererPlatform.queryAttribLocation(programObject, "aPosition");
    attTextureCoords = rendererPlatform.queryAttribLocation(programObject, "aTexCoord");
    uniTexture = rendererPlatform.queryUniformLocation(programObject, "uMainTex");
    uniMvpMatrix = rendererPlatform.queryUniformLocation(programObject, "uMatMVP");
}

void TextureShader::prepareConfig(RenderConfig& config, int activeTexture, const float *const mvpMatrix) {
    config.uniform1is[0] = activeTexture;
    for (int i = 0; i < 16; i++) {
        config.uniformMat4s[i] = mvpMatrix[i];
    }
}

void TextureShader::prepareFrame(PlatformRenderer& rendererPlatform, RenderConfig& config) {
    rendererPlatform.configureAttribute(attPosition, 5, 0, 3);
    rendererPlatform.configureAttribute(attTextureCoords, 5, 3, 2);
    rendererPlatform.setUniform1i(uniTexture, config.uniform1is[0]);
    rendererPlatform.setUniformMatrix4f(uniMvpMatrix, &config.uniformMat4s.front());
}

size_t NormalTextureShader::getStrideElements() {
    return 8;
}

NormalTextureShader::NormalTextureShader(AppPlatform& platform, PlatformRenderer& rendererPlatform) :
        TextureShader(platform, rendererPlatform) {

    // Compile the shader
    std::unique_ptr<Resource> vertexResource(platform.getResource("vs_normal.glsl", true, true));
    std::string vertexSource = vertexResource->getText();
    std::unique_ptr<Resource> fragResource(platform.getResource("fs_normal.glsl", true, true));
    std::string fragSource = fragResource->getText();
    programObject = rendererPlatform.createShader(vertexSource.c_str(), vertexSource.length(), fragSource.c_str(), fragSource.length());

    // Assign attributes and uniforms
    attPosition = rendererPlatform.queryAttribLocation(programObject, "aPosition");
    attNormal = rendererPlatform.queryAttribLocation(programObject, "aNormal");
    attTextureCoords = rendererPlatform.queryAttribLocation(programObject, "aTexCoord");
    uniTexture = rendererPlatform.queryUniformLocation(programObject, "uMainTex");
    uniMvpMatrix = rendererPlatform.queryUniformLocation(programObject, "uMatMVP");
    uniMvMatrix = rendererPlatform.queryUniformLocation(programObject, "uMatMV");
}

void NormalTextureShader::prepareConfig(RenderConfig& config, int activeTexture, const float *const mvpMatrix, const float *const mvMatrix) {
    config.uniform1is[0] = activeTexture;
    for (int i = 0; i < 16; i++) {
        config.uniformMat4s[i] = mvpMatrix[i];
    }
    for (int i = 16; i < 32; i++) {
        config.uniformMat4s[i] = mvMatrix[i - 16];
    }
}

void NormalTextureShader::prepareFrame(PlatformRenderer& rendererPlatform, RenderConfig& config) {
    rendererPlatform.configureAttribute(attPosition, 8, 0, 3);
    rendererPlatform.configureAttribute(attNormal, 8, 3, 3);
    rendererPlatform.configureAttribute(attTextureCoords, 8, 6, 2);
    rendererPlatform.setUniform1i(uniTexture, config.uniform1is[0]);
    rendererPlatform.setUniformMatrix4f(uniMvpMatrix, &config.uniformMat4s.front());
    rendererPlatform.setUniformMatrix4f(uniMvMatrix, &config.uniformMat4s.front() + 16);
}

size_t FontShader::getStrideElements() {
    return 5;
}

FontShader::FontShader(AppPlatform& platform, PlatformRenderer& rendererPlatform) :
        TextureShader(platform, rendererPlatform) {

    // Compile the shader
    std::unique_ptr<Resource> vertexResource(platform.getResource("vs_main.glsl", true, true));
    std::string vertexSource = vertexResource->getText();
    std::unique_ptr<Resource> fragResource(platform.getResource("fs_text.glsl", true, true));
    std::string fragSource = fragResource->getText();
    programObject = rendererPlatform.createShader(vertexSource.c_str(), vertexSource.length(), fragSource.c_str(), fragSource.length());

    // Assign attributes and uniforms
    attPosition = rendererPlatform.queryAttribLocation(programObject, "aPosition");
    attTextureCoords = rendererPlatform.queryAttribLocation(programObject, "aTexCoord");
    uniTexture = rendererPlatform.queryUniformLocation(programObject, "uMainTex");
    uniMvpMatrix = rendererPlatform.queryUniformLocation(programObject, "uMatMVP");
    uniTextColor = rendererPlatform.queryUniformLocation(programObject, "uTextColour");
}

void FontShader::prepareConfig(RenderConfig& config, int activeTexture, const float *const mvpMatrix, float r, float g, float b) {
    config.uniform1is[0] = activeTexture;
    config.uniform3fs[0] = r;
    config.uniform3fs[1] = g;
    config.uniform3fs[2] = b;
    for (int i = 0; i < 16; i++) {
        config.uniformMat4s[i] = mvpMatrix[i];
    }
}

void FontShader::prepareFrame(PlatformRenderer& rendererPlatform, RenderConfig& config) {
    rendererPlatform.configureAttribute(attPosition, 5, 0, 3);
    rendererPlatform.configureAttribute(attTextureCoords, 5, 3, 2);
    rendererPlatform.setUniform1i(uniTexture, config.uniform1is[0]);
    rendererPlatform.setUniformMatrix4f(uniMvpMatrix, &config.uniformMat4s.front());
    rendererPlatform.setUniform3f(uniTextColor, &config.uniform3fs.front());
}
