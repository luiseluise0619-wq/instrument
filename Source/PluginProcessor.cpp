// ============================================================================
//  PluginProcessor.cpp  —  프로세서(두뇌)의 실제 동작 구현
// ----------------------------------------------------------------------------
//  헤더(PluginProcessor.h)에서 "선언"한 함수들을 여기서 진짜로 구현합니다.
//  신호 체인: 입력/MIDI → SliceEngine(자르기) → VoicePool(동시 발음) →
//             PitchFormant(음정·성대) → FXChain(효과) → Limiter(안전) → 출력.
//  파일 맨 아래에 전체 신호 흐름·스레드 모델을 정리한 마무리 주석이 있습니다.
// ============================================================================

// 짝꿍 헤더: 이 파일이 구현하는 클래스 선언.
#include "PluginProcessor.h"
// 화면(에디터) 클래스: createEditor 에서 새 화면을 만들 때 필요.
#include "PluginEditor.h"
// 오디오 파일/메모리를 PCM 버퍼로 디코딩하는 헬퍼.
#include "AudioEngine/SampleLoader.h"
// 빌드시 임베드된 리소스(데모 보컬 wav 등)에 접근하는 자동생성 헤더.
#include "BinaryData.h"

//==============================================================================
// [생성자] 플러그인이 처음 만들어질 때 딱 한 번 실행.
//   ': 멤버(...)' 문법 = 멤버 초기화 리스트. 본문 { } 보다 먼저, 멤버를 '만들 때' 초기화.
VocalChopAudioProcessor::VocalChopAudioProcessor()
    // 부모 AudioProcessor 를 스테레오 출력 1개 구성으로 초기화.
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      // apvts(파라미터 트리)를 생성. createParameterLayout()이 모든 노브 정의를 만들어 넘김.
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
   // #if = 컴파일 타임 분기. 데모 게이트 빌드 옵션이 켜졌는지에 따라 코드가 갈림.
   #if SLYCE_DEMO_GATE
    // 데모 빌드: 디스크에 저장된 정품 활성화 정보를 읽어 licensed 에 반영.
    licensed.store (vcs::Licensing::loadActivation());
   #else
    // 정품 게이트가 빌드시 꺼진 경우: 처음부터 완전 개방(항상 정품 취급).
    licensed.store (true);   // demo gate disabled at build time: fully open
   #endif

    // [파라미터 포인터 캐싱] 아래 한 뭉치는 각 노브의 '값 저장소' 주소를 미리 구해 둡니다.
    //   getRawParameterValue 는 이름(문자열)으로 찾으므로 매 블록 부르면 느립니다.
    //   생성자에서 한 번만 찾아 atomic<float>* 로 들고 있으면, 오디오 스레드가
    //   .load() 로 빠르고 안전하게 값을 읽을 수 있습니다(실시간 오디오의 기본 습관).
    pitchParam     = apvts.getRawParameterValue ("pitch");
    formantParam   = apvts.getRawParameterValue ("formant");
    mixParam       = apvts.getRawParameterValue ("mix");
    widthParam     = apvts.getRawParameterValue ("width");
    grainSizeParam = apvts.getRawParameterValue ("grainSize");
    grainMixParam  = apvts.getRawParameterValue ("grainMix");
    driveParam     = apvts.getRawParameterValue ("drive");
    reverbParam    = apvts.getRawParameterValue ("reverb");
    delayParam     = apvts.getRawParameterValue ("delay");
    attackParam    = apvts.getRawParameterValue ("attack");
    decayParam     = apvts.getRawParameterValue ("decay");
    sustainParam   = apvts.getRawParameterValue ("sustain");
    releaseParam   = apvts.getRawParameterValue ("release");
    filterCutoffParam  = apvts.getRawParameterValue ("filterCutoff");
    filterResoParam    = apvts.getRawParameterValue ("filterReso");
    filterTypeParam    = apvts.getRawParameterValue ("filterType");
    delayFeedbackParam = apvts.getRawParameterValue ("delayFeedback");
    pingpongParam  = apvts.getRawParameterValue ("pingpong");
    reverseParam   = apvts.getRawParameterValue ("reverse");
    playModeParam  = apvts.getRawParameterValue ("playMode");
    outputGainParam  = apvts.getRawParameterValue ("outputGain");
    engineParam      = apvts.getRawParameterValue ("engine");
    synthWaveParam   = apvts.getRawParameterValue ("synthWave");
    synthDetuneParam = apvts.getRawParameterValue ("synthDetune");
    synthOctaveParam = apvts.getRawParameterValue ("synthOctave");
    synthUnisonParam  = apvts.getRawParameterValue ("synthUnison");
    synthSpreadParam  = apvts.getRawParameterValue ("synthSpread");
    synthSubParam     = apvts.getRawParameterValue ("synthSub");
    synthNoiseParam   = apvts.getRawParameterValue ("synthNoise");
    synthFMParam      = apvts.getRawParameterValue ("synthFM");
    synthVibratoParam = apvts.getRawParameterValue ("synthVibrato");
    synthChorusParam  = apvts.getRawParameterValue ("synthChorus");
    synthLfoRateParam = apvts.getRawParameterValue ("synthLfoRate");
    synthLfoAmtParam  = apvts.getRawParameterValue ("synthLfoAmt");
    macroHypeParam  = apvts.getRawParameterValue ("macroHype");
    macroSpaceParam = apvts.getRawParameterValue ("macroSpace");
    macroDirtParam  = apvts.getRawParameterValue ("macroDirt");

    // pitch/formant 두 파라미터는 값이 바뀔 때 즉시 반응해야 하므로 리스너로 등록.
    //   (parameterChanged 콜백이 불림.) 나머지 노브는 매 블록 값을 읽어 처리.
    apvts.addParameterListener ("pitch", this);
    apvts.addParameterListener ("formant", this);

    // Synth mode boots with a designed patch (Supersaw Lead) whenever the
    // user flips the engine over.
    // 신스로 전환하면 곧바로 좋은 소리가 나도록 기본 음색(Supersaw Lead)을 미리 세팅.
    applyEnginePatch (defaultInstrumentIndex());

    // First-run experience = the name promise: decode the embedded demo
    // vocal and slice it, so the very first key press CHOPS. No parameter
    // writes here (hosts dislike notifications mid-construction) — the
    // engine parameter's default is already Chop.
    // [첫 실행 경험] 내장 데모 보컬을 미리 디코딩·슬라이스 해두어, 사용자가 첫 건반을
    //   누르는 순간 바로 '촙'이 됩니다. 생성자 중에는 파라미터를 쓰지 않습니다(호스트가
    //   생성 도중 알림을 싫어함) — engine 기본값이 이미 Chop 이라 그럴 필요도 없음.
    // { } 로 지역 블록을 만들어 임시변수 sr/demo 의 수명을 이 안으로 한정.
    {
        // 데모를 디코딩할 목표 샘플레이트(현재 값).
        double sr = currentSampleRate;
        // auto = 타입 자동추론. decode 는 성공 시 shared_ptr<버퍼>를 반환(실패 시 빈 포인터).
        if (auto demo = SampleLoader::decode (BinaryData::vocal_chop_demo_wav,
                                              BinaryData::vocal_chop_demo_wavSize, sr))
        {
            // 디코딩 성공: 원본 샘플 포인터 교체(공유 소유권 이전).
            sampleBuffer     = demo;
            // 디코딩된 실제 샘플레이트 기억(재생 속도 계산용).
            loadedSampleRate = sr;
            // 새 샘플을 슬라이스/그래뉼러 등 각 엔진에 물려줌.
            reassignSampleToEngines();
            // 슬라이스 경계를 새로 계산.
            rescanSlices();
            // 조(key)를 분석해 코드 바에 반영.
            analyzeSampleKey();
        }
    }
}

// [소멸자] 플러그인이 사라질 때. 생성자에서 등록한 리스너를 반드시 해제(누수/댕글링 방지).
VocalChopAudioProcessor::~VocalChopAudioProcessor()
{
    apvts.removeParameterListener ("pitch", this);
    apvts.removeParameterListener ("formant", this);
}

//==============================================================================
// [파라미터 레이아웃 정의]
//   플러그인의 모든 노브/스위치를 여기서 '정의'합니다. 각 파라미터는
//   ID(코드에서 찾는 문자열), 표시이름, 값 범위, 기본값을 가집니다. 이 목록이
//   apvts 의 뼈대가 되고, 저장/복원·UI 바인딩·오토메이션이 전부 여기에 붙습니다.
juce::AudioProcessorValueTreeState::ParameterLayout
VocalChopAudioProcessor::createParameterLayout()
{
    // using = 긴 타입에 짧은 별명. NormalisableRange = (최소, 최대, 간격) 값 범위 표현.
    using Range = juce::NormalisableRange<float>;
    // params: 각 파라미터를 unique_ptr(단독 소유 스마트포인터)로 담는 벡터.
    //   unique_ptr 는 소유권이 하나뿐 — 마지막에 apvts 로 통째로 넘겨집니다.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 아래 push_back 들은 모두 같은 패턴: make_unique 로 파라미터를 만들어 목록에 추가.
    //   AudioParameterFloat(ID, 이름, 범위, 기본값). 여기부터는 실수형 노브들입니다.
    // 피치: -24 ~ +24 반음, 0.01 단위, 기본 0.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "pitch", "Pitch", Range (-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "formant", "Formant", Range (-12.0f, 12.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix", Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width", "Width", Range (0.0f, 2.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "grainSize", "Grain Size", Range (20.0f, 500.0f, 1.0f), 80.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "grainMix", "Grain Mix", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "drive", "Drive", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "reverb", "Reverb", Range (0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delay", "Delay", Range (0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack", Range (0.0f, 1000.0f, 0.1f), 5.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "decay", "Decay", Range (0.0f, 2000.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "sustain", "Sustain", Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release", Range (0.0f, 2000.0f, 0.1f), 20.0f));

    // Filter cutoff with a musical (logarithmic) response.
    // 필터 컷오프: 20Hz~20kHz. 사람 귀는 로그로 듣기 때문에 선형 노브면 위쪽이 뭉칩니다.
    juce::NormalisableRange<float> cutoffRange (20.0f, 20000.0f, 1.0f);
    // setSkewForCentre(1000): 노브 정중앙이 1kHz가 되도록 곡선을 휘어(skew) 음악적으로.
    cutoffRange.setSkewForCentre (1000.0f);
    // 위에서 만든 로그 범위를 컷오프 파라미터에 적용(기본 20kHz = 필터 활짝 열림).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "filterCutoff", "Filter Cutoff", cutoffRange, 20000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "filterReso", "Filter Reso", Range (0.1f, 8.0f, 0.01f), 0.707f));
    // AudioParameterChoice = 여러 선택지 중 하나(콤보박스). 필터 종류: 끔/로우/하이/밴드.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "filterType", "Filter Type",
        juce::StringArray { "Off", "Low Pass", "High Pass", "Band Pass" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayFeedback", "Delay FB", Range (0.0f, 0.95f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "pingpong", "Ping-Pong", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "reverse", "Reverse", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "playMode", "Play Mode", juce::StringArray { "Gate", "One-Shot" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputGain", "Output", Range (-24.0f, 6.0f, 0.1f), 0.0f));

    // Synth engine mode: play oscillators instead of sample slices.
    // 엔진 선택: 샘플 슬라이스(Chop) / 파형 합성(Synth) / SFZ 멀티샘플(Sampled).
    //   이 파라미터 값이 isSynthMode()/isSamplerMode() 판정의 기준이 됩니다.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "engine", "Engine", juce::StringArray { "Chop", "Synth", "Sampled" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "synthWave", "Synth Wave",
        juce::StringArray { "Saw", "Square", "Sine", "Triangle" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthDetune", "Detune", Range (0.0f, 50.0f, 0.1f), 7.0f));
    // AudioParameterInt = 정수 파라미터. 옥타브: -2 ~ +2, 기본 0.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "synthOctave", "Octave", -2, 2, 0));

    // Synth architecture modules, adjustable like a proper wavetable synth.
    // Defaults match the boot patch (Supersaw Lead).
    // 아래는 제대로 된 웨이브테이블 신스처럼 조절 가능한 신스 구조 모듈들.
    //   기본값은 부팅 음색(Supersaw Lead)에 맞춰져 있습니다.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "synthUnison", "Unison", 1, 7, 7));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthSpread", "Spread", Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthSub", "Sub Osc", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthNoise", "Noise", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthFM", "FM Amount", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthVibrato", "Vibrato", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthChorus", "Chorus", Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthLfoRate", "LFO Rate", Range (0.05f, 8.0f, 0.01f, 0.5f), 2.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthLfoAmt", "Motion", Range (0.0f, 1.0f, 0.001f), 0.0f));

    // Performance macros: one knob each for "the hook hits harder", "wetter
    // space" and "dirtier texture". They OFFSET the underlying values in the
    // audio thread without touching the parameters they shadow.
    // [연주용 매크로] "훅이 더 세게(HYPE)", "더 젖은 공간감(SPACE)", "더 거친 질감(DIRT)".
    //   실제로는 오디오 스레드에서 바탕 값에 '오프셋'만 더할 뿐, 그림자처럼 가리는
    //   원본 파라미터 자체는 건드리지 않습니다(그래서 원래 노브 위치가 유지됨).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "macroHype", "HYPE", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "macroSpace", "SPACE", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "macroDirt", "DIRT", Range (0.0f, 1.0f, 0.001f), 0.0f));

    // 벡터의 처음~끝을 apvts 가 요구하는 형태로 넘겨 ParameterLayout 완성.
    return { params.begin(), params.end() };
}

//==============================================================================
// [prepareToPlay] 재생 시작 직전 호스트가 호출. 여기서 모든 버퍼/엔진을 '미리' 준비.
//   ★핵심★: 메모리 할당 같은 무거운 준비는 전부 여기서. 오디오 콜백(processBlock)에서는
//   절대 하지 않습니다. 미리 안 잡아두면 실제 연주 중 할당이 일어나 소리가 끊깁니다.
void VocalChopAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 호스트가 알려준 실제 샘플레이트를 기억.
    currentSampleRate = sampleRate;

    // spec = 각 DSP 모듈에 넘길 처리 사양(샘플레이트/최대 블록크기/채널수)을 채움.
    spec.sampleRate       = sampleRate;
    // (juce::uint32) = 형변환. 최대 블록 크기를 부호없는 정수로.
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    // 이 플러그인은 스테레오(2채널) 기준으로 준비.
    spec.numChannels      = 2;

    // 아래 prepare 호출들 = 각 부품에게 "이 사양으로 버퍼를 미리 확보하라"고 지시.
    // 보이스 풀 준비.
    voicePool.prepare (spec);
    // 신스 엔진 준비.
    synthEngine.prepare (spec);
    // 샘플러 엔진 준비.
    samplerEngine.prepare (sampleRate, samplesPerBlock);
    // 피치/포먼트 준비. jmax(1, 채널수)로 최소 1채널 보장(0채널 방어).
    pitchFormant.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    // 그래뉼러 준비.
    granularEngine.prepare (spec);
    // FX 체인 준비.
    fxChain.prepare (spec);
    // 루퍼 준비.
    looper.prepare (sampleRate, samplesPerBlock);
    // 리미터 준비.
    limiter.prepare (sampleRate, samplesPerBlock);

    // 스테레오 폭 스무더 초기화: 20ms(0.02s)에 걸쳐 값이 부드럽게 이동하도록.
    widthSmoothed.reset (sampleRate, 0.02);
    // 현재값=목표값을 지금 노브 위치로 맞춤(? : 삼항연산자, 포인터 null 이면 1.0 기본).
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr ? widthParam->load() : 1.0f);

    // 노트→voice 매핑 배열을 전부 -1(비어있음)로 초기화. 재생 시작 시 깨끗한 상태로.
    noteToVoice.fill (-1);
    padKeyToVoice.fill (-1);

    // Total plugin latency: the pitch/formant engine's inherent latency plus
    // the limiter's look-ahead delay.
    // Report only the limiter's tiny look-ahead. The stretch engine is fully
    // BYPASSED at neutral pitch/formant, so a live player feels ~2 ms; when a
    // pitch knob is in use its latency is audible but not host-compensated
    // (re-reporting latency mid-play makes hosts glitch far worse).
    // [지연시간 보고] 호스트에 "이 플러그인은 N샘플 늦게 소리를 낸다"고 알려주면 호스트가
    //   타이밍을 보정합니다. 여기선 리미터의 작은 룩어헤드만 보고합니다. 피치/포먼트가
    //   중립이면 스트레치 엔진이 완전 바이패스되어 체감 ~2ms. 피치를 쓰면 지연이 생기지만
    //   연주 도중 지연값을 다시 보고하면 호스트가 오히려 심하게 글리치되므로 그냥 둡니다.
    setLatencySamples (limiter.getLatencySamples());

    // 준비가 끝났으니 현재 샘플을 각 엔진에 다시 물려줌.
    reassignSampleToEngines();
}

// [releaseResources] 재생이 멈출 때 호출. 울리던 모든 보이스를 정리.
void VocalChopAudioProcessor::releaseResources()
{
    voicePool.releaseAll();
}

// [isBusesLayoutSupported] 이 채널 구성을 지원하는지 호스트에 응답(모노/스테레오만 허용).
bool VocalChopAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 메인 출력 채널 세트를 꺼내와서,
    const auto out = layouts.getMainOutputChannelSet();
    // 모노 또는 스테레오면 지원(true), 그 외(예: 5.1)는 거부.
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

// [reassignSampleToEngines] 현재 로드된 샘플을 슬라이스/보이스풀에 넘겨줌.
//   shared_ptr 을 넘기므로 각 엔진도 같은 버퍼를 안전하게 공유(참조 카운트 증가).
void VocalChopAudioProcessor::reassignSampleToEngines()
{
    // 슬라이스 엔진에 샘플과 그 샘플레이트 지정.
    sliceEngine.setSample (sampleBuffer, loadedSampleRate);
    // 보이스 풀에도 재생 소스로 같은 샘플 지정.
    voicePool.setSource (sampleBuffer, loadedSampleRate);
}

// [analyzeSampleKey] 로드된 샘플의 음악적 '조(key)'를 자동 추정(메시지 스레드에서만).
//   무거운 FFT 분석이라 절대 오디오 스레드에서 하면 안 됩니다.
void VocalChopAudioProcessor::analyzeSampleKey()
{
    // Message thread. Chroma-based key estimate: average FFT magnitudes
    // folded onto 12 pitch classes, correlated against the Krumhansl-Kessler
    // major/minor profiles in all 12 rotations.
    // [원리] 크로마(chroma) 방식: FFT로 주파수 세기를 구해 12개 음이름(C~B)으로 접은 뒤,
    //   음악학의 Krumhansl-Kessler 장/단조 프로필과 12번 회전 비교해 가장 잘 맞는 조를 찾음.
    // 먼저 '모름(-1)'으로 초기화(분석 실패 시 이 값이 남음).
    detectedKeyRoot.store (-1);

    // 지금 샘플을 지역 shared_ptr 로 복사(분석 중 교체돼도 이 버퍼는 살아있게).
    auto sample = sampleBuffer;
    // 샘플이 없거나 너무 짧으면(8192샘플 미만) 분석 불가 — 그냥 종료.
    if (sample == nullptr || sample->getNumSamples() < 8192)
        return;

    // FFT 크기 = 2^12 = 4096. 1<<order 는 비트 시프트로 2의 거듭제곱 계산.
    constexpr int order = 12;
    const int fftSize = 1 << order;             // 4096
    // JUCE FFT 객체 생성(크기 order).
    juce::dsp::FFT fft (order);

    // fftData: FFT 입출력 버퍼(실수/허수 담아 크기 2배). window: 창함수(가장자리 부드럽게).
    std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
    std::vector<float> window ((size_t) fftSize);
    // 해닝(Hann) 창 계산: 프레임 양끝을 0으로 부드럽게 눌러 스펙트럼 누설을 줄임.
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                      * (float) i / (float) (fftSize - 1)));

    // chroma[12]: 12음이름 각각의 누적 세기. numCh: 채널수. total: 분석할 최대 샘플수.
    double chroma[12] = {};
    const int numCh   = sample->getNumChannels();
    // 앞부분 최대 15초만 분석(전체를 다 볼 필요 없음, 속도).
    const int total   = juce::jmin (sample->getNumSamples(),
                                    (int) (loadedSampleRate * 15.0));   // first 15 s
    const double sr   = loadedSampleRate;

    // 샘플을 4096샘플 프레임 단위로 이동하며 반복 분석.
    for (int start = 0; start + fftSize <= total; start += fftSize)
    {
        // 이 프레임을 모노로 합치고 창함수를 곱해 fftData 앞부분에 채움.
        for (int i = 0; i < fftSize; ++i)
        {
            float mono = 0.0f;
            // 모든 채널을 더해서,
            for (int ch = 0; ch < numCh; ++ch)
                mono += sample->getSample (ch, start + i);
            // 채널수로 나눠 평균(모노화)하고 창함수를 곱함.
            fftData[(size_t) i] = (mono / (float) numCh) * window[(size_t) i];
        }
        // 뒷부분(허수/패딩)을 0으로 채운 뒤,
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        // FFT 수행: 시간영역 → 주파수영역(각 bin 의 세기).
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // 각 주파수 bin 을 훑으며 세기를 해당 음이름에 누적.
        for (int bin = 2; bin < fftSize / 2; ++bin)
        {
            // bin 번호를 실제 주파수(Hz)로 변환.
            const double freq = (double) bin * sr / (double) fftSize;
            // 조성과 무관한 초저역/초고역은 건너뜀(60Hz~4500Hz만 사용).
            if (freq < 60.0 || freq > 4500.0)
                continue;
            // 주파수 → MIDI 음높이(로그). 440Hz = A4 = 69 기준.
            const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            // MIDI 를 12로 나눈 나머지 = 음이름(0~11). 음수 방지 위해 +12 후 다시 %12.
            const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
            // 해당 음이름에 이 bin 의 세기를 누적.
            chroma[pc] += (double) fftData[(size_t) bin];
        }
    }

    // Krumhansl-Kessler key profiles.
    // Krumhansl-Kessler 조성 프로필(음악심리학 실험으로 얻은 장/단조 특징 벡터).
    static const double majorP[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                       2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                       2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    // 지금까지 최고 점수/으뜸음/단조여부를 기록할 변수(초기값은 '아직 없음').
    double bestScore = -1.0;
    int bestRoot = -1;
    bool bestMinor = false;

    // 12개 후보 으뜸음(C~B) 각각에 대해 장조/단조 점수를 계산.
    for (int root = 0; root < 12; ++root)
    {
        double sMaj = 0.0, sMin = 0.0;
        // 크로마 벡터를 root 만큼 회전시켜 프로필과 내적(상관도) 계산.
        for (int i = 0; i < 12; ++i)
        {
            const double c = chroma[(root + i) % 12];
            // 장조 프로필과의 유사도 누적.
            sMaj += c * majorP[i];
            // 단조 프로필과의 유사도 누적.
            sMin += c * minorP[i];
        }
        // 지금까지보다 장조 점수가 높으면 최고로 갱신(장조로).
        if (sMaj > bestScore) { bestScore = sMaj; bestRoot = root; bestMinor = false; }
        // 단조 점수가 더 높으면 그것으로 갱신(단조로).
        if (sMin > bestScore) { bestScore = sMin; bestRoot = root; bestMinor = true; }
    }

    // 결과를 atomic 에 저장(UI 가 읽어 코드 바에 표시). 단조여부를 먼저, 그 다음 으뜸음.
    detectedKeyMinor.store (bestMinor);
    detectedKeyRoot.store (bestRoot);
}

