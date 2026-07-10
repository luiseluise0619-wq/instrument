/*  [초보자 안내]
    '보이스 풀' = 동시에 소리 낼 수 있는 목소리(Voice) 여러 개를 미리 만들어
    두고 돌려쓰는 창고예요. 건반을 누르면 빈 보이스 하나를 꺼내 슬라이스를
    재생시키고, 다 쓰면 반납합니다(재생 중 new/delete 금지라서 미리 만들어 둠).
    재미있는 부분은 '무덤(graveyard) 패턴': 새 샘플을 로드해도 옛 샘플 버퍼를
    바로 지우지 않고 무덤 목록에 눕혀 둡니다. 아직 그 버퍼로 소리 내는 보이스가
    있을 수 있으니까요. 아무도 안 쓰는 게 확인되면(참조 수 1) 그때 메시지
    스레드에서 치웁니다 — 오디오 스레드에서 메모리 해제가 터지는 걸 막는 장치.
*/

// [신호 체인에서 담당] 이 파일은 '소리를 실제로 내는 심장부'입니다.
// 건반 하나 = Voice 하나. 여러 건반을 동시에 누르면(화음) Voice가 여러 개
// 동시에 돌아가고, 그 합을 VoicePool이 한 버퍼에 섞어(mix) 내보냅니다.
// 뒤이어 PitchFormant → FXChain → Limiter 순서로 신호가 흘러갑니다.

// [문법] #pragma once = 이 헤더가 한 번만 포함(include)되게 막는 표시.
// 같은 헤더를 여러 번 #include 하면 클래스가 '중복 정의' 되는데, 그걸 방지.
// (옛날식 #ifndef 가드와 같은 목적이지만 한 줄이라 실무에서 이걸 많이 씁니다.)
#pragma once

// [문법] #include = 다른 파일의 선언을 여기로 끌어옵니다.
// juce_dsp: JUCE의 오디오 신호처리(DSP) 모듈. ProcessSpec 등을 여기서 씀.
#include <juce_dsp/juce_dsp.h>
// std::array: 크기가 고정된 배열(컴파일 때 크기 결정). 힙 할당이 없어 오디오에 안전.
#include <array>
// std::atomic: 여러 스레드가 동시에 읽고 써도 '찢어지지' 않는 값. 재생 위치 공유에 씀.
#include <atomic>
// std::shared_ptr / unique_ptr 등 스마트 포인터. 여기선 shared_ptr(공유 소유)로 버퍼를 안전하게 붙듦.
#include <memory>
// std::vector: 크기가 자라는 동적 배열. 여기선 '무덤 목록'(retiredSources)에 사용.
#include <vector>

/**
    A single polyphonic voice. Plays back a region [start, start+len) of a
    shared source buffer, resampled from the sample's native rate to the host
    rate (linear interpolation), shaped by a full ADSR envelope with a tiny
    end-of-slice fade so retriggering/end-of-slice never clicks.

    Supports optional reverse playback (reads the slice backwards) and two play
    modes: Gate (release() starts the release stage) and one-shot (plays the
    whole slice to its end and ignores release()).

    The voice holds a shared_ptr to the source buffer, so the buffer stays alive
    for the voice's whole lifetime even if a new sample is loaded mid-note.
*/
// [클래스란?] class = 데이터(멤버 변수)와 그 데이터를 다루는 함수(멤버 함수)를
// 하나로 묶은 '설계도'입니다. Voice 클래스 = "목소리 하나"를 표현하는 설계도.
// 이 설계도로 만든 실제 물건(객체) 하나가 건반 하나의 소리를 담당합니다.
class Voice
{
// [문법] public: 아래 것들은 클래스 바깥에서도 호출 가능(공개 인터페이스).
public:
    // [역할] start = 이 보이스에게 "지금부터 이 슬라이스를 이 설정으로 재생 시작해"라고 지시.
    // [입력] hostSampleRate: 호스트(DAW)가 초당 처리하는 샘플 수(예: 44100/48000).
    void start (double hostSampleRate,
                // src: 원본 오디오 데이터(공유 소유 포인터). srcSampleRate: 그 원본이 녹음된 샘플레이트.
                std::shared_ptr<const juce::AudioBuffer<float>> src, double srcSampleRate,
                // startSample/lengthSamples: 원본 안에서 재생할 구간(슬라이스)의 시작 위치와 길이(샘플 단위).
                int startSample, int lengthSamples,
                // velocity: 건반을 얼마나 세게 눌렀는지(0~1). 음량/음색에 반영.
                float velocity,
                // ADSR = 소리의 '음량 곡선'. attack(올라감)/decay(내려감)/sustain(유지 레벨)/release(꺼짐). ms 단위.
                float attackMs, float decayMs, float sustain0to1, float releaseMs,
                // reverse: 슬라이스를 거꾸로 재생할지. oneShot: 끝까지 재생하고 release 무시할지.
                bool reverse, bool oneShot);

