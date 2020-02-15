#include "windowsaudiostreamer.h"
#include "../SharedLib/gbc/gbc.h"

#include <cassert>
#include <audioclient.h>
#include <memory>

#define SAMPLES_PER_SEC 48000
#define NO_OF_CHANNELS 2
#define SAMPLE_SIZE_BYTES_PER_CHANNEL sizeof(int16_t)

#define BUFFER_SIZE_IN_HUNDRED_NANOS (REFERENCE_TIME)2500000

WindowsAudioStreamer::WindowsAudioStreamer(Gbc* gbc): AudioStreamer(gbc) {
    isPlaying = false;
    initialised = false;
    hEvent = NULL;
    deviceEnumerator = nullptr;
    device = nullptr;
    audioClient = nullptr;
    renderClient = nullptr;
    framesPerBuffer = 0;
}

WindowsAudioStreamer::~WindowsAudioStreamer() {
    WindowsAudioStreamer::stop();
}

void WindowsAudioStreamer::start() {
    if (isPlaying) {
        return;
    }

    bool opened = buildAndOpenStream();
    if (opened) {
        startPlaying();
    }
}

void WindowsAudioStreamer::stop() {
    close();
}

bool WindowsAudioStreamer::buildAndOpenStream() {

    if (initialised) {
        return !isPlaying;
    }

    const CLSID clsid_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID iid_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    const IID iid_IAudioClient = __uuidof(IAudioClient);
    const IID iid_IAudioRenderClient = __uuidof(IAudioRenderClient);

    // Create device enumerator
    HRESULT hr = CoCreateInstance(
            clsid_MMDeviceEnumerator,
            nullptr,
            CLSCTX_ALL,
            iid_IMMDeviceEnumerator,
            (void**)&deviceEnumerator);
    if (FAILED(hr) || deviceEnumerator == nullptr) {
        close();
        return false;
    }

    // Get the default audio endpoint
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr) || device == nullptr) {
        close();
        return false;
    }

    // Get an audio client
    hr = device->Activate(
            iid_IAudioClient,
            CLSCTX_ALL,
            nullptr,
            (void**)&audioClient);
    if (FAILED(hr) || audioClient == nullptr) {
        close();
        return false;
    }

    // Check if the desired audio format is supported, get a close match otherwise
    WAVEFORMATEX desiredFormat;
    desiredFormat.wFormatTag = WAVE_FORMAT_PCM;
    desiredFormat.nChannels = NO_OF_CHANNELS;
    desiredFormat.nSamplesPerSec = SAMPLES_PER_SEC;
    desiredFormat.nAvgBytesPerSec = SAMPLES_PER_SEC * SAMPLE_SIZE_BYTES_PER_CHANNEL * NO_OF_CHANNELS;
    desiredFormat.nBlockAlign = SAMPLE_SIZE_BYTES_PER_CHANNEL * NO_OF_CHANNELS;
    desiredFormat.wBitsPerSample = SAMPLE_SIZE_BYTES_PER_CHANNEL * 8;
    desiredFormat.cbSize = 0;
    WAVEFORMATEX* closestMatch;
    hr = audioClient->IsFormatSupported(
            AUDCLNT_SHAREMODE_SHARED,
            &desiredFormat,
            &closestMatch);
    if (FAILED(hr)) {
        close();
        return false;
    }

    // The audio source has to have a matching format, else it can't be used
    WAVEFORMATEX* usableFormat = closestMatch != NULL ? closestMatch : &desiredFormat;
    if ((usableFormat->nSamplesPerSec != SAMPLES_PER_SEC)
        || (usableFormat->nChannels != NO_OF_CHANNELS)
        || (usableFormat->wBitsPerSample * 8 == SAMPLE_SIZE_BYTES_PER_CHANNEL)) {
        if (closestMatch != NULL) {
            CoTaskMemFree(closestMatch);
        }
        close();
        return false;
    }

    // Open the stream and associate it with an audio session
    hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            BUFFER_SIZE_IN_HUNDRED_NANOS,
            0,
            usableFormat,
            nullptr);
    if (closestMatch != NULL) {
        // If the desired format is supported, closestMatch will be NULL, else it will be allocated to something
        CoTaskMemFree(closestMatch);
    }
    if (FAILED(hr)) {
        close();
        return false;
    }

    // Get actual size of audio buffer (in frames - a frame contains one sample for each channel)
    hr = audioClient->GetBufferSize(&framesPerBuffer);
    if (FAILED(hr)) {
        close();
        return false;
    }

    // Create an event
    hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hEvent == NULL) {
        close();
        return false;
    }

    // Register for buffer event notifications
    hr = audioClient->SetEventHandle(hEvent);
    if (FAILED(hr)) {
        close();
        return false;
    }

    // Get the render client
    hr = audioClient->GetService(iid_IAudioRenderClient, (void**)&renderClient);
    if (FAILED(hr) || renderClient == nullptr) {
        close();
        return false;
    }

    // Mark as initialised
    initialised = true;
    return true;
}

void WindowsAudioStreamer::startPlaying() {
    if (!initialised) {
        return;
    }

    thread = std::thread(WindowsAudioStreamer::threadMain, std::ref(*this));
}

void WindowsAudioStreamer::close() {
    if (isPlaying) {
        isPlaying = false;
        thread.join();
        thread = std::thread();
    }
    if (device) {
        device->Release();
        device = nullptr;
    }
    if (deviceEnumerator) {
        deviceEnumerator->Release();
        deviceEnumerator = nullptr;
    }
    if (hEvent) {
        CloseHandle(hEvent);
    }
    initialised = false;
}

void WindowsAudioStreamer::threadMain(WindowsAudioStreamer& streamObject) {
    streamObject.playUntilStopped();
}

void WindowsAudioStreamer::playUntilStopped() {
    // To reduce latency, load the first buffer before starting the stream
    BYTE* pData;
    HRESULT hr = renderClient->GetBuffer(framesPerBuffer, &pData);
    if (FAILED(hr)) {
        close();
        return;
    }
    gbc->audioUnit.onAudioThreadNeedingData((int16_t*)pData, framesPerBuffer);
    hr = renderClient->ReleaseBuffer(framesPerBuffer, 0);
    if (FAILED(hr)) {
        close();
        return;
    }

    // Start the stream playing
    isPlaying = true;
    hr = audioClient->Start();
    if (FAILED(hr)) {
        close();
        return;
    }

    // Loop a bunch of times
    int timesRun = 0;
    while (isPlaying) {
        DWORD retval = WaitForSingleObject(hEvent, 2000);
        if (retval != WAIT_OBJECT_0) {
            break;
        }

        // Get amount in buffer that's been filled
        UINT32 currentPadding;
        audioClient->GetCurrentPadding(&currentPadding);
        UINT availableFrames = framesPerBuffer - currentPadding;

        // Get nearest empty buffer, fill it, and release it
        hr = renderClient->GetBuffer(availableFrames, &pData);
        AUDCLNT_E_BUFFER_ERROR;
        if (FAILED(hr)) {
            break;
        }
        gbc->audioUnit.onAudioThreadNeedingData((int16_t*)pData, availableFrames);
        hr = renderClient->ReleaseBuffer(availableFrames, 0);
        if (FAILED(hr)) {
            break;
        }

        timesRun++;
    }

    // Wait for the last buffer to play
    Sleep(BUFFER_SIZE_IN_HUNDRED_NANOS / 10000);
    isPlaying = false;
    hr = audioClient->Stop();
    if (FAILED(hr)) {
        close();
        return;
    }
}
