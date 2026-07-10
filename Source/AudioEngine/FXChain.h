/*  [초보자 안내]
    이펙트 체인 = 소리가 차례로 지나가는 화장대. 순서대로:
    드라이브(tanh 소프트클립 — 부드럽게 찌그러뜨려 따뜻한 질감),
    리버브(방 울림), 딜레이(메아리), 스테레오 위드너(좌우 넓힘).
    각 이펙트의 세기 노브는 SmoothedValue 로 감싸서, 값을 확 돌려도
    소리가 '지익' 끊기지 않고 수 밀리초에 걸쳐 스르륵 변합니다.
    모든 버퍼는 prepare 에서 미리 확보 — 역시 오디오 스레드 규칙 준수.
*/

// [신호 체인에서 담당] 소리의 마지막 '화장(이펙트)' 단계.
//   VoicePool/SynthEngine → PitchFormant → (여기) FXChain → Limiter 순서.
//   각 이펙트는 독립 클래스(Distortion/Filter/Reverb/Delay)로 나뉘어 있고
//   FXChain이 그것들을 하나로 묶어 순서대로 호출합니다.

#pragma once

#include <juce_dsp/juce_dsp.h>
// 우리가 만든 RBJ 바이쿼드 필터(2차 IIR 필터). FilterFX가 사용.
#include "../DSP/Biquad.h"
#include <vector>

/** tanh soft-clip drive. `drive` 0..1 maps to increasing pre-gain. */
// [클래스] DistortionFX — tanh로 부드럽게 찌그러뜨리는 드라이브(따뜻한 왜곡).
class DistortionFX
{
public:
    // 준비(샘플레이트 저장·스무더 리셋)와 처리(★오디오 콜백★).
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, float drive);

private:
    double sampleRate = 44100.0;
    // drive 노브를 부드럽게 보간(지퍼 잡음 방지).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive { 0.0f };
};

/** Room reverb wrapping juce::dsp::Reverb; `amount` 0..1 sets the wet level.
    The wet path runs on its own bus with a 12 ms pre-delay (separates the
    reverb from the transient) and a gentle high-pass (keeps low end dry and
    punchy) — the dry signal passes bit-exact. */
// [클래스] ReverbFX — 방 울림. 젖은(wet) 신호만 별도 버스에서 처리하고 원음은 그대로 두어 섞음.
class ReverbFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, float amount);

private:
    // JUCE 기본 리버브 엔진을 감쌈.
    juce::dsp::Reverb reverb;
    double sampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedAmount { 0.0f };

    // 젖은 신호 전용 버스 버퍼.
    juce::AudioBuffer<float> wetBus;
    // 프리딜레이(12ms) 원형 버퍼: 리버브를 원음 트랜지언트에서 살짝 떼어놓아 또렷하게.
    std::vector<float> preLine[2];   // pre-delay ring buffers
    int   prePos = 0, preSamples = 0;
    // 젖은 리턴에 거는 1극 하이패스 상태/계수: 저음은 마르고 펀치 있게 유지.
    float hpState[2] { 0.0f, 0.0f }; // one-pole HP on the wet return
    float hpCoeff = 0.02f;
};

/** Stereo feedback delay; `amount` 0..1 sets the wet level. */
// [클래스] DelayFX — 스테레오 피드백 딜레이(메아리). 핑퐁(좌우 튐) 옵션 포함.
class DelayFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    // 딜레이 시간(초) 설정 → 즉시 샘플 수로 환산. 인라인 함수.
    void setTimeSeconds (float seconds) { delaySeconds = juce::jlimit (0.01f, 2.0f, seconds); updateDelay(); }
    // 피드백(반복 되먹임) 양. 0.95 넘으면 무한 증폭이라 상한 클램프.
    void setFeedback (float fb)         { feedback = juce::jlimit (0.0f, 0.95f, fb); }
    // 핑퐁(좌↔우 교차 반복) 켜기/끄기.
    void setPingpong (bool shouldPingpong) { pingpong = shouldPingpong; }
    void process (juce::AudioBuffer<float>& buffer, float amount);

private:
    // 딜레이 시간(초)을 샘플 수로 변환하는 내부 함수.
    void updateDelay();

    // 딜레이 라인(좌/우)과 쓰기 위치.
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
    // 피드백 경로의 1극 저역통과: 반복될수록 따뜻하고 어둡게(아날로그 에코 느낌).
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
// [클래스] FilterFX — 다중모드 필터(저역/고역/밴드). 채널별 바이쿼드 2개로 구성.
class FilterFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    // type: 0 = Off/bypass, 1 = LowPass, 2 = HighPass, 3 = BandPass
    void process (juce::AudioBuffer<float>& buffer, float cutoffHz, float resonance, int type);

private:
    // 컷오프/공진/타입이 바뀔 때만 필터 계수를 다시 계산(noexcept=예외 안 던짐, RT-안전).
    void updateCoefficients (float cutoffHz, float q, int type) noexcept;

    // BandPass is realised as a HighPass -> LowPass cascade, so we keep two
    // biquads per channel. LowPass/HighPass use only the first stage.
    // 밴드패스는 '하이패스 → 로우패스'를 잇달아 걸어 만듦. 그래서 채널마다 바이쿼드 2개.
    Biquad lp[2];   // primary stage (LP, HP, or the LP half of a band-pass)
    Biquad hp[2];   // secondary stage (only used for band-pass)

    double sampleRate = 44100.0;
    int    numChannels = 2;

    // Cached design point so we only recompute coefficients on change.
    // 마지막 설계값 캐시 — 값이 안 바뀌면 계수 재계산을 건너뛰어 CPU 절약.
    float  lastCutoff = -1.0f;
    float  lastQ      = -1.0f;
    int    lastType   = -1;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedCutoff { 1000.0f };
};

/**
    Master FX chain. Members are processed in the order the host wants; the
    processor calls distortion → reverb → delay by default.
*/
// [클래스] FXChain — 위 이펙트들을 한데 묶는 마스터 체인. 처리 순서는 프로세서가 정함.
class FXChain
{
public:
    // 모든 하위 이펙트를 한 번에 준비.
    void prepare (juce::dsp::ProcessSpec spec);

    // [문법] public 멤버 객체들: 밖에서 distortion.process(...)처럼 직접 접근해 순서대로 호출.
    DistortionFX distortion;
    FilterFX     filter;
    ReverbFX     reverb;
    DelayFX      delay;

private:
    // 준비 사양을 기억.
    juce::dsp::ProcessSpec spec {};
};
