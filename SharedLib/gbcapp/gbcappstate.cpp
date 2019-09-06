#include "gbcappstate.h"

// Static class variables and functions

GbcAppState GbcAppState::state1;
GbcAppState GbcAppState::state2;
int GbcAppState::currentStateInstanceNumber = 1;

GbcAppState* GbcAppState::getCurrentInstance() {
    if (currentStateInstanceNumber == 1) {
        return &state1;
    } else {
        return &state2;
    }
}

void GbcAppState::setCurrentInstance(GbcAppState& state) {
    if (currentStateInstanceNumber == 1) {
        state1 = state;
    } else {
        state2 = state;
    }
}

void GbcAppState::swapInstances() {
    if (currentStateInstanceNumber == 1) {
        currentStateInstanceNumber = 2;
        state2.clear(state1.appMode);
    } else {
        currentStateInstanceNumber = 1;
        state1.clear(state2.appMode);
    }
}

// Instance members

GbcAppState::GbcAppState() {
    appMode = AppMode::MAIN_MENU;
}

GbcAppState::~GbcAppState() = default;

GbcAppState::GbcAppState(GbcAppState& state) {
    this->appMode = state.appMode;
}

void GbcAppState::clear(AppMode mode) {
    appMode = mode;
}
