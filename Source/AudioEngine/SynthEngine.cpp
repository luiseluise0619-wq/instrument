// [파일 역할] SynthEngine.h의 구현. 파형 합성·필터·엔벨로프·코러스 실제 계산이 여기 있음.
// [실시간 안전] render()가 오디오 콜백에서 돌기 때문에 pow/sin 같은 무거운 계산도 최소화하고,
//   버퍼는 prepare에서 미리 잡아 둠. 재생 중 할당/락 없음.
#include "SynthEngine.h"
// std::sin, std::pow, std::tan 등 수학 함수.
#include <cmath>

// [문법] 이름 없는 namespace = 이 .cpp 파일 안에서만 쓰는 '내부 전용' 도우미들(다른 파일과 이름 충돌 방지).
namespace
{
    // [도우미] polyBlep — 톱니/사각파의 '계단'에서 생기는 앨리어싱(고음 잡음)을 부드럽게 깎아주는 보정.
    inline float polyBlep (float t, float dt)
    {
        // 위상이 방금 0을 지났으면(불연속 직후) 보정값 계산.
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        // 위상이 곧 1로 넘어가면(불연속 직전) 반대쪽 보정.
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        // 그 외 구간은 보정 불필요(0).
        return 0.0f;
    }

    // [도우미] midiToHz — MIDI 노트 번호를 주파수(Hz)로. 69번(A4)=440Hz, 12번당 1옥타브(2배).
    inline double midiToHz (double note)
    {
        return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
    }

    // [도우미] advance — 위상(phase)을 inc만큼 전진시키고 1을 넘으면 되감음(0~1 순환).
    inline void advance (double& phase, double inc)
    {
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
    }
}

//==============================================================================
// [함수] Voice::envelope — 곡선형 ADSR로 현재 순간의 음량 배수를 계산.
float SynthEngine::Voice::envelope() const
{
    // Curved stages: linear ramps sound cheap; these bloom (ease-out attack)
    // and die away with an exponential feel (squared falloff) — all without
    // any pow() on the audio thread.
    // [핵심] 직선 램프는 싸구려로 들림 → 제곱 곡선으로 부드럽게 피고 지게 함. pow 없이 곱셈만 써서 가벼움.
    switch (stage)
    {
        // attack: 1-(1-t)^2 = 처음 빠르게 오르다 끝에서 완만(ease-out).
        case Stage::attack:
        {
            const float t = (float) stagePos / (float) attackSamples;
            const float u = 1.0f - t;
            return 1.0f - u * u;                       // ease-out rise
        }
        // decay: 1.0에서 sustain 레벨까지 제곱 곡선으로 하강.
        case Stage::decay:
        {
            const float t = (float) stagePos / (float) decaySamples;
            const float u = 1.0f - t;
            return sustainLevel + (1.0f - sustainLevel) * u * u;
        }
        // sustain: 유지 레벨 그대로.
        case Stage::sustain:
            return sustainLevel;
        // release: 손 뗀 레벨에서 0까지 제곱 곡선(자연스러운 여운).
        case Stage::release:
        {
            const float t = (float) stagePos / (float) releaseSamples;
            const float u = 1.0f - t;
            return releaseFrom * u * u;                // natural die-away
        }
        // idle/그 외: 소리 없음.
        case Stage::idle:
        default:
            return 0.0f;
    }
}

//==============================================================================
// [함수] prepare — 재생 준비(메시지 스레드). 버퍼/딜레이 라인을 미리 확보.
void SynthEngine::prepare (juce::dsp::ProcessSpec spec)
{
    // 샘플레이트 저장(0 이하면 44100).
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    // 신스 전용 임시 버퍼를 최대 블록 크기로 미리 할당(재생 중엔 할당 금지라 여기서).
    scratch.setSize (2, juce::jmax (16, (int) spec.maximumBlockSize), false, false, true);

    // 코러스 딜레이 라인(좌/우)을 0으로 채워 준비.
    for (auto& line : chorusLine)
        line.assign ((size_t) kChorusSize, 0.0f);
    chorusWrite = 0;
    chorusLfo   = 0.0;

    // 보이스 상태 초기화.
    reset();
}

