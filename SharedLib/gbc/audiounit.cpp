#include "audiounit.h"

#include <cstdio>

#define GB_FREQ  4194304

#define SAMPLE_RATE 44100.0
#define AUDIO_BUFFER_SIZE 4194304
#define NOISE_BUFFER_SIZE 256

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
    currentBufferHead = 0;
    cumulativeTicks = 0;
    fileHasWritten = false;
    buffer = new int16_t[AUDIO_BUFFER_SIZE];

    s1Running = false;

    s1DutyOnLengthInTicks = 4;
    s1DutyPeriodInTicks = 8;
    s1CurrentDutyProgress = 0;

    s1HasLength = false;
    s1LengthInTicks = 0;
    s1CurrentLengthProgress = 0;

    s1HasEnvelope = false;
    s1EnvelopeIncreases = false;
    s1EnvelopeValue = 0;
    s1EnvelopeStepInTicks = 0;
    s1CurrentEnvelopeStepProgress = 0;

    s2Running = false;

    s2DutyOnLengthInTicks = 4;
    s2DutyPeriodInTicks = 8;
    s2CurrentDutyProgress = 0;

    s2HasLength = false;
    s2LengthInTicks = 0;
    s2CurrentLengthProgress = 0;

    s2HasEnvelope = false;
    s2EnvelopeIncreases = false;
    s2EnvelopeValue = 0;
    s2EnvelopeStepInTicks = 0;
    s2CurrentEnvelopeStepProgress = 0;

    s4Running = false;

    lfsr = 0x0001U;
    s4ShiftPeriod = 8;
    s4ShiftProgress = 0;
    s4ShiftFeedbackMask = 0x004000U;

    s4HasLength = false;
    s4LengthInTicks = 0;
    s4CurrentLengthProgress = 0;

    s4HasEnvelope = false;
    s4EnvelopeIncreases = false;
    s4EnvelopeValue = 0;
    s4EnvelopeStepInTicks = 0;
    s4CurrentEnvelopeStepProgress = 0;
}

AudioUnit::~AudioUnit() {
    delete[] buffer;
}

void AudioUnit::reset(uint8_t* gbcPorts) {
    currentBufferHead = 0;
    ioPorts = gbcPorts;

    // TODO - Initialise sound parameters based on initial values in ioPorts
    s4Running = false;
}

void AudioUnit::stopAllSound() {

}

void AudioUnit::simulate(uint64_t clockTicks) {
    if (fileHasWritten) {
        return;
    }

    // Simulate each channel
    simulateChannel1(clockTicks);
    simulateChannel2(clockTicks);
    simulateChannel4(clockTicks);

    // Convert between cumulative clock ticks at the CPU's frequency to the emulated audio sample rate
    cumulativeTicks += clockTicks;
    auto endPosition = (size_t)((SAMPLE_RATE / GB_FREQ) * (double)cumulativeTicks);

    // Clamp value within file size
    if (endPosition > AUDIO_BUFFER_SIZE) {
        endPosition = AUDIO_BUFFER_SIZE;
    }

    while (currentBufferHead < endPosition) {

        // Get channel signals
        int16_t channel1 = getChannel1Signal() / 4;
        int16_t channel2 = getChannel2Signal() / 4;
        int16_t channel4 = getChannel4Signal() / 4;

        // Mix signals
        buffer[currentBufferHead++] = channel1 + channel2 + channel4;
    }

    if (currentBufferHead >= AUDIO_BUFFER_SIZE) {
        writeFile();
    }
}

void AudioUnit::simulateChannel1(size_t clockTicks) {

    if (!s1Running) {
        return;
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

// Get LFSR signal as one of two values (-128 or 128) and multiply by enveloped volume (0 to 240)
int16_t AudioUnit::getChannel4Signal() {
    if (s4Running) {
        int16_t lfsrSignal = (int16_t) (lfsr & 0x0001U) * 256 - 128;
        return lfsrSignal * (int16_t) (s4EnvelopeValue << 4U);
    }
    return MUTE_VALUE;
}

void AudioUnit::startChannel1(uint8_t initByte) {

    // Check running bit
    s1Running = initByte & 0x80U;
    if (!s1Running) {
        return;
    }

    // Set frequency and duty cycle parameters
    size_t dutyBits = NR11 >> 6U;
    size_t frequencyBits = ((size_t)(NR14 & 0x07U) << 8U) + (size_t)NR13;
    s1DutyPeriodInTicks = 32 * (2048 - frequencyBits);
    s1CurrentDutyProgress = 0;
    switch (dutyBits) {
        case 0x0: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 8; break;
        case 0x1: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 4; break;
        case 0x2: s1DutyOnLengthInTicks = s1DutyPeriodInTicks / 2; break;
        default: s1DutyOnLengthInTicks = 3 * s1DutyPeriodInTicks / 4; break;
    }

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

void AudioUnit::startChannel2(uint8_t initByte) {

    // Check running bit
    s2Running = initByte & 0x80U;
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

void AudioUnit::startChannel4(uint8_t initByte) {

    // Check running bit
    s4Running = initByte & 0x80U;
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

void AudioUnit::writeFile() {
    if (fileHasWritten) {
        return;
    }
    FILE* audioFile;
    errno_t res = fopen_s(&audioFile, "output.raw", "wb");
    if (res != 0) {
        return;
    }
    fwrite((const void*)buffer, sizeof(uint8_t), AUDIO_BUFFER_SIZE, audioFile);
    fclose(audioFile);
    fileHasWritten = true;
}
