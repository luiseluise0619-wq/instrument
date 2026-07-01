#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

/**
    Decodes an audio file (WAV / AIFF / FLAC / Ogg, plus MP3 where the platform
    codec is available) into an in-memory float buffer. Returns nullptr on
    failure and leaves outSampleRate untouched.
*/
class SampleLoader
{
public:
    static std::shared_ptr<juce::AudioBuffer<float>>
        decode (const juce::File& file, double& outSampleRate);

    static std::shared_ptr<juce::AudioBuffer<float>>
        decode (const void* data, int sizeBytes, double& outSampleRate);

private:
    static std::shared_ptr<juce::AudioBuffer<float>>
        readAll (juce::AudioFormatReader* reader, double& outSampleRate);
};