    // [역할] render = 오디오 콜백이 "이번 블록에서 numSamples개 만큼 소리를 out에 채워줘"라고 부르는 함수.
    // [실무 핵심] 여기 안에서는 new/delete/lock/파일접근 금지(오디오 스레드라서). 준비된 데이터만 계산해 씀.
    void render (juce::AudioBuffer<float>& out, int numSamples);
    // [역할] release = 건반에서 손을 뗐을 때 호출. ADSR의 release(꺼짐) 단계를 시작.
    void release();
    // [역할] hardStop = '모든 소리 끄기(All Sound Off)'. one-shot 보이스도 빠르게 페이드아웃해서 즉시 끔.
    void hardStop();   // All Sound Off: fast-fades even one-shot voices

    // [역할] isActive = 이 보이스가 지금 소리를 내는 중인지 알려줌. const = 상태를 바꾸지 않는 '읽기 전용' 함수.
    bool isActive() const { return active; }

    // Normalised position of the read head in the WHOLE sample (0..1), or -1 if
    // inactive. Valid after render(). Safe to read from any thread only via the
    // VoicePool atomic mirror; this raw accessor is for the pool's own use.
    // [역할] 지금 재생 헤드가 전체 샘플의 어디쯤인지 0~1로 반환(파형 위 재생선 표시에 사용). 비활성이면 -1.
    float normalisedPosition() const;

// [문법] private: 아래 것들은 클래스 내부에서만 접근 가능(외부 간섭 차단 = 캡슐화).
private:
    // [역할] envelope = 현재 ADSR 단계와 진행도를 보고 이번 샘플의 음량 배수(0~1)를 계산.
    float envelope() const;

    // [문법] enum class = 이름 붙은 상수 묶음. 재생 단계를 숫자 대신 의미 있는 이름으로 표현(오타/혼동 방지).
    enum class Stage { attack, decay, sustain, release, finished };

    // [메모리] source = 원본 버퍼를 '공유 소유'. shared_ptr라 이 보이스가 살아있는 동안 버퍼도 절대 안 사라짐.
    std::shared_ptr<const juce::AudioBuffer<float>> source;
    // srcL/srcR = 원본의 왼/오른쪽 채널 데이터 첫 주소(포인터). 매 샘플 접근을 빠르게 하려고 미리 캐싱.
    const float* srcL = nullptr;
    const float* srcR = nullptr;
    // srcNumSamples = 원본 전체 길이(샘플 수). 범위를 벗어나 읽지 않도록 경계 검사에 씀.
    int   srcNumSamples = 0;

    // pos = 현재 읽는 위치(소수점 포함). 리샘플링 때 두 샘플 사이 '중간 지점'을 읽으므로 double.
    double pos = 0.0;          // fractional read position (source samples)
    // sliceStart/length = 슬라이스의 시작과 길이(원본 샘플 기준).
    double sliceStart = 0.0;   // slice start (source samples)
    double length = 0.0;       // slice length (source samples)
    // ratio = 원본레이트 / 호스트레이트. 이 값만큼 pos를 전진시키면 음정 안 틀리고 속도가 맞음(리샘플링 핵심).
    double ratio = 1.0;        // srcSampleRate / hostSampleRate

