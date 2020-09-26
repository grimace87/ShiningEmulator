#pragma once

#include "../SharedLib/resource.h"

#include <Windows.h>

class WindowsResource : public Resource {
    bool bufferIsCopy;

public:
    WindowsResource(const char* fileName, bool isAsset);
    ~WindowsResource() override;
};
