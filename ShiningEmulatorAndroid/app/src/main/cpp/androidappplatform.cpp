/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "androidappplatform.h"
#include "androidresource.h"
#include "androidrenderer.h"
#include "androidaudiostreamer.h"
#include "../../../../../SharedLib/app.h"

#include <android/log.h>

// Logging
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "android_app", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "android_app", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "android_app", __VA_ARGS__))
#ifndef NDEBUG
#  define LOGV(...)  ((void)__android_log_print(ANDROID_LOG_VERBOSE, "android_app", __VA_ARGS__))
#else
#  define LOGV(...)  ((void)0)
#endif

// Create a blank object
AndroidAppPlatform::AndroidAppPlatform(ANativeActivity* activity, ANativeWindow* window) :
        AppPlatform(),
        activity(activity),
        window(window) {

    // Clear state
    activity = nullptr;
    memset(&contentRect, 0, sizeof(ARect));
    destroyRequested = 0;
    destroyed = 0;
    redrawNeeded = 0;
    memset(&pendingContentRect, 0, sizeof(ARect));
    javaActivityClass = nullptr;

    // Signal using touchscreen
    usesTouch = true;
}

bool AndroidAppPlatform::onAppThreadStarted(App* app) {
    this->app = app;
    return true;
}

uint64_t AndroidAppPlatform::getUptimeMillis() {
    timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    uint64_t seconds = (uint64_t)spec.tv_sec;
    uint64_t nanos = (uint64_t)spec.tv_nsec;
    return (seconds * 1e3) + (nanos / 1e6);
}

PlatformRenderer* AndroidAppPlatform::newPlatformRenderer() {
    return new AndroidRenderer(window);
}

AudioStreamer* AndroidAppPlatform::newAudioStreamer(Gbc* gbc) {
    return new AndroidAudioStreamer(gbc);
}

Resource* AndroidAppPlatform::getResource(const char* fileName, bool isAsset, bool isGlShader) {
    return new AndroidResource(activity, fileName, isAsset, isGlShader);
}

Resource* AndroidAppPlatform::chooseFile(std::string fileTypeDescr, std::vector<std::string> fileTypes) {
    // Attach this thread to the VM if not already (no-op if already attached)
    JavaVM* vm = activity->vm;
    JNIEnv* env;
    vm->AttachCurrentThread(&env, nullptr);

    // Create the Java object and invoke it
    if (javaActivityClass) {
        jmethodID methodId = env->GetStaticMethodID(javaActivityClass, "launchFilePicker", "()V");
        jthrowable exception = env->ExceptionOccurred();
        if (exception) {
            env->ExceptionDescribe();
        } else {
            env->CallStaticVoidMethod(javaActivityClass, methodId);
        }
    }

    // Detach this thread
    vm->DetachCurrentThread();
    return nullptr;
}

FILE* AndroidAppPlatform::openFileInAppDir(std::string fileName, const char* mode) {
    // Attach this thread to the VM if not already (no-op if already attached)
    JavaVM* vm = activity->vm;
    JNIEnv* env;
    vm->AttachCurrentThread(&env, nullptr);

    // Create the Java object and invoke it
    FILE* returnFile = nullptr;
    if (javaActivityClass) {
        jmethodID methodId = env->GetStaticMethodID(javaActivityClass, "getAppDir", "()Ljava/lang/String;");
        jthrowable exception = env->ExceptionOccurred();
        if (exception) {
            env->ExceptionDescribe();
        } else {
            auto filePath = (jstring)env->CallStaticObjectMethod(javaActivityClass, methodId);
            if (filePath) {
                const char* nativeString = env->GetStringUTFChars(filePath, nullptr);
                std::string completeFileName = std::string(nativeString) + "/" + fileName;
                returnFile = fopen(completeFileName.c_str(), mode);
                env->ReleaseStringUTFChars(filePath, nativeString);
            }
        }
    }

    // Detach this thread
    vm->DetachCurrentThread();
    return returnFile;
}

void AndroidAppPlatform::openDebugWindow(Gbc *gbc) {

}

void AndroidAppPlatform::withCurrentTime(std::function<void(struct tm*)> func) {
    time_t timestamp = time(nullptr);
    struct tm* localTime = localtime(&timestamp);
    func(localTime);
}

