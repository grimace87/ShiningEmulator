#include "windowsresource.h"

#include "res/res.h"

#include <map>
#include <fstream>

// Map file names to resource constants
struct ResId {
    int identifier;
    LPWSTR type;
};

std::map<std::string, ResId> resourceMap
        {
                {"vs_main.glsl", {IDR_GLSHADER_VS_MAIN, MAKEINTRESOURCEW(GLSHADER)}},
                {"fs_main.glsl", {IDR_GLSHADER_FS_MAIN, MAKEINTRESOURCEW(GLSHADER)}},
                {"vs_normal.glsl", {IDR_GLSHADER_VS_NORMAL, MAKEINTRESOURCEW(GLSHADER)}},
                {"fs_normal.glsl", {IDR_GLSHADER_FS_NORMAL, MAKEINTRESOURCEW(GLSHADER)}},
                {"fs_text.glsl", {IDR_GLSHADER_FS_TEXT, MAKEINTRESOURCEW(GLSHADER)}},
                {"tex_main.png", {IDB_TEXTURE_MAIN, (LPWSTR)L"PNG"}},
                {"Musica.png", {IDB_TEXTURE_MUSICA, (LPWSTR)L"PNG"}},
                {"Musica.fnt", {IDB_FONT_DESC_MUSICA, MAKEINTRESOURCEW(FONT_DESC)}}
        };

WindowsResource::WindowsResource(const char* fileName, bool isAsset) {
    rawStream = nullptr;
    rawDataLength = 0;
    bufferIsCopy = false;

    if (isAsset) {
        ResId resourceId = resourceMap.at(fileName);
        HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId.identifier), resourceId.type);
        if (hRes) {
            HGLOBAL hResData = LoadResource(NULL, hRes);
            rawDataLength = SizeofResource(NULL, hRes);
            const unsigned char* ptr = static_cast<const unsigned char*>(LockResource(hResData));
            rawStream = new unsigned char[rawDataLength];
            memcpy((void*)rawStream, ptr, rawDataLength);
        }
    } else {
        std::ifstream file(fileName, std::ios::binary | std::ios::ate);
        rawDataLength = file.tellg();
        file.seekg(0, std::ios::beg);
        rawStream = new unsigned char[rawDataLength];
        if (file.read((char*)rawStream, rawDataLength)) {
            this->fileName = fileName;
            return;
        } else {
            rawDataLength = 0;
            rawStream = nullptr;
        }
    }
}

WindowsResource::~WindowsResource() {
    delete[] rawStream;
}
