#include "audiounit.h"

#include <cstdio>
#include <algorithm>

#define GB_FREQ  4194304

#define SAMPLE_RATE 48000.0
#define AUDIO_BUFFER_SIZE_FRAMES 12000
#define NO_OF_CHANNELS 2

#define MUTE_VALUE 0x0000

#define NR10 ioPorts[0x10]
#define NR11 ioPorts[0x11]
#define NR12 ioPorts[0x12]
#define NR13 ioPorts[0x13]
#define NR14 ioPorts[0x14]
#define NR21 ioPorts[0x16]
#define NR22 ioPorts[0x17]
#define NR23 ioPorts[0x18]
#define NR24 ioPorts[0x19]
#define NR30 ioPorts[0x1a]
#define NR31 ioPorts[0x1b]
#define NR32 ioPorts[0x1c]
#define NR33 ioPorts[0x1d]
#define NR34 ioPorts[0x1e]
#define NR41 ioPorts[0x20]
#define NR42 ioPorts[0x21]
#define NR43 ioPorts[0x22]
#define NR44 ioPorts[0x23]
#define NR50 ioPorts[0x24]
#define NR51 ioPorts[0x25]
#define NR52 ioPorts[0x26]

AudioUnit::AudioUnit() {
    ioPorts = nullptr;
    bufferWriteHead = 0;
    bufferReadHead = 0;
    cumulativeTicks = 0;
    buffer = new Sample[AUDIO_BUFFER_SIZE_FRAMES];
    globalAudioEnable = false;

    out1Generator1 = 0;
    out1Generator2 = 0;
    out1Generator3 = 0;
    out1Generator4 = 0;
    out2Generator1 = 0;
    out2Generator2 = 0;
    out2Generator3 = 0;
    out2Generator4 = 0;

    s1Running = false;

    s1HasSweep = false;
    s1SweepIncreases = false;
    s1SweepPeriodInTicks = 8;
    s1CurrentSweepProgress = 0;
    s1CurrentFrequency = 0;
    s1FrequencyDivisor = 2;

    s1DutyOnLengthInTicks = 4;
    s1DutyBits = 0;
    s1DutyPeriodInTicks = 8;
    s1CurrentDutyProgress = 0;

    s1HasLength = false;
    s1LengthInTicks = 8;
    s1CurrentLengthProgress = 0;

    s1HasEnvelope = false;
    s1EnvelopeIncreases = false;
    s1EnvelopeValue = 0;
    s1EnvelopeStepInTicks = 8;
    s1CurrentEnvelopeStepProgress = 0;

    s2Running = false;

    s2DutyOnLengthInTicks = 4;
    s2DutyPeriodInTicks = 8;
    s2CurrentDutyProgress = 0;

    s2HasLength = false;
    s2LengthInTicks = 8;
    s2CurrentLengthProgress = 0;

    s2HasEnvelope = false;
    s2EnvelopeIncreases = false;
    s2EnvelopeValue = 0;
    s2EnvelopeStepInTicks = 8;
    s2CurrentEnvelopeStepProgress = 0;

    s3Running = false;

    s3CurrentWaveformPosition = 0;

    s3HasLength = false;
    s3LengthInTicks = 8;
    s3CurrentLengthProgress = 0;

    s3PeriodInTicks = 8;
    s3CurrentProgress = 0;
    s3VolumeMultiplier = 0;
    s3VolumeDivisor = 1;

    s4Running = false;

    lfsr = 0x0001U;
    s4ShiftPeriod = 8;
    s4ShiftProgress = 0;
    s4ShiftFeedbackMask = 0x004000U;

    s4HasLength = false;
    s4LengthInTicks = 8;
    s4CurrentLengthProgress = 0;

    s4HasEnvelope = false;
    s4EnvelopeIncreases = false;
    s4EnvelopeValue = 0;
    s4EnvelopeStepInTicks = 8;
    s4CurrentEnvelopeStepProgress = 0;
}

AudioUnit::~AudioUnit() {
    delete[] buffer;
}

void AudioUnit::reset(uint8_t* gbcPorts) {
    bufferWriteHead = 0;
    bufferReadHead = 0;
    ioPorts = gbcPorts;

    // TODO - Initialise sound parameters based on initial values in ioPorts
    stopAllSound();
}

void AudioUnit::stopAllSound() {
    globalAudioEnable = false;
    s1Running = false;
    s2Running = false;
    s3Running = false;
    s4Running = false;
}

void AudioUnit::reenableAudio() {
    globalAudioEnable = true;
}

