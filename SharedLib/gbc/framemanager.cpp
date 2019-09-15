#include "framemanager.h"

#define FRAME_STATUS_AVAILABLE      0
#define FRAME_STATUS_BEING_DRAWN    1
#define FRAME_STATUS_BEING_RENDERED 2

#include <libxbr-standalone/filters.h>
#include <sys/types.h>

xbr_data xbrData;
xbr_params xbrParams1;
xbr_params xbrParams2;
uint32_t* extendedBuffer1;
uint32_t* extendedBuffer2;

constexpr off_t BASE_FRAME_W = 160;
constexpr off_t BASE_FRAME_H = 144;
constexpr off_t PADDING_ROWS = 10;
constexpr off_t FRAME_SCALE_FACTOR = 4;

FrameManager::FrameManager() {
    frame1Status = FRAME_STATUS_AVAILABLE;
    frame2Status = FRAME_STATUS_AVAILABLE;
    frame1Buffer = new uint32_t[BASE_FRAME_W * (BASE_FRAME_H + PADDING_ROWS)];
    frame2Buffer = new uint32_t[BASE_FRAME_W * (BASE_FRAME_H + PADDING_ROWS)];
    nextFrameToBegin = 1;

	delete[] extendedBuffer1;
	delete[] extendedBuffer2;
	extendedBuffer1 = new uint32_t[BASE_FRAME_W * BASE_FRAME_H * FRAME_SCALE_FACTOR * FRAME_SCALE_FACTOR];
	extendedBuffer2 = new uint32_t[BASE_FRAME_W * BASE_FRAME_H * FRAME_SCALE_FACTOR * FRAME_SCALE_FACTOR];

	xbr_init_data(&xbrData);

	xbrParams1.data = &xbrData;
	xbrParams1.inHeight = BASE_FRAME_H;
	xbrParams1.inWidth = BASE_FRAME_W;
	xbrParams1.input = (uint8_t*)frame1Buffer;
	xbrParams1.inPitch = BASE_FRAME_W * sizeof(uint32_t);
	xbrParams1.output = (uint8_t*)extendedBuffer1;
	xbrParams1.outPitch = BASE_FRAME_W * 4 * sizeof(uint32_t);

	xbrParams2.data = &xbrData;
	xbrParams2.inHeight = BASE_FRAME_H;
	xbrParams2.inWidth = BASE_FRAME_W;
	xbrParams2.input = (uint8_t*)frame1Buffer;
	xbrParams2.inPitch = BASE_FRAME_W * sizeof(uint32_t);
	xbrParams2.output = (uint8_t*)extendedBuffer2;
	xbrParams2.outPitch = BASE_FRAME_W * FRAME_SCALE_FACTOR * sizeof(uint32_t);
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
		xbr_filter_xbr4x(&xbrParams1);
        frame1Status = FRAME_STATUS_BEING_RENDERED;
        return 1;
    } else if (frame2Status == FRAME_STATUS_BEING_DRAWN) {
		xbr_filter_xbr4x(&xbrParams2);
        frame2Status = FRAME_STATUS_BEING_RENDERED;
        return 2;
    } else {
        return 0;
    }
}

uint32_t* FrameManager::getRenderableFrameBuffer() {
    if (frame1Status == FRAME_STATUS_BEING_RENDERED) {
        return extendedBuffer1;
    } else if (frame2Status == FRAME_STATUS_BEING_RENDERED) {
        return extendedBuffer2;
    } else {
        return nullptr;
    }
}

void FrameManager::freeFrame(uint32_t* frameBuffer) {
    if (frameBuffer == extendedBuffer1) {
        frame1Status = FRAME_STATUS_AVAILABLE;
    } else if (frameBuffer == extendedBuffer2) {
        frame2Status = FRAME_STATUS_AVAILABLE;
    }
}