// [함수] reset — 모든 보이스를 idle로 되돌리고 필터 메모리/드리프트를 0으로.
void SynthEngine::reset()
{
    for (auto& v : voices)
    {
        v.stage = Voice::Stage::idle;
        v.note  = -1;
        v.autoOffCounter = -1;
        v.ic1L = v.ic2L = v.ic1R = v.ic2R = 0.0f;
        v.driftCents = 0.0f;
    }
}

// [함수] setEnvelope — 라이브 ADSR 값 저장(클램프).
void SynthEngine::setEnvelope (float a, float d, float s, float r)
{
    attackMs   = juce::jmax (0.0f, a);
    decayMs    = juce::jmax (0.0f, d);
    sustainLvl = juce::jlimit (0.0f, 1.0f, s);
    releaseMs  = juce::jmax (0.0f, r);
}

// [함수] findFreeVoice — 빈 보이스를 찾거나, 다 차면 '가장 안 들리는' 보이스를 골라 재사용(voice stealing).
SynthEngine::Voice* SynthEngine::findFreeVoice()
{
    // 놀고 있는 보이스가 있으면 그걸 반환(포인터).
    for (auto& v : voices)
        if (! v.isActive())
            return &v;

    // All voices busy: steal the least audible one — prefer voices already
    // in release, then the lowest envelope. Stealing voices[0] blindly cut
    // off held notes.
    // 모두 사용 중이면 '비용'이 가장 낮은 보이스를 훔침. release 중이거나 음량이 낮은 걸 우선.
    Voice* best     = &voices[0];
    float  bestCost = 1.0e9f;

    for (auto& v : voices)
    {
        // release가 아니면 +10 페널티 → 유지 중인 음을 함부로 자르지 않게 함.
        const float cost = v.envelope()
                         + (v.stage == Voice::Stage::release ? 0.0f : 10.0f);
        if (cost < bestCost)
        {
            bestCost = cost;
            best     = &v;
        }
    }
    return best;
}

