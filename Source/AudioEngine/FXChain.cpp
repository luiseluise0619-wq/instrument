// [파일 역할] FXChain.h의 구현. 드라이브/리버브/필터/딜레이 각 이펙트의 실제 계산.
// [공통 패턴] 모든 이펙트는 (1) 노브를 SmoothedValue로 부드럽게, (2) 완전 꺼짐+램프 끝이면
//   안전하게 건너뛰기(idle skip), (3) 버퍼는 prepare에서 미리 확보 — 오디오 스레드 안전 3원칙.
#include "FXChain.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// [함수] DistortionFX::prepare — 샘플레이트 저장 + 드라이브 스무더 초기화.
void DistortionFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    // 20ms 램프로 스무더 준비, 시작값 0.
    smoothedDrive.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedDrive.setCurrentAndTargetValue (0.0f);
}

// [함수] DistortionFX::process — ★오디오 콜백★. tanh 소프트클립을 걸어 원음과 섞음.
void DistortionFX::process (juce::AudioBuffer<float>& buffer, float drive)
{
    // 목표 드라이브를 0~1로 클램프하고 스무더 목표로.
    const float target = juce::jlimit (0.0f, 1.0f, drive);
    smoothedDrive.setTargetValue (target);

    // Safe idle skip: only bail if the effect is fully off AND not mid-ramp,
    // otherwise we'd chop the tail of a fade-out.
    // 완전히 꺼졌고 램프도 끝났을 때만 건너뜀(페이드아웃 꼬리를 자르지 않으려고).
    if (target <= 0.0f && ! smoothedDrive.isSmoothing())
    {
        smoothedDrive.setCurrentAndTargetValue (0.0f);
        return;
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Snapshot the smoother start state so every channel gets the same ramp.
    // 스무더 시작 상태를 복사 — 모든 채널이 '같은 램프'를 밟게 하려고.
    const auto startDrive = smoothedDrive;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        // 채널마다 램프 복사본으로 시작.
        auto smoother = startDrive; // per-channel copy of the ramp
        float* d = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            // 이번 샘플의 드라이브 값.
            const float dNow    = smoother.getNextValue();
            // 입력을 키우는 프리게인(최대 약 +28dB).
            const float preGain = 1.0f + dNow * 24.0f;   // up to ~+28 dB
            // 왜곡 후 커진 음량을 되돌리는 보정.
            const float makeup  = 1.0f / std::sqrt (preGain);

            // 원음과 tanh 왜곡음을 dNow 비율로 섞음.
            const float clean = d[n];
            const float dirty = std::tanh (clean * preGain) * makeup;
            d[n] = clean * (1.0f - dNow) + dirty * dNow;
        }

        // Keep the shared smoother advanced to the block-end state after the
        // last channel so its position stays consistent across blocks.
        // 마지막 채널을 처리한 뒤 공유 스무더를 블록 끝 상태로 맞춤(블록 간 위치 일관).
        if (ch == numChannels - 1)
            smoothedDrive = smoother;
    }

    // If the buffer had no channels, still advance the smoother by the block.
    // 채널이 하나도 없어도 스무더는 블록만큼 전진시켜 위상 유지.
    if (numChannels == 0)
        smoothedDrive.skip (numSamples);
}