    // ADSR (all in output samples).
    // 아래 값들은 ms를 '출력 샘플 수'로 바꿔 저장(콜백은 샘플 단위로 세므로). 0으로 나눔 방지 위해 최소 1.
    int   attackSamples  = 1;
    int   decaySamples   = 1;
    int   releaseSamples = 1;
    // endFadeSamples = 슬라이스 끝에서 아주 짧게 음량을 0으로 낮추는 페이드. 뚝 끊길 때 나는 '틱' 잡음 방지.
    int   endFadeSamples = 1;  // tiny click-free tail at end of slice
    // sustainLevel = 유지 단계에서의 음량 레벨(0~1).
    float sustainLevel   = 1.0f;

    // stage = 지금 ADSR의 어느 단계인지. 시작은 attack.
    Stage stage       = Stage::attack;
    // stagePos = 현재 단계에서 몇 샘플 지났는지(진행도 계산용).
    int   stagePos    = 0;     // output samples elapsed in the current stage
    // releaseFrom = release 시작 순간의 음량. 거기서부터 0까지 자연스럽게 내려가도록 기준점을 잡아둠.
    float releaseFrom = 1.0f;  // env level captured when release starts

    // reversePlay/oneShotMode = 시작 시 정해진 재생 방향/모드를 기억.
    bool  reversePlay = false;
    bool  oneShotMode = false;

    // velocity = 세기(음량 배수). active = 이 보이스가 쓰이는 중인지(false면 창고에서 대기).
    float velocity = 1.0f;
    bool  active   = false;
};

/**
    Fixed-size pool of voices sharing one source buffer.

    triggerVoice() runs on the audio thread only (MIDI and the processor's
    lock-free pad queue both feed it there). setSource() runs on the message
    thread and is guarded by a spin lock; triggerVoice() takes the lock with a
    try-lock and simply drops the trigger on the rare contended block.

    The envelope/reverse/play-mode setters are called from the audio thread once
    per block before any triggers. New voices adopt the current settings.

    Playhead readout: each active voice writes its normalised position in the
    whole sample (0..1) into its atomic slot once per block at the end of
    render; the slot is -1 while inactive. copyPlayheads() lets the message
    thread read them without locking.
*/
// [클래스] VoicePool = Voice 여러 개를 담아 관리하는 '창고 + 지휘자'.
// [스레드 그림] ┌ 메시지(메인) 스레드: 화면/파일 로드 → setSource() 로 새 샘플 넣기.
//              └ 오디오 스레드: 콜백마다 triggerVoice()/render() 로 소리 만들기.
// 이 둘이 '동시에' 같은 데이터를 건드리면 위험 → 그래서 SpinLock/atomic로 조율합니다.
class VoicePool
{
public:
    // [역할] prepare = 재생 시작 전(메시지 스레드)에 호출. 샘플레이트/블록크기 등 준비값을 세팅.
    void prepare (juce::dsp::ProcessSpec spec);
    // [역할] setSource = 새 오디오 샘플로 교체. 메시지 스레드에서 호출되며 SpinLock으로 보호(아래 무덤 패턴 사용).
    void setSource (std::shared_ptr<const juce::AudioBuffer<float>> src, double sampleRate);

    // Called on the audio thread once per block before triggerVoice().
    // [역할] 아래 setter들은 오디오 스레드에서 블록당 1번, 트리거 전에 호출. 새로 눌리는 보이스가 이 값을 물려받음.
    void setEnvelope (float attackMs, float decayMs, float sustain0to1, float releaseMs);
    // 재생 방향(정방향/역방향) 설정.
    void setReverse (bool shouldReverse);
    // 재생 모드(one-shot/gate) 설정.
    void setPlayMode (bool oneShot);

