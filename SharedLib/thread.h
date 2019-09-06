#pragma once

#include "messagedefs.h"

#include <cstdint>
#include <mutex>
#include <queue>

class Message {
public:
    Action msg;
    uint32_t param;
};

class Thread {
    static void main(Thread* instance);
    void loop();
    std::queue<Message> msgQueue;
    bool suspended;

protected:
    Thread();
    bool valid;
    std::mutex threadMutex;
    std::condition_variable cond;
    virtual bool initObject() = 0;
    virtual void doWork() = 0;
    virtual void killObject() = 0;
    virtual void processMsg(const Message& msg) = 0;

public:
    bool running;
    void startThread();
    virtual void stopThread();
    void suspendThread();
    void resumeThread();
    void postMessage(const Message& msg);
    void postMessageAndWait(const Message& msg);
};
