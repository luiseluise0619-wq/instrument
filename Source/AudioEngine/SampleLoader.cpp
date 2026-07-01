#include "SampleLoader.h"

std::shared_ptr<juce::AudioBuffer<float>>
SampleLoader::readAll (juce::AudioFormatReader* reader, double& outSampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> r (reader);
    if (r == nullptr || r->lengthInSamples <= 0)
        return nullptr;

    outSampleRate = r->sampleRate;

    const int numChannels = juce::jmax (1, (int) r->numChannels);
    const int numSamples  = (int) r->lengthInSamples;

    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    r->read (buffer.get(), 0, numSamples, 0, true, true);
    return buffer;
}

std::shared_ptr<juce::AudioBuffer<float>>
SampleLoader::decode (const juce::File& file, double& outSampleRate)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();     // WAV, AIFF (+ FLAC/Ogg where enabled)
    return readAll (manager.createReaderFor (file), outSampleRate);
}

std::shared_ptr<juce::AudioBuffer<float>>
SampleLoader::decode (const void* data, int sizeBytes, double& outSampleRate)
{
    if (data == nullptr || sizeBytes <= 0)
        return nullptr;

    juce::AudioFormatManager manager;
    manager.registerBasicFormats();

    auto stream = std::make_unique<juce::MemoryInputStream> (data, (size_t) sizeBytes, false);
    return readAll (manager.createReaderFor (std::move (stream)), outSampleRate);
}
