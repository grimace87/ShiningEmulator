#pragma once

#include "../app.h"
#include "../gbc/inputset.h"

class GbcApp : public App {
private:
    InputSet gbcKeys;
    void updateState(uint64_t timeDiffMillis);
protected:
    void processMsg(const Message& msg) override;
    bool createRenderer() override;
public:
    GbcApp(AppPlatform& platform, RendererFactory& rendererFactory);
    ~GbcApp() override;
    void persistState() override;
    void loadPersistentState() override;
    void doWork() override;
};

