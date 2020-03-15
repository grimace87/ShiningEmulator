#include "thread.h"

#include <thread>

enum class StateChange {
    SUSPEND,
    RESUME,
    TERMINATE
};

Thread::Thread() {
    isValid = true;
    isInitialised = false;
    isSuspended = false;
    isCleanedUp = false;
}

void Thread::startThread() {
    // Acquire the mutex
    std::unique_lock<std::mutex> lock(threadMutex);

    // Verify correct state - valid, not initialised, not suspended, not cleaned up
    if (!isValid || isInitialised || isSuspended || isCleanedUp) {
        return;
    }

    // Start the thread and wait for signal of initialisation
    std::thread thread(main, this);
    stateChangeCondition.wait(lock, [&]() { return isInitialised; });
    thread.detach();
}

void Thread::suspendThread() {
    // Acquire the mutex
    std::unique_lock<std::mutex> lock(threadMutex);

    // Verify correct state - valid, initialised, not suspended, not cleaned up
    if (!isValid || !isInitialised || isSuspended || isCleanedUp) {
        return;
    }

    // Request suspension and wait for acknowledgement
    postMessageWhileLockAlreadyHeld({ Action::MSG_STATE_TRANSITION, (uint32_t)StateChange::SUSPEND });
    stateChangeCondition.wait(lock, [&]() { return isSuspended; });
}

void Thread::resumeThread() {
    // Acquire the mutex
    std::unique_lock<std::mutex> lock(threadMutex);

    // Verify correct state - valid, initialised, suspended, not cleaned up
    if (!isValid || !isInitialised || !isSuspended || isCleanedUp) {
        return;
    }

    // Request termination and wait for acknowledgement
    postMessageWhileLockAlreadyHeld({ Action::MSG_STATE_TRANSITION, (uint32_t)StateChange::RESUME });
    stateChangeCondition.wait(lock, [&]() { return !isSuspended; });
}

void Thread::stopThread() {
    // Acquire the mutex
    std::unique_lock<std::mutex> lock(threadMutex);

    // Verify correct state - valid, initialised, not cleaned up
    if (!isValid || !isInitialised || isCleanedUp) {
        return;
    }

    // Request termination and wait for acknowledgement
    preCleanup();
    postMessageWhileLockAlreadyHeld({ Action::MSG_STATE_TRANSITION, (uint32_t)StateChange::TERMINATE });
    stateChangeCondition.wait(lock, [&]() { return isCleanedUp; });
}

void Thread::preCleanup() {}

void Thread::main(Thread* instance) {
    // Perform initialisation under mutex lock, any exceptions silently cause failure to start
    {
        std::lock_guard<std::mutex> guard(instance->threadMutex);
        instance->isValid = instance->initObject();
        instance->isInitialised = true;
    }

    // Mutex now unlocked, signal condition variable and run thread if init was successful
    instance->stateChangeCondition.notify_one();
    if (instance->isValid) {
        instance->loop();
    }
}

void Thread::loop() {
    using namespace std::chrono_literals;
    while (true) {
        // Loop while not suspended
        while (!isSuspended && isValid) {
            checkMessages();
            doWork();
        }

        // Check if termination was just requested, else notify that state moved to suspended
        if (!isValid) {
            break;
        }
        stateChangeCondition.notify_one();

        // Sleep while suspended
        if (isSuspended && isValid) {
            checkMessages();
            std::this_thread::sleep_for(250ms);
        }

        // Check if termination was just requested, else notify that state moved to resumed
        if (!isValid) {
            break;
        }
        stateChangeCondition.notify_one();
    }

    // Terminate the thread
    {
        std::unique_lock<std::mutex> lock(threadMutex);
        killObject();
        isCleanedUp = true;
    }
    stateChangeCondition.notify_all();
}

void Thread::checkMessages() {
    if (messageMayBeAvailable) {
        std::lock_guard<std::mutex> guard(threadMutex);
        bool messagesProcessed = false;
        while (!msgQueue.empty()) {
            messagesProcessed = true;
            Message msg = msgQueue.front();
            msgQueue.pop();
            processMsg(msg);
        }
        messageMayBeAvailable = false;
        if (messagesProcessed) {
            messagePostCondition.notify_all();
        }
    }
}

void Thread::postMessage(const Message& msg) {
    std::unique_lock<std::mutex> lock(threadMutex);
    msgQueue.push(msg);
    messageMayBeAvailable = true;
}

void Thread::postMessageWhileLockAlreadyHeld(const Message& msg) {
    msgQueue.push(msg);
    messageMayBeAvailable = true;
}

void Thread::postMessageAndWait(const Message& msg) {
    std::unique_lock<std::mutex> lock(threadMutex);
    msgQueue.push(msg);
    messageMayBeAvailable = true;
    messagePostCondition.wait(lock, [&]() { return messageMayBeAvailable; });
}

void Thread::processMsg(const Message& msg) {
    if (msg.msg == Action::MSG_STATE_TRANSITION) {
        uint32_t param = msg.param;
        if (param == (uint32_t)StateChange::SUSPEND) {
            isSuspended = true;
        } else if (param == (uint32_t)StateChange::RESUME) {
            isSuspended = false;
        } else if (param == (uint32_t)StateChange::TERMINATE) {
            isValid = false;
        } else {
            return;
        }
    } else if (msg.msg == Action::MSG_REQUEST_EXIT) {
        isValid = false;
    }
}

bool Thread::isRunning() {
    return isValid && !isCleanedUp;
}