void AudioUnit::updateRoutingMasks() {
    uint8_t flags = NR51;
    out1Generator1 = (int16_t)(flags & 0x01U);
    out1Generator2 = (int16_t)((flags & 0x02U) >> 1);
    out1Generator3 = (int16_t)((flags & 0x04U) >> 2);
    out1Generator4 = (int16_t)((flags & 0x08U) >> 3);
    out2Generator1 = (int16_t)((flags & 0x10U) >> 4);
    out2Generator2 = (int16_t)((flags & 0x20U) >> 5);
    out2Generator3 = (int16_t)((flags & 0x40U) >> 6);
    out2Generator4 = (int16_t)((flags & 0x80U) >> 7);
}

void AudioUnit::simulate(uint64_t clockTicks) {
    // Simulate each channel
    simulateChannel1(clockTicks);
    simulateChannel2(clockTicks);
    simulateChannel3(clockTicks);
    simulateChannel4(clockTicks);

    // Convert between cumulative clock ticks at the CPU's frequency to the emulated audio sample rate
    cumulativeTicks += clockTicks;
    auto endPosition = (size_t)((SAMPLE_RATE / GB_FREQ) * (double)cumulativeTicks);

    // Wrap head position within buffer
    endPosition %= AUDIO_BUFFER_SIZE_FRAMES;

    while (bufferWriteHead != endPosition) {
        // Get channel signals
        int16_t channel1 = getChannel1Signal() / 4;
        int16_t channel2 = getChannel2Signal() / 4;
        int16_t channel3 = getChannel3Signal() / 4;
        int16_t channel4 = getChannel4Signal() / 4;

        // Mix signals
        int16_t output1 = out1Generator1 * channel1 + out1Generator2 * channel2 + out1Generator3 * channel3 + out1Generator4 * channel4;
        int16_t output2 = out2Generator1 * channel1 + out2Generator2 * channel2 + out2Generator3 * channel3 + out2Generator4 * channel4;
        buffer[bufferWriteHead++] = {output1, output2};
        bufferWriteHead %= AUDIO_BUFFER_SIZE_FRAMES;
    }
}

void AudioUnit::updateWaveformData(size_t ioIndex) {
    uint8_t byte = ioPorts[ioIndex];
    size_t dataIndex = (size_t)(ioIndex - 0x0030U) * 2;
    waveformData[dataIndex] = ((int16_t)(byte & 0xf0U) - 128) * 256;
    waveformData[dataIndex + 1] = ((int16_t)((byte & 0x0fU) << 4U) - 128) * 256;
}

void AudioUnit::simulateChannel1(size_t clockTicks) {

    if (!s1Running) {
        return;
    }

    // Simulate the sweep function
    if (s1HasSweep) {
        size_t postProgress = s1CurrentSweepProgress + clockTicks;
        s1CurrentSweepProgress = postProgress % s1SweepPeriodInTicks;
        if (postProgress > s1SweepPeriodInTicks) {
            if (s1SweepIncreases) {
                s1CurrentFrequency += s1CurrentFrequency / s1FrequencyDivisor;
                if (s1CurrentFrequency > 0x07ffU) {
                    s1Running = false;
                    NR52 &= 0xfeU;
                    return;
                }
                s1SweepPeriodInTicks = GB_FREQ / s1CurrentFrequency;
            } else {
                if (s1FrequencyDivisor > s1CurrentFrequency) {
                    s1CurrentFrequency -= s1CurrentFrequency / s1FrequencyDivisor;
                    s1SweepPeriodInTicks = GB_FREQ / s1CurrentFrequency;
                }
            }
            switch (s1DutyBits) {
                case 0x0: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 8; break;
                case 0x1: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 4; break;
                case 0x2: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 2; break;
                default: s1DutyOnLengthInTicks = 3 * s1DutyPeriodInTicks / 4; break;
            }
        }
    }

    // Simulate the running frequency
    s1CurrentDutyProgress = (s1CurrentDutyProgress + clockTicks) % s1DutyPeriodInTicks;

    // Simulate length
    if (s1HasLength) {
        s1CurrentLengthProgress += clockTicks;
        if (s1CurrentLengthProgress >= s1LengthInTicks) {
            s1HasLength = false;
            s1Running = false;
            NR52 &= 0xfeU;
            return;
        }
    }

    // Simulate envelope
    if (s1HasEnvelope) {
        size_t postProgress = s1CurrentEnvelopeStepProgress + clockTicks;
        if (postProgress >= s1EnvelopeStepInTicks) {
            if (s1EnvelopeIncreases) {
                s1EnvelopeValue++;
            } else {
                s1EnvelopeValue--;
            }
            s1EnvelopeValue &= 0x0fU;
            if (s1EnvelopeValue == 0) {
                s1HasEnvelope = false;
            }
        }
        s1CurrentEnvelopeStepProgress = postProgress % s1EnvelopeStepInTicks;
    }
}

