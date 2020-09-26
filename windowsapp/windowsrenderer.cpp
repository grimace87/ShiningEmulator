#include "windowsrenderer.h"

#include <gl/GL.h>
#include <ShObjIdl_core.h>
#include <cassert>
#include <GL/glext.h>

#include "../SharedLib/resource.h"
#include "../SharedLib/renderconfig.h"
#include "../SharedLib/shader.h"

// OpenGL procedures
PFNGLCREATESHADERPROC glCreateShader;
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLGETSHADERIVPROC glGetShaderiv;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLATTACHSHADERPROC glAttachShader;
PFNGLLINKPROGRAMPROC glLinkProgram;
PFNGLGETPROGRAMIVPROC glGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
PFNGLACTIVETEXTUREPROC glActiveTexture;
PFNGLCREATETEXTURESPROC glCreateTextures;
PFNGLCREATEBUFFERSPROC glCreateBuffers;
PFNGLBINDBUFFERPROC glBindBuffer;
PFNGLBUFFERDATAPROC glBufferData;
PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
PFNGLUSEPROGRAMPROC glUseProgram;
PFNGLUNIFORM1IPROC glUniform1i;
PFNGLUNIFORM3FVPROC glUniform3fv;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;

WindowsRenderer::WindowsRenderer(HDC hDC, int width, int height) : hDC(hDC) {
    canvasWidth = width;
    canvasHeight = height;
    hRC = NULL;
    imageFactory = NULL;
}

bool WindowsRenderer::createContext() {
    // Initialise COM on this thread and create an imaging factory
    CoInitialize(NULL);
    HRESULT factoryMade = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imageFactory));
    if (factoryMade != S_OK) {
        return false;
    }

    // Choose a pixel format
    PIXELFORMATDESCRIPTOR pfd;
    ZeroMemory(&pfd, sizeof(PIXELFORMATDESCRIPTOR));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int format = ChoosePixelFormat(hDC, &pfd);
    if (format == NULL)
    {
        MessageBoxW(NULL, L"Failed to choose a pixel format", L"Error", MB_OK);
        return false;
    }

    // Set the pixel format
    BOOL result = SetPixelFormat(hDC, format, &pfd);
    if (result == FALSE)
    {
        MessageBoxW(NULL, L"Failed to set pixel format", L"Error", MB_OK);
        return false;
    }

    // Create the rendering context
    hRC = wglCreateContext(hDC);
    if (hRC == NULL)
    {
        MessageBoxW(NULL, L"Failed to create rendering context", L"Error", MB_OK);
        return false;
    }

    // Make the context current on this thread
    result = wglMakeCurrent(hDC, hRC);
    if (result == FALSE)
    {
        MessageBoxW(NULL, L"Failed to create rendering context", L"Error", MB_OK);
        return false;
    }

    // Perform OpenGL init
    return initOpenGL();

}

void WindowsRenderer::destroyContext()
{
    wglDeleteContext(hRC);
}

void WindowsRenderer::verifyNoError()
{
    GLenum err = glGetError();
    assert(err == GL_NO_ERROR);
}

unsigned int WindowsRenderer::createShader(const char* vertexShaderSource, int vertexSourceLength, const char* fragmentShaderSource, int fragmentSourceLength)
{
    // Create variables to obtain resource data
    GLint status;

    // Load and compile vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, &vertexSourceLength);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLsizei bufferSize, readLength;
        glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &bufferSize);
        GLchar* errLog = new GLchar[bufferSize];
        glGetShaderInfoLog(vertexShader, bufferSize, &readLength, errLog);
        delete[] errLog;
        return UINT_MAX;
    }

    // Load and compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, &fragmentSourceLength);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLsizei bufferSize, readLength;
        glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &bufferSize);
        GLchar* errLog = new GLchar[bufferSize];
        glGetShaderInfoLog(fragmentShader, bufferSize, &readLength, errLog);
        delete[] errLog;
        return UINT_MAX;
    }

    // Link program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLsizei bufferSize, readLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufferSize);
        GLchar* errLog = new GLchar[bufferSize];
        glGetProgramInfoLog(program, bufferSize, &readLength, errLog);
        delete[] errLog;
        return UINT_MAX;
    }

    return program;
}

int WindowsRenderer::queryAttribLocation(unsigned int program, const char* attribName)
{
    int location = glGetAttribLocation(program, attribName);
    glEnableVertexAttribArray(location);
    return location;
}

int WindowsRenderer::queryUniformLocation(unsigned int program, const char* uniformName)
{
    return glGetUniformLocation(program, uniformName);
}

unsigned int WindowsRenderer::createTexture(int textureFormat, const unsigned char* rawData, size_t imageWidth, size_t imageHeight, bool useLinearMagFilter)
{
    // Get OpenGL pixel format required
    GLint internalFormat = 0;
    GLenum sourceFormat = 0;
    if (textureFormat == TEXTURE_FORMAT_RGBA)
    {
        internalFormat = GL_RGBA;
        sourceFormat = GL_RGBA;
    }
    else if (textureFormat == TEXTURE_FORMAT_GREYSCALE)
    {
        internalFormat = GL_LUMINANCE;
        sourceFormat = GL_LUMINANCE;
    }
    else if (textureFormat == TEXTURE_FORMAT_BGRA_IF_AVAIL)
    {
        internalFormat = GL_BGRA;
        sourceFormat = GL_RGBA;
    }
    assert(internalFormat != 0);

    // Create the OpenGL texture object
    GLuint newTexture;
    glActiveTexture(GL_TEXTURE0);
    glCreateTextures(GL_TEXTURE_2D, 1, &newTexture);
    glBindTexture(GL_TEXTURE_2D, newTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, useLinearMagFilter ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, imageWidth, imageHeight, 0, sourceFormat, GL_UNSIGNED_BYTE, rawData);
    glBindTexture(GL_TEXTURE_2D, 0);
    return newTexture;
}

