#pragma once

#include "../app.h"
#include "gbcappstate.h"
#include "../gbc/inputset.h"
#include "../gbc/gbc.h"

class GbcApp : public App {
private:
	Gbc gbc;
    InputSet gbcKeys;
    GbcAppState state;
    void updateState(uint64_t timeDiffMillis);
protected:
    void processMsg(const Message& msg) override;
    bool createRenderer() override;
    bool createAudioStreamer() override;
public:
    GbcApp(AppPlatform& platform);
    ~GbcApp() override;
	Gbc* getGbc();
    void persistState() override;
    void loadPersistentState() override;
    void doWork() override;
};