// [rescanSlices] 샘플을 다시 분석해 슬라이스 경계를 새로 만듦(메시지 스레드).
void VocalChopAudioProcessor::rescanSlices()
{
    // 현재 모드(트랜지언트/그리드)로 슬라이스 재계산.
    sliceEngine.rebuildSlices();

    // Transient detection on pads / sustained material can yield almost no
    // slices, which reads as "the keyboard is broken". Fall back to an even
    // 16-part grid so a fresh load is always playable.
    // [안전망] 패드/지속음처럼 타격점이 없는 소재는 트랜지언트 검출로 슬라이스가 거의
    //   안 나올 수 있고, 그러면 "건반이 고장난 것"처럼 느껴집니다. 슬라이스가 4개 미만이면
    //   16등분 그리드로 자동 전환해 어떤 샘플이든 항상 연주 가능하게 합니다.
    if (sliceEngine.getMode() == SliceEngine::Transient
        && sliceEngine.getNumSlices() < 4)
    {
        // 그리드 모드로 바꾸고,
        sliceEngine.setMode (SliceEngine::Grid);
        // 16등분으로 설정한 뒤,
        sliceEngine.setGridDivision (16);
        // 다시 슬라이스 계산.
        sliceEngine.rebuildSlices();
    }
}

//==============================================================================
// ★★★ [processBlock] 오디오 스레드의 심장 ★★★
//   호스트가 초당 수백 번, 아주 짧은 마감시간 안에 이 함수를 호출합니다.
//   한 번 호출 = "이 블록(예: 512샘플)의 소리를 완성해서 buffer 에 채워라".
//   [절대 금지] 이 안에서 new/malloc(메모리 할당), 락 대기, 파일 접근, 로그 출력 금지.
//     늦으면 오디오 버퍼가 비어 소리가 '뚝뚝' 끊깁니다(글리치). 그래서 준비는 전부
//     prepareToPlay 에서 미리 했고, 여기선 미리 잡아둔 자원만 굴립니다.
//   buffer = 소리를 담을(그리고 결과를 돌려줄) 오디오 버퍼. midi = 이번 블록의 건반 이벤트.
void VocalChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    // 디노멀(0에 극도로 가까운 수) 연산은 CPU를 급격히 느리게 합니다. 이 스코프 동안
    //   디노멀을 0으로 처리하도록 CPU 플래그를 켜 성능을 보호(소멸 시 원복).
    juce::ScopedNoDenormals noDenormals;

    // 입력 채널보다 출력 채널이 많으면(예: 모노 입력→스테레오 출력) 남는 채널을 0으로.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    // 이 플러그인은 악기이므로 입력을 안 쓰고, 버퍼를 통째로 비워 무음에서 시작.
    buffer.clear();

    // 이번 블록의 샘플 개수(자주 쓰므로 지역변수에 캐시).
    const int numSamples = buffer.getNumSamples();

    // Push per-block voice settings so new triggers use the current ADSR / mode.
    // [블록마다 최신 노브값을 각 엔진에 밀어넣기] 새로 눌리는 음이 현재 ADSR/모드를 쓰도록.
    //   .load() 로 atomic 파라미터 값을 원자적으로 읽음 — 오디오 스레드의 안전한 읽기.
    voicePool.setEnvelope (attackParam->load(), decayParam->load(),
                           sustainParam->load(), releaseParam->load());
    // 역재생 여부(0.5 이상이면 켬). float 파라미터를 bool 로 해석하는 관용구.
    voicePool.setReverse (reverseParam->load() >= 0.5f);
    // 재생 모드(게이트/원샷).
    voicePool.setPlayMode (playModeParam->load() >= 0.5f);

    // 신스 엔진에도 같은 ADSR 을 반영.
    synthEngine.setEnvelope (attackParam->load(), decayParam->load(),
                             sustainParam->load(), releaseParam->load());
    // 파형 설정: 드럼킷 모드면 사인 고정, 아니면 노브가 고른 파형. (int) 로 열거형 캐스팅.
    synthEngine.setWave (kitMode.load() ? (int) SynthEngine::Sine
                                        : (int) synthWaveParam->load());
    // 디튠(센트 단위)과 옥타브 반영.
    synthEngine.setDetuneCents (synthDetuneParam->load());
    synthEngine.setOctave ((int) synthOctaveParam->load());

    // Adjustable synth modules: the knobs drive the patch every block, so
    // tweaking Unison / Sub / FM / Chorus reshapes the sound live.
    // [신스 패치 실시간 반영] 노브가 매 블록 patch 를 갱신하므로, 유니즌/서브/FM/코러스를
    //   돌리면 소리가 즉시 바뀝니다. { } 로 지역 참조 pt 의 수명을 이 블록에 한정.
    {
        // patch() 는 내부 패치 구조체의 참조(&)를 돌려줌 — 복사 없이 직접 수정.
        auto& pt = synthEngine.patch();
        // 유니즌 수(1~7로 제한). jlimit(min,max,x) = x 를 범위 안으로 자름.
        pt.unison        = juce::jlimit (1, 7, (int) synthUnisonParam->load());
        // 스테레오 스프레드.
        pt.stereoSpread  = synthSpreadParam->load();
        // 서브 오실레이터 레벨.
        pt.subLevel      = synthSubParam->load();
        // 노이즈 레벨.
        pt.noiseLevel    = synthNoiseParam->load();
        // FM 양.
        pt.fmAmount      = synthFMParam->load();
        // 비브라토 깊이(0~1 노브를 최대 30센트로 변환).
        pt.vibDepthCents = synthVibratoParam->load() * 30.0f;
        // 코러스 믹스.
        pt.chorusMix     = synthChorusParam->load();
        // LFO 속도(Hz).
        pt.lfoRateHz     = synthLfoRateParam->load();
        // LFO 깊이(0~1 노브를 최대 2옥타브로 변환).
        pt.lfoDepthOct   = synthLfoAmtParam->load() * 2.0f;

        // Performance macros offset the modules they shadow (never the
        // parameters themselves, so host automation stays untouched).
        // 매크로는 그림자처럼 모듈 값에 '오프셋'만 더함(원본 파라미터는 안 건드려
        //   호스트 오토메이션이 그대로 유지됨).
        const float hype = macroHypeParam->load();
        const float dirt = macroDirtParam->load();
        // HYPE 가 켜지면 스프레드와 LFO 깊이를 더 키움(상한을 넘지 않게 jmin).
        if (hype > 0.0f)
        {
            pt.stereoSpread = juce::jmin (1.0f, synthSpreadParam->load() + 0.5f * hype);
            pt.lfoDepthOct  = juce::jmin (2.0f, pt.lfoDepthOct.load() + 0.5f * hype);
        }
        // DIRT 가 켜지면 노이즈를 더 섞음.
        if (dirt > 0.0f)
            pt.noiseLevel = juce::jmin (1.0f, synthNoiseParam->load() + 0.30f * dirt);
    }

    // Effective FX amounts = knob + macro offsets (clamped in the FX).
    // [실효 FX 양 계산] = 노브값 + 매크로 오프셋. 결과를 effXxx 멤버에 저장(오디오 스레드 전용).
    {
        const float hype  = macroHypeParam->load();
        const float space = macroSpaceParam->load();
        const float dirt  = macroDirtParam->load();
        // 드라이브 = 노브 + HYPE·DIRT 기여, 0~1로 클램프.
        effDrive  = juce::jlimit (0.0f, 1.0f, driveParam->load()  + 0.35f * hype + 0.40f * dirt);
        // 리버브 = 노브 + SPACE 기여.
        effReverb = juce::jlimit (0.0f, 1.0f, reverbParam->load() + 0.50f * space);
        // 딜레이 = 노브 + SPACE 기여.
        effDelay  = juce::jlimit (0.0f, 1.0f, delayParam->load()  + 0.35f * space);
        // 스테레오 폭 = 노브 + SPACE 기여(0~2 범위).
        effWidth  = juce::jlimit (0.0f, 2.0f, widthParam->load()  + 0.50f * space);
    }

    // ── 여기부터가 실제 신호 흐름(순서가 곧 신호 체인) ──
    // 1) MIDI + pad-queue → slice / synth triggers.
    // 1) 이번 블록의 MIDI 와 UI 패드 큐를 해석해 슬라이스/신스 발음을 시작.
    handleMidi (midi, numSamples);
    // 락프리 큐에 쌓인 화면 패드 이벤트를 오디오 스레드에서 꺼내 처리.
    drainPadQueue();

    // 2) Render both sources (the unused engine is simply silent, so
    //    switching modes mid-note never clicks).
    // 2) 소스 렌더링. 안 쓰는 엔진은 그냥 무음을 내므로, 연주 중 모드를 바꿔도 '틱' 없이 매끄러움.
    // 슬라이스(촙) 소리를 buffer 에 더함.
    voicePool.renderNextBlock (buffer, numSamples);
    // 신스 소리를 더함.
    synthEngine.render (buffer, numSamples);
    // 샘플러(SFZ) 소리를 더함.
    samplerEngine.render (buffer, numSamples);

    // 3) Pitch / formant transformation.
    // 3) 피치/포먼트 변형. 음정과 성대 특성을 따로 조절(중립이면 내부에서 바이패스).
    pitchFormant.process (buffer,
                          pitchParam->load(),
                          formantParam->load(),
                          mixParam->load());

    // 4) Granular texture (bypassed unless its mix is raised).
    // 4) 그래뉼러 질감. grainMix 를 올리기 전엔 사실상 바이패스.
    granularEngine.setGrainSize (grainSizeParam->load());
    granularEngine.setMix (grainMixParam->load());
    granularEngine.process (buffer);

    // 5) Master FX chain.
    // 5) 마스터 FX 체인(드라이브/리버브/딜레이 등) 적용.
    applyMasterFXChain (buffer);

    // 6) Stereo width.
    // 6) 스테레오 폭 조절.
    applyStereoWidth (buffer);

    // 7) Output gain trim (before the limiter so it protects the boosted signal).
    // 7) 출력 게인. dB 를 배수(gain)로 변환해 적용. 리미터 앞에 둬서 키운 신호를 리미터가 보호.
    const float gain = juce::Decibels::decibelsToGain (outputGainParam->load());
    buffer.applyGain (gain);

    // 8) Loop station: records the performance, then adds the stacked loop
    //    (the limiter after us protects the sum).
    // 8) 루프 스테이션: 연주를 녹음하고 쌓인 루프를 더함(합쳐진 소리는 뒤 리미터가 보호).
    looper.process (buffer, numSamples);

    // 9) Brick-wall limiter.
    // 9) 브릭월 리미터: 0dB 를 넘지 않게 마지막으로 눌러 클리핑(찌그러짐)을 방지.
    limiter.process (buffer);

    // 10) Demo gate: without a license the output mutes for 2 s every
    //     minute (short fades at the window edges so there is no click).
    // 10) 데모 제한: 미인증이면 매 1분마다 2초간 음소거(양끝을 짧게 페이드해 '틱' 방지).
    //   memory_order_relaxed = 순서 보장은 필요 없고 원자성만 필요할 때 쓰는 가벼운 읽기.
    if (! licensed.load (std::memory_order_relaxed))
    {
        // 1분과 2초를 각각 '샘플 개수'로 환산.
        const auto period  = (int64_t) (60.0 * currentSampleRate);
        const auto muteLen = (int64_t) ( 2.0 * currentSampleRate);
        // 이번 블록의 샘플을 하나씩 보며,
        for (int n = 0; n < numSamples; ++n)
        {
            // 전체 주기 안에서 현재 위치(위상). demoClock 은 지금까지 흐른 샘플 누적.
            const auto ph = (demoClock + n) % period;
            // 주기의 마지막 2초 구간이면 음소거(가장자리 페이드 적용).
            if (ph >= period - muteLen)
            {
                // 음소거 구간에 들어온 정도와 남은 정도를 계산.
                const auto into   = ph - (period - muteLen);
                const auto remain = muteLen - into;
                float gain = 0.0f;
                // 처음 256샘플은 1→0 으로 페이드아웃,
                if (into < 256)        gain = 1.0f - (float) into / 256.0f;
                // 마지막 256샘플은 0→1 로 페이드인. 그 사이는 gain=0(완전 무음).
                else if (remain < 256) gain = 1.0f - (float) remain / 256.0f;
                // 모든 채널의 이 샘플에 gain 을 곱함(getWritePointer = 채널 데이터 배열 주소).
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[n] *= gain;
            }
        }
        // 다음 블록을 위해 데모 시계를 이번 블록 샘플수만큼 전진.
        demoClock += numSamples;
    }

    // Publish the output level as a PEAK LATCH: keep the maximum until the
    // meter consumes it (exchange-to-zero), so no transient between two UI
    // frames is ever missed and the bar reacts on the very next frame.
    // [출력 레벨 발행 — 피크 래치] 미터가 값을 가져갈 때까지 '최댓값'을 유지합니다.
    //   UI 프레임 사이에 스쳐간 순간 피크도 놓치지 않도록, 기존값보다 클 때만 갱신.
    {
        // 이번 블록의 최대 진폭(0번 채널부터 numSamples 구간).
        const float mag = buffer.getMagnitude (0, numSamples);
        // 현재 저장된 레벨을 읽고,
        float prev = outputLevel.load (std::memory_order_relaxed);
        // prev < mag 인 동안, 락 없이 원자적으로 outputLevel 을 mag 로 교체 시도(CAS 루프).
        //   compare_exchange_weak 은 다른 스레드가 끼어들어 실패하면 prev 를 갱신해 재시도.
        //   이것이 락프리(lock-free) 갱신 — 오디오 스레드가 절대 대기하지 않게 하는 방법.
        while (prev < mag
               && ! outputLevel.compare_exchange_weak (prev, mag,
                                                       std::memory_order_relaxed)) {}
    }
}

// [handleMidi] 이번 블록의 MIDI 이벤트(노트온/오프 등)를 해석해 각 엔진에 연결.
//   (오디오 스레드에서 실행 — 여기서도 할당/락 금지.)
void VocalChopAudioProcessor::handleMidi (const juce::MidiBuffer& midi, int /*numSamples*/)
{
    // MIDI 버퍼의 이벤트를 하나씩 순회(meta = 각 이벤트의 메타정보).
    for (const auto meta : midi)
    {
        // Note/CC messages only — building a MidiMessage from a large SysEx
        // event would heap-allocate on the audio thread.
        // 3바이트 이하(노트/CC)만 처리. 큰 SysEx 는 MidiMessage 생성 시 힙 할당이
        //   일어나 오디오 스레드 규칙을 위반하므로 건너뜀.
        if (meta.numBytes > 3)
            continue;

        // 원시 바이트에서 MidiMessage 객체를 얻음.
        const auto msg = meta.getMessage();

        // 이 이벤트의 노트 번호와, 현재 신스 모드인지.
        const int  note  = msg.getNoteNumber();
        const bool synth = isSynthMode();

        // 노트온(누름): velocity 0 보다 커야 진짜 '누름'.
        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            // 샘플러 모드: 벨로시티를 0~1로 정규화(127로 나눔)해 샘플러에 노트온.
            if (isSamplerMode())
            {
                samplerEngine.noteOn (note, msg.getVelocity() / 127.0f);
            }
            // 신스 모드:
            else if (synth)
            {
                // 드럼킷 모드면 반음별 드럼을 발음,
                if (kitMode.load (std::memory_order_relaxed))
                    kitNoteOn (note, msg.getVelocity() / 127.0f, false);
                // 아니면 일반 신스 노트.
                else
                    synthEngine.noteOn (note, msg.getVelocity() / 127.0f);
            }
            // 촙(슬라이스) 모드:
            else
            {
                // Retriggering a still-held note releases its old voice so
                // the previous hit doesn't ring on as an orphan.
                // 아직 눌린 채인 같은 노트를 또 치면, 옛 voice 를 먼저 릴리즈해 유령처럼
                //   남아 울리지 않게 함. isPositiveAndBelow(note,128) = 0~127 범위 안전검사.
                if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
                    voicePool.releaseVoice (noteToVoice[(size_t) note]);

                // 노트→슬라이스 인덱스로 바꿔(기준음 C3 기준) 발음, 사용된 voice 번호를 받음.
                const int voice = triggerSliceIndex (note - kRootNote,
                                                     msg.getVelocity() / 127.0f);
                // 나중에 노트오프 때 이 voice 를 찾도록 매핑에 기록.
                if (juce::isPositiveAndBelow (note, 128))
                    noteToVoice[(size_t) note] = voice;
            }
        }
        // 노트오프(뗌): 진짜 오프 또는 velocity 0 인 노트온도 오프로 취급.
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            // Release EVERY engine regardless of the current mode: the note
            // may have started before an engine switch, and each call is a
            // safe no-op when that engine holds nothing for this note.
            // 현재 모드와 상관없이 모든 엔진에 노트오프. 엔진을 바꾸기 전에 시작된 음일 수
            //   있기 때문. 해당 노트를 안 들고 있는 엔진에겐 아무 일도 안 하는 안전한 호출.
            synthEngine.noteOff (note);
            samplerEngine.noteOff (note);

            // 촙 매핑에 이 노트의 voice 가 있으면 릴리즈하고 매핑을 비움(-1).
            if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
            {
                voicePool.releaseVoice (noteToVoice[(size_t) note]);
                noteToVoice[(size_t) note] = -1;
            }
        }
        // All Notes Off / All Sound Off: 호스트의 '전부 끄기' 명령.
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            // All Sound Off is an emergency stop: it must silence even
            // one-shot voices, which ignore ordinary releases by design.
            // All Sound Off 는 비상정지: 원샷 voice(보통 릴리즈를 무시)까지 즉시 침묵시켜야 함.
            if (msg.isAllSoundOff())
                voicePool.stopAll();
            // All Notes Off 는 일반 릴리즈(엔벨로프 릴리즈로 자연스럽게 꺼짐).
            else
                voicePool.releaseAll();

            // 신스/샘플러도 전부 릴리즈하고, 모든 노트 매핑을 초기화.
            synthEngine.releaseAll();
            samplerEngine.releaseAll();
            noteToVoice.fill (-1);
            padKeyToVoice.fill (-1);
        }
    }
}

// [clearVoiceMapping] 특정 voice 슬롯을 가리키는 오래된 매핑을 모두 지움(오디오 스레드).
void VocalChopAudioProcessor::clearVoiceMapping (int voiceIndex)
{
    // Audio thread. A voice slot that self-finished (or was stolen) may still
    // be referenced by another held note's mapping; releasing that note later
    // would then cut whichever NEW note reused the slot. Scrub stale entries
    // whenever a slot is (re)assigned.
    // [왜 필요?] 스스로 끝났거나 뺏긴(voice stealing) 슬롯을 다른 눌린 노트가 아직
    //   가리키고 있을 수 있습니다. 그 노트를 나중에 떼면, 그 슬롯을 재사용한 '새 음'이
    //   엉뚱하게 끊깁니다. 그래서 슬롯이 (재)배정될 때마다 옛 매핑을 청소합니다.
    // 유효하지 않은 인덱스면 할 일 없음.
    if (voiceIndex < 0)
        return;

    // 이 voice 를 가리키던 MIDI 노트 매핑을 전부 -1로.
    for (auto& m : noteToVoice)
        if (m == voiceIndex)
            m = -1;
    // 화면/키보드 패드 매핑도 동일하게 청소.
    for (auto& m : padKeyToVoice)
        if (m == voiceIndex)
            m = -1;
}

// [triggerSliceIndex] 슬라이스 하나를 실제로 발음하고 사용한 voice 번호를 반환(오디오 스레드).
int VocalChopAudioProcessor::triggerSliceIndex (int sliceIndex, float velocity)
{
    // Audio thread. Keys past the last slice WRAP instead of going silent —
    // a demo with 8 slices must still sound on la/si/do (A, B, high C).
    // 마지막 슬라이스를 넘어가는 건반은 무음이 아니라 처음으로 '돌아옵니다(wrap)'.
    //   8슬라이스 데모도 라/시/도에서 계속 소리가 나도록. %가 음수를 낼 수 있어 +numSlices 후 재%.
    const int numSlices = sliceEngine.getNumSlices();
    if (numSlices > 0)
        sliceIndex = ((sliceIndex % numSlices) + numSlices) % numSlices;

    // tryGetSlice() safely no-ops if a re-slice is in progress.
    // 재슬라이싱 중이면 tryGetSlice 가 안전하게 실패(false) — 락 없이 경합 회피.
    SlicePoint slice;
    if (sliceEngine.tryGetSlice (sliceIndex, slice))
    {
        // 슬라이스의 시작/길이/세기로 voice 를 하나 잡아 재생 시작.
        const int voice = voicePool.triggerVoice (slice.startSample,
                                                  slice.lengthSamples, velocity);
        // 이 슬롯을 가리키던 옛 매핑 청소(위 clearVoiceMapping 설명 참고).
        clearVoiceMapping (voice);
        return voice;
    }
    // 슬라이스를 못 얻으면 -1(발음 안 함).
    return -1;
}