    /** Returns the index of the voice that was started (for note-off routing),
        or -1 if the trigger was dropped. */
    // [역할] triggerVoice = 빈 보이스 하나를 찾아 재생 시작. 시작한 보이스 번호를 반환(나중에 note-off로 끄려고).
    // [실무] try-lock 이 실패(setSource와 겹침)하면 이번 트리거는 '버림'. 오디오는 절대 기다리지 않음.
    int  triggerVoice (int startSample, int lengthSamples, float velocity);
    // [역할] releaseVoice = 특정 번호 보이스에게 release(손 뗌) 지시.
    void releaseVoice (int voiceIndex);
    // [역할] releaseAll = 모든 보이스 release(부드럽게 꺼짐 시작).
    void releaseAll();
    // [역할] stopAll = 모든 소리 즉시 정지(one-shot도 강제 페이드).
    void stopAll();    // All Sound Off (hard-stops one-shot voices too)
    // [역할] renderNextBlock = 모든 활성 보이스의 소리를 out 버퍼에 합쳐서 채움(오디오 콜백의 핵심 호출).
    void renderNextBlock (juce::AudioBuffer<float>& out, int numSamples);

    // Message thread: copy active (>=0) normalised playhead positions into dst.
    // Returns the number written (<= maxCount).
    // [역할] copyPlayheads = 재생선 위치들을 화면(메시지 스레드)으로 복사. atomic이라 락 없이 안전하게 읽음.
    int copyPlayheads (float* dst, int maxCount) const;

    // [문법] static constexpr = 컴파일 때 정해지는 클래스 공용 상수. 동시 최대 발음 수 = 16(폴리포니 한도).
    static constexpr int kMaxVoices = 16;

private:
    // voices = Voice 16개를 고정 배열로 미리 만들어 둠(재생 중 할당 금지라 '풀'로 재사용).
    std::array<Voice, kMaxVoices> voices;
    // playheads = 각 보이스의 재생 위치를 담는 atomic 배열. 오디오가 쓰고 화면이 읽는 '공유 거울'.
    std::array<std::atomic<float>, kMaxVoices> playheads;

    // source = 현재 재생 중인 원본 버퍼(공유 소유).
    std::shared_ptr<const juce::AudioBuffer<float>> source;
    // 원본/호스트 샘플레이트 기억(리샘플 비율 계산에 필요).
    double sourceSampleRate = 44100.0;
    double hostSampleRate   = 44100.0;

    // Replaced sources parked here (message thread) until no voice references
    // them any more — otherwise a voice slot being reused could drop the LAST
    // reference and free a multi-megabyte buffer on the audio thread.
    // [무덤 패턴] retiredSources = 교체된 옛 버퍼를 잠시 눕혀두는 곳.
    // 아직 그 버퍼로 소리 내는 보이스가 있을 수 있어 바로 못 지움. 참조 수가 1(=여기만 소유)이 되면
    // 메시지 스레드에서 안전하게 정리. → 오디오 스레드에서 큰 메모리 해제가 터지는 사고를 예방.
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> retiredSources;

    // Current envelope / mode settings adopted by newly triggered voices.
    // 새 보이스가 물려받을 현재 설정값들(기본값 포함).
    float attackMs    = 5.0f;
    float decayMs     = 0.0f;
    float sustainLvl  = 1.0f;
    float releaseMs   = 5.0f;
    bool  reversePlay = false;
    bool  oneShotMode = false;

    // [스레드 안전] SpinLock = 아주 짧게 잡았다 푸는 가벼운 락. setSource(쓰기)와 triggerVoice(try-lock)가
    // 같은 source를 동시에 못 건드리게 조율. 오디오 쪽은 try-lock으로 '기다리지 않고' 실패 시 포기.
    juce::SpinLock sourceLock;
};
