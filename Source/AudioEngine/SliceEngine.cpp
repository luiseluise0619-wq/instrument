#include "SliceEngine.h"
#include "../DSP/TransientDetector.h"

#include <algorithm>
#include <cmath>

namespace
{
    float monoAt (const juce::AudioBuffer<float>& b, int sample)
    {
        sample = juce::jlimit (0, b.getNumSamples() - 1, sample);
        float v = 0.0f;
        const int chans = juce::jmax (1, b.getNumChannels());
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            v += b.getSample (ch, sample);
        return v / (float) chans;
    }

    float monoAbsAt (const juce::AudioBuffer<float>& b, int sample)
    {
        sample = juce::jlimit (0, b.getNumSamples() - 1, sample);
        float v = 0.0f;
        const int chans = juce::jmax (1, b.getNumChannels());
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            v += std::abs (b.getSample (ch, sample));
        return v / (float) chans;
    }

    float localScore (const juce::AudioBuffer<float>& b, int sample, int radius)
    {
        const int n = b.getNumSamples();
        if (n <= 0)
            return 0.0f;

        const int pre0  = juce::jlimit (0, n - 1, sample - radius);
        const int pre1  = juce::jlimit (0, n - 1, sample);
        const int post0 = pre1;
        const int post1 = juce::jlimit (0, n - 1, sample + radius);

        auto mean = [&] (int a, int z)
        {
            double s = 0.0;
            int c = 0;
            for (int i = a; i <= z; i += 16)
            {
                s += monoAbsAt (b, i);
                ++c;
            }
            return c > 0 ? (float) (s / (double) c) : 0.0f;
        };

        return juce::jmax (0.0f, mean (post0, post1) - 0.55f * mean (pre0, pre1));
    }

    int snapToAttackStart (const juce::AudioBuffer<float>& b, int sample, double sr)
    {
        const int n = b.getNumSamples();
        if (n <= 0)
            return 0;

        const int lookBack = juce::jlimit (16, 2048, (int) (0.018 * sr));
        const int preRoll  = juce::jlimit (8,  512, (int) (0.004 * sr));
        const int from = juce::jmax (0, sample - lookBack);
        const int to   = juce::jlimit (0, n - 1, sample);

        float localPeak = 0.0f;
        for (int i = from; i <= to; i += 8)
            localPeak = juce::jmax (localPeak, monoAbsAt (b, i));

        const float floor = juce::jmax (0.0025f, localPeak * 0.12f);
        int start = to;
        for (int i = to; i >= from; --i)
        {
            if (monoAbsAt (b, i) <= floor)
            {
                start = i;
                break;
            }
        }

        return juce::jlimit (0, n - 1, start - preRoll);
    }

    int snapToZeroCrossing (const juce::AudioBuffer<float>& b, int sample, double sr)
    {
        const int n = b.getNumSamples();
        if (n <= 1)
            return 0;

        sample = juce::jlimit (0, n - 1, sample);
        const int search = juce::jlimit (8, 384, (int) (0.006 * sr));
        const int from = juce::jmax (0, sample - search);
        const int to   = juce::jmin (n - 1, sample + search);

        int best = sample;
        float bestCost = std::abs (monoAt (b, sample)) + 0.000001f * (float) search;

        for (int i = from + 1; i <= to; ++i)
        {
            const float a = monoAt (b, i - 1);
            const float c = monoAt (b, i);
            const bool crosses = (a <= 0.0f && c >= 0.0f) || (a >= 0.0f && c <= 0.0f);
            if (! crosses)
                continue;

            const float distancePenalty = 0.000001f * (float) std::abs (i - sample);
            const float cost = std::abs (c) + distancePenalty;
            if (cost < bestCost)
            {
                best = i;
                bestCost = cost;
            }
        }

        return best;
    }

