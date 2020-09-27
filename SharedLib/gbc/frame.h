#pragma once

#include <cstdint>

constexpr size_t BASE_FRAME_W = 160;
constexpr size_t BASE_FRAME_H = 144;
constexpr size_t PADDING_ROWS = 10;
constexpr size_t FRAME_SCALE_FACTOR = 4;

class Frame {
    enum class FrameStatus {
        AVAILABLE,
        BEING_DRAWN,
        BEING_RENDERED
    };

    FrameStatus status;
    uint32_t* buffer;

public:
    Frame();
    ~Frame();
    uint32_t* getForDrawing();
    void markForRendering();
    void markAvailable();

    [[nodiscard]] inline bool isAvailable() const { return status == FrameStatus::AVAILABLE; }
    [[nodiscard]] inline bool isBeingDrawn() const { return status == FrameStatus::BEING_DRAWN; }
    [[nodiscard]] inline bool isBeingRendered() const { return status == FrameStatus::BEING_RENDERED; }
    [[nodiscard]] inline uint32_t* getBuffer() const { return buffer; }
};
