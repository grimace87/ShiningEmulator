#pragma once

#include <cstdint>

class InputSet {
public:
    uint8_t keyBut, keyDir;
    void clear();
    void pressRight();
    void pressLeft();
    void pressUp();
    void pressDown();
    void pressSelect();
    void pressStart();
    void pressA();
    void pressB();
};
