#pragma once

#include "../../../../SharedLib/audiostreamer.h"
#include <oboe/Oboe.h>

class AndroidAudioStreamer : public AudioStreamer, oboe::AudioStreamCallback {
    oboe::AudioStream* stream;

    oboe::DataCallbackResult
    onAudioReady(oboe::AudioStream* oboeStream, void* audioData, int32_t numFrames) override;
    void setStream(oboe::AudioStream* stream);

public:
    explicit AndroidAudioStreamer(Gbc* gbc);
    ~AndroidAudioStreamer();

    void start() override;
    void stop() override;
};
