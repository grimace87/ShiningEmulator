#include "inputset.h"

void InputSet::clear() {
    keyDir = 0x0f;
    keyBut = 0x0f;
}

void InputSet::pressRight() {
    keyDir &= 0x0e;
}

void InputSet::pressLeft() {
    keyDir &= 0x0d;
}

void InputSet::pressUp() {
    keyDir &= 0x0b;
}

void InputSet::pressDown() {
    keyDir &= 0x07;
}

void InputSet::pressSelect() {
    keyBut &= 0x0b;
}

void InputSet::pressStart() {
    keyBut &= 0x07;
}

void InputSet::pressA() {
    keyBut &= 0x0e;
}

void InputSet::pressB() {
    keyBut &= 0x0d;
}