int32_t AndroidAppPlatform::handleInputEvent(AInputEvent* event) {
    int32_t source = AInputEvent_getSource(event);
    int32_t eventType = AInputEvent_getType(event);
    int32_t sourceClass = source & AINPUT_SOURCE_CLASS_MASK;
    LOGE("AndroidThingy %d %d %d", source, eventType, sourceClass);

    if (eventType == AINPUT_EVENT_TYPE_MOTION) {
        if (sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK) {
            gamepadInputs.isConnected = true;
            float xAxis = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float yAxis = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
            float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
            gamepadInputs.left = (hatX < -0.5f) | (xAxis < -0.5f);
            gamepadInputs.right = (hatX > 0.5f) | (xAxis > 0.5f);
            gamepadInputs.up = (hatY < -0.5f) | (yAxis < -0.5f);
            gamepadInputs.down = (hatY > 0.5f) | (yAxis > 0.5f);
            gamepadInputs.xClamped = xAxis;
            gamepadInputs.yClamped = yAxis;
            return 1;
        } else {
            int32_t action = AMotionEvent_getAction(event);
            int32_t actionCode = action & AMOTION_EVENT_ACTION_MASK;
            size_t actionPointerIndex = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            size_t pointerCount = AMotionEvent_getPointerCount(event);

            for (size_t pointerIndex = 0; pointerIndex < pointerCount; pointerIndex++) {
                if ((pointerIndex != actionPointerIndex) || (actionCode == AMOTION_EVENT_ACTION_MOVE)) {
                    int32_t pointerId = AMotionEvent_getPointerId(event, pointerIndex);
                    float x = AMotionEvent_getX(event, pointerIndex);
                    float y = AMotionEvent_getY(event, pointerIndex);
                    app->updateCursor(pointerId, x, y);
                } else if (actionCode == AMOTION_EVENT_ACTION_DOWN) {
                    int32_t pointerId = AMotionEvent_getPointerId(event, 0);
                    float x = AMotionEvent_getX(event, 0);
                    float y = AMotionEvent_getY(event, 0);
                    app->addCursor(pointerId, x, y);
                } else if (actionCode == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                    int32_t pointerId = AMotionEvent_getPointerId(event, actionPointerIndex);
                    float x = AMotionEvent_getX(event, actionPointerIndex);
                    float y = AMotionEvent_getY(event, actionPointerIndex);
                    app->addCursor(pointerId, x, y);
                } else if (actionCode == AMOTION_EVENT_ACTION_UP) {
                    int32_t pointerId = AMotionEvent_getPointerId(event, actionPointerIndex);
                    app->removeCursor(pointerId);
                } else if (actionCode == AMOTION_EVENT_ACTION_POINTER_UP) {
                    int32_t pointerId = AMotionEvent_getPointerId(event, actionPointerIndex);
                    app->removeCursor(pointerId);
                } else if (actionCode == AMOTION_EVENT_ACTION_CANCEL) {
                    app->removeAllCursors();
                }
            }
            return 1;
        }
    }
    else if (eventType == AINPUT_EVENT_TYPE_KEY) {
        int32_t source = AInputEvent_getSource(event);
        if ((source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (source & AINPUT_SOURCE_DPAD) == AINPUT_SOURCE_DPAD) {
            gamepadInputs.isConnected = true;
            int32_t keyCode = AKeyEvent_getKeyCode(event);
            bool keyDown = AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN;
            handleGamepadInput(keyCode, keyDown);
            return 1;
        } else if (source == AINPUT_SOURCE_KEYBOARD) {
            int32_t keyCode = AKeyEvent_getKeyCode(event);
            bool keyDown = AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN;
            handleKeyboardEvent(keyCode, keyDown);
            return 1;
        }
    }
    return 0;
}

void AndroidAppPlatform::pollGamepad() { }

void AndroidAppPlatform::handleKeyboardEvent(uint32_t keyCode, bool keyDown) {
    unsigned int mappedKeycode = 256;
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        mappedKeycode = keyCode + 41;
    } else if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        mappedKeycode = keyCode + 68;
    }
    else {
        switch (keyCode) {
            case AKEYCODE_DPAD_LEFT:
                mappedKeycode = 37;
                break;
            case AKEYCODE_DPAD_UP:
                mappedKeycode = 38;
                break;
            case AKEYCODE_DPAD_RIGHT:
                mappedKeycode = 39;
                break;
            case AKEYCODE_DPAD_DOWN:
                mappedKeycode = 40;
                break;
            default: ;
        }
    }
    if (keyCode < 128) {
        signalKey(mappedKeycode, keyDown);
    }
}

void AndroidAppPlatform::handleGamepadInput(int32_t keyCode, bool keyDown) {
    switch (keyCode) {
        case AKEYCODE_DPAD_LEFT:
            gamepadInputs.left = keyDown;
            break;
        case AKEYCODE_DPAD_RIGHT:
            gamepadInputs.right = keyDown;
            break;
        case AKEYCODE_DPAD_UP:
            gamepadInputs.up = keyDown;
            break;
        case AKEYCODE_DPAD_DOWN:
            gamepadInputs.down = keyDown;
            break;
        case AKEYCODE_BUTTON_SELECT:
            gamepadInputs.select = keyDown;
            break;
        case AKEYCODE_BUTTON_START:
            gamepadInputs.start = keyDown;
            break;
        case AKEYCODE_BUTTON_X:
            gamepadInputs.actionLeft = keyDown;
            break;
        case AKEYCODE_BUTTON_Y:
            gamepadInputs.actionTop = keyDown;
            break;
        case AKEYCODE_BUTTON_B:
            gamepadInputs.actionRight = keyDown;
            break;
        case AKEYCODE_BUTTON_A:
            gamepadInputs.actionBottom = keyDown;
            break;
        default: ;
    }
}

void AndroidAppPlatform::setJavaActivityClass(jclass klass) {
    javaActivityClass = klass;
}