// [함수] startVoice — 보이스 하나를 특정 노트/설정으로 초기화해 켬. 신스의 '음 시작' 세팅이 모두 여기.
void SynthEngine::startVoice (Voice& v, int midiNote, float velocity, int autoOffSamples,
                              int pitchNoteOverride)
{
    // Drum-kit mode plays a FIXED pitch per piece while the voice stays keyed
    // to the pressed note (so note-off still matches).
    // 드럼킷 모드: 실제 재생 음정(pitchNote)은 고정, note-off 매칭은 누른 건반(midiNote)으로.
    const int pitchNote   = pitchNoteOverride >= 0 ? pitchNoteOverride : midiNote;
    // 옥타브 오프셋 반영한 기준 노트 → 주파수(Hz).
    const double baseNote = juce::jlimit (0, 127, pitchNote) + octave * 12;
    const double hz       = midiToHz (baseNote);

    // 이 보이스가 담당할 노트/세기 기록.
    v.note     = midiNote;
    v.velocity = juce::jlimit (0.0f, 1.0f, velocity);

    // --- Unison bank ---------------------------------------------------------
    // 유니즌 수와 스테레오 퍼짐을 패치에서 읽어옴(atomic load).
    v.unison = juce::jlimit (1, kMaxUnison, patchSettings.unison.load());
    const float spread = juce::jlimit (0.0f, 1.0f, patchSettings.stereoSpread.load());

    // 각 유니즌 오실레이터의 디튠 위치(-1~+1)와 좌우 팬을 설정.
    for (int u = 0; u < v.unison; ++u)
    {
        // pos: 유니즌을 좌우 대칭으로 펼친 상대 위치. 1개면 중앙(0).
        const float pos = v.unison > 1
                            ? -1.0f + 2.0f * (float) u / (float) (v.unison - 1)
                            : 0.0f;

        // cents만큼 음정을 어긋나게 → 위상 증가량(inc = 주파수/샘플레이트) 계산. 100cents=반음.
        const double cents = (double) detuneCents * pos;
        v.incs[u] = hz * std::pow (2.0, cents / 1200.0) / sampleRate;

        // equal-power 패닝: 좌우를 cos/sin으로 나눠 합쳐도 음량이 일정.
        const float pan = 0.5f + 0.5f * pos * spread;
        v.panL[u] = std::cos (pan * juce::MathConstants<float>::halfPi);
        v.panR[u] = std::sin (pan * juce::MathConstants<float>::halfPi);
    }
    // 유니즌을 더할수록 커지는 음량을 1/sqrt(N)로 정규화.
    v.unisonNorm = 1.0f / std::sqrt ((float) v.unison);

    // Free-running-oscillator feel: random start phases decorrelate the
    // unison bank and FM pair, so every retrigger blooms slightly
    // differently instead of machine-gunning an identical waveform. The sub
    // keeps phase 0 so bass attacks stay consistent and punchy.
    // 시작 위상을 랜덤으로 → 매번 조금씩 다른 소리(똑같은 파형 반복=기계음 방지). 서브는 0 유지(저음 펀치 일관).
    for (int u = 0; u < v.unison; ++u)
        v.phases[u] = (double) noiseRng.nextFloat();
    v.fmCarPhase = (double) noiseRng.nextFloat();
    v.fmModPhase = (double) noiseRng.nextFloat();
    v.subPhase   = 0.0;

    // --- Sub / noise / FM / vibrato / drift ----------------------------------
    // 서브/노이즈/FM 양을 패치에서 복사.
    v.subLevel   = juce::jlimit (0.0f, 1.0f, patchSettings.subLevel.load());
    v.noiseLevel = juce::jlimit (0.0f, 1.0f, patchSettings.noiseLevel.load());
    v.fmAmount   = juce::jlimit (0.0f, 1.0f, patchSettings.fmAmount.load());

    // 서브(반옥타브 아래)와 FM 캐리어/모듈레이터의 위상 증가량.
    v.subInc   = (hz * 0.5) / sampleRate;
    v.fmCarInc = hz / sampleRate;
    v.fmModInc = (hz * juce::jmax (0.1f, patchSettings.fmRatio.load())) / sampleRate;

    // 비브라토 깊이/속도.
    v.vibDepthCents = patchSettings.vibDepthCents.load();
    v.vibInc        = patchSettings.vibRateHz.load() / sampleRate;

    // 아날로그 드리프트 최대 폭.
    v.driftDepth = juce::jlimit (0.0f, 15.0f, patchSettings.driftCents.load());

    // Percussion pitch envelope: start high, fall exponentially to base.
    // 드럼용 피치 엔벨로프: 높은 음정에서 시작해 기준 음정으로 지수적으로 떨어짐.
    v.penvOct = juce::jlimit (0.0f, 5.0f, patchSettings.pitchEnvOct.load());
    v.penv    = v.penvOct > 0.0f ? 1.0f : 0.0f;
    {
        // 감쇠 계수: exp(-1/(시간*샘플레이트)). 매 샘플 이 값을 곱해 지수 감쇠.
        const float pms = juce::jmax (5.0f, patchSettings.pitchEnvMs.load());
        v.penvCoeff = std::exp (-1.0f / (pms * 0.001f * (float) sampleRate));
    }

    // --- Resonant filter (velocity opens/closes the base cutoff) -------------
    // 세기에 따라 컷오프를 올리고 내림(약하게 치면 어둡게).
    const float velOct = patchSettings.velToFilterOct.load();
    const float velFactor = std::pow (2.0f, velOct * (v.velocity - 1.0f));
    // 기준 컷오프를 40Hz~나이퀴스트(0.45*fs) 사이로 클램프.
    v.fltBaseHz = juce::jlimit (40.0f,
                                juce::jmin (20000.0f, 0.45f * (float) sampleRate),
                                patchSettings.filterCutoff.load() * velFactor);
    v.fltEnvOct = juce::jlimit (0.0f, 6.0f, patchSettings.filterEnvOct.load());
    // fltK = 1/Q. 공진(레조넌스) 계수.
    v.fltK      = 1.0f / juce::jlimit (0.5f, 8.0f, patchSettings.filterQ.load());
    // 필터가 실제로 소리를 바꾸는 경우에만 켜서 CPU 절약.
    v.fltActive = v.fltBaseHz < 19000.0f || v.fltEnvOct > 0.0f
                    || patchSettings.filterQ.load() > 0.8f;

    // 필터 엔벨로프 초기화(1에서 시작해 지수 감쇠).
    v.fenv = 1.0f;
    const float envMs = juce::jmax (5.0f, patchSettings.filterEnvMs.load());
    v.fenvCoeff = std::exp (-1.0f / (envMs * 0.001f * (float) sampleRate));
    v.fltUpdateCounter = 0;
    // 필터 내부 메모리 초기화.
    v.ic1L = v.ic2L = v.ic1R = v.ic2R = 0.0f;

    // --- Curved ADSR ----------------------------------------------------------
    // ADSR 시간을 샘플 수로 변환(최소 1).
    v.attackSamples  = juce::jmax (1, (int) (attackMs  * 0.001 * sampleRate));
    v.decaySamples   = juce::jmax (1, (int) (decayMs   * 0.001 * sampleRate));
    v.releaseSamples = juce::jmax (1, (int) (releaseMs * 0.001 * sampleRate));
    v.sustainLevel   = sustainLvl;

    // 첫 단계는 attack.
    v.stage       = Voice::Stage::attack;
    v.stagePos    = 0;
    v.releaseFrom = 1.0f;
    // autoOff: 지정 샘플 수 뒤 자동 release(tap용). -1이면 사용 안 함.
    v.autoOffCounter = autoOffSamples;
}

