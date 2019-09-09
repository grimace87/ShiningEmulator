#include "gbcapp.h"

#include "gbcappstate.h"
#include "gbcrenderer.h"
#include "../uidefs.h"
#include "../uielements.h"
#include "../gbc/gbc.h"
#include "../rendererfactory.h"
#include "../resource.h"
#include "../appplatform.h"
#include "../messagedefs.h"

GbcAppState persistentState;
Gbc gbc;

GbcApp::GbcApp(AppPlatform& platform, RendererFactory& rendererFactory) : App(platform, rendererFactory) {
    loadPersistentState();
}

GbcApp::~GbcApp() {
    App::~App();
}

Gbc* GbcApp::getGbc() {
	return &gbc;
}

void GbcApp::persistState() {
    persistentState = *GbcAppState::getCurrentInstance();
}

void GbcApp::loadPersistentState() {
    GbcAppState::setCurrentInstance(persistentState);
}

void GbcApp::processMsg(const Message& msg) {
    switch (msg.msg) {
        case Action::MSG_EXIT:
            running = false;
            break;
        case Action::MSG_PAUSE:
            suspendThread();
            break;
        case Action::MSG_RESUME:
            resumeThread();
            break;
        case Action::MSG_OPEN_FILE: {
            Resource* file = platform.chooseFile("ROM file", { ".gb", ".gbc" });
            if (file) {
                gbc.loadRom(file->fileName, file->rawStream, file->rawDataLength, platform);
                if (gbc.romProperties.valid) {
                    GbcAppState::getCurrentInstance()->appMode = AppMode::PLAYING;
                    gbc.reset();
                }
                delete file;
            }
        }
            break;
        case Action::MSG_FILE_RETRIEVED:
            if (App::pendingFileToOpen) {
                Resource* resource = platform.getResource(App::pendingFileToOpen, false, false);
                if (resource) {
                    gbc.loadRom("rom.gb", resource->rawStream, resource->rawDataLength, platform);
                    if (gbc.romProperties.valid) {
                        GbcAppState::getCurrentInstance()->appMode = AppMode::PLAYING;
                        gbc.reset();
                    }
                }
            }
            break;
        case Action::MSG_UNUSED:
            break;
		case Action::MSG_OPEN_DEBUGGER:
			platform.openDebugWindow(&gbc);
			break;
        default: ;
    }
}

bool GbcApp::createRenderer() {
    renderer = new GbcRenderer(&platform, rendererFactory.newPlatformRenderer(), &gbc);
    renderer->startThread();
    return true;
}

