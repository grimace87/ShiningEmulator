#pragma once

#include <cstdint>

struct Sample{
    int16_t left;
    int16_t right;
};

class AudioUnit {
    uint8_t* ioPorts;
    uint64_t cumulativeTicks;
    size_t currentBufferHead;
    Sample* buffer;
    int16_t waveformData[32];

    bool s1Running;

    size_t s1DutyOnLengthInTicks;
    uint8_t s1DutyBits;
    size_t s1DutyPeriodInTicks;
    size_t s1CurrentDutyProgress;

    bool s1HasSweep;
    bool s1SweepIncreases;
    size_t s1SweepPeriodInTicks;
    size_t s1CurrentSweepProgress;
    size_t s1CurrentFrequency;
    size_t s1FrequencyDivisor;

    bool s1HasLength;
    size_t s1LengthInTicks;
    size_t s1CurrentLengthProgress;

    bool s1HasEnvelope;
    bool s1EnvelopeIncreases;
    uint32_t s1EnvelopeValue;
    size_t s1EnvelopeStepInTicks;
    size_t s1CurrentEnvelopeStepProgress;

    bool s2Running;

    size_t s2DutyOnLengthInTicks;
    size_t s2DutyPeriodInTicks;
    size_t s2CurrentDutyProgress;

    bool s2HasLength;
    size_t s2LengthInTicks;
    size_t s2CurrentLengthProgress;

    bool s2HasEnvelope;
    bool s2EnvelopeIncreases;
    uint32_t s2EnvelopeValue;
    size_t s2EnvelopeStepInTicks;
    size_t s2CurrentEnvelopeStepProgress;

    bool s3Running;

    size_t s3CurrentWaveformPosition;

    bool s3HasLength;
    size_t s3LengthInTicks;
    size_t s3CurrentLengthProgress;

    size_t s3PeriodInTicks;
    size_t s3CurrentProgress;
    int16_t s3VolumeMultiplier;
    int16_t s3VolumeDivisor;

    bool s4Running;

    uint32_t lfsr; // 15-bit linear feedback shift register
    uint32_t s4ShiftPeriod;
    uint32_t s4ShiftProgress;
    uint32_t s4ShiftFeedbackMask;

    bool s4HasLength;
    size_t s4LengthInTicks;
    size_t s4CurrentLengthProgress;

    bool s4HasEnvelope;
    bool s4EnvelopeIncreases;
    uint32_t s4EnvelopeValue;
    size_t s4EnvelopeStepInTicks;
    size_t s4CurrentEnvelopeStepProgress;

    void writeFile();
    bool fileHasWritten;

    void simulateChannel1(size_t clockTicks);
    void simulateChannel2(size_t clockTicks);
    void simulateChannel3(size_t clockTicks);
    void simulateChannel4(size_t clockTicks);
    int16_t getChannel1Signal();
    int16_t getChannel2Signal();
    int16_t getChannel3Signal();
    int16_t getChannel4Signal();

public:
    AudioUnit();
    ~AudioUnit();
    void reset(uint8_t* gbcPorts);
    void stopAllSound();
    void simulate(uint64_t clockTicks);
    void updateWaveformData(size_t ioIndex);

    void startChannel1(uint8_t initByte);
    void startChannel2(uint8_t initByte);
    void startChannel3(uint8_t initByte);
    void startChannel4(uint8_t initByte);
};
