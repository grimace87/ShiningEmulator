#pragma once

#include "../SharedLib/audiostreamer.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <thread>

class WindowsAudioStreamer: public AudioStreamer {
    bool initialised;
    HANDLE hEvent;
    IMMDeviceEnumerator* deviceEnumerator;
    IMMDevice* device;
    IAudioClient* audioClient;
    IAudioRenderClient* renderClient;
    UINT framesPerBuffer = 0;
    std::thread thread;

    static void threadMain(WindowsAudioStreamer& streamObject);
    bool buildAndOpenStream();
    void startPlaying();
    void playUntilStopped();
    void close();

public:
    explicit WindowsAudioStreamer(Gbc* gbc);
    ~WindowsAudioStreamer() override;
    void start() final;
    void stop() final;
};