    void uniqueSorted (std::vector<int>& points)
    {
        std::sort (points.begin(), points.end());
        points.erase (std::unique (points.begin(), points.end()), points.end());
    }
}

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
    numSlicesAtomic.store ((int) slices.size(), std::memory_order_release);
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
    p.minGapMs = 70.0f + (1.0f - sensitivity) * 35.0f;
    transients = TransientDetector::detect (*sample, sampleRate, p);

    // Fall back to a grid if onset detection found nothing usable.
    if (transients.empty())
        return buildGrid();

    const int numSamples = sample->getNumSamples();
    const int minSliceSamples = juce::jmax (256, (int) (0.085 * sampleRate));
    const int scoreRadius = juce::jmax (128, (int) (0.018 * sampleRate));

    std::vector<int> points;
    points.reserve (transients.size() + 1);

    // Always keep the beginning reachable. Otherwise a soft breath or pickup
    // before the first detected consonant disappears from the playable sample.
    points.push_back (0);

    for (int t : transients)
    {
        const int snapped = snapToAttackStart (*sample, t, sampleRate);
        if (snapped > 0 && snapped < numSamples - 1)
            points.push_back (snapped);
    }

    uniqueSorted (points);

    // Merge double-triggers: vocal chops often have a consonant and a vowel
    // peak a few milliseconds apart, but they should be one playable pad.
    std::vector<int> merged;
    merged.reserve (points.size());
    for (int pnt : points)
    {
        if (merged.empty() || pnt - merged.back() >= minSliceSamples)
            merged.push_back (pnt);
        else if (localScore (*sample, pnt, scoreRadius)
              > localScore (*sample, merged.back(), scoreRadius))
            merged.back() = pnt;
    }

    // Remove low-confidence cuts that are usually mouth noise, reverb tails, or
    // waveform wiggles inside one syllable. Higher sensitivity keeps more cuts;
    // lower sensitivity only keeps the obvious musical starts.
    if (merged.size() > 8)
    {
        float peakScore = 0.0f;
        float sumScore = 0.0f;
        int countScore = 0;

        for (int pnt : merged)
        {
            if (pnt == 0)
                continue;

            const float score = localScore (*sample, pnt, scoreRadius);
            peakScore = juce::jmax (peakScore, score);
            sumScore += score;
            ++countScore;
        }

        const float meanScore = countScore > 0 ? sumScore / (float) countScore : 0.0f;
        const float sensitivityKeep = juce::jmap (sensitivity, 0.0f, 1.0f, 0.22f, 0.08f);
        const float threshold = juce::jmax (0.0015f, juce::jmax (peakScore * sensitivityKeep, meanScore * 0.42f));

        std::vector<int> confident;
        confident.reserve (merged.size());
        confident.push_back (0);

        for (int pnt : merged)
        {
            if (pnt == 0)
                continue;

            if (localScore (*sample, pnt, scoreRadius) >= threshold)
                confident.push_back (pnt);
        }

        if (confident.size() >= 4)
            merged = std::move (confident);
    }

    for (int& pnt : merged)
        if (pnt > 0)
            pnt = snapToZeroCrossing (*sample, pnt, sampleRate);

    uniqueSorted (merged);

    // A keyboard row stops being readable past 32 slices. Keep the strongest
    // musical cuts, but preserve time order and the first point.
    constexpr int kMaxAutoSlices = 32;
    if ((int) merged.size() > kMaxAutoSlices)
    {
        struct ScoredPoint { int sample; float score; };
        std::vector<ScoredPoint> scored;
        scored.reserve (merged.size());
        for (int pnt : merged)
            scored.push_back ({ pnt, pnt == 0 ? 999.0f : localScore (*sample, pnt, scoreRadius) });

        std::sort (scored.begin(), scored.end(), [] (const ScoredPoint& a, const ScoredPoint& b)
        {
            return a.score > b.score;
        });
        scored.resize (kMaxAutoSlices);

        merged.clear();
        for (const auto& s : scored)
            merged.push_back (s.sample);
        uniqueSorted (merged);
    }

    std::vector<SlicePoint> built;

    for (size_t i = 0; i < merged.size(); ++i)
    {
        const int start = merged[i];
        const int end   = (i + 1 < merged.size()) ? merged[i + 1] : numSamples;
        if (end > start)
            built.push_back ({ start, end - start });
    }

    // If the detector produced a single giant slice, the user expected Auto
    // Slice to do something useful; give them a musical 16-pad grid instead.
    if (built.size() < 2)
        return buildGrid();

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