// [함수] noteOn — 건반 누름. 빈 보이스를 찾아 시작(자동 off 없음: -1).
void SynthEngine::noteOn (int midiNote, float velocity, int pitchNoteOverride)
{
    startVoice (*findFreeVoice(), midiNote, velocity, -1, pitchNoteOverride);
}

// [함수] tapNote — 눌렀다 곧 떼는 미리듣기(코드 프리뷰 등). 약 1초 뒤 자동 release.
void SynthEngine::tapNote (int midiNote, float velocity, int pitchNoteOverride)
{
    // Long enough for chord previews to ring musically before releasing.
    startVoice (*findFreeVoice(), midiNote, velocity, (int) (1.0 * sampleRate),
                pitchNoteOverride);
}

// [함수] noteOff — 손 뗌. 해당 노트의 보이스를 release 단계로.
void SynthEngine::noteOff (int midiNote)
{
    for (auto& v : voices)
    {
        // 같은 노트이고 아직 release가 아니면,
        if (v.isActive() && v.note == midiNote
            && v.stage != Voice::Stage::release)
        {
            // 현재 레벨을 잡아 그 지점부터 자연스럽게 꺼짐 시작.
            v.releaseFrom = juce::jlimit (0.0f, 1.0f, v.envelope());
            v.stage       = Voice::Stage::release;
            v.stagePos    = 0;
        }
    }
}

// [함수] releaseAll — 모든 활성 보이스를 release로.
void SynthEngine::releaseAll()
{
    for (auto& v : voices)
    {
        if (v.isActive() && v.stage != Voice::Stage::release)
        {
            v.releaseFrom = juce::jlimit (0.0f, 1.0f, v.envelope());
            v.stage       = Voice::Stage::release;
            v.stagePos    = 0;
        }
    }
}

