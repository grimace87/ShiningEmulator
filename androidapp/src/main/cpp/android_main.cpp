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

//#include <jni.h>
//#include <errno.h>
//#include <unistd.h>
//#include <sys/resource.h>
//#include <stdio.h>
//#include <EGL/egl.h>
//#include <android/sensor.h>
#include <android/log.h>
#include <android/native_activity.h>

#include "androidappplatform.h"
#include "../../../../SharedLib/resource.h"
#include "../../../../SharedLib/gbcapp/gbcapp.h"

// Forward declarations
void setCallbacks(ANativeActivity* activity);
void onDestroy(ANativeActivity* activity);
void onStart(ANativeActivity* activity);
void onResume(ANativeActivity* activity);
void* onSaveInstanceState(ANativeActivity* activity, size_t* outLen);
void onPause(ANativeActivity* activity);
void onStop(ANativeActivity* activity);
void onConfigurationChanged(ANativeActivity* activity);
void onLowMemory(ANativeActivity* activity);
void onWindowFocusChanged(ANativeActivity* activity, int focused);
void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window);
void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window);
void onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue);
void onInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue);
int processInputEvents(int fd, int events, void* data);

// Logging
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "threaded_app", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "threaded_app", __VA_ARGS__))
#ifndef NDEBUG
#  define LOGV(...)  ((void)__android_log_print(ANDROID_LOG_VERBOSE, "threaded_app", __VA_ARGS__))
#else
#  define LOGV(...)  ((void)0)
#endif

// Global objects
AInputQueue* inputQueue = nullptr;
jclass globalActivityClassRef = nullptr;
const std::string CROSS_WINDOW_PERSISTENCE_FILE = "window_state.gss";

// Entry point
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    LOGV("Creating: %p\n", activity);
    setCallbacks(activity);
    activity->instance = nullptr;
}

// Assign functions for all activity lifecycle events
void setCallbacks(ANativeActivity* activity) {
    activity->callbacks->onDestroy = onDestroy;
    activity->callbacks->onStart = onStart;
    activity->callbacks->onResume = onResume;
    activity->callbacks->onSaveInstanceState = onSaveInstanceState;
    activity->callbacks->onPause = onPause;
    activity->callbacks->onStop = onStop;
    activity->callbacks->onConfigurationChanged = onConfigurationChanged;
    activity->callbacks->onLowMemory = onLowMemory;
    activity->callbacks->onWindowFocusChanged = onWindowFocusChanged;
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated = onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;
}

// Window created/destroyed
void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    LOGV("NativeWindowCreated: %p -- %p\n", activity, window);

    // Close existing app if there is one
    if (GbcApp* prevInstance = (GbcApp*)activity->instance) {
        // Fine to destroy the object - its state should have been saved already
        prevInstance->stopThread();
        delete prevInstance;
        activity->instance = nullptr;
    }

    // Create new app instance
    auto platform = new AndroidAppPlatform(activity, window);
    platform->setJavaActivityClass(globalActivityClassRef);
    GbcApp* newInstance = new GbcApp(*platform);
    newInstance->startThread();
    activity->instance = newInstance;

    // Attempt to restore state
    std::string fullPathedPersistenceFile = newInstance->platform.appendFileNameToAppDir((std::string&)CROSS_WINDOW_PERSISTENCE_FILE);
    std::fstream stream = newInstance->platform.openFile(fullPathedPersistenceFile, FileOpenMode::READ_ONLY_BINARY);
    if (stream.is_open()) {
        newInstance->suspendThread();
        newInstance->loadPersistentState(stream);
        newInstance->resumeThread();
    }

    if (!GbcApp::pendingFileToOpen.empty()) {
        newInstance->postMessage({ Action::MSG_FILE_RETRIEVED, 0 });
    }
}

