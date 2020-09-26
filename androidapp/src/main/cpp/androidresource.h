#pragma once

#include "../../../../SharedLib/resource.h"

#include <android/asset_manager.h>
#include <android/native_activity.h>

class AndroidResource : public Resource {
	AAsset* asset;
	AAssetManager* assetManager;
	bool bufferIsCopy;
	void openFromAsset(ANativeActivity* activity, const char* fileName);
	void openFromFile(const char* fileName);

public:
	AndroidResource(ANativeActivity* activity, const char* fileName, bool isAsset, bool isGlShader);
	~AndroidResource();
};