unsigned int WindowsRenderer::createVbo(float* data, size_t length)
{
    // Create a VBO
    GLuint newBuffer;
    glCreateBuffers(1, &newBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, newBuffer);
    glBufferData(GL_ARRAY_BUFFER, length * sizeof(float), data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return newBuffer;
}

void WindowsRenderer::configureAttribute(int attributeLayout, int strideElements, int index, int size)
{
    glEnableVertexAttribArray(attributeLayout);
    glVertexAttribPointer(attributeLayout, size, GL_FLOAT, GL_FALSE, strideElements * sizeof(float), (const void*)(index * sizeof(float)));
}

void WindowsRenderer::setUniform1i(int layout, int value)
{
    glUniform1i(layout, value);
}

void WindowsRenderer::setUniform3f(int layout, float* values)
{
    glUniform3fv(layout, 1, values);
}

void WindowsRenderer::setUniformMatrix4f(int layout, float* values)
{
    glUniformMatrix4fv(layout, 1, false, values);
}

void WindowsRenderer::subTexture(unsigned int texture, int textureFormat, int x, int y, int width, int height, const unsigned char* rawData)
{
    // Get OpenGL pixel format required
    GLint pixelFormat = 0;
    GLenum type = 0;
    if (textureFormat == TEXTURE_FORMAT_RGBA)
    {
        pixelFormat = GL_RGBA;
        type = GL_UNSIGNED_INT_8_8_8_8_REV;
    }
    else if (textureFormat == TEXTURE_FORMAT_GREYSCALE)
    {
        pixelFormat = GL_LUMINANCE;
        type = GL_UNSIGNED_BYTE;
    }
    else if (textureFormat == TEXTURE_FORMAT_BGRA_IF_AVAIL)
    {
        pixelFormat = GL_BGRA;
        type = GL_UNSIGNED_INT_8_8_8_8_REV;
    }
    assert(pixelFormat != 0);

    // Bind texture and set pixels
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, pixelFormat, type, rawData);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void WindowsRenderer::renderPass(std::vector<FrameConfig>& configs, unsigned int firstIndex, unsigned int count)
{
    glClear(GL_COLOR_BUFFER_BIT);
    for (unsigned int i = firstIndex; i < firstIndex + count; i++)
    {
        auto& frameConfig = configs[i];
        glUseProgram(frameConfig.shader->programObject);
        for (auto& renderConfig : frameConfig.renderConfigs)
        {
            glBindBuffer(GL_ARRAY_BUFFER, renderConfig.vbo);
            glBindTexture(GL_TEXTURE_2D, renderConfig.texture);
            frameConfig.shader->prepareFrame(*this, renderConfig);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDrawArrays(GL_TRIANGLES, renderConfig.startVertex, renderConfig.vertexCount);
        }
    }
    SwapBuffers(hDC);
}

void WindowsRenderer::resizeDisplay(int width, int height)
{
    canvasWidth = width;
    canvasHeight = height;
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
}

bool WindowsRenderer::initOpenGL()
{
    // Initialise standard stuff
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glViewport(0, 0, (GLsizei)canvasWidth, (GLsizei)canvasHeight);

    // Sort out extensions
    glCreateShader = (PFNGLCREATESHADERPROC) wglGetProcAddress("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC) wglGetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC) wglGetProcAddress("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC) wglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC) wglGetProcAddress("glGetShaderInfoLog");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC) wglGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC) wglGetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC) wglGetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC) wglGetProcAddress("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC) wglGetProcAddress("glGetProgramInfoLog");
    glActiveTexture = (PFNGLACTIVETEXTUREPROC) wglGetProcAddress("glActiveTexture");
    glCreateTextures = (PFNGLCREATETEXTURESPROC) wglGetProcAddress("glCreateTextures");
    glCreateBuffers = (PFNGLCREATEBUFFERSPROC) wglGetProcAddress("glCreateBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC) wglGetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC) wglGetProcAddress("glBufferData");
    glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC) wglGetProcAddress("glGetAttribLocation");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC) wglGetProcAddress("glGetUniformLocation");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC) wglGetProcAddress("glEnableVertexAttribArray");
    glUseProgram = (PFNGLUSEPROGRAMPROC) wglGetProcAddress("glUseProgram");
    glUniform1i = (PFNGLUNIFORM1IPROC) wglGetProcAddress("glUniform1i");
    glUniform3fv = (PFNGLUNIFORM3FVPROC) wglGetProcAddress("glUniform3fv");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC) wglGetProcAddress("glUniformMatrix4fv");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC) wglGetProcAddress("glVertexAttribPointer");

    // All good
    return true;
}
