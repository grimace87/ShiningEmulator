#pragma once

#include "../appstate.h"

enum class AppMode {
    MAIN_MENU,
    PLAYING
};

class GbcAppState : public AppState {
    static GbcAppState state1, state2;
    static int currentStateInstanceNumber;
    void clear(AppMode mode);

public:
    GbcAppState();
    ~GbcAppState() override;
    GbcAppState(GbcAppState& state);
    AppMode appMode;
    int frameToRender;
    static GbcAppState* getCurrentInstance();
    static void setCurrentInstance(GbcAppState& state);
    static void swapInstances();
};