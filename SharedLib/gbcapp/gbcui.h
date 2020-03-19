#pragma once

#include "../uielements.h"
#include <vector>

#define BTN_INDEX_DPAD      0
#define BTN_INDEX_B         1
#define BTN_INDEX_A         3
#define BTN_INDEX_SELECT    5
#define BTN_INDEX_START     7
#define BTN_INDEX_SLOWER    9
#define BTN_INDEX_FASTER    10
#define BTN_INDEX_LOAD_SS   11
#define BTN_INDEX_SAVE_SS   12
#define BTN_INDEX_POWER_OFF 13
#define BTN_INDEX_RESET_ROM 15

class GbcUi {
public:
    static void populateGameplayButtonsVector(std::vector<Image>& buttons, bool widescreen, int texture);
};
