// [파일 역할] PitchFormant.h의 구현. 음정/포먼트 변환 + 지연 맞춘 dry/wet 믹스.
#include "PitchFormant.h"
#include <algorithm>

// [함수] prepare — 엔진과 버퍼를 준비(메시지 스레드). 지연을 측정해 링버퍼 크기를 잡음.
void PitchFormant::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    // 샘플레이트/채널 저장(안전값).
    sr       = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = juce::jlimit (1, 2, numChannels);

    // 엔진을 기본 프리셋으로 초기화하고 리셋.
    stretch.presetDefault (channels, (float) sr);
    stretch.reset();

    // 엔진의 입력+출력 지연 합 = 이 모듈의 총 지연.
    latency = stretch.inputLatency() + stretch.outputLatency();

    // 엔진에 넘길 입력 복사 버퍼를 최대 블록 크기로 미리 할당.
    inputScratch.setSize (channels, juce::jmax (1, maxBlockSize), false, false, true);

    // 링버퍼 크기 = 지연 + 블록 + 여유 1. dry를 지연만큼 늦춰 꺼내려고.
    ringCap = juce::jmax (1, latency + juce::jmax (1, maxBlockSize) + 1);
    for (int ch = 0; ch < 2; ++ch)
        dryRing[(size_t) ch].assign ((size_t) ringCap, 0.0f);
    ringWrite = 0;

    // Mix 스무더를 약 20ms 램프로 준비, 시작값 1.0(완전 wet).
    mixSmoothed.reset (sr, 0.02); // ~20 ms ramp
    mixSmoothed.setCurrentAndTargetValue (1.0f);
}

// [함수] reset — 엔진과 링버퍼를 깨끗이 초기화.
void PitchFormant::reset()
{
    stretch.reset();
    for (int ch = 0; ch < 2; ++ch)
        std::fill (dryRing[(size_t) ch].begin(), dryRing[(size_t) ch].end(), 0.0f);
    ringWrite = 0;
}

// [함수] process — ★오디오 콜백★. buffer를 제자리 변환하고 dry/wet를 섞음.
void PitchFormant::process (juce::AudioBuffer<float>& buffer,
                            float pitchSemitones, float formantSemitones, float mix)
{
    // 이번 블록의 목표 음정/포먼트 반영.
    setPitch (pitchSemitones);
    setFormant (formantSemitones);

    // 크기 확인 및 방어.
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), channels);
    if (numSamples == 0 || numChannels == 0)
        return;

    // mix 0~1 클램프.
    mix = juce::jlimit (0.0f, 1.0f, mix);

    // TRUE bypass at neutral settings: the stretcher's ~100 ms latency and
    // CPU must not tax normal playing. Engage only when a knob leaves zero.
    // 음정/포먼트가 사실상 0이면 엔진을 끄고 신호를 그대로 통과(지연 0). 노브가 0을 벗어나면 켬.
    const bool wantEngage = std::abs (pitchSemi) > 0.01f
                         || std::abs (formantSemi) > 0.01f;
    // 켜짐 상태가 바뀌면 페이드를 시작(전환음 튐 방지). 켜질 때는 엔진 리셋.
    if (wantEngage != engaged)
    {
        engaged = wantEngage;
        fadePos = 0;               // mask the path switch with a short fade-in
        if (engaged)
            stretch.reset();
    }
    // 꺼져 있으면 짧은 페이드만 적용하고 원신호 그대로 반환.
    if (! engaged)
    {
        applyToggleFade (buffer, numSamples, numChannels);
        return;                    // signal passes untouched: zero delay
    }

    // Apply pitch/formant on the audio thread (cheap parameter setters).
    // 엔진에 음정/포먼트 지시(가벼운 setter라 오디오 스레드에서 OK). compensate=음색 보존.
    stretch.setTransposeSemitones (pitchSemi);
    stretch.setFormantSemitones (formantSemi, true); // compensate = preserve character

    // Keep an untouched dry copy, then stretch it into the output buffer.
    // 원음(dry)을 복사해 두고(믹스에 쓰려고), 엔진은 그 복사본을 변환해 buffer에 씀.
    for (int ch = 0; ch < numChannels; ++ch)
        inputScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // 엔진에 넘길 입력/출력 포인터 배열 준비.
    float* inPtrs[2]  = { nullptr, nullptr };
    float* outPtrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numChannels; ++ch)
    {
        inPtrs[ch]  = inputScratch.getWritePointer (ch);
        outPtrs[ch] = buffer.getWritePointer (ch);
    }

    // 실제 음정/포먼트 변환 실행(결과가 buffer로 나옴).
    stretch.process (inPtrs, numSamples, outPtrs, numSamples);

    // Latency-matched, per-sample-smoothed dry/wet blend. The ring always
    // advances so toggling the mix at runtime stays coherent; the smoother
    // avoids zipper noise when the Mix knob moves.
    // 지연 맞춤 + 샘플별 부드러운 dry/wet 믹스. 링은 항상 전진, 스무더로 지퍼 잡음 방지.
    mixSmoothed.setTargetValue (mix);
    // 완전 wet(1.0)이 아니거나 스무딩 중일 때만 블렌드 계산(불필요한 연산 절약).
    const bool doBlend = mixSmoothed.isSmoothing() || mix < 1.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        // dry를 latency만큼 앞선 위치에서 읽음(원형 인덱스).
        int readIdx = ringWrite - latency;
        if (readIdx < 0)
            readIdx += ringCap;

        // 이번 샘플의 믹스 값.
        const float m = doBlend ? mixSmoothed.getNextValue() : 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // 현재 dry 샘플을 링에 기록.
            dryRing[(size_t) ch][(size_t) ringWrite] = inputScratch.getSample (ch, n);

            if (doBlend)
            {
                // 지연된 dry와 방금 나온 wet을 m 비율로 섞어 다시 buffer에.
                const float delayedDry = dryRing[(size_t) ch][(size_t) readIdx];
                const float wet = buffer.getSample (ch, n);
                buffer.setSample (ch, n, delayedDry * (1.0f - m) + wet * m);
            }
        }

        // 링 쓰기 위치 전진(원형).
        ringWrite = (ringWrite + 1) % ringCap;
    }

    // 마지막에 토글 페이드 적용.
    applyToggleFade (buffer, numSamples, numChannels);
}

// [함수] applyToggleFade — 경로 전환 시 앞 몇 샘플을 0→1로 서서히 키워 튐을 가림.
void PitchFormant::applyToggleFade (juce::AudioBuffer<float>& buffer,
                                    int numSamples, int numChannels)
{
    // 페이드가 이미 끝났으면 아무것도 안 함.
    if (fadePos >= kFadeLen)
        return;
    // fadePos가 kFadeLen에 닿을 때까지 게인 g를 0→1로 올리며 곱함.
    for (int n = 0; n < numSamples && fadePos < kFadeLen; ++n, ++fadePos)
    {
        const float g = (float) fadePos / (float) kFadeLen;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, n, buffer.getSample (ch, n) * g);
    }
}
