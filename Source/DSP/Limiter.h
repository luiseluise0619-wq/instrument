#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/**
    Look-ahead brick-wall limiter.

    A short look-ahead delay lets the gain-reduction envelope ramp down before
    a peak arrives, so transients are caught without audible clipping. The
    release smooths the recovery. Header-only.
*/
class Limiter
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        lookaheadSamples = juce::jmax (1, (int) (lookaheadMs * 0.001 * sr));
        releaseCoeff = std::exp (-1.0f / (float) (releaseMs * 0.001 * sr));

        for (auto& d : delayLines)
        {
            d.assign ((size_t) lookaheadSamples, 0.0f);
        }
        writePos = 0;
        gain = 1.0f;
    }

    void setCeiling (float linear) noexcept { ceiling = juce::jlimit (0.001f, 1.0f, linear); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), 2);
        const int numSamples = buffer.getNumSamples();
        if (numCh == 0 || lookaheadSamples <= 0)
            return;

        for (int n = 0; n < numSamples; ++n)
        {
            // Peak across channels at the current input sample.
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, n)));

            // Target gain needed to keep this peak under the ceiling.
            const float target = peak > ceiling ? ceiling / peak : 1.0f;

            // Attack instantly, release smoothly.
            if (target < gain) gain = target;
            else               gain = target + (gain - target) * releaseCoeff;

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& line = delayLines[(size_t) ch];
                const float delayed = line[(size_t) writePos];
                line[(size_t) writePos] = buffer.getSample (ch, n);
                buffer.setSample (ch, n, delayed * gain);
            }

            writePos = (writePos + 1) % lookaheadSamples;
        }
    }

private:
    double sr = 44100.0;
    float  lookaheadMs = 2.0f;
    float  releaseMs   = 50.0f;
    float  ceiling     = 0.98f;

    int    lookaheadSamples = 1;
    int    writePos = 0;
    float  gain = 1.0f;
    float  releaseCoeff = 0.0f;
    std::vector<float> delayLines[2];
};
