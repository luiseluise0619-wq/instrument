#include "SampleLoader.h"
#include <cmath>
#include <cstdint>

std::shared_ptr<juce::AudioBuffer<float>>
SampleLoader::readAll (juce::AudioFormatReader* reader, double& outSampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> r (reader);
    if (r == nullptr || r->lengthInSamples <= 0)
        return nullptr;

    // Do not turn an untrusted file header into an overflowing int or an
    // unbounded allocation. Ten minutes at 192 kHz is already well beyond a
    // sensible in-memory sample for a plugin.
    constexpr int64_t kMaxSamples = 192000LL * 60LL * 10LL;
    constexpr int kMaxChannels = 2;
    if (r->numChannels == 0 || r->numChannels > kMaxChannels
        || r->lengthInSamples > kMaxSamples
        || !std::isfinite(r->sampleRate) || r->sampleRate < 8000.0 || r->sampleRate > 384000.0
        || (std::uint64_t)r->lengthInSamples * r->numChannels * sizeof(float) > 128u*1024u*1024u)
        return nullptr;

    const int numChannels = (int) r->numChannels;
    const int numSamples  = (int) r->lengthInSamples;

    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    if (! r->read (buffer.get(), 0, numSamples, 0, true, true))
        return nullptr;
    for(int ch=0;ch<numChannels;++ch)for(int i=0;i<numSamples;++i)
        if(!std::isfinite(buffer->getSample(ch,i)))buffer->setSample(ch,i,0.0f);
    outSampleRate = r->sampleRate;
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
