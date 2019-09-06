#pragma once

class Cursor {
public:
    bool downHandled;
    float downXPixels;
    float downYPixels;
    float xPixels;
    float yPixels;
};

class GamepadInputs {
public:
    bool isConnected;
    float xClamped;
    float yClamped;
    bool left;
    bool right;
    bool up;
    bool down;
    bool select;
    bool start;
    bool actionLeft;
    bool actionTop;
    bool actionRight;
    bool actionBottom;
};
