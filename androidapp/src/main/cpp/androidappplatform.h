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

#include "../../../../SharedLib/appplatform.h"

#include <string>
#include <jni.h>
#include <android/native_window.h>
#include <android/native_activity.h>

class GbcApp;

class AndroidAppPlatform final : public AppPlatform {
	GbcApp* app;
	jclass javaActivityClass;
	ANativeWindow* window;
    ARect pendingContentRect;
    int handleKeyboardEvent(uint32_t keyCode, bool keyDown);
    int handleGamepadInput(int32_t keyCode, bool keyDown);

protected:
	bool onAppThreadStarted(Thread* app) override;
	uint64_t getUptimeMillis() override;
	std::string getAppDir() override;
	char getSeparator() override;

public:
	PlatformRenderer* newPlatformRenderer() final;
	AudioStreamer* newAudioStreamer(Gbc* gbc) final;
	Resource* getResource(const char* fileName, bool isAsset, bool isGlShader) override;
	Resource* chooseFile(std::string fileTypeDescr, std::vector<std::string> fileTypes) override;
	void openDebugWindow(Gbc* gbc) override;
	void withCurrentTime(std::function<void(struct tm*)> func) override;
	void pollGamepad() override;
	int handleInputEvent(AInputEvent* event);

	AndroidAppPlatform(ANativeActivity* activity, ANativeWindow* window);
    ANativeActivity* activity;
	void setJavaActivityClass(jclass klass);

    // Current content rectangle of the window; this is the area where the
    // window's content should be placed to be seen by the user.
    ARect contentRect;

};
