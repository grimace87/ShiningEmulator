#pragma once

#include "../thread.h"
#include "../menu.h"
#include "gbcappstate.h"
#include "../gbc/inputset.h"
#include "../gbc/gbc.h"
#include "../resource.h"

class GbcRenderer;
class AudioStreamer;

class GbcApp : public Thread {
private:
	Gbc gbc;
    InputSet gbcKeys;
    GbcAppState state;
	AudioStreamer* audioStreamer;
    GbcRenderer* renderer;
    void updateState(uint64_t timeDiffMillis);
    void openRomFile(Resource* file);
protected:
    void processMsg(const Message& msg) override;
    bool createRenderer();
    bool createAudioStreamer();
public:
    GbcApp(AppPlatform& platform);
    ~GbcApp();
	bool initObject() final;
	void killObject() final;
	Gbc* getGbc();
    void persistState(std::ostream& stream);
    void loadPersistentState(std::istream& stream);
    void doWork() override;
	void requestWindowResize(int width, int height);
    Menu menu;
    AppPlatform& platform;
    static std::string pendingFileToOpen;
    // Cursor modifiers merely call the equivalents on the AppPlatform object
    void addCursor(int id, float xPixels, float yPixels);
    void updateCursor(int id, float xPixels, float yPixels);
    void removeCursor(int id);
    void removeAllCursors();
};

