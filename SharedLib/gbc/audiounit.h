#pragma once

#include <cstdint>

class AudioUnit {
    uint8_t* ioPorts;
    uint64_t cumulativeTicks;
    size_t currentBufferHead;
    int16_t* buffer;
    int16_t* noise;

    bool s4Running;

    bool s4HasLength;
    size_t s4LengthInSamples;
    size_t s4CurrentLengthProgress;

    bool s4HasEnvelope;
    bool s4EnvelopeIncreases;
    uint32_t s4EnvelopeValue;
    size_t s4EnvelopeStepInSamples;
    size_t s4CurrentEnvelopeStepProgress;

    void writeFile();
    bool fileHasWritten;

public:
    AudioUnit();
    ~AudioUnit();
    void reset(uint8_t* gbcPorts);
    void stopAllSound();
    void simulate(uint64_t clockTicks);

    void startChannel4(uint8_t initByte);
};
