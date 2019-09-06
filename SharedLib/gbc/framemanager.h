#pragma once

#include <cstdint>

class FrameManager {
    unsigned int frame1Status;
    unsigned int frame2Status;
    uint32_t* frame1Buffer;
    uint32_t* frame2Buffer;
    int nextFrameToBegin;
public:
    FrameManager();
    ~FrameManager();
    bool frameIsInProgress();
    uint32_t* getInProgressFrameBuffer();
    uint32_t* beginNewFrame();
    int finishCurrentFrame();
    uint32_t* getRenderableFrameBuffer();
    void freeFrame(uint32_t* frameBuffer);
};