void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    LOGV("NativeWindowDestroyed: %p -- %p\n", activity, window);

    // Close existing renderer
    GbcApp* activityApp = (GbcApp*)activity->instance;
    if (activityApp == nullptr) {
        return;
    }

    // Persist current state so it may be restored next time a window is created
    std::string fullPathedPersistenceFile = activityApp->platform.appendFileNameToAppDir((std::string&)CROSS_WINDOW_PERSISTENCE_FILE);
    std::fstream stream = activityApp->platform.openFile(fullPathedPersistenceFile, FileOpenMode::WRITE_NEW_FILE_BINARY);
    if (stream.is_open()) {
        activityApp->persistState(stream);
    }

    // Clean up the app thread
    activityApp->stopThread();

    // TODO - Re-instate this
    //delete activityApp;

    activity->instance = nullptr;
}

// Basic activity lifecycle
void onStart(ANativeActivity* activity) {
    LOGV("Start: %p\n", activity);
}

void onResume(ANativeActivity* activity) {
    GbcApp* app = (GbcApp*)activity->instance;
    LOGV("Resume: %p\n", activity);
    if (app) {
        app->resumeThread();
    }
}

void onPause(ANativeActivity* activity) {
    GbcApp* app = (GbcApp*)activity->instance;
    LOGV("Pause: %p\n", activity);
    if (app) {
        app->suspendThread();
    }
}

void onStop(ANativeActivity* activity) {
    LOGV("Stop: %p\n", activity);
}

void onDestroy(ANativeActivity* activity) {
    // App* app = (App*)activity->instance;
    LOGV("Destroy: %p\n", activity);
    /* TODO - Consider re-introducing
    if (app) {
        app->stopThread();
        delete app;
        activity->instance = nullptr;
    }
     */
}

// Input queue
void onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue) {
    LOGV("InputQueueCreated: %p -- %p\n", activity, queue);
    inputQueue = queue;
    AInputQueue_attachLooper(queue, ALooper_forThread(), ALOOPER_POLL_CALLBACK, processInputEvents, (void*) activity);
}

void onInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue) {
    LOGV("InputQueueDestroyed: %p -- %p\n", activity, queue);
    if (inputQueue) {
        AInputQueue_detachLooper(queue);
    }
    inputQueue = NULL;
}

// Instance state
void* onSaveInstanceState(ANativeActivity* activity, size_t* outLen) {
    // TODO - Do we need this?
    // App* app = (App*)activity->instance;
    // if (app) { app->persistState(); }
    *outLen = 0;
    return nullptr;
}

// Other stuff
void onConfigurationChanged(ANativeActivity* activity) {
    LOGV("ConfigurationChanged: %p\n", activity);
}

void onLowMemory(ANativeActivity* activity) {
    LOGV("LowMemory: %p\n", activity);
}

void onWindowFocusChanged(ANativeActivity* activity, int focused) {
    LOGV("WindowFocusChanged: %p -- %d\n", activity, focused);
}

// Handling input events
int processInputEvents(int fd, int events, void* data) {
    if (inputQueue) {
        auto activity = (ANativeActivity*)data;
        GbcApp* app = (GbcApp*)activity->instance;
        AInputEvent* event;
        if (AInputQueue_hasEvents(inputQueue)) {
            while (AInputQueue_getEvent(inputQueue, &event) >= 0) {
                LOGV("New input event: type=%d\n", AInputEvent_getType(event));
                if (AInputQueue_preDispatchEvent(inputQueue, event)) {
                    continue;
                }
                auto platform = (AndroidAppPlatform*) &app->platform;
                int handled = platform->handleInputEvent(event);
                AInputQueue_finishEvent(inputQueue, event, handled);
            }
        }
    }

    // Return 1 to keep receiving events
    return 1;
}

// JNI functions
extern "C" {

JNIEXPORT void JNICALL Java_com_grimace_shiningemulatorandroid_MainActivity_filePicked(JNIEnv *env, jobject thisObject, jstring fileName) {
    GbcApp::pendingFileToOpen = (char*)env->GetStringUTFChars(fileName, nullptr);
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }

    jclass klass = env->FindClass("com/grimace/shiningemulatorandroid/MainActivity");
    if (klass) {
        globalActivityClassRef = (jclass)env->NewGlobalRef(klass);
    }

    return JNI_VERSION_1_6;
}
}
