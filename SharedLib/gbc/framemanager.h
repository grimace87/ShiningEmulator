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
    int finishCurrentFrame();
    uint32_t* getRenderableFrameBuffer();
    void freeFrame(const uint32_t* frameBuffer);
};
