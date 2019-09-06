/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "androidrenderer.h"
#include "../../../../../SharedLib/resource.h"
#include "../../../../../SharedLib/renderconfig.h"
#include "../../../../../SharedLib/shader.h"

#include <android/log.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <android/native_window.h>

// Logging
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "android_app", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "android_app", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "android_app", __VA_ARGS__))
#ifndef NDEBUG
#  define LOGV(...)  ((void)__android_log_print(ANDROID_LOG_VERBOSE, "android_app", __VA_ARGS__))
#else
#  define LOGV(...)  ((void)0)
#endif

AndroidRenderer::AndroidRenderer(ANativeWindow* window):
        window(window),
        display{EGL_NO_DISPLAY},
        context{EGL_NO_CONTEXT},
        surface{EGL_NO_SURFACE} { }

AndroidRenderer::~AndroidRenderer() = default;

bool AndroidRenderer::createContext() {
    EGLint w;
    EGLint h;
    EGLint format;
    EGLint numConfigs;
    EGLConfig config;
    EGLSurface _surface;
    EGLContext _context;
    EGLBoolean callResult;

    // Get default display
    EGLDisplay _display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(_display != EGL_NO_DISPLAY);

    // Intialise default display, getting EGL version
    EGLint major;
    EGLint minor;
    callResult = eglInitialize(_display, &major, &minor);
    assert(callResult == EGL_TRUE);
    assert(((10 * major) + minor) >= 14);

    // Choose the first available configuration matching request
    const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
    };
    callResult = eglChooseConfig(_display, attribs, &config, 1, &numConfigs);
    assert(callResult == EGL_TRUE);
    assert(numConfigs > 0);

    // Get the window buffer format and apply it to ANativeWindow buffers
    callResult = eglGetConfigAttrib(_display, config, EGL_NATIVE_VISUAL_ID, &format);
    assert(callResult == EGL_TRUE);
    int32_t setGeometryResult = ANativeWindow_setBuffersGeometry(window, 0, 0, format);
    assert(setGeometryResult == 0);

    // Create a rendering surface
    _surface = eglCreateWindowSurface(_display, config, window, nullptr);
    assert(_surface != EGL_NO_SURFACE);

    // Create a context using OpenGL ES 2 as the client API
    EGLint contextConfig[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    _context = eglCreateContext(_display, config, EGL_NO_CONTEXT, contextConfig);
    assert(_context != EGL_NO_CONTEXT);

    if (eglMakeCurrent(_display, _surface, _surface, _context) == EGL_FALSE) {
        LOGW("Unable to eglMakeCurrent");
        return false;
    }

    eglQuerySurface(_display, _surface, EGL_WIDTH, &w);
    eglQuerySurface(_display, _surface, EGL_HEIGHT, &h);

    display = _display;
    context = _context;
    surface = _surface;
    canvasWidth = w;
    canvasHeight = h;

    // Initialize GL state.
    return initOpenGL();
}

void AndroidRenderer::destroyContext() {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
    }
    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
    surface = EGL_NO_SURFACE;
}

void AndroidRenderer::verifyNoError() {
    GLenum errorStatus = glGetError();
    assert(errorStatus == GL_NO_ERROR);
}

unsigned int AndroidRenderer::createShader(const char* vertexShaderSource, int vertexSourceLength, const char* fragmentShaderSource, int fragmentSourceLength) {
    GLint result;
    assert(context != EGL_NO_CONTEXT);
    assert(display != EGL_NO_DISPLAY);
    assert(surface != EGL_NO_SURFACE);

    // Load and compile vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    assert(glIsShader(vertexShader) == GL_TRUE);
    glShaderSource(vertexShader, 1, &vertexShaderSource, &vertexSourceLength);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &result);
    if (result != GL_TRUE) {
        GLsizei bufferSize;
        GLsizei readLength;
        glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &bufferSize);
        auto errLog = new GLchar[bufferSize];
        glGetShaderInfoLog(vertexShader, bufferSize, &readLength, errLog);
        LOGE("%s", errLog);
        delete[] errLog;
        return UINT_MAX;
    }

    // Load and compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    assert(glIsShader(fragmentShader) == GL_TRUE);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, &fragmentSourceLength);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &result);
    if (result != GL_TRUE) {
        GLsizei bufferSize;
        GLsizei readLength;
        glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &bufferSize);
        auto errLog = new GLchar[bufferSize];
        glGetShaderInfoLog(fragmentShader, bufferSize, &readLength, errLog);
        LOGE("%s", errLog);
        delete[] errLog;
        return UINT_MAX;
    }

    // Link program
    unsigned int program = glCreateProgram();
    assert(glIsProgram(program) == GL_TRUE);
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &result);
    if (result != GL_TRUE) {
        GLsizei bufferSize;
        GLsizei readLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufferSize);
        auto errLog = new GLchar[bufferSize];
        glGetProgramInfoLog(program, bufferSize, &readLength, errLog);
        LOGE("%s", errLog);
        delete[] errLog;
        return UINT_MAX;
    }
    assert(glGetError() == GL_NO_ERROR);
    return program;
}

