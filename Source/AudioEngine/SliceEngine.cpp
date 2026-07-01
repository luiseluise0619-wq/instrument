#include "SliceEngine.h"
#include "../DSP/TransientDetector.h"

void SliceEngine::setSample (std::shared_ptr<juce::AudioBuffer<float>> b, double sr)
{
    sample = std::move (b);
    sampleRate = sr > 0.0 ? sr : 44100.0;
    rebuildSlices();
}

void SliceEngine::rebuildSlices()
{
    slices.clear();
    if (sample == nullptr || sample->getNumSamples() == 0)
        return;

    switch (mode)
    {
        case Transient: sliceByTransient(); break;
        case Grid:      sliceByGrid();      break;
        case Manual:    /* keep whatever sliceByManual() last produced */ break;
    }
}

void SliceEngine::detectTransients()
{
    transients.clear();
    if (sample == nullptr)
        return;

    TransientDetector::Params p;
    p.sensitivity = sensitivity;
    transients = TransientDetector::detect (*sample, sampleRate, p);
}

void SliceEngine::sliceByTransient()
{
    detectTransients();

    // Fall back to a grid if onset detection found nothing usable.
    if (transients.empty())
    {
        sliceByGrid();
        return;
    }

    const int numSamples = sample->getNumSamples();
    for (size_t i = 0; i < transients.size(); ++i)
    {
        const int start = transients[i];
        const int end   = (i + 1 < transients.size()) ? transients[i + 1] : numSamples;
        if (end > start)
            slices.push_back ({ start, end - start });
    }
}

void SliceEngine::sliceByGrid()
{
    if (sample == nullptr)
        return;

    const int numSamples = sample->getNumSamples();
    const int sliceLen   = juce::jmax (1, numSamples / gridDiv);

    for (int i = 0; i < gridDiv; ++i)
    {
        const int start = i * sliceLen;
        if (start >= numSamples)
            break;
        const int end = (i == gridDiv - 1) ? numSamples : juce::jmin (numSamples, start + sliceLen);
        slices.push_back ({ start, end - start });
    }
}

void SliceEngine::sliceByManual (const std::vector<int>& points)
{
    mode = Manual;
    slices.clear();
    const int numSamples = sample != nullptr ? sample->getNumSamples() : 0;
    if (numSamples == 0)
        return;

    for (size_t i = 0; i < points.size(); ++i)
    {
        const int start = juce::jlimit (0, numSamples - 1, points[i]);
        const int end   = (i + 1 < points.size())
                              ? juce::jlimit (0, numSamples, points[i + 1])
                              : numSamples;
        if (end > start)
            slices.push_back ({ start, end - start });
    }
}

std::optional<SlicePoint> SliceEngine::getSlice (int i) const
{
    if (i < 0 || i >= (int) slices.size())
        return std::nullopt;
    return slices[(size_t) i];
}
