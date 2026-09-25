#pragma once

// Small, allocation-free DSP building blocks shared by the sample and synth
// engines. No JUCE dependency: the numerical regression tests compile these
// exact routines, rather than a Python reimplementation of them.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace slyce::quality
{
// SFZ loop_end includes its final sample; readers here use [first, end).
inline int exclusiveLoopEnd (int inclusiveEnd) noexcept
{
    if (inclusiveEnd < 0) return 0;
    return inclusiveEnd < std::numeric_limits<int>::max() ? inclusiveEnd + 1 : inclusiveEnd;
}

inline float smoothstep (float t) noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/** 32-tap, Blackman-windowed sinc reader. The low-pass cutoff follows the
    playback rate when pitching up, rather than folding all the source's
    high frequencies into the audible band. It is not a formant shifter.

    Call warmUp() in prepareToPlay, before starting the audio callback. Tables
    are shared, read-only, and never rebuilt on a note-on. Both stereo channels
    use exactly the same kernel. Samples outside a slice are clamped to that
    slice, never borrowed from the adjacent vocal syllable. */
class SampleReader
{
    static constexpr int radius = 16;
    static constexpr int tableSize = 8192;
    struct Tables
    {
        std::array<float, tableSize + 1> sinc {}, window {};
        Tables()
        {
            constexpr double pi = 3.14159265358979323846;
            for (int i = 0; i <= tableSize; ++i)
            {
                const double x = (double) i * radius / tableSize;
                sinc[(size_t) i] = (float) (i == 0 ? 1.0 : std::sin (pi * x) / (pi * x));
                window[(size_t) i] = (float) (0.42 + 0.5 * std::cos (pi * x / radius)
                                                     + 0.08 * std::cos (2.0 * pi * x / radius));
            }
        }
    };
    static const Tables& sharedTables()
    {
        static const Tables t;
        return t;
    }
    static float lookup (const std::array<float, tableSize + 1>& t, float x) noexcept
    {
        const float p = std::min ((float) tableSize, std::abs (x) * (tableSize / (float) radius));
        const int i = std::min (tableSize - 1, (int) p);
        return t[(size_t) i] + (p - (float) i) * (t[(size_t) i + 1] - t[(size_t) i]);
    }
    const Tables* tables = nullptr;
    float cutoff = 1.0f;

public:
    static void warmUp() { (void) sharedTables(); }
    void setRatio (double step) noexcept
    {
        tables = &sharedTables();
        step = std::isfinite (step) ? std::max (1.0e-9, std::abs (step)) : 1.0;
        cutoff = (float) (step > 1.0 ? 0.94 / step : 1.0);
    }

    void readStereo (const float* left, const float* right, double position,
                     int first, int end, bool wrap, float& l, float& r) const noexcept
    {
        l = r = 0.0f;
        if (left == nullptr || end <= first || ! std::isfinite (position) || tables == nullptr)
            return;
        if (right == nullptr) right = left;
        const int span = end - first;
        // The caller normally keeps position inside the region. Normalise it
        // here as well so malformed/small loop regions cannot index outside it.
        if (wrap)
        {
            position = first + std::fmod (position - first, (double) span);
            if (position < first) position += span;
        }
        else position = std::clamp (position, (double) first, (double) end - 1.0);
        const int centre = (int) std::floor (position);
        const float frac = (float) (position - centre);
        if (cutoff == 1.0f && frac < 1.0e-7f)
        {
            l = left[centre]; r = right[centre];
            return; // Native-rate, integer-position reads remain sample-exact.
        }
        float norm = 0.0f;
        for (int tap = -radius + 1; tap <= radius; ++tap)
        {
            const float distance = (float) tap - frac;
            const float w = lookup (tables->sinc, distance * cutoff)
                          * lookup (tables->window, distance);
            int i = centre + tap;
            if (wrap)
            {
                i = (i - first) % span;
                if (i < 0) i += span;
                i += first;
            }
            else i = std::clamp (i, first, end - 1);
            l += left[i] * w; r += right[i] * w; norm += w;
        }
        if (std::abs (norm) > 1.0e-8f) { l /= norm; r /= norm; }
    }
};

/** A short continuity correction for voice stealing / loop wraps. This is
    not another copy of the old voice: it removes the immediate sample step
    and decays the residual over a few milliseconds. */
struct Declicker
{
    float lastL = 0.0f, lastR = 0.0f, tailL = 0.0f, tailR = 0.0f;
    int position = 0, length = 0;
    void reset() noexcept { *this = {}; }
    void begin (int samples, float nextL = 0.0f, float nextR = 0.0f) noexcept
    {
        tailL = lastL - nextL; tailR = lastR - nextR;
        position = 0; length = std::max (1, samples);
    }
    void process (float& l, float& r) noexcept
    {
        if (position < length)
        {
            const float g = 1.0f - smoothstep ((float) position / (float) length);
            l += tailL * g; r += tailR * g; ++position;
        }
        lastL = l; lastR = r;
    }
};

// Constant-peak bandpass with b1=0 and b2=-b0, transposed direct form II.
// z1/z2 are delay states, NOT previous output samples.
inline float bodyBandPass (float x, float b0, float a1, float a2,
                           float& z1, float& z2) noexcept
{
    const float y = b0 * x + z1;
    z1 = z2 - a1 * y;
    z2 = -b0 * x - a2 * y;
    if (std::abs (z1) < 1.0e-20f) z1 = 0.0f;
    if (std::abs (z2) < 1.0e-20f) z2 = 0.0f;
    return y;
}

// Compensate the phase delay of the Karplus-Strong damping one-pole at the
// fundamental. Without it the filter silently lengthens the string period,
// particularly on higher keys. Fractional interpolation still has limits.
inline double stringDelay (double sampleRate, double frequency, float damping) noexcept
{
    const double period = sampleRate / std::max (20.0, frequency);
    const double w = 6.28318530717958647692 / period;
    const double pole = 1.0 - std::clamp ((double) damping, 0.02, 0.95);
    const double phaseDelay = std::atan2 (pole * std::sin (w), 1.0 - pole * std::cos (w)) / w;
    return std::max (1.0, period - phaseDelay);
}

// Do not fold an oscillator's fundamental back down above Nyquist. This is
// only a guard for extreme keys/modulation, not a complete FM anti-aliaser.
inline float nyquistGain (double cyclesPerSample) noexcept
{
    return 1.0f - smoothstep ((float) ((std::abs (cyclesPerSample) - 0.40) / 0.10));
}
} // namespace slyce::quality
