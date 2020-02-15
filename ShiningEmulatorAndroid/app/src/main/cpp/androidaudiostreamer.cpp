#include "androidaudiostreamer.h"

#include "../../../../../SharedLib/gbc/gbc.h"
#include <string>
#include <android/log.h>

#define LOG_ERR(fmt, val) __android_log_print(ANDROID_LOG_ERROR, "AudioTest", fmt, val)
#define RET_ERR_RES(fmt) if (result != oboe::Result::OK) { LOG_ERR(fmt, oboe::convertToText(result)); return; }

AndroidAudioStreamer::AndroidAudioStreamer(Gbc* gbc): AudioStreamer(gbc) {
    isPlaying = false;
}

AndroidAudioStreamer::~AndroidAudioStreamer() {
    if (isPlaying) {
        stop();
    }
}

oboe::DataCallbackResult
AndroidAudioStreamer::onAudioReady(oboe::AudioStream* oboeStream, void* audioData, int32_t numFrames) {
    if (isPlaying) {
        uint32_t frames = numFrames >= 0 ? (uint32_t)numFrames : 0;
        gbc->audioUnit.onAudioThreadNeedingData((int16_t*)audioData, frames);
        return oboe::DataCallbackResult::Continue;
    }
    return oboe::DataCallbackResult::Stop;
}

void AndroidAudioStreamer::setStream(oboe::AudioStream* stream) {
    this->stream = stream;
    isPlaying = true;
}

void AndroidAudioStreamer::start() {
    if (isPlaying) {
        return;
    }

    oboe::AudioStreamBuilder builder;
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setCallback(this);
    builder.setFormat(oboe::AudioFormat::I16);

    oboe::AudioStream* stream;
    oboe::Result result = builder.openStream(&stream);
    RET_ERR_RES("Error opening stream: %s")

    // Set buffer size using a 'good rule of thumb'
    int32_t sensibleBufferSize = 2 * stream->getFramesPerBurst();
    auto bufferSetResult = stream->setBufferSizeInFrames(sensibleBufferSize);
    if (bufferSetResult) {
        LOG_ERR("Error setting buffer size: %d", bufferSetResult.value());
    }
    RET_ERR_RES("Error setting buffer size: %s")

    setStream(stream);
    result = stream->requestStart();
    RET_ERR_RES("Error starting stream: %s")
}

void AndroidAudioStreamer::stop() {
    isPlaying = false;
    if (stream) {
        stream->requestStop();
        stream->close();
        stream = nullptr;
    }
}