void AudioUnit::simulateChannel2(size_t clockTicks) {

    if (!s2Running) {
        return;
    }

    // Simulate the running frequency
    s2CurrentDutyProgress = (s2CurrentDutyProgress + clockTicks) % s2DutyPeriodInTicks;

    // Simulate length
    if (s2HasLength) {
        s2CurrentLengthProgress += clockTicks;
        if (s2CurrentLengthProgress >= s2LengthInTicks) {
            s2HasLength = false;
            s2Running = false;
            NR52 &= 0xfdU;
            return;
        }
    }

    // Simulate envelope
    if (s2HasEnvelope) {
        size_t postProgress = s2CurrentEnvelopeStepProgress + clockTicks;
        if (postProgress >= s2EnvelopeStepInTicks) {
            if (s2EnvelopeIncreases) {
                s2EnvelopeValue++;
            } else {
                s2EnvelopeValue--;
            }
            s2EnvelopeValue &= 0x0fU;
            if (s2EnvelopeValue == 0) {
                s2HasEnvelope = false;
            }
        }
        s2CurrentEnvelopeStepProgress = postProgress % s2EnvelopeStepInTicks;
    }
}

void AudioUnit::simulateChannel3(size_t clockTicks) {

    if (!s3Running) {
        return;
    }

    // Simulate the running frequency
    s3CurrentProgress = (s3CurrentProgress + clockTicks) % s3PeriodInTicks;
    s3CurrentWaveformPosition = s3CurrentProgress / (s3PeriodInTicks / 32);

    // Simulate length
    if (s3HasLength) {
        s3CurrentLengthProgress += clockTicks;
        if (s3CurrentLengthProgress >= s3LengthInTicks) {
            s3HasLength = false;
            s3Running = false;
            NR52 &= 0xfbU;
            return;
        }
    }
}

void AudioUnit::simulateChannel4(size_t clockTicks) {

    if (!s4Running) {
        return;
    }

    // Simulate the LFSR
    if (s4ShiftPeriod > 0) {
        s4ShiftProgress += clockTicks;
        if (s4ShiftProgress >= s4ShiftPeriod) {
            s4ShiftProgress -= s4ShiftPeriod;
            uint32_t feedbackBits = (lfsr & 0x0001U) ^ ((lfsr & 0x0002U) >> 1U);
            feedbackBits *= s4ShiftFeedbackMask;
            uint32_t shifted = lfsr >> 1U;
            lfsr = (shifted & ~feedbackBits) | feedbackBits;
        }
    }

    // Simulate length
    if (s4HasLength) {
        s4CurrentLengthProgress += clockTicks;
        if (s4CurrentLengthProgress >= s4LengthInTicks) {
            s4HasLength = false;
            s4Running = false;
            NR52 &= 0xf7U;
            return;
        }
    }

    // Simulate envelope
    if (s4HasEnvelope) {
        size_t postProgress = s4CurrentEnvelopeStepProgress + clockTicks;
        if (postProgress >= s4EnvelopeStepInTicks) {
            if (s4EnvelopeIncreases) {
                s4EnvelopeValue++;
            } else {
                s4EnvelopeValue--;
            }
            s4EnvelopeValue &= 0x0fU;
            if (s4EnvelopeValue == 0) {
                s4HasEnvelope = false;
            }
        }
        s4CurrentEnvelopeStepProgress = postProgress % s4EnvelopeStepInTicks;
    }
}

int16_t AudioUnit::getChannel1Signal() {
    // TODO - Apply bias depending on duty cycle?
    if (s1Running) {
        int16_t baseAmplitude = s1CurrentDutyProgress < s1DutyOnLengthInTicks ? 128 : -128;
        return baseAmplitude * (int16_t)(s1EnvelopeValue << 4U);
    }
    return MUTE_VALUE;
}

int16_t AudioUnit::getChannel2Signal() {
    // TODO - Apply bias depending on duty cycle?
    if (s2Running) {
        int16_t baseAmplitude = s2CurrentDutyProgress < s2DutyOnLengthInTicks ? 128 : -128;
        return baseAmplitude * (int16_t)(s2EnvelopeValue << 4U);
    }
    return MUTE_VALUE;
}

