#include "framemanager.h"

#define FRAME_STATUS_AVAILABLE      0
#define FRAME_STATUS_BEING_DRAWN    1
#define FRAME_STATUS_BEING_RENDERED 2

FrameManager::FrameManager() {
    frame1Status = FRAME_STATUS_AVAILABLE;
    frame2Status = FRAME_STATUS_AVAILABLE;
    frame1Buffer = new uint32_t[160 * 154];
    frame2Buffer = new uint32_t[160 * 154];
    nextFrameToBegin = 1;
}

FrameManager::~FrameManager() {
    delete[] frame1Buffer;
    delete[] frame2Buffer;
}

bool FrameManager::frameIsInProgress() {
    return (frame1Status == FRAME_STATUS_BEING_DRAWN) || (frame2Status == FRAME_STATUS_BEING_DRAWN);
}

uint32_t* FrameManager::getInProgressFrameBuffer() {
    if (frame1Status == FRAME_STATUS_BEING_DRAWN) {
        return frame1Buffer;
    } else if(frame2Status == FRAME_STATUS_BEING_DRAWN) {
        return frame2Buffer;
    } else {
        return nullptr;
    }
}

uint32_t* FrameManager::beginNewFrame() {
    if ((nextFrameToBegin == 1) && (frame1Status == FRAME_STATUS_AVAILABLE)) {
        frame1Status = FRAME_STATUS_BEING_DRAWN;
        nextFrameToBegin = 2;
        return frame1Buffer;
    } else if ((nextFrameToBegin == 2) && (frame2Status == FRAME_STATUS_AVAILABLE)) {
        frame2Status = FRAME_STATUS_BEING_DRAWN;
        nextFrameToBegin = 1;
        return frame2Buffer;
    } else {
        return nullptr;
    }
}

int FrameManager::finishCurrentFrame() {
    if (frame1Status == FRAME_STATUS_BEING_DRAWN) {
        frame1Status = FRAME_STATUS_BEING_RENDERED;
        return 1;
    } else if (frame2Status == FRAME_STATUS_BEING_DRAWN) {
        frame2Status = FRAME_STATUS_BEING_RENDERED;
        return 2;
    } else {
        return 0;
    }
}

uint32_t* FrameManager::getRenderableFrameBuffer() {
    if (frame1Status == FRAME_STATUS_BEING_RENDERED) {
        return frame1Buffer;
    } else if (frame2Status == FRAME_STATUS_BEING_RENDERED) {
        return frame2Buffer;
    } else {
        return nullptr;
    }
}

void FrameManager::freeFrame(uint32_t* frameBuffer) {
    if (frameBuffer == frame1Buffer) {
        frame1Status = FRAME_STATUS_AVAILABLE;
    } else if (frameBuffer == frame2Buffer) {
        frame2Status = FRAME_STATUS_AVAILABLE;
    }
}
