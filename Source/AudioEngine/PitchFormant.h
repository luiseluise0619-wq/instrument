// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '음정(pitch)과 포먼트(formant)를 바꾸는 변조기'예요.
// · 음정: 소리를 더 높거나 낮게(반음 단위).
// · 포먼트: 목소리의 '성대 크기감' 같은 음색. 음정만 올리면 '다람쥐 목소리'가
//   되는데, 포먼트를 따로 보정하면 자연스러운 음색을 지킬 수 있어요.
// 신호 순서상 VoicePool/SynthEngine가 만든 소리 다음, FXChain 앞에 놓입니다.
// 내부 엔진(Signalsmith Stretch)은 '지연(latency)'이 있어서, 마른(dry) 소리도
// 같은 만큼 늦춰서 섞어야 위상이 어긋나지 않아요(그래서 링버퍼로 지연을 맞춤).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
// 외부 라이브러리: 고품질 음정/포먼트 변환 엔진(위상 보코더 계열).
#include "signalsmith-stretch.h"
#include <vector>

/**
    Pitch and formant shaper backed by Signalsmith Stretch — a high-quality
    spectral (phase-vocoder-family) engine with proper formant handling.

    - Pitch  : setTransposeSemitones  (independent of formants)
    - Formant: setFormantSemitones (compensated), so shifting pitch keeps the
      natural vocal character instead of the "chipmunk" effect.

    The engine has inherent latency (reported via getLatencySamples()); the
    host is told about it, and the dry path is delay-matched so the dry/wet
    mix stays phase-aligned.
*/
// [클래스] PitchFormant — 음정/포먼트 변환기. 내부에 Signalsmith 엔진을 감싸고 dry/wet 믹스를 관리.
class PitchFormant
{
public:
    // [역할] prepare/reset — 준비(버퍼/링 크기 확보, 지연 측정)와 상태 초기화. 메시지 스레드.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // 음정/포먼트 목표값 설정(안전 범위로 클램프). 헤더 인라인 함수.
    void setPitch (float semitones)   { pitchSemi   = juce::jlimit (-24.0f, 24.0f, semitones); }
    void setFormant (float semitones) { formantSemi = juce::jlimit (-12.0f, 12.0f, semitones); }

    /** Processes in place, blending dry/wet by mix (0 = dry, 1 = wet). */
    // [역할] process — ★오디오 콜백★. buffer를 제자리(in place)에서 변환하고 mix로 원음/변환음 섞음.
    void process (juce::AudioBuffer<float>& buffer,
                  float pitchSemitones, float formantSemitones, float mix);

    // [역할] 이 엔진이 만드는 지연(샘플). 호스트에 알려 전체 위상 정렬에 씀.
    int getLatencySamples() const { return latency; }

private:
    // 실제 변환을 수행하는 외부 엔진 객체(템플릿 <float> = float 샘플용).
    signalsmith::stretch::SignalsmithStretch<float> stretch;

    // 샘플레이트/채널 수/지연 저장.
    double sr = 44100.0;
    int    channels = 2;
    int    latency  = 0;

    // 현재 음정/포먼트 목표값.
    float  pitchSemi   = 0.0f;
    float  formantSemi = 0.0f;

    // 엔진에 넣을 마른(dry) 입력 복사본(미리 확보).
    juce::AudioBuffer<float> inputScratch;   // dry copy fed to the stretcher
    // 지연 맞춤용 원형 버퍼(dry를 latency만큼 늦춰 꺼내려고). 좌/우 2개.
    std::vector<float>       dryRing[2];      // latency-matched dry for the mix
    int    ringCap   = 1;
    int    ringWrite = 0;

    // TRUE bypass at pitch/formant = 0: the stretcher's ~100 ms inherent
    // latency (and its CPU) must never tax plain playing. A short fade-in
    // masks the content jump when the path is toggled mid-note.
    // [최적화] 음정/포먼트가 0이면 엔진을 '완전 우회'. 약 100ms 지연·CPU를 평범한 연주에 물리지 않게.
    // 전환 순간의 소리 튐은 짧은 페이드인으로 가림.
    bool   engaged  = false;
    // fadePos: 토글 페이드 진행 위치. kFadeLen 이상이면 '페이드 없음' 의미.
    int    fadePos  = 1 << 20;   // >= fadeLen means "no fade running"
    static constexpr int kFadeLen = 256;
    // 경로 전환 시 짧은 페이드를 적용하는 도우미.
    void   applyToggleFade (juce::AudioBuffer<float>&, int numSamples, int numChannels);

    // Mix 노브를 움직일 때 지퍼 잡음(계단식 음량변화)이 안 나게 값을 부드럽게 보간.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed { 1.0f };
};