int16_t AudioUnit::getChannel3Signal() {
    if (s3Running) {
        return waveformData[s3CurrentWaveformPosition] * s3VolumeMultiplier / s3VolumeDivisor;
    }
    return MUTE_VALUE;
}

// Get LFSR signal as one of two values (-128 or 128) and multiply by enveloped volume (0 to 240)
int16_t AudioUnit::getChannel4Signal() {
    if (s4Running) {
        int16_t lfsrSignal = (int16_t) (lfsr & 0x0001U) * 256 - 128;
        return lfsrSignal * (int16_t) (s4EnvelopeValue << 4U);
    }
    return MUTE_VALUE;
}

void AudioUnit::startChannel1() {

    // Check running bit
    s1Running = NR14 & 0x80U;
    if (!s1Running) {
        return;
    }

    // Set frequency and duty cycle parameters
    s1DutyBits = NR11 >> 6U;
    size_t frequencyBits = ((size_t)(NR14 & 0x07U) << 8U) + (size_t)NR13;
    s1DutyPeriodInTicks = 32 * (2048 - frequencyBits);
    s1CurrentDutyProgress = 0;
    switch (s1DutyBits) {
        case 0x0: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 8; break;
        case 0x1: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 4; break;
        case 0x2: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 2; break;
        default: s1DutyOnLengthInTicks = 3 * s1DutyPeriodInTicks / 4; break;
    }

    // Set sweep parameters
    uint8_t sweepAmountBits = NR10 & 0x07U;
    uint8_t sweepTimeBits = (NR10 & 0x70U) >> 4U;
    s1HasSweep = sweepTimeBits != 0;
    s1SweepIncreases = (NR10 & 0x08U) == 0;
    s1SweepPeriodInTicks = s1HasSweep ? GB_FREQ / (128 * (size_t)sweepTimeBits) : 8;
    s1CurrentSweepProgress = 0;
    s1CurrentFrequency = GB_FREQ / s1DutyPeriodInTicks;
    s1FrequencyDivisor = 1U << (size_t)sweepAmountBits;

    // Set length parameters
    s1HasLength = NR14 & 0x40U;
    s1LengthInTicks = (64 - (size_t)(NR11 & 0x3FU)) * 16384;
    s1CurrentLengthProgress = 0;

    // Set envelope parameters
    uint8_t stepSizeBits = NR12 & 0x07U;
    s1EnvelopeStepInTicks = (size_t)stepSizeBits * GB_FREQ / 64;
    s1HasEnvelope = stepSizeBits != 0;
    s1EnvelopeIncreases = NR12 & 0x08U;
    s1EnvelopeValue = NR12 >> 4U;
    s1CurrentEnvelopeStepProgress = 0;
}

void AudioUnit::startChannel2() {

    // Check running bit
    s2Running = NR24 & 0x80U;
    if (!s2Running) {
        return;
    }

    // Set frequency and duty cycle parameters
    size_t dutyBits = NR21 >> 6U;
    size_t frequencyBits = ((size_t)(NR24 & 0x07U) << 8U) + (size_t)NR23;
    s2DutyPeriodInTicks = 32 * (2048 - frequencyBits);
    s2CurrentDutyProgress = 0;
    switch (dutyBits) {
        case 0x0: s2DutyOnLengthInTicks = s2DutyPeriodInTicks / 8; break;
        case 0x1: s2DutyOnLengthInTicks = s2DutyPeriodInTicks / 4; break;
        case 0x2: s2DutyOnLengthInTicks = s2DutyPeriodInTicks / 2; break;
        default: s2DutyOnLengthInTicks = 3 * s2DutyPeriodInTicks / 4; break;
    }

    // Set length parameters
    s2HasLength = NR24 & 0x40U;
    s2LengthInTicks = (64 - (size_t)(NR21 & 0x3FU)) * 16384;
    s2CurrentLengthProgress = 0;

    // Set envelope parameters
    uint8_t stepSizeBits = NR22 & 0x07U;
    s2EnvelopeStepInTicks = (size_t)stepSizeBits * GB_FREQ / 64;
    s2HasEnvelope = stepSizeBits != 0;
    s2EnvelopeIncreases = NR22 & 0x08U;
    s2EnvelopeValue = NR22 >> 4U;
    s2CurrentEnvelopeStepProgress = 0;
}