// [queuePadEvent] UI(메시지 스레드)의 패드 입력을 락프리 큐에 밀어넣음.
void VocalChopAudioProcessor::queuePadEvent (int sliceIndex, float velocity, int type)
{
    // Message thread (key click / computer keyboard). Hand the event to the
    // audio thread lock-free.
    // 메시지 스레드(마우스 클릭/컴퓨터 키보드)에서 오디오 스레드로 이벤트를 락 없이 전달.
    // prepareToWrite: FIFO 에 1칸 쓸 자리를 예약. 자리가 배열 끝을 넘으면 두 구간으로 나뉨.
    int start1, size1, start2, size2;
    padFifo.prepareToWrite (1, start1, size1, start2, size2);
    // 쓸 자리가 있으면(큐가 안 찼으면),
    if (size1 > 0)
    {
        // 예약된 인덱스에 슬라이스/세기/종류를 기록.
        padQueue[(size_t) start1]     = sliceIndex;
        padQueueVel[(size_t) start1]  = juce::jlimit (0.0f, 1.0f, velocity);
        padQueueType[(size_t) start1] = type;
        // 쓰기 완료를 알려 읽기 쪽이 이 데이터를 볼 수 있게 함(락프리 발행).
        padFifo.finishedWrite (1);
    }
}

// [triggerSlicePad] 한 번 '탁' 치는 탭 이벤트를 큐에 넣음(원샷처럼 자연히 울리다 사라짐).
void VocalChopAudioProcessor::triggerSlicePad (int sliceIndex, float velocity)
{
    queuePadEvent (sliceIndex, velocity, padTap);
}

// [pressSlicePad] 패드를 '누름' 이벤트를 큐에 넣음(뗄 때까지 유지되는 게이트).
void VocalChopAudioProcessor::pressSlicePad (int sliceIndex, float velocity)
{
    queuePadEvent (sliceIndex, velocity, padOn);
}

// [releaseSlicePad] 패드에서 '뗌' 이벤트를 큐에 넣음.
void VocalChopAudioProcessor::releaseSlicePad (int sliceIndex)
{
    queuePadEvent (sliceIndex, 0.0f, padOff);
}

// [drainPadQueue] 오디오 스레드에서 큐에 쌓인 패드 이벤트를 전부 꺼내 실제 발음/릴리즈.
void VocalChopAudioProcessor::drainPadQueue()
{
    // prepareToRead: 지금 읽을 수 있는 개수만큼 읽기 구간을 확보(역시 두 구간으로 나뉠 수 있음).
    int start1, size1, start2, size2;
    padFifo.prepareToRead (padFifo.getNumReady(), start1, size1, start2, size2);

    // 현재 신스 모드인지 미리 판정.
    const bool synth = isSynthMode();

    // fire = 이벤트 하나를 처리하는 람다(익명 함수). [this, synth] = 멤버와 synth 를 캡처.
    //   람다로 묶어 두 읽기 구간에서 같은 처리를 반복 호출.
    auto fire = [this, synth] (int idx, float vel, int type)
    {
        // 패드 인덱스를 MIDI 와 동일한 노트 번호로 변환(기준음 kRootNote).
        const int note = kRootNote + idx;   // same key mapping as MIDI

        // 뗌(off) 이벤트 처리:
        if (type == padOff)
        {
            // Release both engines: the pad may have been pressed before an
            // engine switch (each call safely no-ops when not applicable).
            // 두 엔진 모두 릴리즈(엔진 전환 전에 눌렸을 수 있어서). 해당 없는 엔진엔 무해.
            synthEngine.noteOff (note);
            samplerEngine.noteOff (note);

            // 촙 패드 매핑에 voice 가 있으면 릴리즈하고 -1로 비움.
            if (juce::isPositiveAndBelow (idx, 128) && padKeyToVoice[(size_t) idx] >= 0)
            {
                voicePool.releaseVoice (padKeyToVoice[(size_t) idx]);
                padKeyToVoice[(size_t) idx] = -1;
            }
            // off 처리 끝, 람다 종료.
            return;
        }

        // 여기부터는 누름/탭(on/tap) 처리.
        // 샘플러 모드: 탭이든 누름이든 노트온(자연히 울리다 사라짐).
        if (isSamplerMode())
        {
            samplerEngine.noteOn (note, vel);   // taps ring out naturally
        }
        // 신스 모드:
        else if (synth)
        {
            // 드럼킷이면 드럼 발음(padOn 이 아니면 탭 취급).
            if (kitMode.load (std::memory_order_relaxed))
                kitNoteOn (note, vel, type != padOn);
            // 누름(padOn)은 지속 노트온,
            else if (type == padOn)
                synthEngine.noteOn (note, vel);
            // 탭은 짧게 쳤다 놓는 tapNote.
            else
                synthEngine.tapNote (note, vel);
        }
        // 촙 모드:
        else
        {
            // 슬라이스를 발음하고,
            const int voice = triggerSliceIndex (idx, vel);
            // 누름(게이트)일 때만 나중에 뗄 수 있도록 매핑 기록(탭은 스스로 끝남).
            if (type == padOn && juce::isPositiveAndBelow (idx, 128))
                padKeyToVoice[(size_t) idx] = voice;
        }
    };

    // 첫 번째 읽기 구간의 이벤트들을 순서대로 처리.
    for (int i = 0; i < size1; ++i)
        fire (padQueue[(size_t) (start1 + i)], padQueueVel[(size_t) (start1 + i)],
              padQueueType[(size_t) (start1 + i)]);
    // (링버퍼가 끝을 넘어 감싼 경우) 두 번째 구간도 처리.
    for (int i = 0; i < size2; ++i)
        fire (padQueue[(size_t) (start2 + i)], padQueueVel[(size_t) (start2 + i)],
              padQueueType[(size_t) (start2 + i)]);

    // 읽기 완료를 알려 그 칸들을 다시 쓸 수 있게 반납.
    padFifo.finishedRead (size1 + size2);
}

// [applyMasterFXChain] 마스터 버스에 필터→디스토션→리버브→딜레이 순으로 FX 적용.
void VocalChopAudioProcessor::applyMasterFXChain (juce::AudioBuffer<float>& buffer)
{
    // 필터: 컷오프/레조넌스/종류를 넘겨 처리.
    fxChain.filter.process     (buffer, filterCutoffParam->load(),
                                filterResoParam->load(),
                                (int) filterTypeParam->load());
    // 디스토션(드라이브): 매크로까지 반영한 effDrive 사용.
    fxChain.distortion.process (buffer, effDrive);
    // 리버브: effReverb 양만큼.
    fxChain.reverb.process     (buffer, effReverb);

    // 딜레이 설정 후 처리: 피드백, 핑퐁 여부, 그리고 effDelay 양.
    fxChain.delay.setFeedback (delayFeedbackParam->load());
    fxChain.delay.setPingpong (pingpongParam->load() >= 0.5f);
    fxChain.delay.process      (buffer, effDelay);
}

// [applyStereoWidth] 미드/사이드(M/S) 방식으로 스테레오 폭을 조절.
void VocalChopAudioProcessor::applyStereoWidth (juce::AudioBuffer<float>& buffer)
{
    // 목표 폭을 스무더에 설정(급변시 지퍼 잡음 방지, 샘플마다 스르륵 이동).
    widthSmoothed.setTargetValue (effWidth);

    // 모노(채널<2)면 M/S 를 못 하므로, 스무더만 진행시키고 종료.
    if (buffer.getNumChannels() < 2)
    {
        widthSmoothed.skip (buffer.getNumSamples());
        return;
    }

    // 좌/우 채널 데이터 배열의 주소(포인터)를 얻음.
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // 각 샘플마다 M/S 변환으로 폭 조절.
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // 이번 샘플의 부드럽게 보간된 폭 값.
        const float width = widthSmoothed.getNextValue();
        // 미드(공통 성분) = (L+R)/2.
        const float mid  = (L[i] + R[i]) * 0.5f;
        // 사이드(차이 성분) = (L-R)/2 에 폭을 곱함. width>1 이면 넓어지고 <1 이면 좁아짐.
        const float side = (L[i] - R[i]) * 0.5f * width;
        // 다시 좌우로 합성: L=mid+side, R=mid-side.
        L[i] = mid + side;
        R[i] = mid - side;
    }
}

//==============================================================================
// [getLoadedSample] 현재 로드된 원본 샘플을 공유 포인터로 반환(UI 파형 표시 등).
std::shared_ptr<juce::AudioBuffer<float>> VocalChopAudioProcessor::getLoadedSample() const
{
    return sampleBuffer;
}

// [loadSampleFromFile] 디스크 오디오 파일을 로드(메시지 스레드, 무거운 디코딩).
bool VocalChopAudioProcessor::loadSampleFromFile (const juce::File& file,
                                                  bool switchEngineToChop)
{
    // 목표 샘플레이트로 파일을 디코딩. auto buffer = shared_ptr<버퍼>.
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (file, sr);
    // 실패(빈 포인터)면 false.
    if (buffer == nullptr)
        return false;

    // 성공: 원본 포인터 교체 + 샘플레이트/파일경로 기억.
    //   기존 sampleBuffer 는 shared_ptr 참조 카운트로, 오디오 스레드가 다 쓸 때까지 살아있음.
    sampleBuffer     = buffer;
    loadedSampleRate = sr;
    loadedSampleFile = file;
    // 각 엔진에 새 샘플을 물리고, 슬라이스/조 재분석.
    reassignSampleToEngines();
    rescanSlices();
    analyzeSampleKey();

    // Loading a sample means the user wants to chop it - switch engines so
    // the keyboard immediately plays slices (state restore passes false).
    // 샘플 로드 = 촙하고 싶다는 뜻 → 엔진을 촙(0)으로 전환해 즉시 슬라이스 연주.
    //   (세션 복원 시엔 false 로 넘겨 모드를 강제로 바꾸지 않음.)
    if (switchEngineToChop)
        if (auto* p = apvts.getParameter ("engine"))
            p->setValueNotifyingHost (0.0f);

    return true;
}

// [loadDemoSample] Demo 버튼을 누를 때마다 내장 보컬을 순환하며 로드.
bool VocalChopAudioProcessor::loadDemoSample()
{
    // Built-in vocals; every Demo press cycles to the next one. The last is
    // a human-beatbox loop: sliced, it turns keys into mouth drums.
    // 내장 보컬 목록. 마지막은 사람 비트박스 루프 — 슬라이스하면 건반이 입드럼이 됩니다.
    // Embedded: 데이터 포인터와 크기를 담는 작은 구조체(지역 정의).
    struct Embedded { const void* data; int size; };
    // static const 배열: 프로그램 수명 동안 한 번만 만들어지는 상수 데모 목록.
    static const Embedded demos[] = {
        { BinaryData::vocal_chop_demo_wav, BinaryData::vocal_chop_demo_wavSize },
        { BinaryData::vox_air_wav,         BinaryData::vox_air_wavSize },
        { BinaryData::vox_rage_wav,        BinaryData::vox_rage_wavSize },
        { BinaryData::vox_beatbox_wav,     BinaryData::vox_beatbox_wavSize },
    };
    // 배열 전체 크기 / 원소 하나 크기 = 원소 개수(컴파일 타임 상수).
    constexpr int numDemos = (int) (sizeof (demos) / sizeof (demos[0]));

    // 현재 순번(% 로 배열 범위 안으로)에 해당하는 데모를 참조로 얻음.
    const auto& d = demos[demoCycle % numDemos];
    // 다음 번을 위해 순번 증가.
    ++demoCycle;
    // 메모리에서 로드하는 공통 함수로 위임.
    return loadSampleFromMemory (d.data, d.size);
}

// [loadSampleFromMemory] 메모리 바이트(내장 리소스 등)에서 샘플을 로드.
bool VocalChopAudioProcessor::loadSampleFromMemory (const void* data, int sizeBytes)
{
    // 메모리 버퍼를 디코딩.
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (data, sizeBytes, sr);
    // 실패면 false.
    if (buffer == nullptr)
        return false;

    // 성공: 포인터/샘플레이트 교체 후 엔진 재배치·재슬라이스·조 분석.
    sampleBuffer     = buffer;
    loadedSampleRate = sr;
    reassignSampleToEngines();
    rescanSlices();
    analyzeSampleKey();

    // 엔진을 촙(0)으로 전환해 바로 슬라이스 연주.
    if (auto* p = apvts.getParameter ("engine"))
        p->setValueNotifyingHost (0.0f);
    return true;
}

//==============================================================================
// [parameterChanged] pitch/formant 파라미터가 바뀔 때 즉시 반응하는 리스너 콜백.
//   (생성자에서 이 둘만 리스너 등록했음. 나머지 노브는 processBlock 에서 매 블록 읽음.)
void VocalChopAudioProcessor::parameterChanged (const juce::String& id, float newValue)
{
    // 피치 파라미터면 피치/포먼트 엔진의 피치를 갱신.
    if (id == "pitch")   pitchFormant.setPitch (newValue);
    // 포먼트 파라미터면 포먼트를 갱신.
    if (id == "formant") pitchFormant.setFormant (newValue);
}

//==============================================================================
// [getPresetNames] FX 중심 공장 프리셋 이름 목록(콤보박스용).
juce::StringArray VocalChopAudioProcessor::getPresetNames()
{
    return { "Init", "Clean Chops", "Vocal Shimmer", "Lo-Fi Tape",
             "Reverse Swell", "Hard Stutter" };
}

// [applyPreset] 프리셋 번호에 맞는 파라미터 값들을 한 번에 세팅.
void VocalChopAudioProcessor::applyPreset (int presetIndex)
{
    // set = "파라미터 하나를 값으로 설정"하는 지역 헬퍼 람다.
    //   convertTo0to1 로 실제 값을 0~1 내부 표현으로 바꿔 호스트에 알리며 설정.
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Start every preset from a known baseline, then apply the character.
    // 모든 프리셋을 동일한 '기본값'에서 시작한 뒤, 아래 switch 로 개성만 덧입힘.
    set ("pitch", 0.0f);      set ("formant", 0.0f);   set ("mix", 1.0f);
    set ("width", 1.0f);      set ("grainSize", 80.0f);
    set ("drive", 0.0f);      set ("reverb", 0.0f);    set ("delay", 0.0f);
    set ("attack", 5.0f);     set ("decay", 0.0f);     set ("sustain", 1.0f);
    set ("release", 20.0f);   set ("filterType", 0.0f); set ("filterCutoff", 20000.0f);
    set ("filterReso", 0.707f); set ("delayFeedback", 0.4f); set ("pingpong", 0.0f);
    set ("reverse", 0.0f);    set ("playMode", 0.0f);  set ("outputGain", 0.0f);

    // presetIndex 에 따라 개성 값을 덧입힘. 각 case 는 하나의 프리셋.
    switch (presetIndex)
    {
        case 1: // Clean Chops
            set ("attack", 2.0f); set ("reverb", 0.15f); set ("delay", 0.1f);
            break;

        case 2: // Vocal Shimmer
            set ("formant", 3.0f); set ("reverb", 0.5f); set ("delay", 0.35f);
            set ("pingpong", 1.0f); set ("release", 200.0f);
            set ("filterType", 1.0f); set ("filterCutoff", 9000.0f);
            break;

        case 3: // Lo-Fi Tape
            set ("drive", 0.45f); set ("filterType", 1.0f); set ("filterCutoff", 3500.0f);
            set ("filterReso", 1.0f); set ("width", 0.8f); set ("reverb", 0.2f);
            set ("pitch", -0.15f); // slight detune wobble feel
            break;

        case 4: // Reverse Swell
            set ("reverse", 1.0f); set ("attack", 200.0f); set ("release", 300.0f);
            set ("reverb", 0.6f); set ("delay", 0.3f); set ("playMode", 1.0f);
            break;

        case 5: // Hard Stutter
            set ("attack", 0.0f); set ("decay", 40.0f); set ("sustain", 0.0f);
            set ("release", 5.0f); set ("drive", 0.5f); set ("grainSize", 40.0f);
            set ("playMode", 1.0f);
            break;

        // Init 은 기본값 그대로. default 도 아무것도 안 함(안전).
        case 0: // Init (baseline already applied)
        default:
            break;
    }
}

//==============================================================================
// [익명 namespace] 이 안의 정의는 이 .cpp 파일 안에서만 보임(파일 지역 캡슐화).
//   다른 파일과 이름 충돌을 막는 관용구입니다.
namespace
{
    /** One designed instrument: the synth-engine architecture plus the public
        knob defaults. Kept as plain data so adding instruments is one line. */
    // [InstrumentDef] 설계된 악기 하나 = 신스 엔진 구조 + 공개 노브 기본값.
    //   순수 데이터(구조체)로 둬서, 악기를 추가할 때 아래 표에 한 줄만 넣으면 됩니다.
    struct InstrumentDef
    {
        // 카테고리 이름(예: BASS, LEAD)과 악기 이름.
        const char* category;
        const char* name;
        // Engine patch --------------------------------------------------------
        // 엔진 패치(신스 내부 구조): 유니즌/스프레드/서브/노이즈/FM/비브라토 등.
        int   unison;  float spread, sub, noise, fm, fmRatio, vibHz, vibCents;
        // 필터 관련(컷오프Hz/엔벨로프 옥타브/엔벨로프 시간).
        float fltHz, fltEnvOct, fltEnvMs;
        // Public knobs --------------------------------------------------------
        // 공개 노브 기본값: 파형/옥타브/디튠.
        int   wave, octave; float detune;
        // ADSR 엔벨로프.
        float atk, dec, sus, rel;
        // FX 기본값: 드라이브/리버브/딜레이/핑퐁/스테레오 폭.
        float drive, reverb, delay, pingpong, width;
    };

