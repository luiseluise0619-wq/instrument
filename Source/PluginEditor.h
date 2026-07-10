/*  [초보자 안내]
    플러그인의 '얼굴'(화면 UI)을 선언하는 파일. 파형 뷰, 슬라이스 패드,
    노브, FX 랙 같은 화면 부품들이 여기 모여 있어요.
    UI는 '메시지 스레드'라는 느긋한 스레드에서 돌아갑니다 — 소리를 만드는
    오디오 스레드와는 완전히 다른 세계예요.
    그래서 UI가 소리 상태(재생 위치 등)를 알고 싶으면 직접 묻지 않고,
    Timer 로 주기적으로 atomic 값을 '훔쳐보는' 방식을 씁니다.
*/

// [JUCE 구조] 플러그인은 크게 둘로 나뉩니다.
//   · Processor(PluginProcessor): 소리를 '만드는' 뇌 — 오디오 스레드에서 processBlock 실행.
//   · Editor(이 파일): 소리를 '보여주고 조작하는' 얼굴 — 메시지 스레드에서 그림/입력 처리.
//   둘은 파라미터(APVTS)와 atomic 값으로만 소통합니다(직접 함수 호출로 오디오를 막으면 안 됨).

#pragma once

// JuceHeader: JUCE의 모든 모듈을 한 번에 가져오는 헤더.
#include <JuceHeader.h>
#include <vector>

// 이 에디터가 조작할 Processor와, 화면에 올릴 커스텀 UI 부품들.
#include "PluginProcessor.h"
#include "UI/WaveformView.h"
#include "UI/SliceGrid.h"
#include "UI/ChordBar.h"
#include "UI/FXRack.h"
#include "UI/KnobComponent.h"
#include "UI/MeterComponent.h"
#include "UI/LooperPanel.h"
#include "UI/UnlockPanel.h"
#include "UI/WelcomePanel.h"
#include "UI/AppleLookAndFeel.h"

//==============================================================================
// [클래스] 에디터. AudioProcessorEditor(화면 기본) + Timer(주기 콜백) + ChangeListener(상태 변화 수신)를 상속.
// [문법] private 상속 = Timer/ChangeListener의 기능은 내부에서만 쓰겠다는 뜻.
class VocalChopAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::ChangeListener
{
public:
    // [문법] explicit 생성자 = Processor 참조를 받아 에디터를 만듦. explicit는 실수로 자동 변환되는 걸 막음.
    explicit VocalChopAudioProcessorEditor (VocalChopAudioProcessor&);
    // 소멸자(override): 창이 닫힐 때 정리. Timer 정지 등.
    ~VocalChopAudioProcessorEditor() override;

    // [JUCE] paint=화면 그리기, resized=크기 바뀔 때 부품 배치. override=부모 함수를 재정의.
    void paint (juce::Graphics&) override;
    void resized() override;

    // Computer-keyboard playing (Z S X D C ... like FL Studio's typing keys).
    // 컴퓨터 키보드로 연주(FL Studio식 타이핑 키). 눌림/상태변화/마우스다운 처리.
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    // [문법] using = 긴 타입 이름에 짧은 별명 붙이기. APVTS의 부착(Attachment) 타입들.
    // Attachment = 노브/콤보/버튼 UI와 파라미터를 '자동으로 연결'해 주는 객체.
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // [도우미] addKnob — 노브 하나를 만들어 파라미터에 연결하고 화면에 추가.
    void addKnob (std::unique_ptr<KnobComponent>& knob,
                  const juce::String& paramID, const juce::String& caption);
    // 파일 열기 대화상자, 슬라이싱 적용, 슬라이스 콤보 동기화, 자식 갱신, 키 포커스 회수.
    void openFileChooser();
    void applySlicing();
    void syncSliceControls();   // reflect the engine's mode/grid in the combos
    void refreshChildren();
    void grabKeysSoon();        // return keyboard focus after combo popups

    /** Syncs held typing-key notes with the OS-global key state. Runs from
        keyStateChanged AND a watchdog timer, so a release that happens while
        focus is elsewhere (combo popup, other window) can never leave a note
        stuck on. */
    // [역할] scanTypingKeys — 눌린 타이핑 키 상태를 OS 전역 키 상태와 맞춤. 포커스가 딴 데 가도 음이 '눌린 채'로 안 남게.
    bool scanTypingKeys (bool forceReleaseAll = false);
    // [Timer] 주기적으로 불려 재생 위치·키 상태 등을 갱신.
    void timerCallback() override;