//==============================================================================
// [함수] ReverbFX::prepare — 리버브 엔진·프리딜레이·하이패스 준비.
void ReverbFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    reverb.reset();
    reverb.prepare (spec);

    smoothedAmount.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedAmount.setCurrentAndTargetValue (0.0f);

    // Wet-only inside the reverb; we do the mixing ourselves.
    // 리버브 내부는 100% 젖은 소리만 내게 설정(원음/젖음 섞기는 우리가 직접).
    juce::dsp::Reverb::Parameters p;
    p.roomSize = 0.68f;
    p.damping  = 0.35f;
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    p.width    = 1.0f;
    reverb.setParameters (p);

    // 젖은 버스 버퍼를 최대 블록 크기로 미리 확보.
    wetBus.setSize (2, (int) spec.maximumBlockSize);

    // 12ms 프리딜레이 링버퍼 준비.
    preSamples = juce::jmax (1, (int) (0.012 * sampleRate));   // 12 ms pre-delay
    for (auto& l : preLine)
        l.assign ((size_t) preSamples, 0.0f);
    prePos = 0;

    // One-pole high-pass ~150 Hz on the wet return keeps the low end dry.
    // 젖은 리턴에 약 150Hz 하이패스 계수 계산(저음은 마르게 유지).
    hpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                               * 150.0f / (float) sampleRate);
    hpState[0] = hpState[1] = 0.0f;
}

// [함수] ReverbFX::process — ★오디오 콜백★. 젖은 버스를 만들어 하이패스 후 원음에 더함.
void ReverbFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedAmount.setTargetValue (target);

    // Safe idle skip: only bail if fully off and not still ramping down.
    // 완전 꺼짐+램프 끝이면 건너뜀.
    if (target <= 0.0f && ! smoothedAmount.isSmoothing())
    {
        smoothedAmount.setCurrentAndTargetValue (0.0f);
        return;
    }

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    // 준비한 버스보다 블록이 크면 안전하게 건너뜀.
    if (numSamples > wetBus.getNumSamples())
        return;   // host exceeded the prepared block; skip defensively

    // Feed the wet bus through the pre-delay ring.
    // 입력을 프리딜레이 링을 통해 젖은 버스로 복사(살짝 늦춘 소리).
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* in  = buffer.getReadPointer (juce::jmin (ch, numChannels - 1));
        float*       wet = wetBus.getWritePointer (ch);
        auto&        line = preLine[ch];
        int          pos  = prePos;

        for (int n = 0; n < numSamples; ++n)
        {
            // 링에서 지연된 샘플을 꺼내 wet에 쓰고, 그 자리에 새 입력을 넣음.
            wet[n] = line[(size_t) pos];
            line[(size_t) pos] = in[n];
            pos = (pos + 1) % preSamples;
        }
        // 오른쪽 채널까지 끝나면 쓰기 위치 저장.
        if (ch == 1)
            prePos = pos;
    }

    // 100%-wet reverb on the bus.
    // 젖은 버스에만 리버브를 걸음(제자리 처리).
    juce::dsp::AudioBlock<float> block (wetBus.getArrayOfWritePointers(), 2,
                                        (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);

    // High-pass the wet return and mix it in; the dry path stays untouched.
    // 젖은 리턴을 하이패스하고 원음에 더함(원음 자체는 그대로).
    smoothedAmount.skip (numSamples);
    const float wetGain = smoothedAmount.getCurrentValue() * 0.85f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        const float* wet = wetBus.getReadPointer (ch);
        float state = hpState[ch];

        for (int n = 0; n < numSamples; ++n)
        {
            // 1극 저역통과 상태 갱신 후, (원신호-저역)=고역 성분만 게인 곱해 더함.
            state += hpCoeff * (wet[n] - state);
            out[n] += (wet[n] - state) * wetGain;
        }
        hpState[ch] = state;
    }
}

//==============================================================================
// [함수] FilterFX::prepare — 채널별 바이쿼드 샘플레이트 세팅 + 스무더 준비.
void FilterFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    numChannels = juce::jlimit (1, 2, (int) spec.numChannels);

    for (int ch = 0; ch < 2; ++ch)
    {
        lp[ch].setSampleRate (sampleRate);
        hp[ch].setSampleRate (sampleRate);
    }

    reset();

    smoothedCutoff.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedCutoff.setCurrentAndTargetValue (1000.0f);

    // Force a recompute on first process().
    // 첫 process()에서 반드시 계수를 다시 계산하도록 캐시를 무효값으로.
    lastCutoff = -1.0f;
    lastQ      = -1.0f;
    lastType   = -1;
}