uint64_t startTime = 0;
uint64_t frameTimeAccumulated = 0;
void GbcApp::doWork() {
    // Get timing
    if (startTime == 0) {
        startTime = platform.getUptimeMillis();
        return;
    }
    uint64_t endTime = platform.getUptimeMillis();
    uint64_t timeDiff = endTime - startTime;

    // Check for zero-update or over-large time passing
    if (timeDiff == 0) {
        return;
    }
    if (timeDiff > 200) {
        timeDiff = 200;
    }

    // Formulate the gbc-compatible input set
    this->gbcKeys.clear();
    platform.pollGamepad();
    threadMutex.lock();
    //if (platform.cursors.size() > 0)
    //{
    //	AppPlatform::Cursor& firstCursor = platform.cursors.front();
    //	dir = 0.01f * (firstCursor.downXPixels - firstCursor.xPixels);
    //}
    if (platform.gamepadInputs.isConnected) {
        if (platform.gamepadInputs.left || platform.gamepadInputs.xClamped < -0.5f) {
            gbcKeys.pressLeft();
        }
        if (platform.gamepadInputs.right || platform.gamepadInputs.xClamped > 0.5f) {
            gbcKeys.pressRight();
        }
        if (platform.gamepadInputs.up || platform.gamepadInputs.yClamped < -0.5f) {
            gbcKeys.pressUp();
        }
        if (platform.gamepadInputs.down || platform.gamepadInputs.yClamped > 0.5f) {
            gbcKeys.pressDown();
        }
        if (platform.gamepadInputs.select) {
            gbcKeys.pressSelect();
        }
        if (platform.gamepadInputs.start) {
            gbcKeys.pressStart();
        }
        if (platform.gamepadInputs.actionBottom) {
            gbcKeys.pressA();
        }
        if (platform.gamepadInputs.actionLeft) {
            gbcKeys.pressB();
        }
    }
    if (platform.keyboardInputs[37]) {
        gbcKeys.pressLeft();
    }
    if (platform.keyboardInputs[38]) {
        gbcKeys.pressUp();
    }
    if (platform.keyboardInputs[39]) {
        gbcKeys.pressRight();
    }
    if (platform.keyboardInputs[40]) {
        gbcKeys.pressDown();
    }
    if (platform.keyboardInputs[88]) {
        gbcKeys.pressA();
    }
    if (platform.keyboardInputs[90]) {
        gbcKeys.pressB();
    }
    if (platform.keyboardInputs[13]) {
        gbcKeys.pressStart();
    }
    if (platform.keyboardInputs[16]) {
        gbcKeys.pressSelect();
    }
    threadMutex.unlock();

    // Handle inputs
    for (auto& iter : platform.cursors) {
        auto& cursor = iter.second;
        int windowWidth, windowHeight;
        this->renderer->queryCanvasSize(&windowWidth, &windowHeight);
        float downXUnits = 2.0f * cursor.downXPixels / (float)windowWidth - 1.0f;
        float downYUnits = -2.0f * cursor.downYPixels / (float)windowHeight + 1.0f;
        float currentXUnits = 2.0f * cursor.xPixels / (float)windowWidth - 1.0f;
        float currentYUnits = -2.0f * cursor.yPixels / (float)windowHeight + 1.0f;
        auto mode = GbcAppState::getCurrentInstance()->appMode;
        if (mode == AppMode::MAIN_MENU) {
            if (!cursor.downHandled) {
                auto renderer = (GbcRenderer*)((GbcAppState*)this->renderer);
                for (auto& button : renderer->uiButtons) {
                    bool clicked = button.containsCoords(downXUnits, downYUnits);
                    if (clicked) {
                        this->postMessage({ Action::MSG_OPEN_FILE, 0 });
                        cursor.downHandled = true;
                        return;
                    }
                }
            }
        }
        else if (mode == AppMode::PLAYING) {
            auto renderer = (GbcRenderer*)this->renderer;
            auto& dpadButton = renderer->gameplayButtons[0];
            auto& bButton = renderer->gameplayButtons[1];
            auto& aButton = renderer->gameplayButtons[3];
            auto& selectButton = renderer->gameplayButtons[5];
            auto& startButton = renderer->gameplayButtons[7];
            if (dpadButton.containsCoords(downXUnits, downYUnits)) {
                auto compassMask = dpadButton.compassFromCentre(currentXUnits, currentYUnits);
                if (compassMask & (unsigned int)CompassMask::NORTH) {
                    gbcKeys.pressUp();
                }
                if (compassMask & (unsigned int)CompassMask::EAST) {
                    gbcKeys.pressRight();
                }
                if (compassMask & (unsigned int)CompassMask::SOUTH) {
                    gbcKeys.pressDown();
                }
                if (compassMask & (unsigned int)CompassMask::WEST) {
                    gbcKeys.pressLeft();
                }
            } else if (bButton.containsCoords(downXUnits, downYUnits)) {
                gbcKeys.pressB();
                if (aButton.containsCoords(currentXUnits, currentYUnits)) {
                    gbcKeys.pressA();
                }
            } else if (aButton.containsCoords(downXUnits, downYUnits)) {
                gbcKeys.pressA();
                if (bButton.containsCoords(currentXUnits, currentYUnits)) {
                    gbcKeys.pressB();
                }
            } else if (selectButton.containsCoords(downXUnits, downYUnits)) {
                gbcKeys.pressSelect();
            } else if (startButton.containsCoords(downXUnits, downYUnits)) {
                gbcKeys.pressStart();
            }
        }
        cursor.downHandled = true;
    }

    // Mutate state
    updateState(timeDiff);
    startTime = endTime;
    frameTimeAccumulated += timeDiff;

    // Signal render frame
    if (renderer) {
        if (renderer->signalFrameReady(frameTimeAccumulated, GbcAppState::getCurrentInstance())) {
            frameTimeAccumulated = 0;
        }
    }
}

void GbcApp::updateState(uint64_t timeDiffMillis) {
    if (GbcAppState::getCurrentInstance()->appMode == AppMode::PLAYING) {
        if (gbc.running) {
            gbc.doWork(timeDiffMillis, this->gbcKeys);
        } else {
            GbcAppState::getCurrentInstance()->appMode = AppMode::MAIN_MENU;
        }
    }
}
