#pragma once

#include "../uielements.h"
#include <vector>

#define BTN_INDEX_DPAD   0
#define BTN_INDEX_B      1
#define BTN_INDEX_A      3
#define BTN_INDEX_SELECT 5
#define BTN_INDEX_START  7
#define BTN_INDEX_SLOWER 9
#define BTN_INDEX_FASTER 10

class GbcUi {
public:
    static void populateGameplayButtonsVector(std::vector<Image>& buttons, bool widescreen, int texture);
};