// [함수] FilterFX::reset — 필터 내부 메모리 초기화.
void FilterFX::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        lp[ch].reset();
        hp[ch].reset();
    }
}

// [함수] updateCoefficients — 타입에 따라 각 채널 바이쿼드에 필터 계수를 설정.
void FilterFX::updateCoefficients (float cutoffHz, float q, int type) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        switch (type)
        {
            case 1: // LowPass
                lp[ch].lowPass (cutoffHz, q);
                break;
            case 2: // HighPass
                lp[ch].highPass (cutoffHz, q);
                break;
            case 3: // BandPass: HighPass -> LowPass cascade at the same centre
                // 밴드패스: 같은 중심주파수로 하이패스와 로우패스를 둘 다 걸음.
                hp[ch].highPass (cutoffHz, q);
                lp[ch].lowPass  (cutoffHz, q);
                break;
            default:
                break;
        }
    }
}

// [함수] FilterFX::process — ★오디오 콜백★. 샘플별로 컷오프를 보간하며 필터를 적용.
void FilterFX::process (juce::AudioBuffer<float>& buffer, float cutoffHz, float resonance, int type)
{
    // 0=Off면 아무것도 안 함.
    if (type == 0) // Off/bypass
        return;

    // 컷오프를 나이퀴스트 근처(0.49*fs)까지로 제한(불안정 방지).
    const float nyqLimit = (float) (sampleRate * 0.49);
    const float targetCut = juce::jlimit (20.0f, nyqLimit, cutoffHz);

    // resonance [0.1, 8] -> Q. Q is proportional to resonance; clamp keeps the
    // filter stable and prevents runaway self-oscillation.
    // 공진(Q)을 안전 범위로 클램프 — 너무 높으면 스스로 발진(삐- 폭주)함.
    const float q = juce::jlimit (0.1f, 8.0f, resonance);

    smoothedCutoff.setTargetValue (targetCut);

    const int numCh      = juce::jmin (buffer.getNumChannels(), numChannels);
    const int numSamples = buffer.getNumSamples();

    // Recompute coefficients per sample only while the cutoff is smoothing;
    // otherwise recompute once when cutoff/reso/type change, then hold.
    // 컷오프가 움직이는 동안엔 샘플마다, 아니면 값이 바뀔 때만 계수 재계산(CPU 절약).
    for (int n = 0; n < numSamples; ++n)
    {
        const float cut = smoothedCutoff.getNextValue();

        // 설계값이 하나라도 바뀌면 계수 갱신 후 캐시 저장.
        if (cut != lastCutoff || q != lastQ || type != lastType)
        {
            updateCoefficients (cut, q, type);
            lastCutoff = cut;
            lastQ      = q;
            lastType   = type;
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            float x = buffer.getSample (ch, n);

            // 밴드패스면 하이패스→로우패스 순서로 통과, 아니면 한 번만.
            if (type == 3) // band-pass cascade
                x = lp[ch].process (hp[ch].process (x));
            else
                x = lp[ch].process (x);

            buffer.setSample (ch, n, x);
        }
    }

    // If no channels were processed, keep the cutoff smoother advanced.
    // 채널이 없었으면 컷오프 스무더만 전진.
    if (numCh == 0)
        smoothedCutoff.skip (numSamples);
}

//==============================================================================
// [함수] DelayFX::prepare — 딜레이 라인(최대 2초) 확보 + 스무더 준비.
void DelayFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxSamples = (int) (2.0 * sampleRate) + 4;
    for (auto& l : line)
        l.assign ((size_t) maxSamples, 0.0f);
    updateDelay();
    reset();

    smoothedWet.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedWet.setCurrentAndTargetValue (0.0f);
}

// [함수] DelayFX::reset — 딜레이 라인/댐핑 상태를 0으로.
void DelayFX::reset()
{
    for (auto& l : line)
        std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0;
    dampState[0] = dampState[1] = 0.0f;
}

