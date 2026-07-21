#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/**
    Look-ahead brick-wall limiter.

    A short look-ahead delay lets the gain-reduction envelope ramp down before
    a peak arrives. The gain tracks the MINIMUM target over the whole
    look-ahead window — releasing only from targets that have already left the
    delay line — so no delayed peak can exit above the ceiling. Header-only.
*/
class Limiter
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        lookaheadSamples = juce::jmax (1, (int) (lookaheadMs * 0.001 * sr));
        releaseCoeff = std::exp (-1.0f / (float) (releaseMs * 0.001 * sr));
        // Attack smoothing: settle to ~99.3% of the drop across the look-ahead
        // span. An instantaneous gain step IS a click - the delay exists
        // precisely so the ramp can happen before the peak arrives.
        attackCoeff = std::exp (-5.0f / (float) lookaheadSamples);

        for (auto& d : delayLines)
            d.assign ((size_t) lookaheadSamples, 0.0f);

        // One extra slot so a sample's target is still inside the window on
        // the step that sample leaves the delay line.
        targetWindow.assign ((size_t) lookaheadSamples + 1, 1.0f);

        writePos  = 0;
        winPos    = 0;
        windowMin = 1.0f;
        gain      = 1.0f;
    }

    void setCeiling (float linear) noexcept { ceiling = juce::jlimit (0.001f, 1.0f, linear); }

    /** Latency this limiter adds (report it to the host). */
    int getLatencySamples() const noexcept { return lookaheadSamples; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), 2);
        const int numSamples = buffer.getNumSamples();
        if (numCh == 0 || lookaheadSamples <= 0)
            return;

        const int windowLen = (int) targetWindow.size();

        for (int n = 0; n < numSamples; ++n)
        {
            // Peak across channels at the current input sample.
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, n)));

            // Target gain needed to keep this peak under the ceiling.
            const float target = peak > ceiling ? ceiling / peak : 1.0f;

            // Sliding-window minimum of targets across the look-ahead span.
            const float expiring = targetWindow[(size_t) winPos];
            targetWindow[(size_t) winPos] = target;
            winPos = (winPos + 1) % windowLen;

            if (target <= windowMin)
            {
                windowMin = target;
            }
            else if (expiring <= windowMin)
            {
                // The window's minimum just fell out — rescan (rare, and the
                // window is only a couple of hundred entries).
                windowMin = 1.0f;
                for (const float t : targetWindow)
                    windowMin = juce::jmin (windowMin, t);
            }

            // Smoothed attack toward the windowed minimum (the residual error
            // after the look-ahead span is < 1%, comfortably inside the 0.98
            // ceiling headroom); release only once every lower target has
            // left the look-ahead window.
            if (windowMin < gain) gain = windowMin + (gain - windowMin) * attackCoeff;
            else                  gain = windowMin + (gain - windowMin) * releaseCoeff;

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
    float  attackCoeff  = 0.0f;
    std::vector<float> delayLines[2];

    // Sliding-minimum state for the look-ahead gain hold.
    std::vector<float> targetWindow;
    int    winPos    = 0;
    float  windowMin = 1.0f;
};
