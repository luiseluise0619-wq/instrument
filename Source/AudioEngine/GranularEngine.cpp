// [파일 역할] GranularEngine.h의 구현. 알갱이 생성/재생과 기록 링버퍼 관리.
#include "GranularEngine.h"
#include <algorithm>
#include <cmath>

// [함수] prepare — 2초짜리 기록 버퍼를 미리 확보(재생 중 할당 금지).
void GranularEngine::prepare (juce::dsp::ProcessSpec s)
{
    spec = s;
    // 기록 길이 = 2초분 샘플(최소 1).
    historyLen = juce::jmax (1, (int) (2.0 * spec.sampleRate)); // 2 s of history
    for (int ch = 0; ch < kMaxChannels; ++ch)
        history[ch].assign ((size_t) historyLen, 0.0f);

    // 기본 알갱이 크기 80ms로 세팅.
    setGrainSize (80.0f);
    reset();
}

// [함수] reset — 기록/알갱이 상태를 모두 초기화.
void GranularEngine::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
        std::fill (history[ch].begin(), history[ch].end(), 0.0f);
    writeAbs = 0;
    hopCounter = 0;
    for (auto& g : grains)
        g.active = false;
}

// [함수] setGrainSize — 알갱이 크기(ms)를 샘플 수로 변환(64~8192 클램프).
void GranularEngine::setGrainSize (float ms)
{
    grainLenSamples = (int) (ms * 0.001f * (float) spec.sampleRate);
    grainLenSamples = juce::jlimit (64, 8192, grainLenSamples);
}

// [함수] historyRead — 기록 버퍼에서 절대위치 샘플을 읽음(원형 인덱스).
float GranularEngine::historyRead (int channel, long long absolutePos) const
{
    // 음수 위치(아직 기록 전)는 0.
    if (absolutePos < 0)
        return 0.0f;
    // 절대위치를 버퍼 길이로 나머지 → 원형 인덱스.
    const int idx = (int) (absolutePos % historyLen);
    return history[channel][(size_t) idx];
}

// [함수] spawnGrain — 놀고 있는 알갱이 하나를 켜서 최근 기록의 랜덤 지점에서 재생 시작.
void GranularEngine::spawnGrain()
{
    for (auto& g : grains)
    {
        if (! g.active)
        {
            // 위치 지터(무작위 오프셋)로 매번 다른 지점에서 알갱이를 뜸.
            const int jitter = (int) (rng.nextFloat() * (float) grainLenSamples);
            g.length = grainLenSamples;
            g.age    = 0;
            // 읽기 시작점 = 현재 - 한 알갱이 - 지터(과거 기록에서).
            g.readPos = writeAbs - (long long) grainLenSamples - jitter;
            // 유효한(음수 아님) 위치일 때만 활성.
            g.active = g.readPos >= 0;

            // 무작위 팬(equal-power)으로 좌우에 흩뿌림.
            const float pan = rng.nextFloat();               // 0..1
            g.panL = std::cos (pan * juce::MathConstants<float>::halfPi);
            g.panR = std::sin (pan * juce::MathConstants<float>::halfPi);
            return;
        }
    }
}

// [함수] process — ★오디오 콜백★. 입력 기록 + 알갱이 합성 + dry/wet 믹스.
void GranularEngine::process (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin (buffer.getNumChannels(), kMaxChannels);
    const int numSamples  = buffer.getNumSamples();
    // 방어: 채널/샘플/기록이 없으면 종료.
    if (numChannels == 0 || numSamples == 0 || historyLen == 0)
        return;

    // Pure pass-through when disabled.
    // mix가 0이면 원음 그대로 통과(단, 나중에 켤 때 읽을 재료가 있게 기록은 계속 갱신).
    if (mix <= 0.0f)
    {
        // Still keep history current so enabling later has material to read.
        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                history[ch][(size_t) (writeAbs % historyLen)] = buffer.getSample (ch, n);
            ++writeAbs;
        }
        return;
    }

    // 홉(다음 알갱이까지 간격): 밀도가 높을수록 짧아져 더 촘촘.
    const int hop = juce::jmax (1, (int) ((float) grainLenSamples * (1.0f - density * 0.75f)));

    float* outL = buffer.getWritePointer (0);
    float* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const float dry = 1.0f - mix;

    for (int n = 0; n < numSamples; ++n)
    {
        // 현재 입력 샘플.
        const float inL = outL[n];
        const float inR = outR != nullptr ? outR[n] : inL;

        // Write input into history.
        // 입력을 기록 버퍼에 저장.
        history[0][(size_t) (writeAbs % historyLen)] = inL;
        if (numChannels > 1)
            history[1][(size_t) (writeAbs % historyLen)] = inR;

        // Schedule grains.
        // 홉 카운터가 0이 되면 새 알갱이를 뿌리고 카운터 재장전.
        if (--hopCounter <= 0)
        {
            spawnGrain();
            hopCounter = hop;
        }

        // Render active grains.
        // 활성 알갱이들을 합산(젖은 신호).
        float wetL = 0.0f, wetR = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            // Hann 창(0→1→0): 알갱이 양끝을 부드럽게 페이드해 '틱' 방지.
            const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                      * (float) g.age / (float) g.length);
            // 이번 알갱이의 현재 읽기 위치.
            const long long pos = g.readPos + g.age;
            const float sL = historyRead (0, pos);
            const float sR = numChannels > 1 ? historyRead (1, pos) : sL;

            // 창과 팬을 곱해 누적.
            wetL += sL * win * g.panL;
            wetR += sR * win * g.panR;

            // 나이가 길이에 닿으면 알갱이 종료.
            if (++g.age >= g.length)
                g.active = false;
        }

        // Compensate for grain overlap (roughly 2 grains overlapping).
        // 대략 2개가 겹치므로 0.5 곱해 음량 보정.
        wetL *= 0.5f;
        wetR *= 0.5f;

        // 원음×dry + 알갱이×mix 출력.
        outL[n] = inL * dry + wetL * mix;
        if (outR != nullptr)
            outR[n] = inR * dry + wetR * mix;

        // 기록 카운터 전진.
        ++writeAbs;
    }
}
