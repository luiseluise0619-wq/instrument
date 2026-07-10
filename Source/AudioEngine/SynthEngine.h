/*  [초보자 안내]
    샘플 재생과 별개로, 파형을 수학으로 직접 만들어내는 '신시사이저' 엔진.
    보이스마다 오실레이터(saw/square/sine/triangle) 최대 7개를 살짝 어긋난
    음정으로 겹쳐(디튠) 두껍고 아날로그 같은 소리를 만듭니다.
    필터(저역통과)·ADSR 엔벨로프(소리의 시작-감쇠-유지-여운 곡선)·비브라토·
    코러스까지 신스의 교과서 부품이 다 들어 있어요.
    이 파일도 오디오 스레드에서 돌기 때문에 모든 버퍼를 prepare 때 미리 잡고,
    재생 중에는 계산만 합니다.
*/

// [신호 체인에서 담당] '샘플 없이도 소리를 만드는' 신스 모드 담당.
//   Slyce는 두 방식으로 소리를 냄: (1) 잘라둔 샘플 조각 재생(VoicePool),
//   (2) 파형을 수학으로 합성(SynthEngine, 이 파일). 패치에 따라 둘 중 하나를 씀.

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

/**
    Polyphonic synth engine v3 — the "Synth" engine mode.

    Per voice:
      - up to 7 unison oscillators (PolyBLEP saw/square, sine, triangle) with
        symmetric detune spread and equal-power stereo panning
      - sine sub-oscillator, white-noise layer, optional 2-op FM/PM pair
      - vibrato LFO plus per-voice analog drift (a slow random walk in cents,
        so stacked voices shimmer like real analog oscillators)
      - resonant TPT state-variable lowpass with its own decay envelope and
        velocity-to-cutoff modulation (soft hits play darker)
      - curved ADSR (ease-out attack, exponential-feel decay/release) — linear
        ramps sound cheap; these bloom and die away naturally

    Synth bus (applied to the summed synth signal only, before it joins the
    host buffer):
      - gentle tanh saturation glue
      - stereo chorus (opposite-phase modulated delays L/R) for pads, keys and
        supersaws — the patch decides how much

    Live parameters (wave/detune/octave/ADSR) come from APVTS every block;
    Patch settings are atomics written on the message thread per instrument.
*/
// [클래스] SynthEngine — 폴리포닉(동시 여러 음) 신시사이저. 위 영어 배너가 부품 목록 설명.
class SynthEngine
{
public:
    // [오실레이터] 기본 파형 4종. saw(톱니)/square(사각)/sine(순음)/triangle(삼각).
    enum Wave { Saw = 0, Square, Sine, Triangle };

    // 한 보이스가 겹쳐 쓸 수 있는 오실레이터 최대 수(유니즌 최대 7).
    static constexpr int kMaxUnison = 7;

    // [구조체] Patch — 악기(음색) 설정 묶음. 값마다 atomic이라 메시지 스레드가 쓰고 오디오가 읽어도 안전.
    struct Patch
    {
        // 유니즌 수(겹치는 오실레이터 개수).
        std::atomic<int>   unison        { 1 };
        // 스테레오 퍼짐 정도.
        std::atomic<float> stereoSpread  { 0.5f };
        // 서브 오실레이터(한 옥타브 아래 사인) 레벨.
        std::atomic<float> subLevel      { 0.0f };
        // 화이트노이즈 레이어 레벨.
        std::atomic<float> noiseLevel    { 0.0f };
        // FM/PM(주파수·위상 변조) 양과 비율.
        std::atomic<float> fmAmount      { 0.0f };
        std::atomic<float> fmRatio       { 2.0f };
        // 비브라토 LFO 속도(Hz)와 깊이(cents).
        std::atomic<float> vibRateHz     { 0.0f };
        std::atomic<float> vibDepthCents { 0.0f };
        // 필터 컷오프 주파수와 필터 엔벨로프 양(옥타브)·시간(ms).
        std::atomic<float> filterCutoff  { 20000.0f };
        std::atomic<float> filterEnvOct  { 0.0f };
        std::atomic<float> filterEnvMs   { 200.0f };
        // v3 lushness controls -------------------------------------------------
        // 필터 공진(레조넌스). 값이 클수록 컷오프 근처가 '삐-' 하고 강조됨.
        std::atomic<float> filterQ       { 0.71f };  // resonance (0.5..8)
        // 아날로그 드리프트 깊이(음정을 미세하게 흔들어 진짜 하드웨어 같은 느낌).
        std::atomic<float> driftCents    { 2.5f };   // analog drift depth
        // 세기→컷오프 변조(약하게 치면 어둡게).
        std::atomic<float> velToFilterOct{ 0.8f };   // velocity -> cutoff
        // 버스 코러스/새추레이션 양.
        std::atomic<float> chorusMix     { 0.0f };   // 0..1 bus chorus
        std::atomic<float> satAmount     { 0.15f };  // 0..1 bus saturation
        // 필터 LFO 속도/깊이.
        std::atomic<float> lfoRateHz     { 2.0f };   // filter LFO rate
        std::atomic<float> lfoDepthOct   { 0.0f };   // filter LFO depth (octaves)
        // 피치 엔벨로프(드럼처럼 순간적으로 음정이 '뚝' 떨어지는 효과)와 그 시간.
        std::atomic<float> pitchEnvOct   { 0.0f };   // pitch drop (octaves, drums)
        std::atomic<float> pitchEnvMs    { 60.0f };  // pitch envelope decay