//==============================================================================
// [함수] renderOsc — 현재 파형 종류에 맞는 한 샘플값을 계산(polyBlep으로 앨리어싱 억제).
float SynthEngine::renderOsc (double phase, double inc) const
{
    const float t  = (float) phase;
    const float dt = (float) inc;

    switch (wave)
    {
        // 톱니: 위상을 -1~1 직선으로 펴고 불연속을 polyBlep로 보정.
        case Saw:
            return (2.0f * t - 1.0f) - polyBlep (t, dt);

        // 사각: 앞 반은 +1, 뒤 반은 -1. 두 불연속(0.5, 1.0)을 각각 보정.
        case Square:
        {
            float sq = t < 0.5f ? 1.0f : -1.0f;
            sq += polyBlep (t, dt);
            sq -= polyBlep (std::fmod (t + 0.5f, 1.0f), dt);
            return sq;
        }

        // 사인: 순음(불연속 없음).
        case Sine:
            return std::sin (juce::MathConstants<float>::twoPi * t);

        // 삼각: |t-0.5| 기반 절댓값 파형.
        case Triangle:
        default:
            return 4.0f * std::abs (t - 0.5f) - 1.0f;
    }
}

// [함수] processBus — 합쳐진 신스 신호(scratch)에만 새추레이션+코러스를 적용.
void SynthEngine::processBus (int numSamples)
{
    float* L = scratch.getWritePointer (0);
    float* R = scratch.getWritePointer (1);

    // --- Gentle saturation glue ----------------------------------------------
    // tanh 새추레이션: 살짝 왜곡을 넣어 소리를 뭉쳐 붙임(글루).
    const float sat = juce::jlimit (0.0f, 1.0f, patchSettings.satAmount.load());
    if (sat > 0.0f)
    {
        // 입력을 키우고(driveIn), 출력에서 다시 정규화(driveOut)해 음량 유지.
        const float driveIn  = 1.0f + sat * 1.5f;
        const float driveOut = 1.0f / std::tanh (driveIn);
        for (int n = 0; n < numSamples; ++n)
        {
            L[n] = std::tanh (L[n] * driveIn) * driveOut;
            R[n] = std::tanh (R[n] * driveIn) * driveOut;
        }
    }

    // --- Stereo chorus ---------------------------------------------------------
    // 코러스: 아주 짧은 딜레이를 LFO로 흔들어 '여러 대가 함께 연주하는' 두께감을 만듦.
    const float mix = juce::jlimit (0.0f, 1.0f, patchSettings.chorusMix.load());
    const double lfoInc = 0.35 / sampleRate;                 // 0.35 Hz sweep
    const float baseDelay = 0.015f * (float) sampleRate;     // ~15 ms centre
    const float depth     = 0.004f * (float) sampleRate;     // ~4 ms swing

    for (int n = 0; n < numSamples; ++n)
    {
        // 현재 샘플을 딜레이 라인에 기록(원형 버퍼).
        chorusLine[0][(size_t) chorusWrite] = L[n];
        chorusLine[1][(size_t) chorusWrite] = R[n];

        if (mix > 0.0f)
        {
            // 코러스 LFO 값(사인).
            const float lfo = std::sin (juce::MathConstants<float>::twoPi
                                        * (float) chorusLfo);

            // Opposite-phase modulation left vs right widens the image.
            // [람다] readTap — 딜레이 라인에서 delaySamples 만큼 뒤 위치를 선형 보간해 읽는 함수.
            auto readTap = [this] (int ch, float delaySamples) -> float
            {
                // 읽을 위치 = 쓰기 위치 - 지연. 음수면 버퍼 크기만큼 감아 넘김(원형).
                float pos = (float) chorusWrite - delaySamples;
                while (pos < 0.0f) pos += (float) kChorusSize;
                const int   i0 = (int) pos;
                const int   i1 = (i0 + 1) % kChorusSize;
                const float fr = pos - (float) i0;
                const auto& ln = chorusLine[(size_t) ch];
                // 이웃 두 샘플 선형 보간.
                return ln[(size_t) i0] + fr * (ln[(size_t) i1] - ln[(size_t) i0]);
            };

            // 좌/우를 반대 위상으로 흔들어 스테레오 폭을 넓힘.
            const float wetL = readTap (0, baseDelay + depth * lfo);
            const float wetR = readTap (1, baseDelay - depth * lfo);

            // 원음과 젖은 신호를 mix 비율로 섞음.
            L[n] = L[n] * (1.0f - 0.5f * mix) + wetL * mix * 0.7f;
            R[n] = R[n] * (1.0f - 0.5f * mix) + wetR * mix * 0.7f;
        }

        // 쓰기 위치와 LFO 위상 전진(원형).
        chorusWrite = (chorusWrite + 1) % kChorusSize;
        chorusLfo += lfoInc;
        if (chorusLfo >= 1.0) chorusLfo -= 1.0;
    }
}

