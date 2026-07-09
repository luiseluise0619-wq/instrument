// ============================================================================
//  PluginProcessor.h  —  Slyce 플러그인의 "심장" 선언부(헤더)
// ----------------------------------------------------------------------------
//  [이 파일이 신호 체인에서 맡는 역할]
//  이 파일은 VocalChopAudioProcessor 라는 클래스를 "선언"만 합니다.
//  (실제 동작 코드는 짝꿍 파일 PluginProcessor.cpp 에 있습니다.)
//  플러그인의 전체 흐름은 대략 이렇습니다:
//     MIDI/키 입력 → 슬라이스(SliceEngine)로 샘플을 잘라 → 여러 목소리(VoicePool)로
//     동시에 재생 → 피치/포먼트(PitchFormant) 보정 → FX(FXChain) → 리미터(Limiter) → 출력
//  이 프로세서가 그 모든 부품(엔진)을 소유하고, 오디오 콜백에서 순서대로 굴립니다.
//
//  [JUCE 가 뭔가요?]
//  JUCE 는 오디오 플러그인/앱을 만드는 C++ 프레임워크입니다. VST3/AU 같은
//  플러그인은 "AudioProcessor(소리 처리 두뇌)" + "AudioProcessorEditor(화면)"
//  두 부분으로 나뉩니다. 이 클래스가 바로 그 두뇌(AudioProcessor)입니다.
//
//  [가장 중요한 개념 — 스레드 두 개]
//  1) 메시지(메인) 스레드: 화면 그리기, 버튼 클릭, 파일 열기 담당. 좀 느려도 됨.
//  2) 오디오 스레드: processBlock() 을 초당 수백 번 호출. 마감시간이 매우 빡빡함.
//     여기서 malloc(메모리 할당)/lock(대기)/파일 접근을 하면 소리가 '뚝' 끊깁니다(글리치).
//  이 헤더 곳곳의 atomic, 락프리 큐, "graveyard(지연 삭제)" 패턴은 전부
//  이 두 스레드가 안전하게 데이터를 주고받기 위한 장치입니다.
// ============================================================================

// #pragma once = 이 헤더가 한 번만 포함되도록 하는 표준 관용구(중복 정의 방지).
#pragma once

// JuceHeader.h = JUCE 의 모든 모듈을 한 번에 끌어오는 통합 헤더.
#include <JuceHeader.h>

// 아래 include 들 = 이 프로세서가 소유하는 각 "부품(엔진)"의 선언을 가져옵니다.
// 헤더에서 멤버 변수로 실제 객체(값)를 담으려면, 그 타입의 정의를 알아야 하므로
// 전방선언이 아니라 헤더 전체를 include 합니다.
// 샘플을 잘게 자르는 슬라이스 엔진.
#include "AudioEngine/SliceEngine.h"
// 파형을 합성해 소리를 만드는 신디사이저 엔진.
#include "AudioEngine/SynthEngine.h"
// 동시에 울리는 여러 '목소리(voice)'를 관리하는 보이스 풀(폴리포니).
#include "AudioEngine/VoicePool.h"
// 음정(pitch)과 성대 특성(formant)을 따로 조절하는 처리기.
#include "AudioEngine/PitchFormant.h"
// 소리 알갱이(grain)를 뿌려 질감을 만드는 그래뉼러 엔진.
#include "AudioEngine/GranularEngine.h"
// 드라이브/리버브/딜레이/스테레오 등 최종 이펙트 체인.
#include "AudioEngine/FXChain.h"
// 연주를 겹겹이 녹음·반복하는 루프 스테이션.
#include "AudioEngine/LoopStation.h"
// SFZ 멀티샘플(실제 녹음 악기)을 재생하는 샘플러 엔진.
#include "AudioEngine/SamplerEngine.h"
// 마지막 안전장치: 소리가 0dB 를 넘어 찌그러지지 않게 눌러주는 리미터.
#include "DSP/Limiter.h"
// 정품 인증(라이선스) 관련 헬퍼.
#include "Licensing.h"

