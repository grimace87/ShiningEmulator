#include "gbcapp.h"

#include "gbcappstate.h"
#include "gbcrenderer.h"
#include "../uidefs.h"
#include "../resource.h"
#include "../appplatform.h"
#include "../audiostreamer.h"
#include "../messagedefs.h"
#include "gbcui.h"

#include <string>

char* GbcApp::pendingFileToOpen = nullptr;

GbcApp::GbcApp(AppPlatform& platform) : menu(Menu::buildMain()), platform(platform), gbcKeys() {
    platform.releaseAllInputs();
    state = GbcAppState::MAIN_MENU;
    gbcKeys.clear();
    renderer = nullptr;
    audioStreamer = nullptr;
}

GbcApp::~GbcApp() = default;

bool GbcApp::initObject() {
    platform.onAppThreadStarted(this);
    return createRenderer() && createAudioStreamer();
}

void GbcApp::killObject() {
    if (renderer) {
        renderer->stopThread();
    }
    if (audioStreamer) {
        audioStreamer->stop();
    }
}

Gbc* GbcApp::getGbc() {
	return &gbc;
}

void GbcApp::requestWindowResize(int width, int height) {
    if (renderer) {
        renderer->requestWindowResize(width, height);
    }
}

void GbcApp::persistState(std::ostream& stream) {
    stream.write(reinterpret_cast<char*>(&state), sizeof(GbcAppState));
    if (gbc.isRunning || gbc.isPaused) {
        bool True = true;
        stream.write(reinterpret_cast<char*>(&True), sizeof(bool));
        std::string fileName = gbc.getLoadedFileName();
        size_t stringLength = fileName.length();
        stream.write(reinterpret_cast<char*>(&stringLength), sizeof(size_t));
        stream.write(fileName.c_str(), stringLength);
        gbc.saveSaveState(stream);
    } else {
        bool False = false;
        stream.write(reinterpret_cast<char*>(&False), sizeof(bool));
    }
}

void GbcApp::loadPersistentState(std::istream& stream) {
    bool gbcWasSaved;
    stream.read(reinterpret_cast<char*>(&state), sizeof(GbcAppState));
    stream.read(reinterpret_cast<char*>(&gbcWasSaved), sizeof(bool));
    if (gbcWasSaved && (state == GbcAppState::PLAYING)) {
        size_t stringLength;
        stream.read(reinterpret_cast<char*>(&stringLength), sizeof(size_t));
        if (stringLength == 0) {
            state = GbcAppState::MAIN_MENU;
            return;
        }
        char* fileNameChars = new char[stringLength + 1];
        stream.read(fileNameChars, stringLength * sizeof(char));
        fileNameChars[stringLength] = '\0';

        // TODO - Open a file properly!!!
        Resource* file = platform.getResource(fileNameChars, false, false);
        if (file) {
            openRomFile(file);
            gbc.loadSaveState(stream);
            delete file;
        } else {
            state = GbcAppState::MAIN_MENU;
        }
        delete[] fileNameChars;
    } else {
        state = GbcAppState::MAIN_MENU;
    }
}

void GbcApp::openRomFile(Resource* file) {
    if (file) {
        gbc.loadRom(file->fileName, file->rawStream, file->rawDataLength, platform);
        if (gbc.romProperties.valid) {
            state = GbcAppState::PLAYING;
            gbc.reset();
        }
    }
}