    // wave: 0 Saw, 1 Square, 2 Sine, 3 Triangle
    // [악기 데이터 표] 각 줄이 InstrumentDef 하나. 열 순서는 위 구조체 필드 순서와 같습니다.
    //   (아래 수백 줄은 순수 숫자 데이터이므로 줄마다 주석을 달지 않습니다 — 위 스키마 참고.)
    //   wave 값: 0=톱니(Saw), 1=사각(Square), 2=사인(Sine), 3=삼각(Triangle).
    static const InstrumentDef kInstruments[] = {
    // cat      name              uni sprd  sub   noise fm    fmRat vibHz vibC  fltHz  envOct envMs | wav oct det   atk   dec   sus   rel   drv   rev   dly   pp   wid
    { "INIT",  "Init Synth",       1, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 20000, 0.0f,  200, 0,  0,  7.0f,  5,   120, 0.75f,  60, 0.00f, 0.15f, 0.0f, 0, 1.0f },

    { "BASS",  "Neon Bass",        3, 0.5f, 0.6f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   220, 3.0f,  140, 0, -1, 14.0f,  0,   220, 0.80f,  80, 0.30f, 0.00f, 0.0f, 0, 0.9f },
    { "BASS",  "Sub 808",          1, 0.0f, 1.0f, 0.03f,0.0f, 2.0f, 0.0f, 0.0f,   900, 2.0f,   60, 2, -2,  0.0f,  0,   900, 0.00f, 300, 0.45f, 0.00f, 0.0f, 0, 0.6f },
    { "BASS",  "Reese Bass",       5, 0.85f,0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   450, 1.0f,  400, 0, -1, 28.0f, 10,   300, 0.90f, 120, 0.25f, 0.10f, 0.0f, 0, 1.0f },
    { "BASS",  "Wobble Growl",     3, 0.6f, 0.4f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   300, 2.5f,  500, 1, -1, 18.0f,  5,   350, 0.85f, 120, 0.40f, 0.05f, 0.0f, 0, 1.0f },
    { "BASS",  "Pluck Bass",       1, 0.0f, 0.4f, 0.05f,0.0f, 2.0f, 0.0f, 0.0f,   250, 3.5f,   90, 0, -1,  5.0f,  0,   160, 0.20f,  80, 0.20f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Analog Warm",      1, 0.0f, 0.5f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   700, 1.5f,  250, 3, -1,  4.0f,  2,   250, 0.70f, 120, 0.10f, 0.10f, 0.0f, 0, 0.9f },
    { "BASS",  "FM Knock",         1, 0.0f, 0.3f, 0.0f, 0.6f, 2.0f, 0.0f, 0.0f,   500, 2.0f,   80, 2, -1,  0.0f,  0,   200, 0.00f, 100, 0.20f, 0.05f, 0.0f, 0, 0.8f },
    { "BASS",  "Moog Bass",        1, 0.0f, 0.6f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   480, 2.2f,  180, 0, -1,  4.0f,  1,   180, 0.70f,  90, 0.12f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Future Bass",      5, 0.7f, 0.3f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  1200, 1.6f,  220, 0, -1, 22.0f,  5,   300, 0.50f, 150, 0.15f, 0.20f, 0.10f, 0, 1.3f },
    { "BASS",  "Deep House",       1, 0.0f, 0.8f, 0.02f,0.0f, 2.0f, 0.0f, 0.0f,   350, 1.8f,  120, 1, -1,  0.0f,  2,   240, 0.35f, 110, 0.08f, 0.05f, 0.0f, 0, 0.85f },
    { "BASS",  "Neuro Bass",       1, 0.0f, 0.4f, 0.00f, 0.70f, 3.5f, 0.0f, 0.0f,   320, 3.2f,  260, 2, -1,  0.0f,  2,   300, 0.60f, 120, 0.45f, 0.05f, 0.0f, 0, 0.8f },
    { "BASS",  "Garage Sub",       1, 0.0f, 1.0f, 0.00f, 0.25f, 1.0f, 0.0f, 0.0f,   240, 1.6f,  140, 2, -2,  0.0f,  1,   350, 0.55f, 140, 0.20f, 0.03f, 0.0f, 0, 0.6f },
    { "BASS",  "Slap Funk",        1, 0.0f, 0.5f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,   500, 3.8f,   90, 1, -1,  3.0f,  1,   220, 0.15f,  80, 0.30f, 0.06f, 0.0f, 0, 0.7f },
    { "BASS",  "Dark Reese",       4, 0.6f, 0.3f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   650, 0.8f,  600, 0, -1, 28.0f,  3,   400, 0.80f, 150, 0.35f, 0.08f, 0.0f, 0, 0.9f },
    { "BASS",  "Rubber Bass",      1, 0.0f, 0.5f, 0.00f, 0.50f, 2.0f, 0.0f, 0.0f,   420, 3.0f,  130, 2, -1,  0.0f,  1,   260, 0.20f,  90, 0.25f, 0.05f, 0.0f, 0, 0.7f },
    { "BASS",  "Acid Bass",        1, 0.0f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   220, 3.6f,  160, 0, -1,  0.0f,  1,   240, 0.30f,  70, 0.40f, 0.04f, 0.0f, 0, 0.6f },
    { "BASS",  "Gnarl Bass",    2, 0.3f, 0.4f, 0.00f, 0.65f, 2.0f, 4.5f,  8.0f,  300, 2.5f,  420, 3, -1, 10.0f,  1,  380, 0.60f, 140, 0.50f, 0.05f, 0.0f, 0, 1.0f },
    { "BASS",  "Memphis 808",   1, 0.0f, 0.8f, 0.00f, 0.50f, 1.0f, 0.0f,  0.0f,  700, 1.5f,  300, 2, -2,  0.0f,  0,  900, 0.30f, 300, 0.60f, 0.04f, 0.0f, 0, 0.8f },
    { "BASS",  "Trap Knock",    1, 0.0f, 1.0f, 0.00f, 0.30f, 3.0f, 0.0f,  0.0f,  200, 2.0f,   80, 2, -2,  0.0f,  0, 1200, 0.40f, 250, 0.25f, 0.03f, 0.0f, 0, 0.7f },
    { "BASS",  "Outrun Bass",   2, 0.2f, 0.5f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f,  900, 2.0f,  160, 0, -1,  6.0f,  1,  260, 0.20f, 120, 0.22f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Octave Disco",  1, 0.0f, 0.4f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 1200, 1.8f,  120, 1, -1,  0.0f,  1,  200, 0.15f, 100, 0.18f, 0.04f, 0.0f, 0, 0.8f },
    { "BASS",  "Psy Stomp",     1, 0.0f, 0.3f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f,  500, 1.8f,   90, 0, -1,  0.0f,  0,  140, 0.00f,  70, 0.30f, 0.03f, 0.0f, 0, 0.7f },
    { "BASS",  "Dusty Bass",    1, 0.0f, 0.5f, 0.04f, 0.00f, 2.0f, 0.0f,  0.0f,  600, 1.0f,  350, 3, -1,  0.0f,  8,  500, 0.60f, 220, 0.15f, 0.10f, 0.0f, 0, 0.8f },
    { "BASS",  "Lately Bass",   1, 0.0f, 0.4f, 0.00f, 0.70f, 1.0f, 0.0f,  0.0f,  800, 2.5f,  120, 2, -1,  0.0f,  0,  350, 0.35f, 150, 0.20f, 0.06f, 0.0f, 0, 0.8f },
    { "BASS",  "Talk Bass",     1, 0.0f, 0.3f, 0.00f, 0.50f, 3.0f, 5.0f, 12.0f,  900, 1.2f,  250, 3, -1,  0.0f,  2,  300, 0.65f, 130, 0.30f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Pedal Organ",   1, 0.0f, 0.9f, 0.00f, 0.20f, 2.0f, 0.0f,  0.0f, 1400, 0.5f,  100, 2, -1,  0.0f,  2,  120, 1.00f,  90, 0.15f, 0.08f, 0.0f, 0, 0.9f },
    { "BASS",  "Rust Bass",     3, 0.3f, 0.3f, 0.20f, 0.00f, 2.0f, 0.0f,  0.0f,  700, 1.5f,  300, 0, -1, 15.0f,  1,  400, 0.55f, 180, 0.70f, 0.06f, 0.0f, 0, 1.0f },
    { "BASS",  "Liquid Sub",    1, 0.0f, 0.6f, 0.00f, 0.10f, 2.0f, 0.0f,  0.0f,  300, 0.8f,  400, 2, -2,  0.0f,  5,  350, 0.85f, 250, 0.08f, 0.06f, 0.0f, 0, 0.7f },
    { "BASS",  "Syn Jazz Bass",  1, 0.0f, 0.5f, 0.05f, 0.00f, 2.0f, 0.0f,  0.0f,  350, 1.5f,  180, 3, -1,  0.0f,  3,  700, 0.25f, 150, 0.10f, 0.12f, 0.0f, 0, 0.8f },
    { "BASS",  "Rage 808",      1, 0.0f, 1.0f, 0.05f, 0.30f, 1.0f, 0.0f, 0.0f,   500, 2.5f,  150, 2, -2,  0.0f,  0, 1400, 0.30f, 250, 0.55f, 0.05f, 0.0f, 0, 0.8f },
    { "BASS",  "Growl 808",     1, 0.0f, 0.7f, 0.00f, 0.60f, 2.5f, 0.0f, 0.0f,   400, 3.0f,  300, 2, -2,  0.0f,  0, 1000, 0.40f, 220, 0.60f, 0.05f, 0.0f, 0, 0.85f },
    { "BASS",  "Bounce Bass",   1, 0.0f, 0.6f, 0.03f, 0.00f, 2.0f, 0.0f, 0.0f,   700, 2.8f,  110, 1, -1,  4.0f,  0,  300, 0.20f, 120, 0.35f, 0.08f, 0.0f, 0, 0.9f },
    { "BASS",  "Seoul Bass",    1, 0.0f, 0.75f,0.00f, 0.15f, 1.0f, 0.0f, 0.0f,   450, 1.5f,  250, 2, -1,  0.0f,  2,  450, 0.50f, 200, 0.15f, 0.10f, 0.0f, 0, 0.85f },

    { "BASS",  "Donk Bass",     1, 0.00f,0.3f, 0.00f, 0.75f, 1.0f, 0.0f, 0.0f,  1400, 2.5f,   90, 2, -1,  0.0f,   0,  220, 0.10f,  90, 0.30f, 0.06f, 0.00f, 0, 0.8f },
    { "BASS",  "Hoover Bass",   5, 0.80f,0.4f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,   800, 1.5f,  300, 0, -1, 26.0f,   5,  350, 0.75f, 160, 0.35f, 0.10f, 0.00f, 0, 1.0f },
    { "BASS",  "UK Bass",       2, 0.30f,0.7f, 0.00f, 0.30f, 2.0f, 0.0f, 0.0f,   500, 2.0f,  180, 1, -1,  8.0f,   1,  300, 0.45f, 130, 0.25f, 0.06f, 0.00f, 0, 0.8f },
    { "BASS",  "Growl Sub",     1, 0.00f,0.9f, 0.00f, 0.40f, 2.0f, 0.0f, 0.0f,   350, 1.2f,  450, 2, -2,  0.0f,   3,  500, 0.70f, 220, 0.30f, 0.05f, 0.00f, 0, 0.65f },
    { "BASS",  "Metal Bass",    3, 0.40f,0.3f, 0.00f, 0.55f, 3.5f, 0.0f, 0.0f,   600, 2.2f,  240, 0, -1, 14.0f,   1,  320, 0.60f, 140, 0.50f, 0.08f, 0.00f, 0, 0.95f },
    { "DRUMS", "Drum Kit",     1, 0.00f,0.0f, 0.02f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2, -1,  0.0f,   0,  500, 0.00f, 200, 0.30f, 0.02f, 0.00f, 0, 0.7f },
    { "DRUMS", "Kick 808",      1, 0.0f, 0.0f, 0.02f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2, -1,  0.0f,  0,  500, 0.00f, 200, 0.30f, 0.02f, 0.0f, 0, 0.7f },
    { "DRUMS", "Kick Punch",    1, 0.0f, 0.0f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  4000, 0.0f,  200, 2, -1,  0.0f,  0,  260, 0.00f, 120, 0.45f, 0.02f, 0.0f, 0, 0.7f },
    { "DRUMS", "Snare 808",     1, 0.0f, 0.15f,0.85f, 0.00f, 2.0f, 0.0f, 0.0f,  7000, 0.6f,  120, 2,  0,  0.0f,  0,  220, 0.00f, 140, 0.25f, 0.12f, 0.0f, 0, 1.0f },
    { "DRUMS", "Snare Tight",   1, 0.0f, 0.10f,0.90f, 0.00f, 2.0f, 0.0f, 0.0f,  9000, 0.4f,   90, 2,  0,  0.0f,  0,  150, 0.00f, 100, 0.30f, 0.08f, 0.0f, 0, 1.0f },
    { "DRUMS", "Clap",          1, 0.0f, 0.0f, 1.00f, 0.00f, 2.0f, 0.0f, 0.0f,  6500, 0.3f,  110, 2,  0,  0.0f,  8,  190, 0.00f, 160, 0.20f, 0.22f, 0.0f, 0, 1.15f },
    { "DRUMS", "Hat Closed",    1, 0.0f, 0.0f, 0.75f, 0.90f, 7.31f,0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0,   55, 0.00f,  45, 0.15f, 0.03f, 0.0f, 0, 1.0f },
    { "DRUMS", "Hat Open",      1, 0.0f, 0.0f, 0.75f, 0.90f, 7.31f,0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0,  420, 0.00f, 320, 0.15f, 0.08f, 0.0f, 0, 1.1f },
    { "DRUMS", "Rim Perc",      1, 0.0f, 0.20f,0.35f, 0.60f, 3.7f, 0.0f, 0.0f,  8000, 0.8f,   60, 2,  0,  0.0f,  0,   90, 0.00f,  70, 0.25f, 0.10f, 0.0f, 0, 1.0f },

    { "DRUMS", "Tom Low",       1, 0.00f,0.0f, 0.08f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2, -1,  0.0f,   0,  420, 0.00f, 180, 0.15f, 0.10f, 0.00f, 0, 0.85f },
    { "DRUMS", "Tom High",      1, 0.00f,0.0f, 0.08f, 0.00f, 2.0f, 0.0f, 0.0f,  3500, 0.0f,  200, 2,  0,  0.0f,   0,  300, 0.00f, 140, 0.12f, 0.10f, 0.00f, 0, 0.9f },
    { "DRUMS", "Perc 909",      1, 0.00f,0.0f, 0.95f, 0.00f, 2.0f, 0.0f, 0.0f,  5000, 0.4f,   90, 2,  0,  0.0f,   0,  160, 0.00f, 110, 0.10f, 0.12f, 0.00f, 0, 1.05f },
    { "DRUMS", "Cowbell 808",   1, 0.00f,0.0f, 0.05f, 0.60f, 1.48f,0.0f, 0.0f,  5500, 0.0f,  200, 1,  0,  0.0f,   0,  180, 0.00f, 120, 0.10f, 0.06f, 0.00f, 0, 1.0f },
    { "DRUMS", "Shaker",        1, 0.00f,0.0f, 1.00f, 0.00f, 2.0f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,   5,   90, 0.05f,  70, 0.00f, 0.06f, 0.00f, 0, 1.0f },
    { "DRUMS", "Tambo Jingle",  1, 0.00f,0.0f, 0.95f, 0.55f, 7.3f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,   0,  170, 0.00f, 130, 0.00f, 0.15f, 0.00f, 0, 1.1f },
    { "DRUMS", "Syn Conga",     1, 0.00f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2800, 0.0f,  200, 2,  0,  0.0f,   0,  210, 0.00f, 110, 0.08f, 0.10f, 0.00f, 0, 0.9f },
    { "DRUMS", "Clave",         1, 0.00f,0.0f, 0.04f, 0.00f, 2.0f, 0.0f, 0.0f,  2500, 0.0f,  200, 2,  1,  0.0f,   0,   90, 0.00f,  70, 0.05f, 0.10f, 0.00f, 0, 0.9f },
    { "VOCAL", "Vox Choir",     5, 0.85f,0.0f, 0.08f, 0.00f, 2.0f, 4.5f, 8.0f,  1800, 0.4f,  500, 0,  0, 12.0f, 260,  700, 0.85f, 700, 0.00f, 0.55f, 0.00f, 0, 1.5f },
    { "VOCAL", "Vox Ahh",       4, 0.70f,0.0f, 0.06f, 0.00f, 2.0f, 5.0f,10.0f,  2600, 0.6f,  300, 0,  0, 10.0f, 120,  500, 0.85f, 450, 0.00f, 0.45f, 0.00f, 0, 1.35f },
    { "VOCAL", "Vox Ooh",       3, 0.60f,0.0f, 0.05f, 0.00f, 2.0f, 4.5f, 8.0f,   950, 0.4f,  350, 3,  0,  8.0f, 150,  600, 0.85f, 500, 0.00f, 0.45f, 0.00f, 0, 1.25f },
    { "VOCAL", "Vox Lead",      2, 0.30f,0.0f, 0.04f, 0.15f, 2.0f, 5.5f,14.0f,  2800, 1.0f,  220, 0,  0,  7.0f,  25,  300, 0.90f, 260, 0.10f, 0.30f, 0.10f, 0, 1.1f },
    { "VOCAL", "Vox Pluck",     3, 0.50f,0.0f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  2400, 2.0f,  120, 0,  0,  8.0f,   0,  240, 0.05f, 220, 0.05f, 0.35f, 0.12f, 0, 1.2f },
    { "VOCAL", "Vox Stab",      5, 0.70f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2600, 1.5f,  180, 0,  0, 12.0f,   2,  320, 0.15f, 240, 0.05f, 0.40f, 0.15f, 0, 1.3f },
    { "VOCAL", "Chop Vox",      4, 0.60f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 1.8f,  140, 0,  0, 10.0f,   2,  180, 0.10f, 160, 0.08f, 0.30f, 0.20f, 0, 1.25f },
    { "VOCAL", "Robot Vox",     1, 0.00f,0.0f, 0.02f, 0.55f, 1.0f, 0.0f, 0.0f,  1600, 1.0f,  260, 1,  0,  0.0f,   5,  260, 0.70f, 180, 0.25f, 0.20f, 0.10f, 0, 1.0f },
    { "VOCAL", "Talkbox Vox",   1, 0.00f,0.1f, 0.00f, 0.50f, 3.0f, 5.0f,10.0f,  1200, 1.2f,  250, 3,  0,  0.0f,   8,  300, 0.65f, 200, 0.30f, 0.25f, 0.05f, 0, 1.0f },
    { "VOCAL", "Vox Hum",       1, 0.00f,0.15f,0.04f, 0.00f, 2.0f, 4.0f, 6.0f,   750, 0.3f,  400, 2,  0,  0.0f, 200,  600, 0.90f, 500, 0.00f, 0.35f, 0.00f, 0, 1.0f },
    { "VOCAL", "Whisper Air",   2, 0.60f,0.0f, 0.60f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 0,  0, 10.0f, 300,  900, 0.80f, 900, 0.00f, 0.70f, 0.10f, 0, 1.6f },
    { "VOCAL", "Angel Choir",   6, 0.90f,0.0f, 0.10f, 0.00f, 2.0f, 4.0f, 7.0f,  2200, 0.3f,  700, 0,  0, 14.0f, 500, 1000, 0.85f,1100, 0.00f, 0.75f, 0.10f, 0, 1.6f },
    { "VOCAL", "Beatbox Kick",  1, 0.00f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2500, 0.0f,  200, 2, -1,  0.0f,   0,  320, 0.00f, 140, 0.20f, 0.03f, 0.00f, 0, 0.7f },
    { "VOCAL", "Beatbox Snare", 1, 0.00f,0.1f, 0.95f, 0.00f, 2.0f, 0.0f, 0.0f,  4500, 0.5f,  100, 2,  0,  0.0f,   0,  200, 0.00f, 120, 0.10f, 0.10f, 0.00f, 0, 1.0f },
    { "VOCAL", "Beatbox Hat",   1, 0.00f,0.0f, 1.00f, 0.00f, 2.0f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,   0,   70, 0.00f,  55, 0.05f, 0.02f, 0.00f, 0, 1.0f },

    { "VOCAL", "Diva Vox",      5, 0.75f,0.0f, 0.05f, 0.00f, 2.0f, 5.5f,12.0f,  3200, 0.8f,  250, 0,  0, 11.0f,  60,  400, 0.85f, 380, 0.05f, 0.40f, 0.10f, 0, 1.4f },
    { "VOCAL", "Deep Choir",    5, 0.85f,0.15f,0.07f, 0.00f, 2.0f, 4.0f, 7.0f,  1200, 0.3f,  600, 0, -1, 12.0f, 350,  800, 0.85f, 850, 0.00f, 0.60f, 0.00f, 0, 1.5f },
    { "HITS",  "Neon 84 Lead",  5, 0.70f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  4500, 0.6f,  250, 0,  0, 14.0f,   5,  300, 0.80f, 250, 0.15f, 0.35f, 0.20f, 0, 1.4f },
    { "HITS",  "Trap Flute",    1, 0.00f,0.0f, 0.10f, 0.00f, 2.0f, 4.5f, 9.0f,  3200, 0.3f,  300, 2,  0,  0.0f,  60,  250, 0.85f, 300, 0.00f, 0.40f, 0.15f, 0, 1.1f },
    { "HITS",  "Moody Keys",    2, 0.30f,0.05f,0.02f, 0.15f, 2.0f, 0.0f, 0.0f,  1800, 0.9f,  320, 3,  0,  6.0f,   8,  900, 0.35f, 700, 0.05f, 0.50f, 0.20f, 0, 1.25f },
    { "HITS",  "Isla Pluck",    2, 0.30f,0.0f, 0.02f, 0.35f, 3.0f, 0.0f, 0.0f,  2800, 2.0f,  120, 0,  0,  7.0f,   0,  260, 0.05f, 220, 0.08f, 0.30f, 0.15f, 0, 1.15f },
    { "HITS",  "Tropic Flute",  1, 0.00f,0.0f, 0.08f, 0.30f, 2.0f, 4.0f, 7.0f,  2600, 0.5f,  220, 2,  0,  0.0f,  25,  300, 0.75f, 260, 0.00f, 0.35f, 0.20f, 1, 1.2f },
    { "HITS",  "Whisper Bass",  1, 0.00f,0.9f, 0.02f, 0.20f, 1.0f, 0.0f, 0.0f,   300, 1.0f,  300, 2, -2,  0.0f,   4,  500, 0.60f, 250, 0.10f, 0.05f, 0.00f, 0, 0.7f },
    { "HITS",  "Chart 808",     1, 0.00f,0.9f, 0.00f, 0.40f, 1.0f, 0.0f, 0.0f,   500, 1.8f,  200, 2, -2,  0.0f,   0, 1000, 0.35f, 350, 0.35f, 0.05f, 0.00f, 0, 0.75f },
    { "HITS",  "Emo Lead",      3, 0.50f,0.0f, 0.03f, 0.20f, 2.0f, 5.0f,10.0f,  2600, 0.8f,  300, 0,  0, 12.0f,  30,  400, 0.80f, 400, 0.12f, 0.45f, 0.25f, 0, 1.3f },
    { "HITS",  "Funk Brass",    4, 0.40f,0.1f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 2.2f,  160, 0,  0, 10.0f,   4,  280, 0.55f, 180, 0.25f, 0.20f, 0.05f, 0, 1.2f },
    { "HITS",  "Boogie Bass",   1, 0.00f,0.5f, 0.00f, 0.45f, 2.0f, 0.0f, 0.0f,   900, 2.6f,  110, 1, -1,  0.0f,   1,  220, 0.20f, 100, 0.20f, 0.06f, 0.00f, 0, 0.85f },
    { "HITS",  "Sunny Pluck",   3, 0.50f,0.0f, 0.00f, 0.25f, 2.0f, 0.0f, 0.0f,  3000, 1.8f,  140, 3,  0,  9.0f,   2,  320, 0.10f, 280, 0.05f, 0.40f, 0.25f, 0, 1.3f },
    { "HITS",  "Stadium Lead",  6, 0.80f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  5500, 0.8f,  220, 0,  0, 18.0f,   6,  280, 0.80f, 260, 0.18f, 0.35f, 0.20f, 0, 1.5f },
    { "HITS",  "Wave Keys",     2, 0.40f,0.1f, 0.03f, 0.20f, 1.0f, 0.0f, 0.0f,  1500, 0.7f,  400, 3,  0,  8.0f,  15,  700, 0.50f, 550, 0.06f, 0.45f, 0.15f, 0, 1.25f },
    { "HITS",  "Retro Pop Poly",4, 0.60f,0.1f, 0.02f, 0.00f, 2.0f, 4.0f, 5.0f,  2400, 0.4f,  450, 0,  0, 12.0f,  90,  600, 0.80f, 600, 0.00f, 0.45f, 0.10f, 0, 1.4f },
    { "HITS",  "Anthem Brass",  5, 0.50f,0.15f,0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1800, 1.8f,  220, 0,  0, 14.0f,   8,  320, 0.75f, 220, 0.30f, 0.25f, 0.05f, 0, 1.3f },
    { "HITS",  "Smooth EP",     1, 0.00f,0.1f, 0.00f, 0.35f, 1.0f, 0.0f, 0.0f,  1700, 0.9f,  300, 2,  0,  4.0f,   6,  850, 0.45f, 480, 0.04f, 0.30f, 0.10f, 0, 1.15f },
    { "HITS",  "Hit Pad",       5, 0.80f,0.1f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  1600, 0.3f,  700, 0,  0, 13.0f, 350,  900, 0.85f, 900, 0.00f, 0.60f, 0.10f, 0, 1.5f },
    { "HITS",  "K-Pop Pluck",   4, 0.60f,0.0f, 0.02f, 0.30f, 3.0f, 0.0f, 0.0f,  3400, 2.2f,  110, 0,  0, 10.0f,   0,  240, 0.08f, 200, 0.10f, 0.35f, 0.25f, 1, 1.35f },

    { "HITS",  "Hook Marimba",  1, 0.00f,0.0f, 0.02f, 0.50f, 4.0f, 0.0f, 0.0f,  2800, 1.5f,  120, 2,  0,  0.0f,   0,  300, 0.05f, 240, 0.05f, 0.30f, 0.12f, 0, 1.2f },
    { "HITS",  "Log Drum",      1, 0.00f,0.6f, 0.00f, 0.50f, 1.0f, 0.0f, 0.0f,   800, 2.0f,   90, 2, -1,  0.0f,   0,  350, 0.10f, 200, 0.15f, 0.10f, 0.00f, 0, 0.9f },
    { "HITS",  "Future Chords", 7, 0.90f,0.1f, 0.02f, 0.00f, 2.0f, 0.0f, 0.0f,  3500, 0.8f,  300, 0,  0, 20.0f,  15,  400, 0.60f, 350, 0.10f, 0.40f, 0.15f, 0, 1.6f },
    { "HITS",  "Cloud Bell",    2, 0.30f,0.0f, 0.02f, 0.45f, 3.5f, 0.0f, 0.0f,  4000, 0.3f,  400, 2,  0,  6.0f,   4,  900, 0.10f, 800, 0.00f, 0.60f, 0.25f, 1, 1.4f },
    { "HITS",  "Phonk Cowbell", 1, 0.00f,0.1f, 0.04f, 0.60f, 1.48f,0.0f, 0.0f,  3500, 0.4f,  150, 1,  0,  0.0f,   0,  200, 0.08f, 130, 0.30f, 0.10f, 0.05f, 0, 1.0f },
    { "HITS",  "Piano Stab",    2, 0.30f,0.05f,0.02f, 0.20f, 2.0f, 0.0f, 0.0f,  3000, 1.2f,  200, 3,  0,  6.0f,   2,  350, 0.10f, 260, 0.08f, 0.30f, 0.10f, 0, 1.25f },
    { "HITS",  "Dancehall Pluck",1,0.00f,0.1f, 0.02f, 0.30f, 2.0f, 0.0f, 0.0f,  2400, 1.8f,  130, 1,  0,  0.0f,   0,  220, 0.06f, 180, 0.10f, 0.25f, 0.15f, 0, 1.1f },
    { "HITS",  "Drill 808",     1, 0.00f,0.9f, 0.00f, 0.35f, 1.0f, 0.0f, 0.0f,   450, 1.5f,  250, 2, -2,  0.0f,   0,  900, 0.40f, 320, 0.30f, 0.04f, 0.00f, 0, 0.75f },
    { "HITS",  "Slap House",    1, 0.00f,0.7f, 0.00f, 0.40f, 2.0f, 0.0f, 0.0f,   700, 2.2f,  130, 1, -1,  0.0f,   1,  240, 0.20f, 110, 0.25f, 0.06f, 0.00f, 0, 0.85f },
    { "HITS",  "Sped-Up Pluck", 3, 0.50f,0.0f, 0.02f, 0.30f, 2.0f, 0.0f, 0.0f,  3400, 2.4f,   90, 0,  0,  9.0f,   0,  160, 0.05f, 140, 0.10f, 0.30f, 0.20f, 1, 1.3f },
    { "LEAD",  "Supersaw Lead",    7, 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  9000, 0.0f,  200, 0,  0, 22.0f,  2,   150, 0.85f, 200, 0.00f, 0.30f, 0.20f, 0, 1.6f },
    { "LEAD",  "Retro Lead",       1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 5.5f, 14.0f, 7000, 0.0f,  200, 1,  0,  6.0f,  3,   100, 0.70f, 150, 0.00f, 0.15f, 0.25f, 0, 1.0f },
    { "LEAD",  "Acid Lead",        1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   700, 3.0f,  200, 0,  0,  3.0f,  0,   180, 0.55f,  90, 0.35f, 0.10f, 0.15f, 0, 1.0f },
    { "LEAD",  "Chip Lead",        1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 6.0f,  8.0f, 20000, 0.0f, 200, 1,  0,  0.0f,  0,    80, 0.60f,  60, 0.00f, 0.10f, 0.15f, 0, 1.0f },
    { "LEAD",  "Scream Lead",      5, 0.6f, 0.0f, 0.0f, 0.0f, 2.0f, 5.0f, 10.0f, 4000, 1.0f,  800, 0,  0, 20.0f,  5,   200, 0.80f, 200, 0.50f, 0.25f, 0.20f, 0, 1.3f },
    { "LEAD",  "PWM Lead",         3, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 4.8f,  6.0f, 5200, 0.8f,  300, 1,  0, 12.0f,  8,   200, 0.75f, 180, 0.10f, 0.20f, 0.18f, 0, 1.2f },
    { "LEAD",  "Velvet Lead",      1, 0.0f, 0.15f,0.0f, 0.0f, 2.0f, 4.2f,  7.0f, 2600, 0.6f,  400, 3,  0,  4.0f, 25,   300, 0.85f, 260, 0.00f, 0.30f, 0.15f, 0, 1.05f },
    { "LEAD",  "Hard Lead",        5, 0.7f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2600, 1.5f,  300, 1,  0, 22.0f,  2,   250, 0.80f, 180, 0.60f, 0.20f, 0.15f, 0, 1.2f },
    { "LEAD",  "Italo Lead",       2, 0.4f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 10.0f, 3200, 0.8f,  250, 1,  0,  9.0f,  4,   200, 0.75f, 220, 0.15f, 0.25f, 0.35f, 1, 1.2f },
    { "LEAD",  "Flute Lead",       1, 0.0f, 0.0f, 0.15f, 0.00f, 2.0f, 5.0f, 14.0f, 4000, 0.5f,  200, 2,  0,  0.0f, 40,   300, 0.80f, 260, 0.05f, 0.30f, 0.20f, 0, 0.9f },
    { "LEAD",  "Rude Lead",        2, 0.2f, 0.1f, 0.00f, 0.00f, 2.0f, 4.5f,  8.0f, 1800, 2.0f,  400, 0,  0,  6.0f,  2,   300, 0.70f, 160, 0.70f, 0.15f, 0.10f, 0, 0.8f },
    { "LEAD",  "Dream Lead",       6, 0.9f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 0.6f,  500, 0,  0, 16.0f, 25,   350, 0.70f, 500, 0.10f, 0.40f, 0.35f, 1, 1.6f },
    { "LEAD",  "Wire Lead",        1, 0.0f, 0.0f, 0.00f, 0.55f, 7.0f, 4.8f,  6.0f, 5000, 1.0f,  350, 2,  0,  0.0f,  3,   300, 0.65f, 240, 0.20f, 0.25f, 0.30f, 1, 1.1f },
    { "LEAD",  "Midnight Lead", 3, 0.5f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 10.0f, 2800, 1.0f,  250, 0,  0, 12.0f,  5,  300, 0.75f, 250, 0.15f, 0.25f, 0.35f, 1, 1.3f },
    { "LEAD",  "Idol Pluck",    4, 0.6f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 1200, 2.5f,  200, 0,  0, 10.0f,  1,  260, 0.00f, 200, 0.10f, 0.25f, 0.30f, 1, 1.4f },
    { "LEAD",  "Funk Worm",     1, 0.0f, 0.2f, 0.00f, 0.00f, 2.0f, 5.5f, 15.0f,  600, 2.8f,  180, 1,  0,  0.0f,  2,  220, 0.70f, 120, 0.25f, 0.08f, 0.10f, 0, 1.0f },
    { "LEAD",  "Grime Hoover",  6, 0.7f, 0.2f, 0.05f, 0.00f, 2.0f, 0.0f,  0.0f, 1500, 1.5f,  300, 0, -1, 30.0f,  3,  400, 0.80f, 200, 0.50f, 0.15f, 0.15f, 0, 1.5f },
    { "LEAD",  "Haze Lead",     1, 0.0f, 0.0f, 0.00f, 0.15f, 2.0f, 4.5f,  6.0f, 1800, 0.5f,  600, 2,  0,  0.0f, 200,  500, 0.80f, 800, 0.05f, 0.60f, 0.30f, 1, 1.4f },
    { "LEAD",  "Glide Solo",    1, 0.0f, 0.1f, 0.00f, 0.00f, 2.0f, 5.8f, 14.0f, 2200, 1.2f,  300, 0,  0,  0.0f, 15,  350, 0.85f, 180, 0.20f, 0.20f, 0.25f, 0, 1.0f },
    { "LEAD",  "Solo Brass",    2, 0.2f, 0.0f, 0.00f, 0.00f, 2.0f, 5.0f,  8.0f,  900, 2.0f,  350, 0,  0,  8.0f, 40,  400, 0.80f, 300, 0.30f, 0.25f, 0.10f, 0, 1.1f },
    { "LEAD",  "NES Round",     1, 0.0f, 0.0f, 0.00f, 0.00f, 2.0f, 6.0f, 12.0f, 4000, 0.0f,  100, 3,  0,  0.0f,  1,  150, 0.90f, 100, 0.05f, 0.10f, 0.15f, 0, 0.8f },
    { "LEAD",  "Goa Lead",      2, 0.3f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 1800, 2.0f,  220, 0,  0, 10.0f,  1,  240, 0.40f, 160, 0.30f, 0.20f, 0.40f, 1, 1.2f },
    { "LEAD",  "Mainstage",     7, 0.8f, 0.1f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 6000, 0.5f,  200, 0,  0, 35.0f,  4,  300, 0.85f, 350, 0.20f, 0.30f, 0.30f, 1, 1.7f },
    { "LEAD",  "Silk Lead",     1, 0.0f, 0.0f, 0.00f, 0.25f, 2.0f, 5.5f, 12.0f, 2000, 0.8f,  400, 2,  0,  0.0f, 10,  350, 0.80f, 400, 0.08f, 0.35f, 0.30f, 1, 1.2f },
    { "LEAD",  "Epic Horn",     3, 0.3f, 0.1f, 0.00f, 0.00f, 2.0f, 4.5f,  6.0f,  700, 1.8f,  600, 0, -1,  6.0f, 60,  700, 0.90f, 500, 0.20f, 0.45f, 0.15f, 0, 1.3f },
    { "LEAD",  "Vowel Lead",    1, 0.0f, 0.0f, 0.00f, 0.55f, 5.0f, 5.2f, 14.0f, 1100, 1.2f,  400, 3,  0,  0.0f, 20,  400, 0.75f, 300, 0.15f, 0.30f, 0.25f, 1, 1.2f },
    { "LEAD",  "Rage Bell",     5, 0.8f, 0.0f, 0.05f, 0.35f, 3.5f, 0.0f, 0.0f,  3500, 1.0f,  400, 1,  0, 30.0f,  1,  600, 0.30f, 400, 0.45f, 0.35f, 0.25f, 1, 1.5f },
    { "LEAD",  "Rage Lead",     7, 0.9f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2800, 1.5f,  300, 0,  0, 38.0f,  3,  350, 0.55f, 250, 0.55f, 0.25f, 0.20f, 0, 1.5f },

    { "LEAD",  "Laser Lead",    1, 0.00f,0.0f, 0.00f, 0.50f, 4.0f, 6.0f,10.0f,  6000, 2.0f,  150, 0,  0,  4.0f,   1,  140, 0.65f, 110, 0.20f, 0.18f, 0.20f, 0, 1.0f },
    { "LEAD",  "Whistle Lead",  1, 0.00f,0.0f, 0.02f, 0.00f, 2.0f, 5.5f,12.0f,  8000, 0.0f,  200, 2,  1,  0.0f,  40,  200, 0.85f, 180, 0.00f, 0.25f, 0.10f, 0, 1.0f },
    { "LEAD",  "Saw Stack",     7, 0.90f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  7000, 0.5f,  250, 0,  0, 20.0f,   8,  220, 0.80f, 220, 0.15f, 0.25f, 0.15f, 0, 1.5f },
    { "LEAD",  "Festival Lead", 6, 0.80f,0.3f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  5000, 1.0f,  200, 0,  0, 18.0f,   3,  260, 0.75f, 240, 0.20f, 0.30f, 0.20f, 0, 1.45f },
    { "LEAD",  "Neon Lead",     3, 0.50f,0.0f, 0.00f, 0.25f, 2.0f, 5.0f, 8.0f,  3500, 1.2f,  220, 1,  0, 10.0f,  15,  300, 0.80f, 260, 0.15f, 0.30f, 0.25f, 0, 1.2f },
    { "SYNTH", "Analog Poly",      3, 0.6f, 0.2f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  1800, 1.2f,  300, 0,  0, 12.0f,  8,   300, 0.65f, 300, 0.05f, 0.25f, 0.10f, 0, 1.2f },
    { "SYNTH", "PWM Strings",      5, 0.8f, 0.0f, 0.0f, 0.0f, 2.0f, 4.0f,  4.0f, 3000, 0.0f,  200, 1,  0, 18.0f, 120,  400, 0.80f, 600, 0.00f, 0.35f, 0.00f, 0, 1.4f },
    { "SYNTH", "Hoover",           5, 0.9f, 0.3f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2500, 1.0f,  500, 0, -1, 35.0f, 15,   250, 0.75f, 350, 0.15f, 0.25f, 0.10f, 0, 1.4f },
    { "SYNTH", "80s Poly",         3, 0.7f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2200, 1.5f,  250, 1,  0, 14.0f, 10,   350, 0.60f, 400, 0.05f, 0.30f, 0.12f, 0, 1.3f },
    { "SYNTH", "FM Digital",       1, 0.0f, 0.0f, 0.0f, 0.45f,3.0f, 0.0f, 0.0f,  8000, 0.0f,  200, 2,  0,  4.0f,  3,   500, 0.35f, 350, 0.00f, 0.30f, 0.10f, 0, 1.1f },
    { "SYNTH", "Trance Saw",       7, 0.9f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  3800, 1.2f,  350, 0,  0, 18.0f,  6,   400, 0.55f, 240, 0.10f, 0.25f, 0.30f, 1, 1.5f },
    { "SYNTH", "Rave Stab",        3, 0.6f, 0.2f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2400, 2.4f,  140, 0,  0, 14.0f,  0,   220, 0.15f, 120, 0.18f, 0.20f, 0.10f, 0, 1.25f },
    { "SYNTH", "Techno Stab",      3, 0.5f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   480, 3.0f,  150, 0, -1, 14.0f,  1,   280, 0.05f, 200, 0.35f, 0.30f, 0.20f, 1, 1.1f },
    { "SYNTH", "French Chord",     5, 0.8f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1100, 1.8f,  180, 0,  0, 15.0f,  1,   320, 0.15f, 160, 0.20f, 0.15f, 0.10f, 0, 1.5f },
    { "SYNTH", "Ambient Drone",    5, 0.7f, 0.4f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,   850, 0.0f, 3000, 0, -1, 10.0f, 450,  800, 0.90f, 1800, 0.10f, 0.65f, 0.25f, 1, 1.7f },
    { "SYNTH", "Glass Keys",       1, 0.0f, 0.0f, 0.00f, 0.50f, 4.0f, 0.0f, 0.0f,  6000, 0.8f,  400, 2,  0,  0.0f,  1,   700, 0.25f, 500, 0.05f, 0.35f, 0.25f, 1, 1.3f },
    { "SYNTH", "Ice Pluck",        2, 0.4f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 4.0f,  110, 3,  0,  7.0f,  1,   240, 0.00f, 300, 0.05f, 0.35f, 0.45f, 1, 1.4f },
    { "SYNTH", "Soft Organ",       1, 0.0f, 0.7f, 0.00f, 0.00f, 2.0f, 6.0f,  5.0f, 3500, 0.0f,  200, 2,  0,  0.0f,  5,   100, 0.95f, 120, 0.10f, 0.25f, 0.00f, 0, 1.0f },
    { "SYNTH", "Cold Strings",     6, 0.8f, 0.0f, 0.04f, 0.00f, 2.0f, 0.0f, 0.0f,  1600, 0.4f,  900, 0,  0, 13.0f, 220,  600, 0.80f, 900, 0.10f, 0.45f, 0.15f, 0, 1.6f },
    { "SYNTH", "Lo-Fi Keys",       2, 0.3f, 0.0f, 0.07f, 0.00f, 2.0f, 0.8f,  6.0f, 1400, 1.2f,  350, 3,  0,  5.0f,  2,   850, 0.35f, 400, 0.15f, 0.30f, 0.20f, 0, 1.1f },
    { "SYNTH", "Modular Blip",   1, 0.0f, 0.0f, 0.00f, 0.30f, 3.0f, 0.0f, 0.0f,   900, 3.0f,   90, 2,  0,  0,  1.0f,  140, 0.00f,  120, 0.05f, 0.10f, 0.45f, 1, 1.0f },
    { "SYNTH", "Squelch 303",    1, 0.0f, 0.1f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   420, 3.5f,  300, 1, -1,  0,  0.5f,  260, 0.35f,  120, 0.55f, 0.08f, 0.15f, 0, 0.8f },
    { "SYNTH", "Bigroom Stab",   7, 0.8f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  5000, 1.5f,  200, 0,  0, 30,  1.0f,  300, 0.10f,  200, 0.25f, 0.30f, 0.10f, 0, 1.4f },
    { "SYNTH", "2-Step Chord",   3, 0.6f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 1.2f,  180, 1,  0, 10,  1.0f,  220, 0.15f,  180, 0.10f, 0.20f, 0.25f, 1, 1.2f },
    { "SYNTH", "8-Bit Poly",     1, 0.0f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 8.0f,  8000, 0.0f,  200, 1,  0,  0,  1.0f,  400, 0.60f,  100, 0.10f, 0.15f, 0.20f, 1, 0.9f },
    { "SYNTH", "Mall Keys",      2, 0.5f, 0.1f, 0.00f, 0.45f, 2.0f, 0.8f, 10.0f, 3200, 0.8f,  500, 2,  0,  8,  2.0f,  700, 0.50f,  500, 0.05f, 0.35f, 0.30f, 1, 1.5f },
    { "SYNTH", "CS-80 Brass",    4, 0.6f, 0.1f, 0.00f, 0.00f, 2.0f, 5.0f, 6.0f,  1400, 1.0f,  800, 0,  0, 14, 90.0f,  600, 0.80f,  400, 0.30f, 0.35f, 0.10f, 0, 1.3f },
    { "SYNTH", "Gabber Stab",    1, 0.0f, 0.4f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 2.5f,  150, 1, -1,  0,  0.5f,  250, 0.10f,  150, 0.85f, 0.15f, 0.05f, 0, 0.8f },
    { "SYNTH", "Tech Blip",      1, 0.0f, 0.0f, 0.00f, 0.20f, 1.0f, 0.0f, 0.0f,  2500, 1.5f,   60, 2,  0,  0,  0.5f,   90, 0.00f,   80, 0.05f, 0.12f, 0.35f, 1, 1.0f },
    { "SYNTH", "Gate Dream",     7, 0.9f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  4500, 0.8f,  400, 0,  0, 22,  2.0f,  300, 0.65f,  300, 0.10f, 0.30f, 0.45f, 1, 1.5f },
    { "SYNTH", "Italo Arp",      2, 0.4f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 1.8f,  220, 0,  0,  8,  1.0f,  260, 0.20f,  180, 0.10f, 0.20f, 0.40f, 1, 1.1f },
    { "SYNTH", "Night Drive",    3, 0.5f, 0.3f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1000, 0.6f,  600, 0, -1, 10,  8.0f,  500, 0.75f,  350, 0.20f, 0.30f, 0.20f, 0, 1.2f },
    { "SYNTH", "Dembow Pluck",   3, 0.5f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1800, 2.0f,  160, 0,  0, 12,  1.0f,  200, 0.10f,  160, 0.12f, 0.18f, 0.22f, 1, 1.2f },
    { "SYNTH", "Hyper Saw",      7, 1.0f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  9000, 0.5f,  300, 0,  0, 40,  1.0f,  400, 0.70f,  250, 0.35f, 0.25f, 0.15f, 0, 1.6f },
    { "SYNTH", "Dark Rage",      5, 0.7f, 0.3f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  1200, 1.8f,  500, 0, -1, 26.0f,  5,  500, 0.45f, 350, 0.50f, 0.30f, 0.15f, 0, 1.4f },
    { "SYNTH", "Soul Chords",    3, 0.5f, 0.1f, 0.00f, 0.20f, 1.0f, 0.0f, 0.0f,  2200, 0.6f,  400, 3,  0,  8.0f, 10,  700, 0.45f, 400, 0.05f, 0.35f, 0.15f, 0, 1.35f },

    { "SYNTH", "Analog Strings",5, 0.80f,0.0f, 0.04f, 0.00f, 2.0f, 4.5f, 6.0f,  2600, 0.2f,  400, 0,  0, 14.0f, 280,  700, 0.85f, 700, 0.00f, 0.45f, 0.00f, 0, 1.45f },
    { "SYNTH", "Digital Ice",   3, 0.60f,0.0f, 0.03f, 0.45f, 6.0f, 0.0f, 0.0f,  6000, 0.3f,  300, 2,  1,  8.0f,  40,  500, 0.70f, 500, 0.00f, 0.50f, 0.25f, 1, 1.4f },
    { "SYNTH", "Glass Sync",    2, 0.40f,0.0f, 0.00f, 0.55f, 3.0f, 0.0f, 0.0f,  4500, 1.5f,  280, 1,  0,  9.0f,  10,  380, 0.60f, 300, 0.20f, 0.35f, 0.15f, 0, 1.2f },
    { "SYNTH", "Warm Poly",     3, 0.50f,0.15f,0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 0.6f,  320, 0,  0, 10.0f,  45,  450, 0.80f, 420, 0.05f, 0.30f, 0.10f, 0, 1.25f },
    { "SYNTH", "Hard Sync",     2, 0.30f,0.0f, 0.00f, 0.65f, 2.5f, 0.0f, 0.0f,  3000, 2.2f,  240, 0,  0, 12.0f,   2,  260, 0.55f, 180, 0.35f, 0.20f, 0.10f, 0, 1.1f },
    { "PIANO", "Syn Grand",      1, 0.0f, 0.05f,0.02f,0.0f, 2.0f, 0.0f, 0.0f,  3200, 1.2f,  700, 3,  0,  3.0f,  1,   900, 0.22f, 260, 0.00f, 0.20f, 0.00f, 0, 1.0f },
    { "PIANO", "Syn Bright",     1, 0.0f, 0.0f, 0.03f,0.12f,1.0f, 0.0f, 0.0f,  5200, 1.0f,  500, 3,  0,  4.0f,  1,   750, 0.28f, 220, 0.05f, 0.18f, 0.00f, 0, 1.05f },
    { "PIANO", "Syn Soft Key",       1, 0.0f, 0.08f,0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2000, 0.8f,  900, 2,  0,  2.0f,  2,  1100, 0.18f, 350, 0.00f, 0.30f, 0.00f, 0, 1.0f },
    { "PIANO", "House Keys",      3, 0.4f, 0.0f, 0.0f, 0.10f,2.0f, 0.0f, 0.0f,  4500, 1.0f,  300, 0,  0,  8.0f,  1,   500, 0.40f, 180, 0.08f, 0.20f, 0.10f, 0, 1.15f },
    { "PIANO", "Syn Upright",    2, 0.2f, 0.10f,0.05f,0.12f,3.0f, 0.0f, 0.0f,  2200, 1.6f,  350, 0,  0,  4.0f,  2,  1000, 0.22f, 380, 0.10f, 0.25f, 0.00f, 0, 1.1f },
    { "PIANO", "Syn Felt",       2, 0.2f, 0.20f,0.08f,0.05f,2.0f, 0.0f, 0.0f,  1200, 0.9f,  500, 3,  0,  3.0f,  4,  1100, 0.25f, 600, 0.00f, 0.40f, 0.08f, 0, 1.15f },
    { "PIANO", "Syn Honky",     3, 0.3f, 0.08f,0.06f,0.15f,3.0f, 0.0f, 0.0f,  2600, 1.7f,  320, 0,  0, 14.0f,  3,   950, 0.20f,  350, 0.15f, 0.22f, 0.00f, 0, 1.15f },
    { "PIANO", "Noir Keys",     2, 0.2f, 0.18f,0.04f,0.10f,2.0f, 0.0f, 0.0f,   900, 1.2f,  500, 0,  0,  4.0f,  4,  1300, 0.15f,  900, 0.08f, 0.50f, 0.12f, 0, 1.2f },
    { "PIANO", "Syn Pop Key",      3, 0.25f,0.12f,0.05f,0.18f,3.0f, 0.0f, 0.0f,  3200, 1.8f,  300, 0,  0,  8.0f,  2,   900, 0.25f,  320, 0.12f, 0.28f, 0.08f, 0, 1.25f },
    { "PIANO", "Ballad Keys",   2, 0.2f, 0.12f,0.03f,0.08f,2.0f, 0.0f, 0.0f,  1800, 1.4f,  420, 0,  0,  3.0f,  4,  1200, 0.30f,  700, 0.05f, 0.40f, 0.10f, 0, 1.1f },
    { "PIANO", "Syn Tack",     2, 0.2f, 0.05f,0.12f,0.25f,5.0f, 0.0f, 0.0f,  4200, 2.0f,  180, 0,  0, 10.0f,  1,   700, 0.12f,  250, 0.20f, 0.18f, 0.00f, 0, 1.0f },
    { "PIANO", "Ghost Keys",    2, 0.35f,0.15f,0.06f,0.10f,2.0f, 0.8f, 6.0f,  1400, 1.3f,  600, 0,  0,  6.0f,  5,  1400, 0.30f, 1600, 0.05f, 0.75f, 0.35f, 1, 1.5f },

    { "PIANO", "Royal Grand",   2, 0.15f,0.08f,0.015f,0.10f, 3.5f, 0.0f, 0.0f,  3200, 1.3f,  260, 3,  0,  2.5f,   1, 1400, 0.22f, 500, 0.02f, 0.30f, 0.04f, 0, 1.2f },
    { "PIANO", "Concert Bright",2, 0.20f,0.05f,0.02f, 0.14f, 3.5f, 0.0f, 0.0f,  4200, 1.5f,  220, 3,  0,  3.0f,   1, 1200, 0.20f, 450, 0.05f, 0.32f, 0.05f, 0, 1.25f },
    { "GUITAR","Syn Nylon",     1, 0.0f, 0.0f, 0.04f,0.0f, 2.0f, 0.0f, 0.0f,  1200, 2.0f,  140, 3,  0,  3.0f,  0,   380, 0.10f, 200, 0.00f, 0.25f, 0.08f, 0, 1.0f },
    { "GUITAR","Syn Steel",     1, 0.0f, 0.0f, 0.05f,0.15f,2.0f, 0.0f, 0.0f,  2600, 1.8f,  160, 0,  0,  5.0f,  0,   420, 0.12f, 220, 0.05f, 0.22f, 0.06f, 0, 1.05f },
    { "GUITAR","Syn Clean Gtr",     1, 0.0f, 0.0f, 0.0f, 0.08f,1.0f, 0.0f, 0.0f,  2400, 1.2f,  250, 3,  0,  2.0f,  1,   550, 0.30f, 250, 0.06f, 0.18f, 0.10f, 0, 1.1f },
    { "GUITAR","Syn Mute Gtr",     1, 0.0f, 0.10f,0.02f,0.0f, 2.0f, 0.0f, 0.0f,   900, 1.5f,   60, 3,  0,  0.0f,  0,   140, 0.05f,  90, 0.10f, 0.08f, 0.00f, 0, 0.95f },
    { "GUITAR","Funk Gtr Syn",      1, 0.0f, 0.0f, 0.02f,0.0f, 2.0f, 0.0f, 0.0f,  1600, 2.2f,   80, 1,  0,  2.0f,  0,   160, 0.08f, 100, 0.12f, 0.10f, 0.05f, 0, 1.0f },
    { "GUITAR","Syn 12-String",        4, 0.5f, 0.0f, 0.07f,0.10f,2.0f, 0.0f, 0.0f,  2600, 1.8f,  180, 3,  0,  9.0f,  1,   850, 0.12f, 400, 0.08f, 0.30f, 0.10f, 0, 1.35f },
    { "GUITAR","Syn Jazz Gtr",         1, 0.0f, 0.25f,0.03f,0.0f, 2.0f, 0.0f, 0.0f,  1100, 1.3f,  150, 3,  0,  0.0f,  1,   700, 0.15f, 300, 0.12f, 0.18f, 0.00f, 0, 1.0f },
    { "GUITAR","Syn Spanish",  2, 0.3f, 0.10f,0.08f,0.30f,3.0f, 0.0f, 0.0f,  2400, 1.9f,  120, 3,  0,  6.0f,  0,   500, 0.10f,  260, 0.10f, 0.30f, 0.00f, 0, 1.1f },
    { "GUITAR","Chorus Gtr",   2, 0.4f, 0.08f,0.02f,0.35f,2.0f, 0.9f, 8.0f,  3000, 1.5f,  200, 3,  0, 10.0f,  0,   800, 0.25f,  400, 0.05f, 0.30f, 0.15f, 1, 1.4f },
    { "GUITAR","Crunch Syn",  2, 0.25f,0.10f,0.04f,0.10f,2.0f, 0.0f, 0.0f,  1600, 1.6f,  140, 0,  0,  8.0f,  0,   450, 0.20f,  200, 0.60f, 0.15f, 0.00f, 0, 1.0f },
    { "GUITAR","Drive Lead Gtr", 1, 0.0f, 0.15f,0.03f,0.12f,2.0f, 5.5f,12.0f,  1300, 1.4f,  220, 0,  0,  0.0f,  0,   600, 0.30f,  300, 0.70f, 0.25f, 0.20f, 1, 0.9f },
    { "GUITAR","Syn Slide",   1, 0.0f, 0.12f,0.02f,0.40f,2.0f, 4.5f,18.0f,  2000, 1.5f,  250, 3,  0,  0.0f,  5,   900, 0.30f,  500, 0.20f, 0.35f, 0.10f, 0, 1.0f },
    { "GUITAR","Chime Syn", 2, 0.3f, 0.05f,0.02f,0.50f,3.5f, 0.0f, 0.0f,  5000, 1.8f,   90, 3,  1,  5.0f,  0,   700, 0.05f,  500, 0.05f, 0.40f, 0.20f, 1, 1.3f },
    { "GUITAR","Syn Bass Gtr",    1, 0.0f, 0.40f,0.03f,0.30f,2.0f, 0.0f, 0.0f,   700, 1.7f,  150, 3, -1,  0.0f,  0,   500, 0.25f,  180, 0.25f, 0.05f, 0.00f, 0, 0.7f },

    { "GUITAR","Pop Mute Gtr",  1, 0.00f,0.05f,0.03f, 0.30f, 2.0f, 0.0f, 0.0f,  2100, 1.6f,  110, 3,  0,  0.0f,   0,  190, 0.04f, 150, 0.10f, 0.18f, 0.08f, 0, 1.05f },
    { "GUITAR","Tropic Gtr",    2, 0.25f,0.0f, 0.02f, 0.35f, 2.0f, 0.0f, 0.0f,  2600, 1.4f,  140, 3,  0,  5.0f,   0,  260, 0.06f, 220, 0.06f, 0.28f, 0.12f, 0, 1.15f },
    { "GUITAR","Syn Acoustic",  2, 0.20f,0.0f, 0.04f, 0.30f, 3.0f, 0.0f, 0.0f,  3400, 1.1f,  180, 3,  0,  4.0f,   1,  700, 0.12f, 380, 0.03f, 0.25f, 0.06f, 0, 1.2f },
    { "PAD",   "Dream Pad",        5, 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2600, 0.0f,  200, 0,  0, 18.0f, 450,  800, 0.80f, 1000, 0.00f, 0.60f, 0.00f, 0, 1.6f },
    { "PAD",   "Warm Strings",     5, 0.7f, 0.0f, 0.0f, 0.0f, 2.0f, 4.5f,  6.0f, 3400, 0.0f,  200, 0,  0, 12.0f, 220,  500, 0.85f, 500, 0.00f, 0.45f, 0.00f, 0, 1.3f },
    { "PAD",   "Dark Pad",         5, 0.8f, 0.3f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   900, 0.0f,  200, 0, -1, 15.0f, 600, 1000, 0.85f, 1200, 0.00f, 0.70f, 0.00f, 0, 1.4f },
    { "PAD",   "Glass Pad",        3, 0.8f, 0.0f, 0.0f, 0.35f,2.0f, 0.0f, 0.0f,  5000, 0.0f,  200, 2,  0, 10.0f, 400,  800, 0.75f, 900, 0.00f, 0.65f, 0.10f, 0, 1.5f },
    { "PAD",   "Choir Air",        5, 0.9f, 0.0f, 0.06f,0.0f, 2.0f, 4.0f,  5.0f, 3000, 0.0f,  200, 2,  0, 14.0f, 350,  700, 0.85f, 800, 0.00f, 0.70f, 0.00f, 0, 1.5f },
    { "PAD",   "Analog Sweep",     5, 0.8f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   400, 2.5f, 3000, 0,  0, 16.0f, 500,  900, 0.85f, 1200, 0.10f, 0.55f, 0.00f, 0, 1.5f },
    { "PAD",   "Ocean Pad",        3, 0.9f, 0.2f, 0.10f,0.0f, 2.0f, 0.0f, 0.0f,  1800, 0.0f,  200, 2,  0, 10.0f, 800,  900, 0.85f, 1400, 0.00f, 0.70f, 0.15f, 0, 1.6f },
    { "PAD",   "Cinema Strings",   7, 0.8f, 0.0f, 0.0f, 0.0f, 2.0f, 4.6f,  5.0f, 3800, 0.0f,  200, 0,  0,  9.0f, 350,  600, 0.90f, 800, 0.00f, 0.50f, 0.00f, 0, 1.45f },
    { "PAD",   "Vapor Pad",        5, 0.85f,0.0f, 0.04f,0.0f, 2.0f, 0.0f, 0.0f,  1500, 0.5f, 1500, 1,  0, 16.0f, 600,  800, 0.80f, 1200, 0.05f, 0.60f, 0.35f, 1, 1.55f },
    { "PAD",   "Shimmer Pad",      5, 0.9f, 0.10f,0.05f,0.25f,3.0f, 0.0f, 0.0f,  3200, 1.0f, 2500, 0,  0, 14.0f, 350,  900, 0.85f, 1800, 0.00f, 0.70f, 0.30f, 1, 1.7f },
    { "PAD",   "Tape Strings",     4, 0.7f, 0.15f,0.12f,0.0f, 2.0f, 0.5f,  8.0f, 2400, 0.5f, 1200, 0,  0, 10.0f, 280,  800, 0.80f, 1200, 0.15f, 0.50f, 0.10f, 0, 1.4f },
    { "PAD",   "Cathedral",        6, 0.8f, 0.20f,0.06f,0.08f,2.0f, 4.5f,  6.0f, 1400, 0.8f, 2000, 3,  0,  8.0f, 450, 1000, 0.90f, 2000, 0.00f, 0.70f, 0.05f, 0, 1.6f },
    { "PAD",   "Velvet Pad",       3, 0.6f, 0.40f,0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   900, 0.4f, 1500, 3, -1,  7.0f, 320,  900, 0.85f, 1500, 0.10f, 0.50f, 0.00f, 0, 1.3f },
    { "PAD",   "Aurora Pad",       5, 0.85f,0.10f,0.04f,0.15f,2.0f, 0.3f,  5.0f, 1600, 1.5f, 2800, 1,  0, 12.0f, 400, 1100, 0.80f, 1700, 0.00f, 0.60f, 0.35f, 1, 1.7f },
    { "PAD",   "Solar Winds",      2, 0.5f, 0.15f,0.35f,0.0f, 2.0f, 0.0f, 0.0f,  5000, 0.0f,  200, 2,  0,  6.0f, 500, 1000, 0.75f, 2000, 0.00f, 0.65f, 0.20f, 1, 1.8f },
    { "PAD",   "Ensemble Str",   5, 0.8f, 0.05f,0.05f, 0.00f, 2.0f, 5.5f, 5.0f,  2600, 0.0f,  200, 0,  0, 16, 350.0f, 600, 0.85f,  900, 0.00f, 0.50f, 0.10f, 0, 1.5f },
    { "PAD",   "Soft Brass",     4, 0.6f, 0.1f, 0.00f, 0.00f, 2.0f, 4.5f, 4.0f,  1500, 0.8f,  900, 0,  0, 12, 250.0f, 700, 0.80f,  700, 0.15f, 0.45f, 0.10f, 0, 1.3f },
    { "PAD",   "Boys Choir",     3, 0.6f, 0.0f, 0.08f, 0.25f, 5.0f, 4.5f, 7.0f,  2000, 0.0f,  200, 3,  0, 10, 400.0f, 700, 0.85f, 1200, 0.00f, 0.60f, 0.10f, 0, 1.4f },
    { "PAD",   "Grain Cloud",    6, 1.0f, 0.0f, 0.25f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 0.0f,  200, 3,  0, 28, 700.0f, 900, 0.80f, 1600, 0.00f, 0.65f, 0.30f, 1, 1.7f },
    { "PAD",   "Frost Pad",      4, 0.8f, 0.0f, 0.12f, 0.00f, 2.0f, 0.0f, 0.0f,  1300, 0.0f,  200, 3,  0, 18, 500.0f, 800, 0.80f, 1400, 0.00f, 0.60f, 0.25f, 1, 1.6f },
    { "PAD",   "Juno Warmth",    3, 0.7f, 0.25f,0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1700, 0.3f,  800, 0,  0, 12, 300.0f, 600, 0.85f,  900, 0.05f, 0.45f, 0.10f, 0, 1.4f },
    { "PAD",   "Hollow Glass",   2, 0.6f, 0.0f, 0.00f, 0.50f, 3.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2,  0,  8, 400.0f, 800, 0.75f, 1300, 0.00f, 0.55f, 0.30f, 1, 1.5f },
    { "PAD",   "Nebula Drone",   5, 0.9f, 0.3f, 0.15f, 0.00f, 2.0f, 0.0f, 0.0f,  1100, 0.0f,  200, 3, -1, 20, 900.0f, 1000, 0.90f, 2000, 0.00f, 0.70f, 0.35f, 0, 1.7f },
    { "PAD",   "E-Bow Swell",    2, 0.5f, 0.0f, 0.00f, 0.30f, 2.0f, 0.0f, 0.0f,  2400, 0.6f, 1500, 3,  0,  6, 800.0f, 900, 0.80f, 1100, 0.10f, 0.55f, 0.30f, 1, 1.3f },
    { "PAD",   "Seraph Pad",     5, 0.8f, 0.0f, 0.05f, 0.00f, 2.0f, 5.0f, 5.0f,  3500, 0.0f,  200, 0,  0, 16, 500.0f, 800, 0.85f, 1600, 0.00f, 0.65f, 0.15f, 0, 1.6f },
    { "PAD",   "Tension Bed",    4, 0.7f, 0.1f, 0.20f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 1.2f, 2500, 0, -1, 26, 600.0f, 800, 0.85f, 1200, 0.15f, 0.55f, 0.10f, 0, 1.4f },
    { "PAD",   "Submerged",      3, 0.7f, 0.35f,0.00f, 0.30f, 0.5f, 0.6f, 12.0f,  700, 0.0f,  200, 2,  0, 10, 600.0f, 800, 0.85f, 1500, 0.00f, 0.65f, 0.30f, 0, 1.5f },
    { "PAD",   "Sunset Haze",    4, 0.7f, 0.2f, 0.00f, 0.00f, 2.0f, 0.7f, 6.0f,  1900, 0.0f,  200, 3,  0, 14, 400.0f, 700, 0.85f, 1300, 0.05f, 0.50f, 0.20f, 0, 1.5f },
    { "PAD",   "Retro Cosmos",   3, 0.6f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 12.0f, 2100, 0.4f, 1200, 1,  0, 12, 350.0f, 700, 0.80f, 1000, 0.10f, 0.55f, 0.35f, 1, 1.4f },
    { "PAD",   "Bass Pad",       3, 0.5f, 0.8f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   600, 0.3f,  200, 0, -2, 10.0f, 350,  800, 0.85f, 1200, 0.10f, 0.35f, 0.00f, 0, 1.1f },
    { "PAD",   "Sub Drone",      2, 0.4f, 1.0f, 0.03f, 0.00f, 2.0f, 0.0f, 0.0f,   350, 0.0f,  200, 2, -2,  6.0f, 450,  900, 0.90f, 1500, 0.05f, 0.30f, 0.00f, 0, 1.0f },
    { "PAD",   "Soul Pad",       4, 0.7f, 0.2f, 0.05f, 0.15f, 1.0f, 0.0f, 0.0f,  1600, 0.4f,  900, 3,  0,  9.0f, 400,  800, 0.85f, 1300, 0.00f, 0.55f, 0.15f, 0, 1.5f },

    { "PAD",   "Nebula Pad",    6, 0.90f,0.1f, 0.08f, 0.00f, 2.0f, 0.0f, 0.0f,  1800, 0.3f,  900, 0,  0, 16.0f, 600, 1200, 0.85f,1200, 0.00f, 0.75f, 0.15f, 1, 1.6f },
    { "PAD",   "Ice Pad",       4, 0.70f,0.0f, 0.06f, 0.35f, 6.0f, 0.0f, 0.0f,  4000, 0.0f,  200, 2,  1, 10.0f, 500, 1000, 0.85f,1100, 0.00f, 0.70f, 0.20f, 1, 1.55f },
    { "PAD",   "Analog Wash",   5, 0.85f,0.2f, 0.05f, 0.00f, 2.0f, 4.0f, 5.0f,  1500, 0.2f,  800, 0,  0, 14.0f, 450,  900, 0.85f, 950, 0.00f, 0.65f, 0.10f, 0, 1.5f },
    { "PAD",   "Deep Space",    3, 0.70f,0.5f, 0.10f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 0.4f, 1200, 0, -1, 12.0f, 700, 1500, 0.85f,1400, 0.00f, 0.80f, 0.20f, 0, 1.55f },
    { "PAD",   "Dusk Pad",      4, 0.75f,0.1f, 0.05f, 0.15f, 2.0f, 4.5f, 6.0f,  1400, 0.3f,  700, 3,  0, 11.0f, 400,  900, 0.85f, 900, 0.00f, 0.60f, 0.10f, 0, 1.45f },
    { "PLUCK", "Crystal Pluck",    1, 0.0f, 0.0f, 0.10f,0.0f, 2.0f, 0.0f, 0.0f,   500, 4.0f,  120, 3,  0,  7.0f,  0,   200, 0.00f, 140, 0.00f, 0.25f, 0.30f, 1, 1.2f },
    { "PLUCK", "Syn Kalimba",          1, 0.0f, 0.0f, 0.02f,0.5f, 4.2f, 0.0f, 0.0f,  3000, 2.0f,  100, 2,  0,  0.0f,  0,   250, 0.00f, 150, 0.00f, 0.30f, 0.10f, 0, 1.1f },
    { "PLUCK", "Syn Marimba",          1, 0.0f, 0.0f, 0.0f, 0.25f,3.0f, 0.0f, 0.0f,  2500, 2.0f,   90, 2,  0,  0.0f,  0,   220, 0.00f, 160, 0.00f, 0.30f, 0.05f, 0, 1.0f },
    { "PLUCK", "Trance Pluck",     1, 0.0f, 0.0f, 0.03f,0.0f, 2.0f, 0.0f, 0.0f,   700, 3.6f,  110, 0,  0,  6.0f,  0,   190, 0.00f, 150, 0.08f, 0.25f, 0.35f, 1, 1.2f },
    { "PLUCK", "Syn Harp",             1, 0.0f, 0.0f, 0.05f,0.10f,2.0f, 0.0f, 0.0f,  2200, 1.8f,  300, 3,  0,  3.0f,  0,   600, 0.00f, 420, 0.00f, 0.35f, 0.10f, 0, 1.15f },
    { "PLUCK", "Syn Koto",             1, 0.0f, 0.0f, 0.08f,0.18f,3.0f, 0.0f, 0.0f,  1800, 2.5f,  160, 3,  0,  0.0f,  1,   380, 0.00f, 320, 0.10f, 0.30f, 0.12f, 0, 1.1f },
    { "PLUCK", "Syn Pizz",        2, 0.3f, 0.10f,0.06f,0.0f, 2.0f, 0.0f, 0.0f,  1200, 2.0f,  120, 0,  0,  5.0f,  1,   260, 0.00f, 240, 0.05f, 0.35f, 0.00f, 0, 1.2f },
    { "PLUCK", "Syn Music Box",        1, 0.0f, 0.0f, 0.0f, 0.40f,3.5f, 0.0f, 0.0f,  6000, 1.5f,  200, 2,  1,  0.0f,  0,   550, 0.00f, 600, 0.00f, 0.45f, 0.15f, 1, 1.2f },
    { "PLUCK", "Neon Pluck",       3, 0.6f, 0.20f,0.0f, 0.12f,2.0f, 0.0f, 0.0f,   900, 3.5f,   90, 1,  0,  8.0f,  0,   300, 0.00f, 260, 0.20f, 0.25f, 0.40f, 1, 1.4f },
    { "PLUCK", "Syn Dulcimer",         2, 0.30f,0.00f,0.04f,0.15f,3.0f, 0.0f, 0.0f,  3400, 2.2f,  220, 3,  0,  6.0f,  0,   620, 0.00f, 500, 0.05f, 0.30f, 0.12f, 0, 1.20f },
    { "PLUCK", "Syn Steel Pan",        1, 0.00f,0.00f,0.00f,0.50f,2.0f, 5.0f, 6.0f,  2600, 2.0f,  180, 2,  0,  0.0f,  0,   550, 0.00f, 480, 0.08f, 0.35f, 0.10f, 0, 1.10f },
    { "PLUCK", "Syn Banjo",      1, 0.00f,0.00f,0.10f,0.18f,5.0f, 0.0f, 0.0f,  3800, 3.2f,   90, 0,  0,  0.0f,  0,   320, 0.00f, 240, 0.15f, 0.15f, 0.00f, 0, 1.00f },
    { "PLUCK", "Syn Sitar",       2, 0.35f,0.00f,0.06f,0.20f,5.0f, 0.0f, 0.0f,  2400, 3.0f,  260, 0,  0,  9.0f,  0,   680, 0.10f, 600, 0.45f, 0.30f, 0.20f, 1, 1.20f },
    { "PLUCK", "Tropic Pluck",     3, 0.60f,0.10f,0.00f,0.00f,1.0f, 0.0f, 0.0f,   800, 3.2f,  140, 1,  0,  8.0f,  0,   380, 0.00f, 300, 0.05f, 0.25f, 0.35f, 1, 1.30f },
    { "PLUCK", "Mallet Choir",     4, 0.50f,0.00f,0.00f,0.30f,4.0f, 0.0f, 0.0f,  2800, 1.8f,  200, 3,  0, 10.0f,  0,   480, 0.00f, 420, 0.00f, 0.40f, 0.12f, 0, 1.35f },
    { "PLUCK", "Water Drop",       1, 0.00f,0.00f,0.00f,0.30f,0.5f, 0.0f, 0.0f,   600, 4.0f,   60, 2,  0,  0.0f,  0,   200, 0.00f, 180, 0.00f, 0.30f, 0.30f, 1, 1.10f },
    { "PLUCK", "Rubber Toy",       1, 0.00f,0.10f,0.00f,0.50f,1.5f, 0.0f, 0.0f,   900, 2.2f,  110, 3,  0,  0.0f,  0,   260, 0.05f, 200, 0.20f, 0.15f, 0.08f, 0, 1.00f },
    { "PLUCK", "Syn Gamelan",          1, 0.00f,0.00f,0.00f,0.60f,3.5f, 0.0f, 0.0f,  3200, 1.6f,  240, 2,  0,  0.0f,  0,   700, 0.00f, 950, 0.00f, 0.45f, 0.10f, 0, 1.25f },
    { "PLUCK", "Felt Mallet",      1, 0.00f,0.20f,0.02f,0.10f,4.0f, 0.0f, 0.0f,  1100, 1.5f,  160, 3,  0,  0.0f,  2,   520, 0.05f, 480, 0.00f, 0.35f, 0.08f, 0, 1.10f },
    { "PLUCK", "Organ Pluck",      2, 0.40f,0.50f,0.00f,0.00f,1.0f, 0.0f, 0.0f,  1300, 2.0f,  130, 1,  0,  5.0f,  0,   300, 0.15f, 220, 0.10f, 0.20f, 0.18f, 1, 1.15f },
    { "PLUCK", "Soul Pluck",       1, 0.00f,0.05f,0.06f,0.20f,2.0f, 0.0f, 0.0f,  1500, 1.8f,  180, 3,  0,  4.0f,  0,   450, 0.05f, 300, 0.05f, 0.30f, 0.18f, 1, 1.2f },

    { "PLUCK", "Dance Pluck",   3, 0.50f,0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2600, 2.2f,  110, 0,  0, 10.0f,   0,  200, 0.00f, 170, 0.10f, 0.30f, 0.20f, 0, 1.2f },
    { "PLUCK", "Water Pluck",   2, 0.40f,0.0f, 0.02f, 0.40f, 3.0f, 0.0f, 0.0f,  3200, 2.5f,   90, 2,  0,  7.0f,   0,  240, 0.00f, 220, 0.00f, 0.40f, 0.25f, 1, 1.25f },
    { "PLUCK", "Bubble Pluck",  1, 0.00f,0.0f, 0.00f, 0.55f, 2.0f, 0.0f, 0.0f,  2000, 3.0f,   70, 2,  0,  0.0f,   0,  160, 0.00f, 140, 0.05f, 0.30f, 0.20f, 0, 1.1f },
    { "PLUCK", "Glass Pluck",   2, 0.30f,0.0f, 0.00f, 0.50f, 5.0f, 0.0f, 0.0f,  5000, 1.8f,  120, 2,  1,  6.0f,   0,  300, 0.00f, 260, 0.00f, 0.45f, 0.25f, 0, 1.3f },
    { "KEYS",  "EP Keys",          1, 0.0f, 0.0f, 0.0f, 0.45f,1.0f, 0.0f, 0.0f,  6500, 0.0f,  200, 2,  0,  4.0f,  2,   450, 0.40f, 260, 0.00f, 0.30f, 0.12f, 0, 1.1f },
    { "KEYS",  "Soft Keys",        1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  5200, 0.0f,  200, 3,  0,  5.0f,  5,   300, 0.60f, 250, 0.00f, 0.30f, 0.00f, 0, 1.0f },
    { "KEYS",  "House Organ",      1, 0.0f, 0.8f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  20000, 0.0f, 200, 1,  0,  4.0f,  0,    50, 1.00f,  60, 0.05f, 0.15f, 0.10f, 0, 1.1f },
    { "KEYS",  "Retro Organ",      1, 0.0f, 0.8f, 0.0f, 0.0f, 2.0f, 6.5f,  4.0f, 20000, 0.0f, 200, 1,  0,  5.0f,  2,    50, 1.00f,  90, 0.15f, 0.20f, 0.00f, 0, 1.1f },
    { "KEYS",  "Funk Clav",        1, 0.0f, 0.0f, 0.03f,0.0f, 2.0f, 0.0f, 0.0f,  1800, 2.5f,   60, 0,  0,  5.0f,  0,   180, 0.30f,  60, 0.30f, 0.10f, 0.05f, 0, 1.0f },
    { "KEYS",  "Tine EP",          1, 0.0f, 0.0f, 0.0f, 0.30f,7.0f, 0.0f, 0.0f,  5600, 0.5f,  350, 3,  0,  3.0f,  1,   650, 0.30f, 300, 0.05f, 0.28f, 0.10f, 0, 1.1f },
    { "KEYS",  "Dusty Keys",       2, 0.3f, 0.15f,0.10f,0.20f,3.0f, 5.0f,  4.0f, 2800, 0.6f,  400, 3,  0,  6.0f,  2,   700, 0.35f, 350, 0.25f, 0.35f, 0.15f, 0, 1.1f },
    { "KEYS",  "Wurli EP",         1, 0.0f, 0.10f,0.0f, 0.18f,1.0f, 5.5f,  3.0f, 3400, 0.7f,  300, 2,  0,  0.0f,  2,   750, 0.30f, 280, 0.40f, 0.20f, 0.05f, 0, 1.0f },
    { "KEYS",  "Pipe Organ",       2, 0.4f, 0.50f,0.05f,0.10f,2.0f, 0.0f, 0.0f,  4500, 0.0f,  200, 2,  0,  4.0f, 30,   100, 1.00f, 250, 0.05f, 0.50f, 0.00f, 0, 1.3f },
    { "KEYS",  "DX Piano",         1, 0.00f,0.00f,0.00f,0.50f,14.0f,0.0f, 0.0f,  8000, 0.8f,  300, 2,  0,  0.0f,  1,   900, 0.35f, 400, 0.05f, 0.25f, 0.15f, 0, 1.20f },
    { "KEYS",  "Wah Clav",         1, 0.00f,0.00f,0.03f,0.00f,1.0f, 0.0f, 0.0f,   600, 3.0f,  350, 0,  0,  0.0f,  0,   700, 0.25f, 150, 0.30f, 0.10f, 0.10f, 0, 1.00f },
    { "KEYS",  "Soft Celeste",     1, 0.00f,0.10f,0.00f,0.25f,4.0f, 0.0f, 0.0f,  4500, 0.6f,  400, 2,  1,  0.0f,  2,  1200, 0.20f, 800, 0.00f, 0.45f, 0.10f, 0, 1.20f },
    { "KEYS",  "Toy Keys",         1, 0.00f,0.00f,0.02f,0.45f,4.0f, 0.0f, 0.0f,  3000, 1.2f,  250, 2,  0,  0.0f,  0,   800, 0.15f, 350, 0.08f, 0.30f, 0.10f, 0, 1.10f },
    { "KEYS",  "Syn Accordion",        3, 0.40f,0.00f,0.00f,0.00f,1.0f, 5.5f, 8.0f,  2200, 0.3f,  500, 1,  0,  8.0f, 25,   200, 0.90f, 180, 0.05f, 0.20f, 0.00f, 0, 1.15f },
    { "KEYS",  "Syn Harmonium",        2, 0.30f,0.10f,0.05f,0.00f,1.0f, 0.0f, 0.0f,  1600, 0.2f,  600, 0,  0,  6.0f, 60,   300, 0.92f, 300, 0.05f, 0.30f, 0.00f, 0, 1.10f },
    { "KEYS",  "Full Organ",       5, 0.50f,0.60f,0.00f,0.00f,1.0f, 0.0f, 0.0f,  5000, 0.0f,   40, 1,  0,  7.0f, 10,   100, 1.00f, 450, 0.10f, 0.55f, 0.00f, 0, 1.40f },
    { "KEYS",  "Perc Organ",       1, 0.00f,0.50f,0.00f,0.20f,3.0f, 0.0f, 0.0f,  2000, 2.2f,   80, 2,  0,  0.0f,  0,   150, 0.85f, 120, 0.25f, 0.15f, 0.00f, 0, 1.00f },
    { "KEYS",  "Glass CP80",       2, 0.30f,0.00f,0.00f,0.20f,7.0f, 0.0f, 0.0f,  7500, 0.7f,  350, 0,  0,  4.0f,  0,  1100, 0.30f, 420, 0.08f, 0.30f, 0.10f, 0, 1.25f },
    { "KEYS",  "Suitcase EP",      1, 0.00f,0.05f,0.00f,0.22f,7.0f, 0.0f, 0.0f,  3200, 0.5f,  380, 2,  0,  0.0f,  3,   950, 0.35f, 380, 0.05f, 0.35f, 0.10f, 0, 1.15f },
    { "KEYS",  "Syn Harpsi",      2, 0.20f,0.00f,0.02f,0.00f,1.0f, 0.0f, 0.0f,  5200, 0.9f,  200, 0,  0,  4.0f,  0,   850, 0.40f,  90, 0.10f, 0.25f, 0.05f, 0, 1.10f },
    { "KEYS",  "Soul Keys",        1, 0.00f,0.05f,0.00f,0.35f,2.0f, 0.0f, 0.0f,  3800, 0.5f,  400, 3,  0,  5.0f,  2,   900, 0.30f, 350, 0.08f, 0.35f, 0.12f, 0, 1.2f },
    { "KEYS",  "K-RnB Keys",       2, 0.30f,0.00f,0.10f,0.25f,5.5f, 0.0f, 0.0f,  3000, 0.6f,  350, 2,  0,  6.0f,  3,   800, 0.35f, 320, 0.05f, 0.30f, 0.15f, 1, 1.25f },

    { "KEYS",  "Dream Keys",    3, 0.50f,0.05f,0.02f, 0.10f, 2.0f, 4.0f, 4.0f,  2000, 0.8f,  350, 3,  0,  8.0f,  15,  700, 0.55f, 600, 0.00f, 0.50f, 0.20f, 0, 1.3f },
    { "KEYS",  "Soft EP",       1, 0.00f,0.1f, 0.00f, 0.30f, 1.0f, 0.0f, 0.0f,  1600, 1.0f,  300, 2,  0,  4.0f,   8,  900, 0.40f, 500, 0.05f, 0.30f, 0.10f, 0, 1.1f },
    { "KEYS",  "Night Keys",    2, 0.30f,0.1f, 0.03f, 0.20f, 2.0f, 0.0f, 0.0f,  1400, 0.8f,  400, 3,  0,  6.0f,  12,  800, 0.45f, 550, 0.08f, 0.40f, 0.15f, 0, 1.2f },
    { "KEYS",  "Chord Keys",    4, 0.60f,0.05f,0.00f, 0.15f, 2.0f, 0.0f, 0.0f,  2400, 1.0f,  280, 0,  0, 12.0f,   5,  500, 0.50f, 350, 0.10f, 0.30f, 0.15f, 0, 1.3f },
    { "BELL",  "Glass Bell",       1, 0.0f, 0.0f, 0.0f, 0.85f,3.5f, 0.0f, 0.0f,  20000, 0.0f, 200, 2,  0,  4.0f,  2,   700, 0.15f, 800, 0.00f, 0.50f, 0.00f, 0, 1.3f },
    { "BELL",  "Deep Bell",        1, 0.0f, 0.0f, 0.0f, 0.90f,2.76f,0.0f, 0.0f,  20000, 0.0f, 200, 2,  0,  3.0f,  3,  1500, 0.00f, 1500, 0.00f, 0.60f, 0.00f, 0, 1.3f },
    { "BELL",  "Syn Celesta",          1, 0.0f, 0.0f, 0.0f, 0.35f,4.0f, 0.0f, 0.0f,  6000, 0.0f,  200, 2,  0,  2.0f,  0,   900, 0.00f, 600, 0.00f, 0.45f, 0.08f, 0, 1.2f },
    { "BELL",  "Syn Tubular",     1, 0.0f, 0.10f,0.02f,0.60f,3.5f, 0.0f, 0.0f,  4200, 1.2f,  900, 2,  0,  0.0f,  0,  1500, 0.00f, 1400, 0.00f, 0.55f, 0.10f, 0, 1.3f },
    { "BELL",  "Syn Gong",   1, 0.0f, 0.15f,0.02f,0.80f,2.7f, 0.0f, 0.0f,  2600, 1.0f, 1200, 2,  0,  0.0f,  0,  1600, 0.00f, 1600, 0.10f, 0.50f, 0.05f, 0, 1.2f },
    { "BELL",  "Syn Handbell",      1, 0.0f, 0.08f,0.02f,0.55f,3.0f, 0.0f, 0.0f,  5000, 1.2f,  700, 2,  0,  0.0f,  0,  1100, 0.00f, 1100, 0.00f, 0.45f, 0.05f, 0, 1.1f },
    { "BELL",  "Fairy Bell",     1, 0.0f, 0.10f,0.02f,0.65f,5.2f, 0.5f, 5.0f,  3000, 1.0f,  500, 2,  0,  0.0f,  0,   900, 0.00f, 1200, 0.00f, 0.70f, 0.30f, 1, 1.5f },
    { "BELL",  "Syn Church",    1, 0.0f, 0.15f,0.02f,0.75f,2.5f, 0.0f, 0.0f,  3600, 1.1f, 1400, 2,  0,  0.0f,  0,  1600, 0.00f, 1800, 0.05f, 0.65f, 0.10f, 0, 1.3f },
    { "BELL",  "Syn Vibes",     1, 0.0f, 0.12f,0.01f,0.35f,4.0f, 5.0f,10.0f,  3200, 0.8f,  600, 2,  0,  0.0f,  0,  1200, 0.00f,  900, 0.00f, 0.40f, 0.08f, 0, 1.2f },
    { "BELL",  "Syn Glock",   1, 0.0f, 0.05f,0.01f,0.60f,5.5f, 0.0f, 0.0f,  7500, 1.4f,  400, 2,  0,  0.0f,  0,   700, 0.00f,  700, 0.00f, 0.35f, 0.05f, 0, 1.0f },

    { "BELL",  "Crystal Bell",  1, 0.00f,0.0f, 0.00f, 0.55f, 7.0f, 0.0f, 0.0f,  8000, 0.0f,  200, 2,  1,  5.0f,   0,  900, 0.00f, 900, 0.00f, 0.55f, 0.25f, 0, 1.35f },
    { "BELL",  "Toy Bell",      1, 0.00f,0.0f, 0.02f, 0.45f, 4.0f, 0.0f, 0.0f,  6000, 0.0f,  200, 2,  1,  4.0f,   0,  500, 0.00f, 450, 0.00f, 0.35f, 0.15f, 0, 1.15f },
    { "BELL",  "Night Bell",    2, 0.30f,0.0f, 0.00f, 0.40f, 3.5f, 0.0f, 0.0f,  4000, 0.3f,  400, 2,  0,  7.0f,   5, 1100, 0.00f,1000, 0.00f, 0.60f, 0.30f, 1, 1.4f },
    { "MISC",  "Syn Flute",       1, 0.0f, 0.0f, 0.12f,0.0f, 2.0f, 5.0f, 10.0f, 4000, 0.0f,  200, 2,  1,  0.0f, 90,   200, 0.80f, 220, 0.00f, 0.35f, 0.00f, 0, 1.0f },
    { "MISC",  "Synth Brass",      3, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  1500, 1.5f,  300, 0,  0, 10.0f, 40,   200, 0.90f, 150, 0.20f, 0.20f, 0.00f, 0, 1.2f },
    { "MISC",  "Brass Stab",       5, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2000, 2.0f,  180, 0,  0, 12.0f,  3,   260, 0.25f, 140, 0.20f, 0.18f, 0.05f, 0, 1.25f },
    { "MISC",  "Rave Hit",         6, 0.7f, 0.40f,0.15f,0.10f,2.0f, 0.0f, 0.0f,  1500, 2.2f,  260, 0, -1, 15.0f,  0,   500, 0.00f, 500, 0.30f, 0.50f, 0.10f, 0, 1.5f },
    { "MISC",  "Noise Riser",      1, 0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f,   300, 4.0f, 2500, 2,  1,  0.0f, 500,  500, 1.00f, 500, 0.00f, 0.40f, 0.20f, 0, 1.5f },
    { "MISC",  "Cinema Braam",   5, 0.6f, 0.30f,0.05f,0.10f,2.0f, 0.0f, 0.0f,   700, 1.5f, 1500, 0, -1, 22.0f,200,  1500, 0.80f,  900, 0.50f, 0.60f, 0.10f, 0, 1.4f },
    { "MISC",  "Sub Drop",       1, 0.0f, 0.50f,0.02f,0.00f,2.0f, 0.0f, 0.0f,    80, 3.0f,  800, 2, -2,  0.0f,  0,  1200, 0.00f,  400, 0.20f, 0.10f, 0.00f, 0, 0.6f },
    { "MISC",  "Vinyl Keys",     2, 0.25f,0.10f,0.35f,0.20f,2.0f, 0.6f, 4.0f,  1600, 1.0f,  400, 3,  0,  5.0f, 10,   800, 0.40f,  500, 0.10f, 0.40f, 0.20f, 0, 1.1f },
    { "MISC",  "Tonal Wind",     3, 0.5f, 0.10f,0.70f,0.00f,2.0f, 0.3f, 8.0f,  1200, 0.5f, 1000, 2,  0, 12.0f,600,  1000, 0.90f, 1500, 0.05f, 0.80f, 0.10f, 0, 1.6f },
    { "MISC",  "Sci-Fi Sweep",   4, 0.5f, 0.10f,0.06f,0.00f,2.0f, 6.0f,20.0f,   300, 4.5f, 2500, 0,  0, 18.0f,100,  2000, 0.60f,  800, 0.25f, 0.50f, 0.40f, 1, 1.5f },
    
    { "MISC",  "Air Horn",      3, 0.40f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2500, 0.5f,  200, 0,  0, 16.0f,  10,  400, 0.90f, 300, 0.40f, 0.25f, 0.10f, 0, 1.2f },
    { "MISC",  "Horror Drone",  4, 0.80f,0.4f, 0.15f, 0.20f, 2.5f, 0.0f, 0.0f,   700, 0.3f, 1500, 0, -1, 22.0f, 800, 1500, 0.90f,1500, 0.20f, 0.70f, 0.20f, 0, 1.5f },
    { "MISC",  "Impact Hit",    2, 0.50f,0.6f, 0.30f, 0.00f, 2.0f, 0.0f, 0.0f,  1200, 1.8f,  300, 0, -1, 18.0f,   0,  900, 0.00f, 700, 0.30f, 0.55f, 0.10f, 0, 1.3f },};

    // 표 전체 크기 / 한 줄 크기 = 악기 개수(컴파일 타임 상수).
    constexpr int kNumInstruments = (int) (sizeof (kInstruments) / sizeof (kInstruments[0]));
}

// [defaultInstrumentIndex] 부팅 기본 음색("Supersaw Lead")을 이름으로 찾아 인덱스 반환.
int VocalChopAudioProcessor::defaultInstrumentIndex()
{
    // 표를 훑어 이름이 일치하는 첫 악기의 인덱스를 반환.
    for (int i = 0; i < kNumInstruments; ++i)
        if (juce::String (kInstruments[i].name) == "Supersaw Lead")
            return i;
    // 못 찾으면 0번(안전 기본값).
    return 0;
}

// [getInstrumentNames] 모든 악기 이름을 StringArray 로 모아 반환(UI 목록용).
juce::StringArray VocalChopAudioProcessor::getInstrumentNames()
{
    juce::StringArray names;
    // 범위 기반 for: 표의 각 줄 d 에서 이름을 추가.
    for (const auto& d : kInstruments)
        names.add (d.name);
    return names;
}

// [getInstrumentCategories] 위 이름과 1:1 로 짝지어진 카테고리 목록.
juce::StringArray VocalChopAudioProcessor::getInstrumentCategories()
{
    juce::StringArray cats;
    for (const auto& d : kInstruments)
        cats.add (d.category);
    return cats;
}

// [buildKitPieces] 드럼킷 선택 시 12반음 각각의 드럼 설정 스냅샷을 미리 만듦(메시지 스레드).
void VocalChopAudioProcessor::buildKitPieces()
{
    // C=kick up to B=cowbell; the layout repeats every octave.
    // C=킥부터 B=카우벨까지. 이 배치가 옥타브마다 반복됩니다.
    static const char* pieceNames[12] = {
        "Kick 808", "Kick Punch", "Snare 808", "Snare Tight", "Clap",
        "Hat Closed", "Hat Open", "Tom Low", "Tom High", "Rim Perc",
        "Shaker", "Cowbell 808" };

    // 이름→인덱스 조회를 위해 전체 악기 이름 목록을 얻고,
    const auto names = getInstrumentNames();
    // 라이브 패치 참조.
    auto& p = synthEngine.patch();

    // 12개 반음 자리 각각에 대응하는 드럼을 찾아 설정을 복사.
    for (int k = 0; k < 12; ++k)
    {
        // 이 자리의 드럼 이름을 인덱스로 변환. 없으면 건너뜀.
        const int idx = names.indexOf (pieceNames[k]);
        if (idx < 0)
            continue;

        applyEnginePatch (idx);   // writes the piece into the live patch...
        // 위 호출이 라이브 패치에 이 드럼 값을 씀. d = 표의 원본 줄, kp = 저장할 조각.
        const auto& d = kInstruments[idx];
        auto& kp = kitPieces[(size_t) k];

        // ...which we snapshot into plain floats the audio thread can use.
        // ...그 값을 오디오 스레드가 쓸 수 있는 '평범한 float'로 스냅샷 복사.
        //   (atomic 을 매번 읽지 않고, 발음 순간 이 복사본을 참조 → 경쟁 없이 빠름.)
        kp.unison  = p.unison.load();        kp.spread   = p.stereoSpread.load();
        kp.sub     = p.subLevel.load();      kp.noise    = p.noiseLevel.load();
        kp.fm      = p.fmAmount.load();      kp.fmRatio  = p.fmRatio.load();
        kp.vibHz   = p.vibRateHz.load();     kp.vibCents = p.vibDepthCents.load();
        kp.fltHz   = p.filterCutoff.load();  kp.fltEnvOct= p.filterEnvOct.load();
        kp.fltEnvMs= p.filterEnvMs.load();   kp.filterQ  = p.filterQ.load();
        kp.drift   = p.driftCents.load();    kp.velFlt   = p.velToFilterOct.load();
        kp.pitchEnvOct = p.pitchEnvOct.load();
        kp.pitchEnvMs  = p.pitchEnvMs.load();
        // 파형과, 조성과 무관하게 고정된 자연 드럼 음정(옥타브만 반영).
        kp.wave     = d.wave;
        kp.playNote = 60 + d.octave * 12;    // natural drum pitch, key-independent
        // ADSR 복사.
        kp.atk = d.atk; kp.dec = d.dec; kp.sus = d.sus; kp.rel = d.rel;
    }
}

// [kitNoteOn] 드럼 노트가 들어오면 해당 반음의 조각으로 발음(오디오 스레드).
void VocalChopAudioProcessor::kitNoteOn (int note, float velocity, bool tap)
{
    // Audio thread: atomic stores only, no allocations. The voice snapshots
    // everything at start, so each drum keeps its sound while others ring.
    // 오디오 스레드: atomic 저장만, 할당 없음. voice 는 시작 시 전부 복사해 두므로,
    //   여러 드럼이 동시에 울려도 각자 자기 소리를 유지합니다.
    // 노트를 12로 나눈 나머지로 반음 자리를 골라 미리 만든 조각을 참조.
    const auto& kp = kitPieces[(size_t) (((note % 12) + 12) % 12)];
    auto& p = synthEngine.patch();

    // 이 드럼 조각의 값들을 라이브 패치에 원자적으로 씀(store).
    p.unison.store (kp.unison);          p.stereoSpread.store (kp.spread);
    p.subLevel.store (kp.sub);           p.noiseLevel.store (kp.noise);
    p.fmAmount.store (kp.fm);            p.fmRatio.store (kp.fmRatio);
    p.vibRateHz.store (kp.vibHz);        p.vibDepthCents.store (kp.vibCents);
    p.filterCutoff.store (kp.fltHz);     p.filterEnvOct.store (kp.fltEnvOct);
    p.filterEnvMs.store (kp.fltEnvMs);   p.filterQ.store (kp.filterQ);
    p.driftCents.store (kp.drift);       p.velToFilterOct.store (kp.velFlt);
    p.pitchEnvOct.store (kp.pitchEnvOct);
    p.pitchEnvMs.store (kp.pitchEnvMs);

    // 파형과 ADSR 설정 후,
    synthEngine.setWave (kp.wave);
    synthEngine.setEnvelope (kp.atk, kp.dec, kp.sus, kp.rel);

    // 탭이면 짧게, 아니면 지속 노트온. kp.playNote 로 드럼 고유 음정 지정.
    if (tap) synthEngine.tapNote (note, velocity, kp.playNote);
    else     synthEngine.noteOn (note, velocity, kp.playNote);
}

// [applyEnginePatch] 악기의 '엔진 구조' 절반을 라이브 패치에 적용(메시지 스레드).
void VocalChopAudioProcessor::applyEnginePatch (int i)
{
    // Drum Kit: build the 12 per-key piece snapshots first (recursive calls
    // guarded), then fall through and apply this row's base patch normally.
    // 드럼킷이면 12조각을 먼저 만들고(재귀 호출은 buildingKit 플래그로 방지),
    //   그 다음 아래로 내려가 이 줄의 기본 패치를 평소처럼 적용.
    {
        // 인덱스를 범위 안으로 자른 뒤 그 악기 이름을 얻음.
        const juce::String nm (kInstruments[juce::jlimit (0, (int) (sizeof (kInstruments) / sizeof (kInstruments[0])) - 1, i)].name);
        // 킷 빌드 중 재진입이 아니면,
        if (! buildingKit)
        {
            // 드럼킷이면 12조각을 만들되, 재귀 방지 플래그로 감싸서 한 번만.
            if (nm == "Drum Kit")
            {
                buildingKit = true;
                buildKitPieces();
                buildingKit = false;
            }
            // 현재 드럼킷 모드인지 기록.
            kitMode.store (nm == "Drum Kit");
        }
    }

    // 인덱스를 안전 범위로 자르고 해당 악기 줄을 참조.
    i = juce::jlimit (0, kNumInstruments - 1, i);
    const auto& d = kInstruments[i];

    // 라이브 패치를 초기화한 뒤, 아래에서 이 악기 값으로 채움.
    auto& p = synthEngine.patch();
    p.resetToInit();
    // 표의 각 필드를 패치에 그대로 복사(엔진 구조 세팅).
    p.unison        = d.unison;
    p.stereoSpread  = d.spread;
    p.subLevel      = d.sub;
    p.noiseLevel    = d.noise;
    p.fmAmount      = d.fm;
    p.fmRatio       = d.fmRatio;
    // Keep a musical LFO rate even when the patch ships without vibrato, so
    // raising the Vibrato knob always does something.
    // 비브라토가 0인 음색이라도 LFO 속도는 음악적 기본(5Hz)으로 둠 → Vibrato 노브를
    //   올리면 항상 뭔가 반응하게. (조건 ? 참 : 거짓 삼항연산자.)
    p.vibRateHz     = d.vibHz > 0.01f ? d.vibHz : 5.0f;
    p.vibDepthCents = d.vibCents;
    p.filterCutoff  = d.fltHz;
    p.filterEnvOct  = d.fltEnvOct;
    p.filterEnvMs   = d.fltEnvMs;

    // --- Lushness settings by category, with a few name-specific accents ----
    // Chorus is computed into a local: the patch field is param-driven and
    // rewritten by the audio thread every block, so it can't be read back.
    // [카테고리별 풍성함 설정 + 이름별 미세조정] 코러스는 지역변수에 계산합니다.
    //   patch 의 코러스 필드는 매 블록 오디오 스레드가 파라미터로 덮어쓰므로 되읽을 수 없음.
    const juce::String cat (d.category);
    const juce::String name (d.name);
    float chorus = 0.12f;

    // 카테고리별로 코러스/드리프트/벨로시티→필터 특성을 다르게(악기 성격 부여).
    if (cat == "PAD")         { chorus = 0.50f; p.driftCents = 4.0f;  p.velToFilterOct = 0.4f; }
    else if (cat == "LEAD")   { chorus = 0.28f; p.driftCents = 3.0f;  p.velToFilterOct = 0.6f; }
    else if (cat == "SYNTH")  { chorus = 0.35f; p.driftCents = 3.5f;  p.velToFilterOct = 0.6f; }
    else if (cat == "PIANO")  { chorus = 0.06f; p.driftCents = 1.0f;  p.velToFilterOct = 1.4f; }
    else if (cat == "GUITAR") { chorus = 0.08f; p.driftCents = 1.0f;  p.velToFilterOct = 1.3f;
                                p.filterQ = 1.2f; }
    else if (cat == "KEYS")   { chorus = 0.25f; p.driftCents = 2.0f;  p.velToFilterOct = 1.1f; }
    else if (cat == "BELL")   { chorus = 0.22f; p.driftCents = 1.5f;  p.velToFilterOct = 0.8f; }
    else if (cat == "PLUCK")  { chorus = 0.18f; p.driftCents = 2.0f;  p.velToFilterOct = 1.2f;
                                p.filterQ = 1.5f; }
    else if (cat == "BASS")   { chorus = 0.0f;  p.driftCents = 1.5f;  p.velToFilterOct = 1.0f;
                                p.satAmount = 0.30f; }
    else if (cat == "DRUMS")  { chorus = 0.0f;  p.driftCents = 0.0f;  p.velToFilterOct = 1.2f;
                                p.satAmount = 0.25f; }
    else if (cat == "VOCAL")  { chorus = 0.45f; p.driftCents = 3.5f;  p.velToFilterOct = 0.4f;
                                p.filterQ = 1.6f; }   // resonance ~= formant vowel colour
    else if (cat == "HITS")   { chorus = 0.30f; p.driftCents = 3.0f;  p.velToFilterOct = 0.7f; }
    else /* MISC / INIT */    { chorus = 0.12f; p.driftCents = 2.5f;  p.velToFilterOct = 0.6f; }

    // 특정 악기 이름에만 적용하는 개별 강조(카테고리 기본값 위에 덧씀).
    if (name == "Acid Lead")     { p.filterQ = 5.5f; p.satAmount = 0.35f; }
    if (name == "Wobble Growl")  p.filterQ = 2.2f;
    if (name == "Neon Bass")     p.filterQ = 1.4f;
    if (name == "Supersaw Lead") chorus = 0.40f;
    if (name == "Chip Lead")     { chorus = 0.0f; p.driftCents = 0.0f; }
    if (name == "Sub 808")       { p.driftCents = 0.5f; chorus = 0.0f; }
    if (name == "Synth Brass")   chorus = 0.30f;
    if (name == "Syn Flute")    chorus = 0.20f;
    if (name == "Robot Vox")    { chorus = 0.10f; p.driftCents = 0.0f; }   // machines don't drift
    if (name.startsWith ("Beatbox")) { chorus = 0.0f; p.driftCents = 0.0f;
                                       p.velToFilterOct = 1.2f; }

    // Percussion pitch drops (the 808 "boo" and snare thwack).
    // 타악기의 '피치 드롭'(808 특유의 "부-", 스네어 타격감). 짧은 시간에 음정이 뚝 떨어짐.
    if (name == "Kick 808")    { p.pitchEnvOct = 2.2f; p.pitchEnvMs = 42.0f; }
    if (name == "Kick Punch")  { p.pitchEnvOct = 3.0f; p.pitchEnvMs = 26.0f; }
    if (name == "Snare 808")   { p.pitchEnvOct = 1.2f; p.pitchEnvMs = 34.0f; }
    if (name == "Snare Tight") { p.pitchEnvOct = 1.5f; p.pitchEnvMs = 22.0f; }
    if (name == "Rim Perc")    { p.pitchEnvOct = 1.8f; p.pitchEnvMs = 16.0f; }
    if (name == "Beatbox Kick")  { p.pitchEnvOct = 1.7f; p.pitchEnvMs = 55.0f; }   // the 'buh' drop
    if (name == "Beatbox Snare") { p.pitchEnvOct = 0.8f; p.pitchEnvMs = 40.0f; }
    if (name == "Tom Low")     { p.pitchEnvOct = 1.0f; p.pitchEnvMs = 70.0f; }
    if (name == "Tom High")    { p.pitchEnvOct = 1.0f; p.pitchEnvMs = 55.0f; }
    if (name == "Syn Conga")   { p.pitchEnvOct = 0.5f; p.pitchEnvMs = 28.0f; }
    if (name == "Clave")       { p.pitchEnvOct = 0.3f; p.pitchEnvMs = 10.0f; }
    if (name == "Chart 808")   { p.pitchEnvOct = 1.8f; p.pitchEnvMs = 50.0f; }
    if (name == "Log Drum")    { p.pitchEnvOct = 0.8f; p.pitchEnvMs = 60.0f; }
    if (name == "Drill 808")   { p.pitchEnvOct = 1.2f; p.pitchEnvMs = 80.0f; }
    if (name == "Slap House")  { p.pitchEnvOct = 0.6f; p.pitchEnvMs = 45.0f; }

    // Flagship grands: hammer chirp, hard velocity->brightness, no wobble.
    if (name == "Royal Grand" || name == "Concert Bright")
    {
        p.pitchEnvOct = 0.04f; p.pitchEnvMs = 6.0f;    // hammer strike transient
        p.velToFilterOct = 1.8f;                        // soft = felt, hard = glass
        p.driftCents = 0.6f;
        chorus = 0.04f;
        p.filterQ = 0.8f;
    }
    if (name == "Impact Hit")  { p.pitchEnvOct = 1.5f; p.pitchEnvMs = 150.0f; }

    // 계산한 코러스 값을 패치에 반영.
    p.chorusMix = chorus;

    // Publish the resolved module values for applyInstrument to mirror into
    // the knobs (race-free snapshot; see ModuleDefaults in the header).
    // 확정된 모듈 값을 moduleDefaults 에 저장 → applyInstrument 가 노브로 옮겨 씀
    //   (경쟁 없는 스냅샷; 헤더의 ModuleDefaults 설명 참고).
    moduleDefaults = { d.unison, d.spread, d.sub, d.noise, d.fm,
                       d.vibCents, chorus };

    // 현재 악기 인덱스 기록.
    currentInstrument = i;
}

// [applyInstrument] 악기 선택: 엔진 구조를 적용하고, 그 값을 화면 노브에도 반영.
void VocalChopAudioProcessor::applyInstrument (int instrumentIndex)
{
    // 먼저 엔진 구조 절반을 적용(kitMode/patch 세팅). 이 안에서 currentInstrument 도 갱신됨.
    applyEnginePatch (instrumentIndex);
    // 확정된 현재 악기의 표 줄을 참조.
    const auto& d = kInstruments[currentInstrument];

    // 파라미터 설정 헬퍼(앞서와 동일).
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Common baseline, then the instrument's knob values.
    // 공통 기본값부터 세팅(엔진=신스, 필터 열림, 믹스 100%, 피치/포먼트 중립).
    set ("engine",       1.0f);          // Synth mode
    set ("filterType",   0.0f);   set ("filterCutoff", 20000.0f);
    set ("filterReso",   0.707f);
    set ("mix",          1.0f);
    set ("pitch",        0.0f);   set ("formant", 0.0f);

    // 이어서 악기별 노브 값(파형/옥타브/디튠).
    set ("synthWave",    (float) d.wave);
    set ("synthOctave",  (float) d.octave);
    set ("synthDetune",  d.detune);

    // Mirror the resolved architecture into the module knobs (the knobs take
    // over from here, so every module stays hand-adjustable).
    // 확정된 구조 값을 모듈 노브에 반영. 이후부터는 노브가 주도권을 가져 손으로 조절 가능.
    set ("synthUnison",  (float) moduleDefaults.unison);
    set ("synthSpread",  moduleDefaults.spread);
    set ("synthSub",     moduleDefaults.sub);
    set ("synthNoise",   moduleDefaults.noise);
    set ("synthFM",      moduleDefaults.fm);
    set ("synthVibrato", juce::jlimit (0.0f, 1.0f, moduleDefaults.vibCents / 30.0f));
    set ("synthChorus",  moduleDefaults.chorus);
    set ("synthLfoAmt",  0.0f);   // motion is a per-user seasoning, not baked in
    set ("attack",       d.atk);
    set ("decay",        d.dec);
    set ("sustain",      d.sus);
    set ("release",      d.rel);
    set ("drive",        d.drive);
    set ("reverb",       d.reverb);
    set ("delay",        d.delay);
    set ("pingpong",     d.pingpong);
    set ("width",        d.width);
}

//==============================================================================
void VocalChopAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Full session state: parameters + sample path + slicing + theme, so that
    // reopening the project restores everything, not just the knobs.
    juce::XmlElement root ("VocalChopState");
    root.setAttribute ("samplePath",  loadedSampleFile.getFullPathName());
    root.setAttribute ("sfzPath",     loadedSfzFile.getFullPathName());
    root.setAttribute ("sliceMode",   (int) sliceEngine.getMode());
    root.setAttribute ("gridDiv",     sliceEngine.getGridDivision());
    root.setAttribute ("sensitivity", (double) sliceEngine.getSensitivity());
    root.setAttribute ("theme",       ThemeManager::current());
    // Like the instrument: the NAME is authoritative — the theme list grows
    // and indices drift across versions.
    root.setAttribute ("themeName",   ThemeManager::active().name);
    root.setAttribute ("instrument",  currentInstrument);
    // The name is authoritative across plugin versions — the table grows and
    // indices drift, but "Syn Grand" is forever.
    root.setAttribute ("instrumentName", getInstrumentNames()[currentInstrument]);

    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    copyXmlToBinary (root, destData);
}

void VocalChopAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    // Legacy format: bare parameter tree.
    if (xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        return;
    }

    if (! xml->hasTagName ("VocalChopState"))
        return;

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    // Prefer the saved theme NAME; sessions saved before the Studio themes
    // were prepended carry only an index into the OLD 7-theme list, which
    // now sits shifted by 8 (Neon Rider was 0, is 8).
    {
        int themeIdx = -1;
        const auto savedTheme = xml->getStringAttribute ("themeName");
        if (savedTheme.isNotEmpty())
        {
            const auto& list = ThemeManager::themes();
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].name == savedTheme)
                    { themeIdx = i; break; }
        }
        if (themeIdx < 0 && xml->hasAttribute ("theme"))
            themeIdx = xml->getIntAttribute ("theme")
                       + (savedTheme.isEmpty() ? 8 : 0);   // legacy index -> shifted list
        if (themeIdx >= 0)
            ThemeManager::setIndex (themeIdx);
    }

