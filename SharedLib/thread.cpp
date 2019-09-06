#include "thread.h"

#include <thread>

Thread::Thread() {
    running = false;
    suspended = false;
    valid = false;
}

void Thread::startThread() {
    // Signal init conditions not met
    running = false;
    suspended = false;

    // Acquire the mutex and wait for signal of initialisation (running)
    std::unique_lock<std::mutex> lock(threadMutex);
    std::thread thread(main, this);
    cond.wait(lock, [&]() { return running; });
    lock.unlock();
    thread.detach();
}

void Thread::stopThread() {
    // Acquire the mutex, signal to stop looping, and wait for signal of end
    if (running) {
        std::unique_lock<std::mutex> lock(threadMutex);
        running = false;
        cond.wait(lock, [&]() { return !valid; });
        lock.unlock();
    }
}

void Thread::main(Thread* instance) {
    instance->valid = false;
    // Perform initialisation under mutex lock, any exceptions silently cause failure to start
    {
        std::lock_guard<std::mutex> guard(instance->threadMutex);
        instance->valid = instance->initObject();
    }

    // Mutex now unlocked, signal condition variable and run thread if init was successful
    instance->running = true;
    instance->cond.notify_one();
    if (instance->valid) {
        instance->loop();
    }
}

void Thread::loop() {
    using namespace std::chrono_literals;
    while (running) {
        if (suspended) {
            std::this_thread::sleep_for(250ms);
            continue;
        }
        while (!msgQueue.empty()) {
            std::lock_guard<std::mutex> guard(threadMutex);
            Message msg = msgQueue.front();
            msgQueue.pop();
            processMsg(msg);
        }
        doWork();
    }
    threadMutex.lock();
    valid = false;
    killObject();
    threadMutex.unlock();
    cond.notify_all();
}

void Thread::postMessage(const Message& msg) {
    threadMutex.lock();
    msgQueue.push(msg);
    threadMutex.unlock();
}

void Thread::postMessageAndWait(const Message& msg) {
    std::unique_lock<std::mutex> lock(threadMutex);
    msgQueue.push(msg);
    cond.wait(lock);
}

void Thread::suspendThread() {
    suspended = true;
}

void Thread::resumeThread() {
    suspended = false;
}
