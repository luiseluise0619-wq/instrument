#pragma once

#include <cmath>

/**
    Minimal Direct-Form-II transposed biquad. Header-only so it can be dropped
    into any translation unit without adding a compile target.

    Coefficients follow the RBJ audio EQ cookbook. Call one of the design
    helpers (lowPass / highPass / peak) then process() per sample.
*/
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }

    void setSampleRate (double sr) noexcept { sampleRate = sr > 0.0 ? sr : 44100.0; }

    void lowPass (float freqHz, float q) noexcept
    {
        const float w0    = twoPi * clampFreq (freqHz) / (float) sampleRate;
        const float cosw  = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * juceMax (q, 0.0001f));

        const float b0 = (1.0f - cosw) * 0.5f;
        const float b1 =  1.0f - cosw;
        const float b2 = (1.0f - cosw) * 0.5f;
        const float a0 =  1.0f + alpha;
        const float a1 = -2.0f * cosw;
        const float a2 =  1.0f - alpha;
        normalise (b0, b1, b2, a0, a1, a2);
    }

    void highPass (float freqHz, float q) noexcept
    {
        const float w0    = twoPi * clampFreq (freqHz) / (float) sampleRate;
        const float cosw  = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * juceMax (q, 0.0001f));

        const float b0 =  (1.0f + cosw) * 0.5f;
        const float b1 = -(1.0f + cosw);
        const float b2 =  (1.0f + cosw) * 0.5f;
        const float a0 =   1.0f + alpha;
        const float a1 =  -2.0f * cosw;
        const float a2 =   1.0f - alpha;
        normalise (b0, b1, b2, a0, a1, a2);
    }

    inline float process (float x) noexcept
    {
        const float y = b0c * x + z1;
        z1 = b1c * x - a1c * y + z2;
        z2 = b2c * x - a2c * y;
        return y;
    }

private:
    static constexpr float twoPi = 6.28318530717958647692f;

    static float juceMax (float a, float b) noexcept { return a > b ? a : b; }
    float clampFreq (float f) const noexcept
    {
        const float nyq = (float) sampleRate * 0.49f;
        return f < 10.0f ? 10.0f : (f > nyq ? nyq : f);
    }

    void normalise (float b0, float b1, float b2,
                    float a0, float a1, float a2) noexcept
    {
        const float inv = 1.0f / a0;
        b0c = b0 * inv; b1c = b1 * inv; b2c = b2 * inv;
        a1c = a1 * inv; a2c = a2 * inv;
    }

    double sampleRate = 44100.0;
    float b0c = 1.0f, b1c = 0.0f, b2c = 0.0f, a1c = 0.0f, a2c = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};
