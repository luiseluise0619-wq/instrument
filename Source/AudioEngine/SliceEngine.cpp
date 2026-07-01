#include "SliceEngine.h"
#include "../DSP/TransientDetector.h"

void SliceEngine::setSample (std::shared_ptr<juce::AudioBuffer<float>> b, double sr)
{
    sample = std::move (b);
    sampleRate = sr > 0.0 ? sr : 44100.0;
    rebuildSlices();
}

void SliceEngine::publish (std::vector<SlicePoint> newSlices)
{
    const juce::SpinLock::ScopedLockType sl (slicesLock);
    slices = std::move (newSlices);
}

void SliceEngine::rebuildSlices()
{
    if (sample == nullptr || sample->getNumSamples() == 0)
    {
        publish ({});
        return;
    }

    switch (mode)
    {
        case Transient: publish (buildTransient()); break;
        case Grid:      publish (buildGrid());      break;
        case Manual:    /* keep whatever sliceByManual() last published */ break;
    }
}

std::vector<SlicePoint> SliceEngine::buildTransient()
{
    transients.clear();
    if (sample == nullptr)
        return {};

    TransientDetector::Params p;
    p.sensitivity = sensitivity;
    transients = TransientDetector::detect (*sample, sampleRate, p);

    // Fall back to a grid if onset detection found nothing usable.
    if (transients.empty())
        return buildGrid();

    std::vector<SlicePoint> built;
    const int numSamples = sample->getNumSamples();
    for (size_t i = 0; i < transients.size(); ++i)
    {
        const int start = transients[i];
        const int end   = (i + 1 < transients.size()) ? transients[i + 1] : numSamples;
        if (end > start)
            built.push_back ({ start, end - start });
    }
    return built;
}

std::vector<SlicePoint> SliceEngine::buildGrid() const
{
    std::vector<SlicePoint> built;
    if (sample == nullptr)
        return built;

    const int numSamples = sample->getNumSamples();
    const int sliceLen   = juce::jmax (1, numSamples / gridDiv);

    for (int i = 0; i < gridDiv; ++i)
    {
        const int start = i * sliceLen;
        if (start >= numSamples)
            break;
        const int end = (i == gridDiv - 1) ? numSamples : juce::jmin (numSamples, start + sliceLen);
        built.push_back ({ start, end - start });
    }
    return built;
}

void SliceEngine::sliceByManual (const std::vector<int>& points)
{
    mode = Manual;

    std::vector<SlicePoint> built;
    const int numSamples = sample != nullptr ? sample->getNumSamples() : 0;
    if (numSamples > 0)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            const int start = juce::jlimit (0, numSamples - 1, points[i]);
            const int end   = (i + 1 < points.size())
                                  ? juce::jlimit (0, numSamples, points[i + 1])
                                  : numSamples;
            if (end > start)
                built.push_back ({ start, end - start });
        }
    }
    publish (std::move (built));
}

std::optional<SlicePoint> SliceEngine::getSlice (int i) const
{
    if (i < 0 || i >= (int) slices.size())
        return std::nullopt;
    return slices[(size_t) i];
}

bool SliceEngine::tryGetSlice (int index, SlicePoint& out) const
{
    const juce::SpinLock::ScopedTryLockType sl (slicesLock);
    if (! sl.isLocked())
        return false;
    if (index < 0 || index >= (int) slices.size())
        return false;
    out = slices[(size_t) index];
    return true;
}
