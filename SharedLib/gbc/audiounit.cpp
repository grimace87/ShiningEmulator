#include "audiounit.h"

#include <cstdio>
#include <cstdlib>

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
    noise = new int16_t[NOISE_BUFFER_SIZE];
    for (int i = 0; i < NOISE_BUFFER_SIZE; i++) {
        noise[i] = (int16_t)(rand() % 512) - 256;
    }

    s4Running = false;

    s4HasLength = false;
    s4LengthInSamples = 0;
    s4CurrentLengthProgress = 0;

    s4HasEnvelope = false;
    s4EnvelopeIncreases = false;
    s4EnvelopeValue = 0;
    s4EnvelopeStepInSamples = 0;
    s4CurrentEnvelopeStepProgress = 0;
}

AudioUnit::~AudioUnit() {
    delete[] buffer;
    delete[] noise;
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

    // Convert between cumulative clock ticks at the CPU's frequency to the emulated audio sample rate
    cumulativeTicks += clockTicks;
    auto endPosition = (size_t)((SAMPLE_RATE / GB_FREQ) * (double)cumulativeTicks);

    // Simulate channel 4 (noise channel)
    if (endPosition > AUDIO_BUFFER_SIZE) {
        endPosition = AUDIO_BUFFER_SIZE;
    }
    if (s4Running) {
        while (currentBufferHead < endPosition) {
            size_t head = currentBufferHead;

            // Determine if the end of length or an envelope step is reached before the given end position
            size_t lengthEnd = s4HasLength ? head + (s4LengthInSamples - s4CurrentLengthProgress) : endPosition + 1;
            size_t envelopeEnd = s4HasEnvelope ? head + (s4EnvelopeStepInSamples - s4CurrentEnvelopeStepProgress) : endPosition + 1;

            size_t nextEnd = endPosition;
            bool endsLength = false;
            bool endsEnvelope = false;
            if (lengthEnd <= endPosition) {
                nextEnd = lengthEnd;
                if (lengthEnd <= envelopeEnd) {
                    endsLength = true;
                }
            }
            if (envelopeEnd <= nextEnd) {
                nextEnd = envelopeEnd;
                endsEnvelope = true;
            }

            size_t thisStart = head;
            while (head < nextEnd) {
                uint32_t noiseValue = noise[head % NOISE_BUFFER_SIZE];
                buffer[head] = noiseValue * (int16_t)(s4EnvelopeValue << 4U);
                head++;
            }
            currentBufferHead = head;

            if (endsLength) {
                s4HasLength = false;
                s4Running = false;
                NR52 &= 0xf7U;
                break;
            } else {
                s4CurrentLengthProgress += currentBufferHead - thisStart;
            }
            if (endsEnvelope) {
                if (s4EnvelopeValue > 0) {
                    s4EnvelopeValue--;
                    s4CurrentEnvelopeStepProgress = 0;
                }
                if (s4EnvelopeValue == 0) {
                    s4HasEnvelope = false;
                }
            } else {
                s4CurrentEnvelopeStepProgress += currentBufferHead - thisStart;
            }
        }
    }
    for (size_t head = currentBufferHead; head < endPosition; head++) {
        buffer[head] = MUTE_VALUE;
    }
    currentBufferHead = endPosition;

    if (currentBufferHead >= AUDIO_BUFFER_SIZE) {
        writeFile();
    }
}

void AudioUnit::startChannel4(uint8_t initByte) {

    // Check running bit
    s4Running = initByte & 0x80U;
    if (!s4Running) {
        return;
    }

    // Set length parameters
    s4HasLength = NR44 & 0x40U;
    s4LengthInSamples = (size_t)((double)(NR41 & 0x3FU) * SAMPLE_RATE / GB_FREQ);
    s4CurrentLengthProgress = 0;

    // Set envelope parameters
    uint8_t stepSizeBits = NR12 & 0x07U;
    s4EnvelopeStepInSamples = (size_t)((double)stepSizeBits * SAMPLE_RATE / 64);
    s4HasEnvelope = stepSizeBits != 0;
    s4EnvelopeIncreases = NR42 & 0x80U;
    s4EnvelopeValue = NR12 >> 4U;
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