int AndroidRenderer::queryAttribLocation(unsigned int program, const char* attribName) {
    int location = glGetAttribLocation(program, attribName);
    glEnableVertexAttribArray((GLuint)location);
    return location;
}

int AndroidRenderer::queryUniformLocation(unsigned int program, const char* uniformName) {
    int location = glGetUniformLocation(program, uniformName);
    return location;
}

unsigned int AndroidRenderer::createTexture(int textureFormat, const unsigned char* rawData, size_t imageWidth, size_t imageHeight, bool useLinearMagFilter) {
    // Get OpenGL pixel format required
    GLint pixelFormat = 0;
    if (textureFormat == TEXTURE_FORMAT_RGBA) {
        pixelFormat = GL_RGBA;
    } else if (textureFormat == TEXTURE_FORMAT_GREYSCALE) {
        pixelFormat = GL_LUMINANCE;
    } else if (textureFormat == TEXTURE_FORMAT_BGRA_IF_AVAIL) {
        pixelFormat = GL_RGBA;
    }
    assert(pixelFormat != 0);

    // Create a texture object
    GLuint texture;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, useLinearMagFilter ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, pixelFormat, imageWidth, imageHeight, 0, (GLenum)pixelFormat, GL_UNSIGNED_BYTE, rawData);
    glBindTexture(GL_TEXTURE_2D, 0);
    assert(glGetError() == GL_NO_ERROR);
    return texture;
}

unsigned int AndroidRenderer::createVbo(float* data, size_t length) {
    // Create a VBO
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, length * sizeof(float), data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    GLenum errorState = glGetError();
    assert(errorState == GL_NO_ERROR);
    return vbo;
}

void AndroidRenderer::configureAttribute(int attributeLayout, int strideElements, int index, int size) {
    glEnableVertexAttribArray(attributeLayout);
    glVertexAttribPointer(attributeLayout, size, GL_FLOAT, GL_FALSE, strideElements * sizeof(float), (const void*)(index * sizeof(float)));
}

void AndroidRenderer::setUniform1i(int layout, int value) {
    glUniform1i(layout, value);
}

void AndroidRenderer::setUniform3f(int layout, float* values) {
    glUniform3fv(layout, 1, values);
}

void AndroidRenderer::setUniformMatrix4f(int layout, float* values) {
    glUniformMatrix4fv(layout, 1, false, values);
}

void AndroidRenderer::subTexture(unsigned int texture, int textureFormat, int x, int y, int width, int height, const unsigned char* rawData) {
    // Get OpenGL pixel format required
    GLint pixelFormat = 0;
    if (textureFormat == TEXTURE_FORMAT_RGBA) {
        pixelFormat = GL_RGBA;
    } else if (textureFormat == TEXTURE_FORMAT_GREYSCALE) {
        pixelFormat = GL_LUMINANCE;
    } else if (textureFormat == TEXTURE_FORMAT_BGRA_IF_AVAIL) {
        pixelFormat = GL_RGBA;
    }
    assert(pixelFormat != 0);

    // Bind texture and set pixels
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, pixelFormat, GL_UNSIGNED_BYTE, rawData);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void AndroidRenderer::renderPass(std::vector<FrameConfig>& configs, unsigned int firstIndex, unsigned int count) {
    // Check for errors
    auto err = glGetError();
    assert(err == GL_NO_ERROR);
    glClear(GL_COLOR_BUFFER_BIT);
    for (unsigned int i = firstIndex; i < firstIndex + count; i++) {
        auto& frameConfig = configs[i];
        glUseProgram(frameConfig.shader->programObject);
        for (auto& renderConfig : frameConfig.renderConfigs) {
            glBindBuffer(GL_ARRAY_BUFFER, renderConfig.vbo);
            glBindTexture(GL_TEXTURE_2D, renderConfig.texture);
            frameConfig.shader->prepareFrame(*this, renderConfig);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDrawArrays(GL_TRIANGLES, renderConfig.startVertex, renderConfig.vertexCount);
        }
    }
    eglSwapBuffers(display, surface);
}

void AndroidRenderer::resizeDisplay(int width, int height) {
    // TODO: Implement this!
}

bool AndroidRenderer::initOpenGL() {
    // Initialise standard stuff
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, (GLsizei)canvasWidth, (GLsizei)canvasHeight);
    return true;
}