// [함수] render — ★오디오 콜백★. 모든 보이스를 합성 → 버스 처리 → 호스트 버퍼에 더함.
void SynthEngine::render (juce::AudioBuffer<float>& out, int numSamples)
{
    // 방어: 채널/샘플이 없으면 즉시 반환.
    const int numChannels = out.getNumChannels();
    if (numChannels == 0 || numSamples <= 0)
        return;

    // 준비한 scratch보다 블록이 크면(예외적) 안전하게 건너뜀 — 오버런 방지.
    if (scratch.getNumSamples() < numSamples)
        return;   // host exceeded the prepared block size; skip defensively

    // scratch를 0으로 지우고 쓰기 포인터 확보.
    scratch.clear (0, 0, numSamples);
    scratch.clear (1, 0, numSamples);
    float* L = scratch.getWritePointer (0);
    float* R = scratch.getWritePointer (1);

    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    // 이번 블록에 소리 낸 보이스가 하나라도 있었는지.
    bool anyVoice = false;

    // Global filter LFO: one shared phase so all voices breathe together.
    // 전역 필터 LFO: 모든 보이스가 같은 위상으로 '함께 숨쉬게'.
    const double lfoInc   = (double) juce::jlimit (0.0f, 12.0f,
                                patchSettings.lfoRateHz.load()) / sampleRate;
    const float  lfoDepth = juce::jlimit (0.0f, 2.0f,
                                patchSettings.lfoDepthOct.load());

    // 각 활성 보이스를 순회하며 샘플별로 합성.
    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;
        anyVoice = true;

        for (int n = 0; n < numSamples; ++n)
        {
            // --- Vibrato + analog drift -------------------------------------
            // 이번 샘플의 음정 흔들림(cents) 누적: 비브라토 + 드리프트.
            float cents = 0.0f;
            if (v.vibDepthCents > 0.0f)
            {
                cents += v.vibDepthCents * std::sin (twoPi * (float) v.vibPhase);
                advance (v.vibPhase, v.vibInc);
            }
            if (v.driftDepth > 0.0f)
            {
                // Slow random walk, softly pulled back toward centre.
                // 느린 랜덤워크 + 중심으로 살짝 복원(0.99999 곱). 진짜 아날로그 흔들림 흉내.
                v.driftCents += (noiseRng.nextFloat() - 0.5f) * 0.004f;
                v.driftCents *= 0.99999f;
                v.driftCents  = juce::jlimit (-v.driftDepth, v.driftDepth, v.driftCents);
                cents += v.driftCents;
            }
            // cents를 주파수 배수로 변환(0.000578 ≈ ln2/1200).
            float pitchRatio = 1.0f + cents * 0.000578f;
            if (v.penv > 0.00001f)
            {
                // Exponential drop from +penvOct octaves down to the note.
                // 드럼 피치 드롭: 높은 데서 시작해 지수적으로 기준 음정으로 하강.
                pitchRatio *= std::exp2 (v.penvOct * v.penv);
                v.penv *= v.penvCoeff;
            }

            // --- Unison oscillator bank -------------------------------------
            // 유니즌 오실레이터들을 합산(좌/우 팬 적용).
            float oscL = 0.0f, oscR = 0.0f;
            for (int u = 0; u < v.unison; ++u)
            {
                const double inc = v.incs[u] * pitchRatio;
                const float  s   = renderOsc (v.phases[u], inc);
                oscL += s * v.panL[u];
                oscR += s * v.panR[u];
                advance (v.phases[u], inc);
            }
            oscL *= v.unisonNorm;
            oscR *= v.unisonNorm;

            // --- FM/PM pair ----------------------------------------------------
            // FM 있으면 모듈레이터로 캐리어 위상을 흔들어 만든 소리와 원음을 섞음.
            if (v.fmAmount > 0.0f)
            {
                const float mod = std::sin (twoPi * (float) v.fmModPhase);
                const float car = std::sin (twoPi * (float) v.fmCarPhase
                                            + v.fmAmount * 4.0f * mod);
                advance (v.fmCarPhase, v.fmCarInc * pitchRatio);
                advance (v.fmModPhase, v.fmModInc * pitchRatio);

                oscL = oscL * (1.0f - v.fmAmount) + car * v.fmAmount;
                oscR = oscR * (1.0f - v.fmAmount) + car * v.fmAmount;
            }

            // --- Sub + noise -----------------------------------------------------
            // 서브 오실레이터(저음 보강) 더하기.
            if (v.subLevel > 0.0f)
            {
                const float sub = std::sin (twoPi * (float) v.subPhase) * v.subLevel;
                advance (v.subPhase, v.subInc * (double) pitchRatio);
                oscL += sub;
                oscR += sub;
            }
            // 노이즈 레이어 더하기.
            if (v.noiseLevel > 0.0f)
            {
                const float nz = (noiseRng.nextFloat() * 2.0f - 1.0f) * v.noiseLevel;
                oscL += nz;
                oscR += nz;
            }

            // --- Resonant TPT SVF lowpass with decay envelope --------------------
            // 공진 저역통과 필터(State Variable Filter, TPT 방식). 계수는 몇 샘플마다 갱신.
            if (v.fltActive)
            {
                if (--v.fltUpdateCounter <= 0)
                {
                    v.fltUpdateCounter = 32;
                    // Filter LFO ("Motion"): slow sine wobble in octaves.
                    // 필터 LFO('모션'): 컷오프를 옥타브 단위로 느리게 흔듦.
                    float lfoOct = 0.0f;
                    if (lfoDepth > 0.0f)
                        lfoOct = lfoDepth * std::sin (twoPi * (float) std::fmod (
                                     lfoPhaseBase + lfoInc * (double) n, 1.0));
                    // Clamp relative to Nyquist, not just an absolute 18 kHz:
                    // past fs/2 the tan() warp goes negative and the SVF poles
                    // leave the unit circle (inf/NaN at 32 kHz hosts).
                    // 컷오프를 나이퀴스트(fs/2) 아래로 제한 — 넘으면 tan()이 음수가 되며 필터가 폭주(NaN).
                    const float hz = juce::jmin (0.45f * (float) sampleRate,
                                                 juce::jmin (18000.0f,
                        v.fltBaseHz * std::pow (2.0f, v.fltEnvOct * v.fenv + lfoOct)));
                    // TPT SVF 계수(g, a1~a3) 계산.
                    const float g  = std::tan (juce::MathConstants<float>::pi
                                               * hz / (float) sampleRate);
                    v.svfA1 = 1.0f / (1.0f + g * (g + v.fltK));
                    v.svfA2 = g * v.svfA1;
                    v.svfA3 = g * v.svfA2;
                }
                // 필터 엔벨로프 지수 감쇠.
                v.fenv *= v.fenvCoeff;

                {   // left
                    // 좌 채널 SVF 한 스텝(적분기 상태 ic1/ic2 갱신, 저역통과 출력 v2).
                    const float v3 = oscL - v.ic2L;
                    const float v1 = v.svfA1 * v.ic1L + v.svfA2 * v3;
                    const float v2 = v.ic2L + v.svfA2 * v.ic1L + v.svfA3 * v3;
                    v.ic1L = 2.0f * v1 - v.ic1L;
                    v.ic2L = 2.0f * v2 - v.ic2L;
                    oscL = v2;
                }
                {   // right
                    // 우 채널 SVF 한 스텝.
                    const float v3 = oscR - v.ic2R;
                    const float v1 = v.svfA1 * v.ic1R + v.svfA2 * v3;
                    const float v2 = v.ic2R + v.svfA2 * v.ic1R + v.svfA3 * v3;
                    v.ic1R = 2.0f * v1 - v.ic1R;
                    v.ic2R = 2.0f * v2 - v.ic2R;
                    oscR = v2;
                }
            }

            // --- Amp envelope -----------------------------------------------------
            // 최종 음량 = ADSR × 세기 × 0.32(헤드룸). scratch에 더함.
            const float env = v.envelope() * v.velocity * 0.32f;
            L[n] += oscL * env;
            R[n] += oscR * env;

            // --- Envelope state machine -------------------------------------------
            // ADSR 단계 전환.
            ++v.stagePos;
            switch (v.stage)
            {
                case Voice::Stage::attack:
                    if (v.stagePos >= v.attackSamples) { v.stage = Voice::Stage::decay; v.stagePos = 0; }
                    break;
                case Voice::Stage::decay:
                    if (v.stagePos >= v.decaySamples) { v.stage = Voice::Stage::sustain; v.stagePos = 0; }
                    break;
                case Voice::Stage::sustain:
                    break;
                case Voice::Stage::release:
                    if (v.stagePos >= v.releaseSamples) { v.stage = Voice::Stage::idle; v.note = -1; }
                    break;
                case Voice::Stage::idle:
                default:
                    break;
            }
            // 이번에 idle이 되었으면 이 보이스 루프 종료.
            if (! v.isActive())
                break;

            // autoOff(탭/프리뷰): 카운터가 0에 닿으면 자동 release.
            if (v.autoOffCounter > 0 && --v.autoOffCounter == 0
                && v.stage != Voice::Stage::release)
            {
                v.releaseFrom = juce::jlimit (0.0f, 1.0f, v.envelope());
                v.stage       = Voice::Stage::release;
                v.stagePos    = 0;
            }
        }
    }

    // Advance the shared filter-LFO phase once per block.
    // 공유 필터 LFO 위상을 블록당 1번 전진.
    lfoPhaseBase = std::fmod (lfoPhaseBase + lfoInc * (double) numSamples, 1.0);

    // Bus polish runs even without voices so the chorus tail rings out.
    // 코러스 꼬리가 남아있을 수 있어, 보이스가 없어도 코러스가 켜져 있으면 버스 처리를 돌림.
    const bool chorusActive = patchSettings.chorusMix.load() > 0.0f;
    if (anyVoice || chorusActive)
        processBus (numSamples);
    else
        return;   // nothing to add

    // Mix the synth bus into the host buffer.
    // 신스 버스를 실제 출력 버퍼에 더함(스테레오/모노 분기).
    float* outL = out.getWritePointer (0);
    if (numChannels > 1)
    {
        float* outR = out.getWritePointer (1);
        for (int n = 0; n < numSamples; ++n)
        {
            outL[n] += L[n];
            outR[n] += R[n];
        }
    }
    else
    {
        // 모노면 좌우를 평균 내어 한 채널로.
        for (int n = 0; n < numSamples; ++n)
            outL[n] += (L[n] + R[n]) * 0.5f;
    }
}