    /** Host restored our state (project revert, preset switch): re-sync the
        combos, theme and cached waveform that attachments don't cover. */
    // [역할] 호스트가 상태를 되돌리면(프로젝트 열기/프리셋 전환) attachment가 못 챙기는 것들을 다시 맞춤.
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Lays out and paints on a fixed kBaseW x kBaseH canvas; the whole
        canvas scales uniformly with the window (see content below). */
    // 고정 크기 캔버스에 배치/그림 → 창 크기에 맞춰 통째로 확대·축소(부품별로 찌그러지지 않음).
    void layoutContent();
    void paintContent (juce::Graphics&);

    // Draws a rounded "material" card with hairline border and soft shadow.
    // 둥근 카드(그림자+얇은 테두리)를 그리는 도우미.
    void drawCard (juce::Graphics&, juce::Rectangle<float> bounds) const;

    // Draws a small section caption above a card's content.
    // 카드 위 작은 제목(캡션)을 그리는 도우미.
    void drawCaption (juce::Graphics&, const juce::String& text,
                      juce::Rectangle<int> cardBounds) const;

    // [참조] 이 에디터가 조작하는 Processor(소리 뇌)에 대한 참조.
    VocalChopAudioProcessor& processor;

    // Shared Apple-style look for buttons / combos / menus.
    // 버튼/콤보/메뉴의 공통 애플풍 룩앤필.
    AppleLookAndFeel appleLaf;

    // Top bar.
    // 상단 바 위젯들(제목/부제/프리셋/테마/버튼들).
    juce::Label      titleLabel;
    juce::Label      subtitleLabel;
    juce::Label      presetLabel;
    juce::ComboBox   presetBox;
    juce::ComboBox   themeBox;
    juce::TextButton loadButton   { "Load Sample" };
    juce::TextButton demoButton   { "Demo" };
    juce::TextButton looperTabButton { "LOOPER" };
    bool             showLooper = false;

    // Slicing controls.
    // 슬라이싱 관련 콤보/노브(엔진 종류, 슬라이스 모드, 그리드, 파형, 감도).
    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox sliceModeBox;
    juce::ComboBox gridBox;
    juce::ComboBox synthWaveBox;   // Saw / Square / Sine / Triangle
    KnobComponent  sensitivityKnob { "Sensitivity" };

    // Octave shift for the whole synth (drives the synthOctave parameter).
    // 옥타브 -/+ 버튼과 라벨, 그 값을 파라미터에 연결하는 부착.
    juce::TextButton octDownButton { "-" };
    juce::TextButton octUpButton   { "+" };
    juce::Label      octLabel;
    std::unique_ptr<juce::ParameterAttachment> octAttachment;

    // Main / grouped knobs.
    // [메모리] 노브들은 unique_ptr로 소유(자동 삭제). 메인/그룹 노브 묶음들.
    std::unique_ptr<KnobComponent> pitchKnob, formantKnob, mixKnob, widthKnob,
                                   grainKnob, attackKnob, detuneKnob;
    std::unique_ptr<KnobComponent> decayKnob, sustainKnob, releaseKnob,
                                   filterCutoffKnob, filterResoKnob, outputGainKnob,
                                   grainMixKnob;

    // Synth module knobs (Serum-style architecture controls).
    // 신스 모듈 노브들(유니즌/스프레드/서브/노이즈/FM/비브라토/코러스/LFO/모션).
    std::unique_ptr<KnobComponent> unisonKnob, spreadKnob, subKnob, noiseKnob,
                                   fmKnob, vibratoKnob, chorusKnob,
                                   lfoRateKnob, motionKnob;

    // Performance macros (HYPE / SPACE / DIRT).
    // 한 노브로 여러 값을 한꺼번에 움직이는 '매크로' 노브들.
    std::unique_ptr<KnobComponent> hypeKnob, spaceKnob, dirtKnob;

    // Grouped choice / bool controls.
    // 필터 타입/재생 모드 콤보와 역재생/핑퐁 토글.
    juce::ComboBox   filterTypeBox;
    juce::ComboBox   playModeBox;
    juce::ToggleButton reverseButton  { "Reverse" };
    juce::ToggleButton pingpongButton { "Ping-Pong" };

