#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <algorithm>
#include <cmath>

/**
    High-Frequency-Content (HFC) transient detector.

    Header-only utility used by SliceEngine. Computes a short-time HFC onset
    function, normalises it, and returns peak positions (in samples) that lie
    above a sensitivity-scaled threshold and are separated by at least a
    minimum gap.
*/
class TransientDetector
{
public:
    struct Params
    {
        int   fftOrder    = 10;    // window = 1 << fftOrder (1024)
        int   hop         = 256;
        float sensitivity = 0.3f;  // 0..1, higher = fewer onsets
        float minGapMs    = 50.0f;
    };

    // Note: no default argument for `p` — GCC/Clang reject `= {}` for a nested
    // struct with member initializers used inside the enclosing class.
    static std::vector<int> detect (const juce::AudioBuffer<float>& buffer,
                                     double sampleRate,
                                     const Params& p)
    {
        std::vector<int> onsets;
        const int numSamples = buffer.getNumSamples();
        const int windowSize = 1 << p.fftOrder;
        if (numSamples < windowSize || sampleRate <= 0.0)
            return onsets;

        const float* data = buffer.getReadPointer (0);

        juce::dsp::FFT fft (p.fftOrder);
        std::vector<float> window ((size_t) windowSize);
        for (int i = 0; i < windowSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                         * (float) i / (float) windowSize);

        std::vector<float> fftBuf ((size_t) windowSize * 2);
        std::vector<float> onsetFn;
        std::vector<int>   onsetPos;

        for (int start = 0; start + windowSize <= numSamples; start += p.hop)
        {
            std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
            for (int i = 0; i < windowSize; ++i)
                fftBuf[(size_t) i] = data[start + i] * window[(size_t) i];

            fft.performFrequencyOnlyForwardTransform (fftBuf.data());

            // HFC: sum of |X[k]| weighted by bin index k.
            float hfc = 0.0f;
            for (int k = 0; k < windowSize / 2; ++k)
                hfc += fftBuf[(size_t) k] * (float) k;

            onsetFn.push_back (hfc);
            onsetPos.push_back (start);
        }

        if (onsetFn.empty())
            return onsets;

        const float peak = *std::max_element (onsetFn.begin(), onsetFn.end());
        if (peak <= 0.0f)
            return onsets;

        const float threshold = peak * juce::jlimit (0.01f, 0.99f, p.sensitivity);
        const int   minGap    = (int) (p.minGapMs * 0.001f * (float) sampleRate);
        int lastOnset = -minGap;

        for (size_t i = 1; i + 1 < onsetFn.size(); ++i)
        {
            const bool isPeak = onsetFn[i] > onsetFn[i - 1] && onsetFn[i] >= onsetFn[i + 1];
            if (isPeak && onsetFn[i] >= threshold && onsetPos[i] - lastOnset >= minGap)
            {
                onsets.push_back (onsetPos[i]);
                lastOnset = onsetPos[i];
            }
        }

        return onsets;
    }
};
