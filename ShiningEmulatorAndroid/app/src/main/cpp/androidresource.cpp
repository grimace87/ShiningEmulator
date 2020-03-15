#include "androidresource.h"

constexpr size_t fileReadChunkSize = 16384;

AndroidResource::AndroidResource(ANativeActivity* activity, const char* fileName, bool isAsset, bool isGlShader):
        asset(nullptr),
        assetManager(nullptr),
        bufferIsCopy(false) {

    if (isAsset) {
        char file[128];
        if (isGlShader) {
            strcpy(file, "gles/");
            strcpy(file + 5, fileName);
        } else {
            strcpy(file, fileName);
        }
        openFromAsset(activity, file);
    } else {
        openFromFile(fileName);
    }
}

AndroidResource::~AndroidResource() {
    if (bufferIsCopy) {
        if (rawStream) {
            delete[] rawStream;
        }
    } else if (asset) {
        AAsset_close(asset);
    }
}

void AndroidResource::openFromAsset(ANativeActivity* activity, const char* fileName) {

    // Create Android-specific things
    assetManager = activity->assetManager;
    asset = AAssetManager_open(assetManager, fileName, AASSET_MODE_BUFFER);

    // Get buffer for parent class
    rawStream = nullptr;
    rawDataLength = (size_t)AAsset_getLength(asset);
    if (AAsset_isAllocated(asset)) {
        bufferIsCopy = false;
        rawStream = (const unsigned char*)AAsset_getBuffer(asset);
    } else {
        bufferIsCopy = true;
        rawStream = new unsigned char[rawDataLength];
        auto bufferPtr = (unsigned char*)rawStream;
        int bytesRead;
        int totalBytesRead = 0;
        do {
            bytesRead = AAsset_read(asset, bufferPtr, fileReadChunkSize);
            bufferPtr += bytesRead;
            if (bytesRead > 0) {
                totalBytesRead += bytesRead;
            }
        } while (bytesRead > 0);
        assert(totalBytesRead == rawDataLength);
        AAsset_close(asset);
        asset = nullptr;
    }
}

void AndroidResource::openFromFile(const char* fileName) {
    FILE* file = fopen(fileName, "re");
    if (file) {
        this->fileName = fileName;
        fseek(file, 0L, SEEK_END);
        rawDataLength = (size_t)ftell(file);
        rewind(file);

        rawStream = new unsigned char[rawDataLength];
        size_t index = 0;
        while (index < rawDataLength) {
            size_t bytesToCopy = (index + fileReadChunkSize < rawDataLength) ? fileReadChunkSize : rawDataLength - index;
            size_t bytesCopied = fread((void*)(rawStream + index), (size_t)1, bytesToCopy, file);
            index += bytesCopied;
            if (bytesToCopy != bytesCopied) {
                rawStream = nullptr;
                break;
            }
        }
        fclose(file);
    }
}
