/*  [초보자 안내]
    이펙트 체인 = 소리가 차례로 지나가는 화장대. 순서대로:
    드라이브(tanh 소프트클립 — 부드럽게 찌그러뜨려 따뜻한 질감),
    리버브(방 울림), 딜레이(메아리), 스테레오 위드너(좌우 넓힘).
    각 이펙트의 세기 노브는 SmoothedValue 로 감싸서, 값을 확 돌려도
    소리가 '지익' 끊기지 않고 수 밀리초에 걸쳐 스르륵 변합니다.
    모든 버퍼는 prepare 에서 미리 확보 — 역시 오디오 스레드 규칙 준수.
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../DSP/Biquad.h"
#include <vector>

/** tanh soft-clip drive. `drive` 0..1 maps to increasing pre-gain. */
class DistortionFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, float drive);

private:
    double sampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive { 0.0f };
};

/** Room reverb wrapping juce::dsp::Reverb; `amount` 0..1 sets the wet level.
    The wet path runs on its own bus with a 12 ms pre-delay (separates the
    reverb from the transient) and a gentle high-pass (keeps low end dry and
    punchy) — the dry signal passes bit-exact. */
class ReverbFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, float amount);

private:
    juce::dsp::Reverb reverb;
    double sampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedAmount { 0.0f };

    juce::AudioBuffer<float> wetBus;
    std::vector<float> preLine[2];   // pre-delay ring buffers
    int   prePos = 0, preSamples = 0;
    float hpState[2] { 0.0f, 0.0f }; // one-pole HP on the wet return
    float hpCoeff = 0.02f;
};

/** Stereo feedback delay; `amount` 0..1 sets the wet level. */
class DelayFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setTimeSeconds (float seconds) { delaySeconds = juce::jlimit (0.01f, 2.0f, seconds); updateDelay(); }
    void setFeedback (float fb)         { feedback = juce::jlimit (0.0f, 0.95f, fb); }
    void setPingpong (bool shouldPingpong) { pingpong = shouldPingpong; }
    void process (juce::AudioBuffer<float>& buffer, float amount);

private:
    void updateDelay();

    std::vector<float> line[2];
    int   writePos = 0;
    int   delaySamples = 22050;
    int   maxSamples   = 96000;
    float delaySeconds = 0.35f;
    float feedback     = 0.4f;
    bool  pingpong     = false;
    double sampleRate  = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWet { 0.0f };

    // One-pole lowpass in the feedback path: repeats decay warm, not harsh.
    static constexpr float kDampCoeff = 0.35f;
    float dampState[2] { 0.0f, 0.0f };
};

/**
    Multimode filter using per-channel RBJ biquads (DSP/Biquad.h).

    type: 0 = Off/bypass, 1 = LowPass, 2 = HighPass, 3 = BandPass.
    The cutoff is smoothed to avoid zipper noise; coefficients are recomputed
    only when cutoff/resonance/type actually change. RT-safe: process() does no
    allocation, locking or String use.
*/
class FilterFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    // type: 0 = Off/bypass, 1 = LowPass, 2 = HighPass, 3 = BandPass
    void process (juce::AudioBuffer<float>& buffer, float cutoffHz, float resonance, int type);

private:
    void updateCoefficients (float cutoffHz, float q, int type) noexcept;

    // BandPass is realised as a HighPass -> LowPass cascade, so we keep two
    // biquads per channel. LowPass/HighPass use only the first stage.
    Biquad lp[2];   // primary stage (LP, HP, or the LP half of a band-pass)
    Biquad hp[2];   // secondary stage (only used for band-pass)

    double sampleRate = 44100.0;
    int    numChannels = 2;

    // Cached design point so we only recompute coefficients on change.
    float  lastCutoff = -1.0f;
    float  lastQ      = -1.0f;
    int    lastType   = -1;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedCutoff { 1000.0f };
};

/**
    Master FX chain. Members are processed in the order the host wants; the
    processor calls distortion → reverb → delay by default.
*/
class FXChain
{
public:
    void prepare (juce::dsp::ProcessSpec spec);

    DistortionFX distortion;
    FilterFX     filter;
    ReverbFX     reverb;
    DelayFX      delay;

private:
    juce::dsp::ProcessSpec spec {};
};
