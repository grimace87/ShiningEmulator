#include "framemanager.h"

#include <filters.h>

xbr_data xbrData;
xbr_params xbrParams1;
xbr_params xbrParams2;
uint32_t* extendedBuffer1;
uint32_t* extendedBuffer2;

FrameManager::FrameManager() :
    frame1(),
    frame2(),
    nextFrameToBegin(1) {

	delete[] extendedBuffer1;
	delete[] extendedBuffer2;
	extendedBuffer1 = new uint32_t[BASE_FRAME_W * BASE_FRAME_H * FRAME_SCALE_FACTOR * FRAME_SCALE_FACTOR];
	extendedBuffer2 = new uint32_t[BASE_FRAME_W * BASE_FRAME_H * FRAME_SCALE_FACTOR * FRAME_SCALE_FACTOR];

	xbr_init_data(&xbrData);

	xbrParams1.data = &xbrData;
	xbrParams1.inHeight = BASE_FRAME_H;
	xbrParams1.inWidth = BASE_FRAME_W;
	xbrParams1.input = (uint8_t*)frame1.getBuffer();
	xbrParams1.inPitch = BASE_FRAME_W * sizeof(uint32_t);
	xbrParams1.output = (uint8_t*)extendedBuffer1;
	xbrParams1.outPitch = BASE_FRAME_W * 4 * sizeof(uint32_t);

	xbrParams2.data = &xbrData;
	xbrParams2.inHeight = BASE_FRAME_H;
	xbrParams2.inWidth = BASE_FRAME_W;
	xbrParams2.input = (uint8_t*)frame2.getBuffer();
	xbrParams2.inPitch = BASE_FRAME_W * sizeof(uint32_t);
	xbrParams2.output = (uint8_t*)extendedBuffer2;
	xbrParams2.outPitch = BASE_FRAME_W * FRAME_SCALE_FACTOR * sizeof(uint32_t);
}

FrameManager::~FrameManager() = default;

bool FrameManager::frameIsInProgress() const {
    return frame1.isBeingDrawn() || frame2.isBeingDrawn();
}

uint32_t* FrameManager::getInProgressFrameBuffer() const {
    if (frame1.isBeingDrawn()) {
        return frame1.getBuffer();
    } else if(frame2.isBeingDrawn()) {
        return frame2.getBuffer();
    } else {
        return nullptr;
    }
}

uint32_t* FrameManager::beginNewFrame() {
    if ((nextFrameToBegin == 1) && frame1.isAvailable()) {
        nextFrameToBegin = 2;
        return frame1.getForDrawing();
    } else if ((nextFrameToBegin == 2) && frame2.isAvailable()) {
        nextFrameToBegin = 1;
        return frame2.getForDrawing();
    } else {
        return nullptr;
    }
}

int FrameManager::finishCurrentFrame() {
    if (frame1.isBeingDrawn()) {
		xbr_filter_xbr4x(&xbrParams1);
        frame1.markForRendering();
        return 1;
    } else if (frame2.isBeingDrawn()) {
		xbr_filter_xbr4x(&xbrParams2);
        frame2.markForRendering();
        return 2;
    } else {
        return 0;
    }
}

uint32_t* FrameManager::getRenderableFrameBuffer() {
    if (frame1.isBeingRendered()) {
        return extendedBuffer1;
    } else if (frame2.isBeingRendered()) {
        return extendedBuffer2;
    } else {
        return nullptr;
    }
}

void FrameManager::freeFrame(const uint32_t* frameBuffer) {
    if (frameBuffer == extendedBuffer1) {
        frame1.markAvailable();
    } else if (frameBuffer == extendedBuffer2) {
        frame2.markAvailable();
    }
}
