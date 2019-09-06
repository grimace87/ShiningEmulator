#pragma once

enum class Gravity {
    START = 0,
    MIDDLE = 1,
    END = 2
};

struct Size {
    float height;
    float width;
};

struct Rect {
    float left;
    float top;
    float right;
    float bottom;
};

// Compass direction masks
enum class CompassMask {
    NORTH = 0x01,
    EAST = 0x02,
    SOUTH = 0x04,
    WEST = 0x08
};