//==============================================================================
// [클래스 선언] VocalChopAudioProcessor
//  여러 부모 클래스를 동시에 상속(다중 상속)합니다:
//   - juce::AudioProcessor: 플러그인 두뇌의 표준 인터페이스(반드시 구현할 함수 목록).
//   - AudioProcessorValueTreeState::Listener: 노브/파라미터 값이 바뀌면 통보받음.
//   - juce::ChangeBroadcaster: 화면(UI)에 "상태가 바뀌었다"고 방송하는 기능.
class VocalChopAudioProcessor : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener,
                                public juce::ChangeBroadcaster
{
// public = 클래스 바깥(주로 UI)에서 호출해도 되는 함수/데이터.
public:
    // 생성자: 플러그인이 만들어질 때 딱 한 번 실행(파라미터 준비, 라이선스 로드 등).
    VocalChopAudioProcessor();
    // 소멸자: 플러그인이 없어질 때 실행. override = 부모의 가상 소멸자를 재정의.
    ~VocalChopAudioProcessor() override;

    //==========================================================================
    // [오디오 수명주기 4대 함수 — JUCE 가 정해진 순간에 불러줌]
    // prepareToPlay: 재생 시작 직전 호출. 샘플레이트/블록크기를 알려주므로
    //   여기서 버퍼를 미리 할당해 둡니다(오디오 스레드에서 할당 안 하려고!).
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    // releaseResources: 재생이 멈출 때 호출. 잡아둔 자원을 풀어줄 자리.
    void releaseResources() override;
    // isBusesLayoutSupported: 이 입출력 채널 구성(예: 스테레오)을 지원하는지 응답.
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    // processBlock: ★오디오 스레드의 심장★. 한 블록(예: 512샘플)의 소리를 매번 처리.
    //   여기서는 절대 malloc/lock/파일접근 금지 — 늦으면 소리가 끊깁니다.
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    // createEditor: 화면(UI) 객체를 새로 만들어 반환. 두뇌가 화면을 낳는 지점.
    juce::AudioProcessorEditor* createEditor() override;
    // hasEditor: 이 플러그인은 전용 화면이 있다(true). { } 안에 바로 구현한 인라인 함수.
    bool hasEditor() const override { return true; }

    // getName: 호스트(DAW)에 표시될 플러그인 이름.
    const juce::String getName() const override { return "Slyce"; }
    // acceptsMidi: 건반(MIDI)을 입력으로 받는다(true) — 이건 악기이므로.
    bool acceptsMidi()  const override { return true;  }
    // producesMidi: MIDI 를 내보내진 않는다(false).
    bool producesMidi() const override { return false; }
    // isMidiEffect: 순수 MIDI 이펙트가 아니다(소리를 낸다).
    bool isMidiEffect() const override { return false; }
    // getTailLengthSeconds: 건반을 떼도 리버브/딜레이 꼬리가 2초간 남는다고 호스트에 알림.
    double getTailLengthSeconds() const override { return 2.0; }

    // 아래 프로그램(프리셋 슬롯) 관련 함수들은 이 플러그인에선 안 쓰므로 최소 구현만.
    // getNumPrograms: 호스트 프로그램 슬롯 개수(1개로 고정).
    int getNumPrograms() override { return 1; }
    // getCurrentProgram: 현재 프로그램 번호(항상 0).
    int getCurrentProgram() override { return 0; }
    // setCurrentProgram: 프로그램 바꾸기 요청 — 무시(빈 구현).
    void setCurrentProgram (int) override {}
    // getProgramName: 프로그램 이름 — 빈 문자열.
    const juce::String getProgramName (int) override { return {}; }
    // changeProgramName: 프로그램 이름 변경 — 무시.
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    // [세션 저장/복원] DAW 프로젝트를 저장/열 때 이 두 함수로 플러그인 상태를 주고받음.
    // getStateInformation: 현재 노브값·로드된 샘플 경로 등을 바이트 뭉치로 직렬화.
    void getStateInformation (juce::MemoryBlock&) override;
    // setStateInformation: 저장했던 바이트 뭉치를 다시 읽어 상태를 복원.
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // GUI-facing API.
    // [UI 가 부르는 공개 API 구역]
    // createParameterLayout: 모든 노브/스위치(파라미터)의 정의 목록을 만드는 정적 함수.
    //   static = 객체 없이도 호출 가능(생성자에서 apvts 초기화에 씀).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // getLoadedSample: 현재 로드된 원본 샘플 버퍼를 공유 포인터로 반환.
    //   shared_ptr = 참조 카운트로 수명 관리 — 오디오 스레드도 UI도 안전하게 함께 봄.
    std::shared_ptr<juce::AudioBuffer<float>> getLoadedSample() const;
    // loadSampleFromFile: 디스크의 오디오 파일을 읽어 로드(메시지 스레드에서, 무거운 작업).
    //   switchEngineToChop=true 면 로드 후 자동으로 촙(슬라이스) 모드로 전환.
    bool loadSampleFromFile (const juce::File&, bool switchEngineToChop = true);
    // loadSampleFromMemory: 메모리에 있는 오디오 바이트(내장 리소스 등)에서 로드.
    bool loadSampleFromMemory (const void* data, int sizeBytes);
    // loadDemoSample: 내장된 데모 보컬을 한 번에 로드(원클릭 체험용).
    bool loadDemoSample();   // embedded demo vocal, one-click start

    // 아래 getter 들 = UI 가 각 엔진의 세부 기능을 직접 만지도록 참조(&)를 넘겨줌.
    //   참조 반환은 복사 없이 실제 내부 객체를 가리킴(주소로 다루는 것과 비슷).
    // 슬라이스 엔진 접근.
    SliceEngine&    getSliceEngine()  { return sliceEngine; }
    // 보이스 풀(동시 발음) 접근.
    VoicePool&      getVoicePool()    { return voicePool;   }
    // 그래뉼러 엔진 접근.
    GranularEngine& getGranular()     { return granularEngine; }
    // 루프 스테이션 접근.
    LoopStation&    getLooper()       { return looper; }
    // 파라미터 트리(APVTS) 접근 — UI 노브를 파라미터에 붙일 때 사용.
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // getLoadedSampleRate: 로드된 샘플의 원래 샘플레이트(재생 속도 계산에 필요).
    double getLoadedSampleRate() const { return loadedSampleRate; }

    /** Message-thread-safe: queues a key/pad hit that the audio thread plays.
        Velocity 0..1 (keyboard clicks pass the strike position). */
    // triggerSlicePad: UI(메시지 스레드)에서 "이 패드 쳤다"를 락프리 큐에 넣음.
    //   실제 재생은 오디오 스레드가 큐를 비우며 처리 — 스레드 간 안전한 신호 전달.
    //   velocity(세기) 0~1, 건반 클릭 위치가 세기로 전달됨.
    void triggerSlicePad (int sliceIndex, float velocity = 0.9f);

    /** Gate-style pad events for the on-screen / computer keyboard: press
        starts the note, release enters its release stage (like real MIDI). */
    // pressSlicePad/releaseSlicePad: 진짜 MIDI 처럼 "누름=소리 시작 / 뗌=릴리즈 진입"의
    //   게이트 방식. 화면 건반이나 컴퓨터 키보드용.
    void pressSlicePad (int sliceIndex, float velocity = 0.9f);
    // 패드에서 손을 뗐을 때(노트 오프) 호출.
    void releaseSlicePad (int sliceIndex);

    /** Live output level (0..1, peak-ish) for the UI meter. */
    // getOutputLevelRef: 미터에 표시할 현재 출력 레벨(0~1)을 atomic 참조로 넘김.
    //   오디오 스레드가 값을 쓰고 UI 가 읽으므로 atomic 이어야 안전(찢긴 값 방지).
    std::atomic<float>& getOutputLevelRef() { return outputLevel; }

    /** True when the Synth engine is selected (keyboard plays synth notes). */
    // isSynthMode: 신스 엔진이 선택되었는지. engine 파라미터 값이 0.5 이상이면 신스.
    bool isSynthMode() const
    {
        // engineParam 이 null 이 아닐 때만 안전하게 load()로 현재 값을 원자적으로 읽음.
        return engineParam != nullptr && engineParam->load() >= 0.5f;
    }

    /** True when the Sampled (SFZ multisample) engine is selected. */
    // isSamplerMode: 샘플러(SFZ 멀티샘플) 모드인지. engine 값이 1.5 이상이면 샘플러.
    bool isSamplerMode() const
    {
        return engineParam != nullptr && engineParam->load() >= 1.5f;
    }

    /** Loads an SFZ multisample bank (message thread; heavy - decodes all
        samples). On success switches to Sampled mode and remembers the path
        in the session state. */
    // loadSfzBank: SFZ 멀티샘플 뱅크를 로드(메시지 스레드; 모든 샘플 디코딩 — 무거움).
    //   성공하면 샘플러 모드로 전환하고, 경로를 기억해 세션 복원 때 다시 불러옴.
    bool loadSfzBank (const juce::File& f, juce::String& error)
    {
        // 실제 로딩은 샘플러 엔진에 위임. 실패하면 error 문자열에 이유가 담기고 false 반환.
        if (! samplerEngine.loadSfz (f, error))
            return false;
        // 성공: 다음에 다시 로드할 수 있도록 파일 경로를 저장.
        loadedSfzFile = f;
        // engine 파라미터를 통해 모드를 바꿈. 파라미터로 바꿔야 UI/호스트가 동기화됨.
        if (auto* p = apvts.getParameter ("engine"))
            p->setValueNotifyingHost (1.0f);   // choice index 2 of 0..2
        // 모든 절차 성공.
        return true;
    }
    // getSampler: 샘플러 엔진 접근(UI 가 뱅크 정보 등을 볼 때).
    SamplerEngine& getSampler() { return samplerEngine; }

    /** Applies a named factory preset's parameter values. */
    // applyPreset: 공장 프리셋 하나를 골라 그 파라미터 값들을 적용.
    void applyPreset (int presetIndex);
    // getPresetNames: 프리셋 이름 목록(콤보박스 채우기용).
    static juce::StringArray getPresetNames();

    /** Built-in synth instruments: applying one switches to Synth mode and
        dials in a designed patch (engine architecture + knob defaults). */
    // getInstrumentNames: 내장 신스 악기(음색) 이름 목록.
    static juce::StringArray getInstrumentNames();
    // getInstrumentCategories: 위 이름과 1:1 로 짝지어진 카테고리 목록.
    static juce::StringArray getInstrumentCategories();   // parallel to names
    // applyInstrument: 악기 하나를 선택 — 신스 모드로 바꾸고 설계된 음색을 세팅.
    void applyInstrument (int instrumentIndex);
    // getCurrentInstrument: 현재 선택된 악기 번호.
    int  getCurrentInstrument() const { return currentInstrument; }

    /** Detected musical key of the loaded sample: root 0..11 (C=0) or -1
        when unknown; minor flag alongside. The chord bar follows this. */
    // getDetectedKeyRoot: 로드된 샘플에서 자동 감지한 조(key)의 으뜸음(0=C ... 11=B, -1=모름).
    //   .load() = atomic 값을 원자적으로 읽음(메시지 스레드가 쓰고 UI 가 읽음).
    int  getDetectedKeyRoot() const  { return detectedKeyRoot.load(); }
    // isDetectedKeyMinor: 감지된 조가 단조(minor)인지 여부.
    bool isDetectedKeyMinor() const  { return detectedKeyMinor.load(); }

    /** Licensing: unlicensed = demo (output mutes 2 s every minute). */
    // isLicensed: 정품 인증 여부. 미인증(데모)이면 매 분마다 2초 음소거로 제한.
    bool isLicensed() const { return licensed.load(); }
    // finalizeActivation: 사용자가 이메일+키를 입력해 정품 인증을 마무리.
    bool finalizeActivation (const juce::String& email, const juce::String& key)
    {
        // 활성화 정보를 디스크에 저장 시도. 실패하면 인증 안 된 상태로 false.
        if (! vcs::Licensing::saveActivation (email, key))
            return false;
        // 성공: licensed 플래그를 원자적으로 true 로 바꿔 데모 제한 해제.
        licensed.store (true);
        return true;
    }

// private = 클래스 내부 구현 상세. 바깥에서 직접 못 건드림(캡슐화).
private:
    //==========================================================================
    // [내부 헬퍼 함수들 — 대부분 오디오/메시지 스레드 역할이 갈림]
    // parameterChanged: 노브 값이 바뀌면 JUCE 가 불러줌(리스너 콜백).
    void parameterChanged (const juce::String& id, float newValue) override;
    // handleMidi: 이번 블록에 들어온 MIDI 이벤트(노트온/오프)를 해석해 보이스에 연결.
    void handleMidi (const juce::MidiBuffer& midi, int numSamples);
    // drainPadQueue: 락프리 큐에 쌓인 UI 패드 이벤트를 오디오 스레드에서 꺼내 처리.
    void drainPadQueue();
    // triggerSliceIndex: 슬라이스 하나를 실제로 발음 — 사용한 voice 번호를 돌려줌.
    int  triggerSliceIndex (int sliceIndex, float velocity);
    // clearVoiceMapping: 특정 voice 의 노트 매핑을 지움(오디오 스레드에서).
    void clearVoiceMapping (int voiceIndex);   // audio thread
    // applyMasterFXChain: 마스터 버스에 드라이브/리버브/딜레이 등 FX 적용.
    void applyMasterFXChain (juce::AudioBuffer<float>&);
    // applyStereoWidth: 좌우 스테레오 폭을 조절.
    void applyStereoWidth (juce::AudioBuffer<float>&);
    // reassignSampleToEngines: 새 샘플을 각 엔진(슬라이스/그래뉼러 등)에 다시 물려줌.
    void reassignSampleToEngines();
    // rescanSlices: 샘플을 다시 분석해 슬라이스 경계를 새로 계산.
    void rescanSlices();
    // analyzeSampleKey: 샘플의 음악적 조(key)를 분석(메시지 스레드).
    void analyzeSampleKey();
    // queuePadEvent: 패드 이벤트(탭/온/오프)를 락프리 큐에 밀어넣는 내부 공통 함수.
    void queuePadEvent (int sliceIndex, float velocity, int type);

    //==========================================================================
    // [멤버 변수 — 이 프로세서가 소유하는 모든 상태와 부품]
    // apvts: 모든 파라미터(노브 값)를 담는 트리. 저장/복원과 UI 바인딩의 중심.
    juce::AudioProcessorValueTreeState apvts;

    // 아래는 각 DSP 부품을 '값'으로 직접 보유(포인터 아님) — 프로세서가 소유·수명 관리.
    // 동시 발음(폴리포니)을 담당하는 보이스 풀.
    VoicePool      voicePool;
    // 파형 합성 신스 엔진.
    SynthEngine    synthEngine;
    // SFZ 멀티샘플 재생 샘플러 엔진.
    SamplerEngine  samplerEngine;
    // 로드된 SFZ 파일 경로(세션 저장에 포함해 재로드 때 복원).
    juce::File     loadedSfzFile;   // persisted so reload restores the bank
    // 샘플 슬라이스 엔진.
    SliceEngine    sliceEngine;
    // 피치/포먼트 처리기.
    PitchFormant   pitchFormant;
    // 그래뉼러 엔진.
    GranularEngine granularEngine;
    // 최종 FX 체인.
    FXChain        fxChain;
    // 루프 스테이션.
    LoopStation    looper;
    // 마지막 리미터(클리핑 방지).
    Limiter        limiter;

    // sampleBuffer: 현재 로드된 원본 오디오. shared_ptr 로 감싼 이유가 핵심입니다.
    //   메시지 스레드가 새 샘플을 로드하면 이 포인터를 새 버퍼로 교체합니다. 그런데
    //   오디오 스레드는 예전 버퍼를 재생 중일 수 있죠. shared_ptr 의 참조 카운트 덕에,
    //   마지막으로 쓰던 스레드가 놓을 때까지 옛 버퍼가 살아 있어 크래시가 없습니다.
    //   (오디오 스레드에서 직접 free 하지 않는 "graveyard/지연 삭제" 철학과 같은 맥락.)
    std::shared_ptr<juce::AudioBuffer<float>> sampleBuffer;
    // 로드된 샘플 파일 경로(세션에 저장 → 프로젝트 다시 열면 자동 재로드).
    juce::File loadedSampleFile;   // persisted in plugin state so reload restores it

    // Cached parameter pointers.
    // [파라미터 포인터 캐시]
    //   매 블록 apvts 에서 이름으로 파라미터를 찾으면(문자열 검색) 느립니다. 그래서
    //   생성자에서 한 번만 주소를 찾아 여기에 저장해두고, 오디오 스레드는 이 포인터로
    //   .load() 만 해서 값을 빠르고 안전하게 읽습니다. 전부 atomic<float>* 입니다.
    // 피치(음정) 노브.
    std::atomic<float>* pitchParam   = nullptr;
    // 포먼트(성대 특성) 노브.
    std::atomic<float>* formantParam = nullptr;
    // 드라이/웻 믹스 노브.
    std::atomic<float>* mixParam     = nullptr;
    // 스테레오 폭 노브.
    std::atomic<float>* widthParam   = nullptr;
    // 그래뉼러 그레인 크기.
    std::atomic<float>* grainSizeParam = nullptr;
    // 그래뉼러 믹스 양.
    std::atomic<float>* grainMixParam  = nullptr;
    // 드라이브(왜곡) 양.
    std::atomic<float>* driveParam   = nullptr;
    // 리버브 양.
    std::atomic<float>* reverbParam  = nullptr;
    // 딜레이 양.
    std::atomic<float>* delayParam   = nullptr;
    // 앰프 엔벨로프 어택.
    std::atomic<float>* attackParam  = nullptr;
    // 앰프 엔벨로프 디케이.
    std::atomic<float>* decayParam   = nullptr;
    // 앰프 엔벨로프 서스테인.
    std::atomic<float>* sustainParam = nullptr;
    // 앰프 엔벨로프 릴리즈.
    std::atomic<float>* releaseParam = nullptr;
    // 필터 컷오프 주파수.
    std::atomic<float>* filterCutoffParam = nullptr;
    // 필터 레조넌스(Q).
    std::atomic<float>* filterResoParam   = nullptr;
    // 필터 종류(로우패스/하이패스 등).
    std::atomic<float>* filterTypeParam    = nullptr;
    // 딜레이 피드백(반복 횟수).
    std::atomic<float>* delayFeedbackParam = nullptr;
    // 핑퐁 딜레이(좌우 교차) 여부.
    std::atomic<float>* pingpongParam = nullptr;
    // 역재생(reverse) 여부.
    std::atomic<float>* reverseParam  = nullptr;
    // 재생 모드(원샷/게이트 등).
    std::atomic<float>* playModeParam = nullptr;
    // 최종 출력 게인.
    std::atomic<float>* outputGainParam = nullptr;
    // 엔진 선택(촙/신스/샘플러).
    std::atomic<float>* engineParam      = nullptr;
    // 신스 파형 종류.
    std::atomic<float>* synthWaveParam   = nullptr;
    // 신스 디튠.
    std::atomic<float>* synthDetuneParam = nullptr;
    // 신스 옥타브.
    std::atomic<float>* synthOctaveParam = nullptr;
    // 신스 유니즌(겹치는 보이스 수).
    std::atomic<float>* synthUnisonParam  = nullptr;
    // 유니즌 스프레드(퍼짐).
    std::atomic<float>* synthSpreadParam  = nullptr;
    // 서브 오실레이터 양.
    std::atomic<float>* synthSubParam     = nullptr;
    // 노이즈 양.
    std::atomic<float>* synthNoiseParam   = nullptr;
    // FM(주파수 변조) 양.
    std::atomic<float>* synthFMParam      = nullptr;
    // 비브라토 양.
    std::atomic<float>* synthVibratoParam = nullptr;
    // 코러스 양.
    std::atomic<float>* synthChorusParam  = nullptr;
    // LFO 속도.
    std::atomic<float>* synthLfoRateParam = nullptr;
    // LFO 적용량.
    std::atomic<float>* synthLfoAmtParam  = nullptr;
    // 매크로 "Hype"(전체적 화려함) 노브.
    std::atomic<float>* macroHypeParam  = nullptr;
    // 매크로 "Space"(공간감) 노브.
    std::atomic<float>* macroSpaceParam = nullptr;
    // 매크로 "Dirt"(거칠기) 노브.
    std::atomic<float>* macroDirtParam  = nullptr;

    // outputLevel: 미터용 현재 출력 레벨. { } 로 0.0 초기화. 오디오→UI 공유라 atomic.
    std::atomic<float> outputLevel { 0.0f };

    // Licensing (loaded once in the constructor; demo gate in processBlock).
    // licensed: 정품 여부 플래그(생성자에서 1회 로드, processBlock 의 데모 제한이 참조).
    std::atomic<bool> licensed { false };
    // demoClock: 데모 음소거 타이밍을 세는 샘플 카운터.
    int64_t demoClock = 0;

    // Detected key of the loaded sample (message thread writes, UI reads).
    // 감지된 조의 으뜸음(-1=모름). 메시지 스레드가 쓰고 UI 가 읽어 atomic.
    std::atomic<int>  detectedKeyRoot { -1 };
    // 감지된 조가 단조인지.
    std::atomic<bool> detectedKeyMinor { false };

    // Effective FX amounts after macro offsets (audio thread only).
    // 매크로 보정까지 반영한 '실효' FX 양들. 오디오 스레드에서만 쓰므로 atomic 불필요.
    float effDrive = 0.0f, effReverb = 0.0f, effDelay = 0.0f, effWidth = 1.0f;

    // 호스트가 실제로 돌리는 현재 샘플레이트(prepareToPlay 에서 설정).
    double currentSampleRate = 44100.0;
    // 로드된 샘플 원본의 샘플레이트(재생 속도 비율 계산에 씀).
    double loadedSampleRate  = 44100.0;

    // MIDI note that maps to the first slice (C3).
    // 첫 슬라이스에 대응하는 기준 MIDI 노트(48 = C3). constexpr = 컴파일 타임 상수.
    static constexpr int kRootNote = 48;

    // Lock-free queue of key/pad hits (message thread -> audio thread).
    // Generous size: a dropped note-off would leave a note stuck on.
    // [락프리 큐] UI(메시지)→오디오 스레드로 패드 입력을 넘기는 통로.
    //   왜 락프리? 오디오 스레드는 절대 락을 기다리면 안 되기 때문(대기=글리치).
    //   AbstractFifo 는 락 없이 인덱스만으로 안전하게 읽기/쓰기 구간을 나눠줍니다.
    //   크기를 넉넉히(256): 만약 노트오프가 유실되면 소리가 멈추지 않고 계속 나므로.
    enum PadEvent { padTap = 0, padOn = 1, padOff = 2 };
    // 읽기/쓰기 위치를 관리하는 FIFO 컨트롤러(용량 256).
    juce::AbstractFifo padFifo { 256 };
    // 실제 데이터 배열들(FIFO 는 인덱스만 관리, 데이터는 이 배열에 저장).
    // 슬라이스 번호 저장 큐.
    std::array<int, 256>   padQueue {};
    // 세기(velocity) 저장 큐.
    std::array<float, 256> padQueueVel {};
    // 이벤트 종류(탭/온/오프) 저장 큐.
    std::array<int, 256>   padQueueType {};

    // Which voice each held MIDI note started (-1 = none), for note-off routing.
    // 각 MIDI 노트(0~127)가 어느 voice 로 울렸는지 기억(-1=없음). 노트오프를 올바른
    //   voice 로 보내려면 이 매핑이 필요합니다.
    std::array<int, 128> noteToVoice {};

    // Same, for held on-screen / computer-keyboard pads (audio thread only).
    // 화면/컴퓨터 키보드 패드용 같은 매핑(오디오 스레드 전용).
    std::array<int, 128> padKeyToVoice {};

    // Which embedded demo vocal the Demo button loads next.
    // 데모 버튼을 누를 때마다 다음에 로드할 내장 보컬을 고르는 순번.
    int demoCycle = 0;

    // Engine-architecture half of an instrument (non-APVTS synth settings).
    // applyEnginePatch: 악기의 '엔진 구조' 절반(APVTS 에 없는 신스 세부 설정)을 적용.
    void applyEnginePatch (int instrumentIndex);
    // defaultInstrumentIndex: 부팅 시 기본 음색을 이름으로 찾아 인덱스 반환.
    static int defaultInstrumentIndex();   // boot patch, found by name
    // 현재 선택된 악기 번호.
    int  currentInstrument = 0;

    // "Drum Kit" instrument: every semitone is a different drum (C=kick,
    // D=snare, E=clap, F=hat...). Pieces are snapshotted on the message
    // thread when the kit is selected; note-ons swap the patch atomically
    // per hit on the audio thread (voices snapshot at start, so ringing
    // drums keep their own sound).
    // ["드럼 킷" 특수 악기] 반음마다 다른 드럼(C=킥, D=스네어, E=클랩, F=하이햇...).
    //   각 드럼의 설정(KitPiece)은 킷 선택 시 메시지 스레드에서 미리 만들어 두고,
    //   노트온마다 오디오 스레드가 해당 조각을 원자적으로 골라 씁니다. 이미 울리고 있는
    //   드럼은 자기 소리를 그대로 유지(보이스가 시작 시점에 설정을 복사해 두므로).
    // KitPiece: 드럼 하나의 신스 설정 묶음(구조체).
    struct KitPiece
    {
        // 정수형 설정: 유니즌 수, 파형, 재생 노트.
        int   unison = 1, wave = 2, playNote = 60;
        // 실수형 설정들: 스프레드/서브/노이즈/FM/필터/엔벨로프 등 드럼 음색 파라미터.
        float spread = 0, sub = 0, noise = 0, fm = 0, fmRatio = 2,
              vibHz = 0, vibCents = 0,
              fltHz = 20000, fltEnvOct = 0, fltEnvMs = 200, filterQ = 0.71f,
              drift = 0, velFlt = 1.0f, pitchEnvOct = 0, pitchEnvMs = 60,
              atk = 0, dec = 200, sus = 0, rel = 150;
    };
    // 12반음 각각의 드럼 조각을 담는 고정 배열.
    std::array<KitPiece, 12> kitPieces {};
    // 현재 드럼 킷 모드인지(오디오/메시지 공유라 atomic).
    std::atomic<bool> kitMode { false };
    // 킷 조각을 만드는 중인지(중복 빌드 방지 플래그).
    bool buildingKit = false;
    // buildKitPieces: 드럼 조각 12개를 미리 계산(메시지 스레드).
    void buildKitPieces();                              // message thread
    // kitNoteOn: 드럼 노트가 들어오면 해당 조각으로 발음(오디오 스레드).
    void kitNoteOn (int note, float velocity, bool tap); // audio thread

    // Module values applyEnginePatch resolved for the current instrument.
    // applyInstrument mirrors THESE into the knob params — reading them back
    // from synthEngine.patch() would race the audio thread, which rewrites
    // the param-driven patch fields every block.
    // [ModuleDefaults] applyEnginePatch 가 계산한 '모듈 기본값'.
    //   applyInstrument 는 이 값을 노브 파라미터로 옮겨 씁니다. 만약 synthEngine.patch()
    //   에서 되읽으려 하면 매 블록 그 필드를 덮어쓰는 오디오 스레드와 경쟁(race)이 생깁니다.
    struct ModuleDefaults
    {
        // 유니즌(겹침) 수.
        int   unison = 1;
        // 스프레드/서브/노이즈/FM/비브라토/코러스 기본값들.
        float spread = 0.5f, sub = 0.0f, noise = 0.0f,
              fm = 0.0f, vibCents = 0.0f, chorus = 0.0f;
    };
    // 현재 악기의 모듈 기본값 저장소.
    ModuleDefaults moduleDefaults;

    // Smoothed stereo width to avoid zipper noise when the knob moves.
    // 스테레오 폭을 부드럽게 보간. 노브를 확 돌려도 '지익(zipper)' 잡음이 안 나게
    //   목표값까지 여러 샘플에 걸쳐 스르륵 이동시킴.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed { 1.0f };

    // spec: JUCE DSP 모듈에 넘길 처리 사양(샘플레이트/블록크기/채널수) 저장소.
    juce::dsp::ProcessSpec spec {};

    // JUCE 매크로: 이 클래스를 복사 금지로 만들고, 메모리 누수를 자동 감지.
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessor)
};
