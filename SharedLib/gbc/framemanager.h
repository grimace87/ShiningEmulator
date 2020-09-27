#pragma once

#include "frame.h"

class FrameManager {
    Frame frame1;
    Frame frame2;
    int nextFrameToBegin;
public:
    FrameManager();
    ~FrameManager();
    [[nodiscard]] bool frameIsInProgress() const;
    [[nodiscard]] uint32_t* getInProgressFrameBuffer() const;
    uint32_t* beginNewFrame();
    [[nodiscard]] int finishCurrentFrame();
    uint32_t* getRenderableFrameBuffer();
    [[nodiscard]] bool freeFrame(const uint32_t* frameBuffer);
};