void GbcApp::processMsg(const Message& msg) {
    Thread::processMsg(msg);
    switch (msg.msg) {
        case Action::MSG_OPEN_FILE: {
            Resource* file = platform.chooseFile("ROM file", { ".gb", ".gbc" });
            if (file) {
                openRomFile(file);
            }
        }
            break;
        case Action::MSG_FILE_RETRIEVED:
            if (GbcApp::pendingFileToOpen) {
                Resource* resource = platform.getResource(GbcApp::pendingFileToOpen, false, false);
                if (resource) {
                    std::string nameForSramFile(GbcApp::pendingFileToOpen);
                    auto lastSlash = nameForSramFile.find_last_of("/\\", nameForSramFile.length());
                    if (lastSlash != std::string::npos) {
                        nameForSramFile = nameForSramFile.substr(lastSlash + 1);
                    }
                    gbc.loadRom(nameForSramFile, resource->rawStream, resource->rawDataLength, platform);
                    if (gbc.romProperties.valid) {
                        state = GbcAppState::PLAYING;
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

void GbcApp::addCursor(int id, float xPixels, float yPixels) {
    platform.addCursor(threadMutex, id, xPixels, yPixels);
}

void GbcApp::updateCursor(int id, float xPixels, float yPixels) {
    platform.updateCursor(threadMutex, id, xPixels, yPixels);
}

void GbcApp::removeCursor(int id) {
    platform.removeCursor(threadMutex, id);
}

void GbcApp::removeAllCursors() {
    platform.removeAllCursors(threadMutex);
}

bool GbcApp::createRenderer() {
    renderer = new GbcRenderer(&platform, platform.newPlatformRenderer(), &gbc);
    renderer->startThread();
    return true;
}

bool GbcApp::createAudioStreamer() {
    audioStreamer = platform.newAudioStreamer(&gbc);
    audioStreamer->start();
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
        if (state == GbcAppState::MAIN_MENU) {
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
        else if (state == GbcAppState::PLAYING) {
            auto renderer = (GbcRenderer*)this->renderer;
            auto& dpadButton = renderer->gameplayButtons[BTN_INDEX_DPAD];
            auto& bButton = renderer->gameplayButtons[BTN_INDEX_B];
            auto& aButton = renderer->gameplayButtons[BTN_INDEX_A];
            auto& selectButton = renderer->gameplayButtons[BTN_INDEX_SELECT];
            auto& startButton = renderer->gameplayButtons[BTN_INDEX_START];
            auto& slowerButton = renderer->gameplayButtons[BTN_INDEX_SLOWER];
            auto& fasterButton = renderer->gameplayButtons[BTN_INDEX_FASTER];
            auto& loadSaveStateButton = renderer->gameplayButtons[BTN_INDEX_LOAD_SS];
            auto& saveSaveStateButton = renderer->gameplayButtons[BTN_INDEX_SAVE_SS];
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
            } else if (slowerButton.containsCoords(downXUnits, downYUnits)) {
                if (!cursor.downHandled) {
                    gbc.slowDown();
                }
            } else if (fasterButton.containsCoords(downXUnits, downYUnits)) {
                if (!cursor.downHandled) {
                    gbc.speedUp();
                }
            } else if (loadSaveStateButton.containsCoords(downXUnits, downYUnits)) {
                if (!cursor.downHandled) {
                    std::fstream file = platform.openFileInAppDir("temp.gss", FileOpenMode::READ_ONLY_BINARY);
                    if (file.is_open()) {
                        gbc.loadSaveState(file);
                    }
                }
            } else if (saveSaveStateButton.containsCoords(downXUnits, downYUnits)) {
                if (!cursor.downHandled) {
                    std::fstream file = platform.openFileInAppDir("temp.gss", FileOpenMode::WRITE_NEW_FILE_BINARY);
                    if (file.is_open()) {
                        gbc.saveSaveState(file);
                    }
                }
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
        if (renderer->signalFrameReady(frameTimeAccumulated, (uint32_t)state)) {
            frameTimeAccumulated = 0;
        }
    }
}

void GbcApp::updateState(uint64_t timeDiffMillis) {
    if (state == GbcAppState::PLAYING) {
        if (gbc.isRunning) {
            gbc.doWork(timeDiffMillis, this->gbcKeys);
        } else {
            state = GbcAppState::MAIN_MENU;
        }
    }
}
