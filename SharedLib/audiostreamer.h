#pragma once

class Gbc;

class AudioStreamer {
protected:
    Gbc* gbc;

public:
    explicit AudioStreamer(Gbc* gbc);
    virtual ~AudioStreamer();
    virtual void start() = 0;
    virtual void stop() = 0;
    bool isPlaying;
};