    // Attachments (kept alive as members).
    // [중요] 부착들은 멤버로 계속 살아 있어야 연결이 유지됨(사라지면 노브↔파라미터 연결이 끊김).
    std::vector<std::unique_ptr<SliderAttachment>>   sliderAttachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>>   buttonAttachments;

    // Views.
    // 큰 시각 부품들: 파형/코드바/악기콤보/레벨미터/슬라이스패드/FX랙/루퍼/잠금패널.
    WaveformView   waveform;
    ChordBar       chordBar;
    juce::ComboBox instrumentBox;
    // 미터는 Processor의 출력 레벨(atomic 참조)을 받아 표시.
    MeterComponent meter { processor.getOutputLevelRef() };
    SliceGrid      sliceGrid;
    FXRack         fxRack;
    LooperPanel    looperPanel { processor };
    // 잠금 해제 패널: 입력받은 이메일/키를 Processor의 활성화 함수로 넘기는 콜백(람다)을 받음.
    UnlockPanel    unlockPanel { [this] (juce::String e, juce::String k)
                                 { return processor.finalizeActivation (e, k); } };
    juce::TextButton unlockButton { "UNLOCK" };

    // First-run quick start (re-openable from the toolbar "?").
    // 첫 실행 안내 패널과 툴바 '?' 버튼.
    WelcomePanel     welcomePanel;
    juce::TextButton helpButton { "?" };

    // Hover help on every major control.
    // 마우스를 올리면 뜨는 도움말(툴팁) 창.
    juce::TooltipWindow tooltipWindow { this, 700 };

    // Cached card rectangles (populated in resized(), painted in paint()).
    // 각 카드의 위치를 미리 계산해 저장(resized에서 채우고 paint에서 사용).
    juce::Rectangle<int> macroCardBounds;
    juce::Rectangle<int> sliceCardBounds;
    juce::Rectangle<int> envCardBounds;
    juce::Rectangle<int> toneCardBounds;
    juce::Rectangle<int> synthCardBounds;
    juce::Rectangle<int> filterCardBounds;
    juce::Rectangle<int> playbackCardBounds;

    // [메모리] 파일 선택 대화상자(비동기라 멤버로 살려둠).
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Computer-keyboard note state (one flag per mapped key).
    // 타이핑 연주 키가 눌려 있는지 표시하는 플래그 배열.
    std::array<bool, 32> typingKeyHeld {};

    // Cached Ocean Pluck scene (repainted only on resize / theme change).
    // 배경 그림 캐시(크기/테마가 바뀔 때만 다시 그림 → 매 프레임 낭비 방지).
    juce::Image backdropCache;
    int backdropTheme = -1;

    // Fixed-size design canvas, scaled as one unit so the window can shrink
    // to 60% without per-widget cramming. All children live inside it.
    // 고정 설계 캔버스 크기. 모든 자식이 이 안에 있고, 창은 이걸 통째로 확대/축소.
    static constexpr int kBaseW = 1080, kBaseH = 1060;
    // [내부 컴포넌트] ContentComp — 캔버스 역할. paint를 에디터의 paintContent로 위임.
    struct ContentComp : juce::Component
    {
        explicit ContentComp (VocalChopAudioProcessorEditor& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        VocalChopAudioProcessorEditor& owner;
    };
    ContentComp content { *this };

    // Hero motion FX: on glow themes every keypress fires neon speed lines
    // and a glow pulse across the artwork band — the "riding" illusion with
    // zero frame animation. Only heroRect repaints, and only while the
    // effect is alive, so knobs and audio never feel it.
    // [연출] 키를 누를 때 네온 속도선과 글로우 펄스를 그려 '달리는' 느낌. heroRect만 다시 그려 부담 최소화.
    struct SpeedLine { float x, y, len, speed, life; int hue; };
    std::vector<SpeedLine> speedLines;
    float heroGlow = 0.0f;
    juce::Rectangle<int> heroRect;
    juce::Random fxRng;
    void spawnHeroFx (float velocity);
    void drawHeroFx (juce::Graphics&);

    // [JUCE 매크로] 복사 금지 + 메모리 누수 감지기를 자동으로 붙임(디버그 안전장치).
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessorEditor)
};
