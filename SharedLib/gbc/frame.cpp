#include "frame.h"

Frame::Frame() {
    status = FrameStatus::AVAILABLE;
    buffer = new uint32_t[BASE_FRAME_W * (BASE_FRAME_H + PADDING_ROWS)];
}

Frame::~Frame() {
    delete[] buffer;
}

uint32_t* Frame::getForDrawing() {
    if (status != FrameStatus::AVAILABLE) {
        return nullptr;
    }
    status = FrameStatus::BEING_DRAWN;
    return buffer;
}

bool Frame::markForRendering() {
    if (status != FrameStatus::BEING_DRAWN) {
        return false;
    }
    status = FrameStatus::BEING_RENDERED;
    return true;
}

bool Frame::markAvailable() {
    if (status != FrameStatus::BEING_RENDERED) {
        return false;
    }
    status = FrameStatus::AVAILABLE;
    return true;
}