        // [함수] resetToInit — 모든 패치 값을 기본으로 되돌림(새 악기 로드 전 초기화).
        void resetToInit()
        {
            unison = 1; stereoSpread = 0.5f; subLevel = 0.0f; noiseLevel = 0.0f;
            fmAmount = 0.0f; fmRatio = 2.0f; vibRateHz = 0.0f; vibDepthCents = 0.0f;
            filterCutoff = 20000.0f; filterEnvOct = 0.0f; filterEnvMs = 200.0f;
            filterQ = 0.71f; driftCents = 2.5f; velToFilterOct = 0.8f;
            chorusMix = 0.0f; satAmount = 0.15f;
            lfoRateHz = 2.0f; lfoDepthOct = 0.0f;
            pitchEnvOct = 0.0f; pitchEnvMs = 60.0f;
        }
    };

    // [역할] prepare/reset — 재생 준비(버퍼 미리 확보)와 상태 초기화(메시지 스레드).
    void prepare (juce::dsp::ProcessSpec spec);
    void reset();

    // ADSR/파형/디튠/옥타브 설정(라이브 파라미터).
    void setEnvelope (float attackMs, float decayMs, float sustain0to1, float releaseMs);
    void setWave (int waveType)      { wave = juce::jlimit (0, 3, waveType); }
    void setDetuneCents (float c)    { detuneCents = juce::jlimit (0.0f, 50.0f, c); }
    void setOctave (int oct)         { octave = juce::jlimit (-2, 2, oct); }

    // [문법] Patch& = 참조 반환. 복사본이 아니라 '원본 자체'를 돌려줘 밖에서 바로 수정 가능.
    Patch& patch() { return patchSettings; }

    /** pitchNoteOverride >= 0 plays THAT pitch while the voice stays keyed to
        midiNote for note-off matching (drum-kit mode: fixed drum pitches). */
    // [역할] noteOn — 건반 누름. noteOff — 손 뗌. tapNote — 눌렀다 곧 떼는 '탭'(짧게 한 번).
    void noteOn  (int midiNote, float velocity, int pitchNoteOverride = -1);
    void noteOff (int midiNote);
    void tapNote (int midiNote, float velocity, int pitchNoteOverride = -1);
    // 모든 음 release.
    void releaseAll();

    // [역할] render — ★오디오 콜백★. 모든 활성 보이스를 합성해 out에 더함.
    void render (juce::AudioBuffer<float>& out, int numSamples);

private:
    // [내부 구조체] Voice — 신스 보이스 하나(건반 하나)의 모든 상태.
    struct Voice
    {
        // ADSR 단계 + idle(놀고 있음).
        enum class Stage { attack, decay, sustain, release, idle };

        // 현재 이 보이스가 담당하는 MIDI 노트 번호(-1이면 비어있음)와 세기.
        int    note = -1;
        float  velocity = 0.0f;

        // 유니즌 오실레이터별 현재 위상(phase)과 위상 증가량(inc = 주파수에 해당).
        double phases[kMaxUnison] {};
        double incs[kMaxUnison] {};
        // 유니즌별 좌/우 팬 게인(equal-power 패닝).
        float  panL[kMaxUnison] {}, panR[kMaxUnison] {};
        // 실제 사용 중인 유니즌 수와 음량 정규화 계수.
        int    unison = 1;
        float  unisonNorm = 1.0f;

        // 서브 오실레이터 위상/증가량.
        double subPhase = 0.0,  subInc = 0.0;