// [함수] updateDelay — 딜레이 시간(초)을 라인 안 샘플 수로 환산.
void DelayFX::updateDelay()
{
    delaySamples = juce::jlimit (1, maxSamples - 1, (int) (delaySeconds * sampleRate));
}

// [함수] DelayFX::process — ★오디오 콜백★. 지연된 소리를 되먹임하며 원음에 더함.
void DelayFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedWet.setTargetValue (target);

    // Safe idle skip: only bail if fully off and not still ramping down; a
    // fade-out still needs to feed the delay line and mix its tail.
    // 완전 꺼짐+램프 끝이면 건너뜀(페이드아웃 중엔 라인에 계속 먹여야 꼬리가 남음).
    if (target <= 0.0f && ! smoothedWet.isSmoothing())
    {
        smoothedWet.setCurrentAndTargetValue (0.0f);
        return;
    }

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    // Ping-pong only makes sense with a genuine stereo pair.
    // 핑퐁(좌우 튐)은 진짜 스테레오일 때만 의미 있음.
    const bool doPingpong = pingpong && numChannels == 2;

    for (int n = 0; n < numSamples; ++n)
    {
        // 이번 샘플의 젖음 비율과, 라인에서 읽을 위치(현재-지연, 원형).
        const float wet     = smoothedWet.getNextValue(); // per-sample wet mix
        const int   readPos = (writePos - delaySamples + maxSamples) % maxSamples;

        if (doPingpong)
        {
            auto& lL = line[0];
            auto& lR = line[1];

            const float inL = buffer.getSample (0, n);
            const float inR = buffer.getSample (1, n);

            const float delayedL = lL[(size_t) readPos];
            const float delayedR = lR[(size_t) readPos];

            // Damped feedback (one-pole lowpass) so repeats get warmer and
            // darker like an analog echo instead of building up harshness.
            // 되먹임에 저역통과를 걸어 반복이 점점 부드럽고 어둡게(딱딱한 축적 방지).
            dampState[0] += kDampCoeff * (delayedR - dampState[0]);
            dampState[1] += kDampCoeff * (delayedL - dampState[1]);

            // Cross-feed the feedback: the left tap feeds the right line and
            // vice-versa, so echoes bounce L<->R.
            // 좌 탭을 우 라인에, 우 탭을 좌 라인에 되먹여 메아리가 좌우로 튀게.
            lL[(size_t) writePos] = inL + dampState[0] * feedback;
            lR[(size_t) writePos] = inR + dampState[1] * feedback;

            // 원음 + 지연음(젖음 비율) 출력.
            buffer.setSample (0, n, inL + delayedL * wet);
            buffer.setSample (1, n, inR + delayedR * wet);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& l = line[(size_t) ch];
                const float in      = buffer.getSample (ch, n);
                const float delayed = l[(size_t) readPos];

                // Damped feedback: each repeat passes a gentle lowpass.
                // 되먹임마다 부드러운 저역통과 통과.
                dampState[(size_t) ch] += kDampCoeff * (delayed - dampState[(size_t) ch]);

                // 같은 채널로 되먹이고, 원음+지연음 출력.
                l[(size_t) writePos] = in + dampState[(size_t) ch] * feedback;
                buffer.setSample (ch, n, in + delayed * wet);
            }
        }

        // 쓰기 위치 전진(원형).
        writePos = (writePos + 1) % maxSamples;
    }

    // If fewer than expected channels were present, keep the smoother advanced.
    // 채널이 없었으면 스무더만 전진.
    if (numChannels == 0)
        smoothedWet.skip (numSamples);
}

//==============================================================================
// [함수] FXChain::prepare — 묶여 있는 모든 이펙트를 한 번에 준비.
void FXChain::prepare (juce::dsp::ProcessSpec s)
{
    spec = s;
    distortion.prepare (spec);
    filter.prepare (spec);
    reverb.prepare (spec);
    delay.prepare (spec);
}
