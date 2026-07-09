/*  [초보자 안내]
    맨 마지막 안전장치, '브릭월(벽돌벽) 리미터'예요. 어떤 일이 있어도 소리가
    천장(0dB 근처)을 못 넘게 막아 스피커와 귀를 보호합니다.
    비결은 look-ahead(미리보기): 소리를 몇 밀리초 늦게 내보내는 대신,
    그 사이에 앞으로 올 피크를 먼저 보고 볼륨을 미리 줄여 둡니다.
    덕분에 피크가 도착할 때는 이미 게인이 내려가 있어서, '퍽' 하고 잘리는
    대신 자연스럽게 눌러집니다. 헤더 한 장짜리 구현이니 통째로 읽기 좋아요.
*/

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

            // Attack instantly to the windowed minimum; release toward it
            // only once every lower target has left the look-ahead window.
            if (windowMin < gain) gain = windowMin;
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
    std::vector<float> delayLines[2];

    // Sliding-minimum state for the look-ahead gain hold.
    std::vector<float> targetWindow;
    int    winPos    = 0;
    float  windowMin = 1.0f;
};
