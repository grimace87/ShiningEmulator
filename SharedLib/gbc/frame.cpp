#include "frame.h"

#include <exception>

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

void Frame::markForRendering() {
    if (status != FrameStatus::BEING_DRAWN) {
        throw std::exception("adaw");
    }
    status = FrameStatus::BEING_RENDERED;
}

void Frame::markAvailable() {
    if (status != FrameStatus::BEING_RENDERED) {
        throw std::exception("adaw");
    }
    status = FrameStatus::AVAILABLE;
}
