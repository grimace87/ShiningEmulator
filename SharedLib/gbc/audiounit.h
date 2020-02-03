#pragma once

#include <cstdint>

class AudioUnit {
    uint8_t* ioPorts;
    uint64_t cumulativeTicks;
    size_t currentBufferHead;
    int16_t* buffer;

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

    void simulateChannel2(size_t clockTicks);
    void simulateChannel4(size_t clockTicks);
    int16_t getChannel2Signal();
    int16_t getChannel4Signal();

public:
    AudioUnit();
    ~AudioUnit();
    void reset(uint8_t* gbcPorts);
    void stopAllSound();
    void simulate(uint64_t clockTicks);

    void startChannel2(uint8_t initByte);
    void startChannel4(uint8_t initByte);
};