void AudioUnit::startChannel3() {

    // Check running bit
    s3Running = NR34 & 0x80U;
    if (!s3Running) {
        return;
    }

    s3CurrentWaveformPosition = 0;

    // Set length parameters
    s3HasLength = NR34 & 0x40U;
    s3LengthInTicks = (256 - (size_t)(NR31 & 0x3FU)) * 16384;
    s3CurrentLengthProgress = 0;

    // Set period parameters
    size_t frequencyBits = ((size_t)(NR34 & 0x07U) << 8U) + (size_t)NR33;
    s3PeriodInTicks = 32 * (2048 - frequencyBits);
    s3CurrentProgress = 0;

    // Set volume
    uint8_t volumeBits = (NR32 & 0x60U) >> 5U;
    switch (volumeBits) {
        case 0: s3VolumeMultiplier = 0; s3VolumeDivisor = 1; break;
        case 1: s3VolumeMultiplier = 1; s3VolumeDivisor = 1; break;
        case 2: s3VolumeMultiplier = 1; s3VolumeDivisor = 2; break;
        default: s3VolumeMultiplier = 1; s3VolumeDivisor = 4; break;
    }
    if ((NR30 & 0x80U) == 0) {
        s3VolumeMultiplier = 0;
    }
}

void AudioUnit::startChannel4() {

    // Check running bit
    s4Running = NR44 & 0x80U;
    if (!s4Running) {
        return;
    }

    // Set feedback shifting parameters
    uint32_t basePeriod;
    uint8_t dividerFlags = NR43 & 0x07U;
    if (dividerFlags == 0) {
        basePeriod = 4;
    } else {
        basePeriod = dividerFlags * 8;
    }
    uint32_t bitShift = ((NR43 & 0xf0U) >> 4U) + 0x0001;
    if (bitShift > 14) {
        bitShift = 0;
    }

    s4ShiftFeedbackMask = (NR43 & 0x08U) ? 0x4040U : 0x4000U;
    s4ShiftPeriod = basePeriod << bitShift;
    s4ShiftProgress = 0;

    // Set length parameters
    s4HasLength = NR44 & 0x40U;
    s4LengthInTicks = (64 - (size_t)(NR41 & 0x3FU)) * 16384;
    s4CurrentLengthProgress = 0;

    // Set envelope parameters
    uint8_t stepSizeBits = NR42 & 0x07U;
    s4EnvelopeStepInTicks = (size_t)stepSizeBits * GB_FREQ / 64;
    s4HasEnvelope = stepSizeBits != 0;
    s4EnvelopeIncreases = NR42 & 0x08U;
    s4EnvelopeValue = NR42 >> 4U;
    s4CurrentEnvelopeStepProgress = 0;
}

void AudioUnit::onAudioThreadNeedingData(int16_t* dstBuffer, uint32_t frameCount) {
    // Sound off - mute audio
    if (!globalAudioEnable) {
        muteExternalBufferFrames((Sample*)dstBuffer, frameCount);
        return;
    }

    // Fill as many of 'frameCount' frames are available, mute the rest
    uint32_t latestHeadPosition = bufferWriteHead;
    uint32_t availableFrames = (latestHeadPosition + AUDIO_BUFFER_SIZE_FRAMES - bufferReadHead) % AUDIO_BUFFER_SIZE_FRAMES;
    if (availableFrames >= frameCount) {
        writeAudioBuffer((Sample*)dstBuffer, frameCount);
    } else {
        // Buffer under-run - move write head forward (and mute) to compensate
        uint32_t frameShortage = frameCount - availableFrames;
        writeAudioBuffer((Sample*)dstBuffer, availableFrames);
        muteExternalBufferFrames((Sample*)dstBuffer + availableFrames, frameShortage);
        bufferWriteHead = (latestHeadPosition + frameShortage) % AUDIO_BUFFER_SIZE_FRAMES;
    }
}

void AudioUnit::writeAudioBuffer(Sample* dstBuffer, uint32_t frameCount) {
    uint32_t head = bufferReadHead;
    uint32_t target = (bufferReadHead + frameCount) % AUDIO_BUFFER_SIZE_FRAMES;
    while (head != target) {
        *dstBuffer = buffer[head];
        dstBuffer++;
        head++;
        head %= AUDIO_BUFFER_SIZE_FRAMES;
    }
    bufferReadHead = target;
}

void AudioUnit::muteExternalBufferFrames(Sample* dstBuffer, uint32_t frameCount) {
    for (uint32_t head = 0; head < frameCount; head++) {
        dstBuffer[head] = { MUTE_VALUE, MUTE_VALUE };
    }
}