        // Pitch envelope (percussion "drop"): 1 -> 0 exponential.
        // 피치 엔벨로프(드럼의 순간 음정 하강): 현재값/감쇠계수/깊이.
        float penv = 0.0f, penvCoeff = 0.0f, penvOct = 0.0f;
        // FM 캐리어/모듈레이터 위상·증가량.
        double fmCarPhase = 0.0, fmCarInc = 0.0;
        double fmModPhase = 0.0, fmModInc = 0.0;
        // 비브라토 LFO 위상/증가량.
        double vibPhase = 0.0,  vibInc = 0.0;

        // 서브/노이즈/FM 레벨과 비브라토 깊이(보이스 시작 시 패치에서 복사해 옴).
        float subLevel = 0.0f, noiseLevel = 0.0f, fmAmount = 0.0f;
        float vibDepthCents = 0.0f;

        // Analog drift: slow random walk in cents.
        // 아날로그 드리프트: 음정을 천천히 무작위로 흔드는 값(현재값/최대폭).
        float driftCents = 0.0f;     // current value
        float driftDepth = 0.0f;     // max cents

        // Resonant TPT SVF (lowpass), per channel states.
        // 공진 저역통과 필터(TPT SVF)의 계수와 채널별 내부 상태(ic1/ic2 = 적분기 메모리).
        float fltBaseHz = 20000.0f, fltEnvOct = 0.0f, fltK = 1.4f;
        float fenv = 0.0f, fenvCoeff = 1.0f;
        float svfA1 = 0, svfA2 = 0, svfA3 = 0;
        float ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0;
        // 계수를 매 샘플이 아니라 몇 샘플마다 갱신하기 위한 카운터(CPU 절약).
        int   fltUpdateCounter = 0;
        bool  fltActive = false;

        // Curved ADSR.
        // 곡선형 ADSR 상태(단계/진행도/release 시작레벨/각 단계 샘플 수/유지 레벨).
        Stage  stage = Stage::idle;
        int    stagePos = 0;
        float  releaseFrom = 1.0f;
        int    attackSamples = 1, decaySamples = 1, releaseSamples = 1;
        float  sustainLevel = 1.0f;

        // tapNote용: 이 샘플 수가 지나면 자동으로 note-off(음수면 사용 안 함).
        int    autoOffCounter = -1;

        // idle이 아니면 활성.
        bool  isActive() const { return stage != Stage::idle; }
        // 현재 순간 음량 배수 계산.
        float envelope() const;
    };

    // [도우미] renderOsc — 파형 하나의 현재 샘플값 계산. findFreeVoice — 빈 보이스 찾기.
    float  renderOsc (double phase, double inc) const;
    Voice* findFreeVoice();
    // [도우미] startVoice — 보이스를 특정 노트로 초기화해 켜기.
    void   startVoice (Voice&, int midiNote, float velocity, int autoOffSamples,
                       int pitchNoteOverride = -1);
    // [도우미] processBus — 합쳐진 신스 신호에만 새추레이션+코러스 적용(scratch 버퍼에서).
    void   processBus (int numSamples);   // saturation + chorus on the scratch

    // 동시 최대 발음 수와 코러스 딜레이 라인 크기.
    static constexpr int kMaxVoices = 16;
    static constexpr int kChorusSize = 8192;

    // 보이스 16개 고정 배열(미리 확보).
    std::array<Voice, kMaxVoices> voices;

    // 현재 패치 설정과 노이즈 난수기.
    Patch  patchSettings;
    juce::Random noiseRng;
    // 전역 필터 LFO 위상(모든 보이스가 공유).
    double lfoPhaseBase = 0.0;   // global filter-LFO phase (0..1)

    // Synth-only scratch bus so chorus/saturation never touch the chop signal.
    // [설계] 신스 전용 임시 버퍼. 코러스/새추레이션이 샘플(chop) 신호를 건드리지 않게 분리.
    juce::AudioBuffer<float> scratch;

    // Chorus state.
    // 코러스 딜레이 라인(좌/우)과 쓰기 위치·LFO 위상.
    std::vector<float> chorusLine[2];
    int    chorusWrite = 0;
    double chorusLfo = 0.0;

    // 현재 샘플레이트/파형/옥타브/디튠.
    double sampleRate = 44100.0;
    int    wave = Saw;
    int    octave = 0;
    float  detuneCents = 7.0f;

    // 현재 ADSR 값(기본값 포함).
    float attackMs = 5.0f, decayMs = 120.0f, sustainLvl = 0.75f, releaseMs = 60.0f;
};
