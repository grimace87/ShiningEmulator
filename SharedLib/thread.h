#pragma once

#include "messagedefs.h"

#include <cstdint>
#include <mutex>
#include <queue>

struct Message {
    Action msg;
    uint32_t param;
};

class Thread {
    static void main(Thread* instance);
    std::condition_variable stateChangeCondition;
    std::condition_variable messagePostCondition;

    std::queue<Message> msgQueue;
    bool messageMayBeAvailable;
    void loop();
    void checkMessages();
    void postMessageWhileLockAlreadyHeld(const Message& msg);

    bool isInitialised;
    bool isSuspended;
    bool isCleanedUp;

protected:
    Thread();
    bool isValid;
    std::mutex threadMutex;
    virtual bool initObject() = 0;
    virtual void doWork() = 0;
    virtual void killObject() = 0;
    virtual void preCleanup();
    virtual void processMsg(const Message& msg);

public:
    void startThread();
    void stopThread();
    void suspendThread();
    void resumeThread();
    void postMessage(const Message& msg);
    void postMessageAndWait(const Message& msg);
    bool isRunning();
};