    // Restore the instrument's engine architecture only — the knob values come
    // from the restored parameter tree above, not the instrument defaults.
    // Prefer the saved NAME (indices drift as the table grows across
    // versions); fall back to the index for old sessions.
    {
        int idx = xml->getIntAttribute ("instrument", 0);
        const auto savedName = xml->getStringAttribute ("instrumentName");
        if (savedName.isNotEmpty())
        {
            const auto names = getInstrumentNames();
            const int byName = names.indexOf (savedName);
            if (byName >= 0)
                idx = byName;
        }
        applyEnginePatch (idx);
    }

    sliceEngine.setMode ((SliceEngine::Mode) xml->getIntAttribute ("sliceMode",
                                                                   (int) SliceEngine::Transient));
    sliceEngine.setGridDivision (xml->getIntAttribute ("gridDiv", 16));
    sliceEngine.setSensitivity ((float) xml->getDoubleAttribute ("sensitivity", 0.3));

    // Reload the sample from disk; loadSampleFromFile re-slices with the
    // settings restored above. If the file moved/was deleted, keep running
    // with no sample rather than failing state restore.
    const juce::File sample (xml->getStringAttribute ("samplePath"));
    if (sample.existsAsFile())
        loadSampleFromFile (sample, false);
    else
        sliceEngine.rebuildSlices();

    // Restore the SFZ bank (quietly skipped if the files moved).
    const juce::File sfz (xml->getStringAttribute ("sfzPath"));
    if (sfz.existsAsFile())
    {
        juce::String ignored;
        if (samplerEngine.loadSfz (sfz, ignored))
            loadedSfzFile = sfz;
    }

    // Let an open editor re-sync its combos / theme / cached waveform
    // (sendChangeMessage is async and safe from any thread).
    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* VocalChopAudioProcessor::createEditor()
{
    return new VocalChopAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalChopAudioProcessor();
}
