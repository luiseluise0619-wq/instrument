// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 'LOOPER 탭' 화면이에요. RC-505 스타일의 다중 트랙 루퍼 UI로, 각 트랙의 녹음/오버덥/재생
// 버튼과 볼륨/팬/진행 링을 그립니다. 실제 루프 로직은 AudioEngine/LoopStation이 하고, 이 파일은
// 그걸 조작하는 '조종석'입니다(버튼 → LoopStation의 tap 명령). BPM 메트로놈도 여기서 제어합니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../AudioEngine/LoopStation.h"
#include <memory>

class VocalChopAudioProcessor;

/**
    The LOOPER tab: an RC-505-style multi-track looper for the plugin's
    output (4 tracks visible, expandable to 6 with + TRACK).

    Each track has its own instrument picker (chosen BEFORE recording, so a
    tap on REC drops you straight into the right sound), one big pad
    (record -> set length -> overdub/play), RE-record, UNDO for the last
    dub pass, mute, clear and volume, plus a progress ring. Track 1 defines
    the loop length; later tracks quantise to a multiple of it. A BPM
    metronome click (never recorded) keeps takes honest. The on-screen
    keyboard below stays live the whole time.
*/
// [클래스] LooperPanel — 화면 부품 + Timer(트랙 상태/진행 링 갱신용).
class LooperPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit LooperPanel (VocalChopAudioProcessor& processor);
    ~LooperPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    // 악기 콤보 채우기, 트랙 악기 적용, 보이는 트랙 수 갱신.
    void populateInstrumentBox (juce::ComboBox&, bool withQuickShelf);
    void applyTrackInstrument (int track);
    void updateTrackVisibility();

    VocalChopAudioProcessor& proc;

    // [구조체] TrackUI — 트랙 한 줄의 위젯 모음(악기 콤보·버튼들·팬/볼륨 슬라이더·진행 링 영역).
    struct TrackUI
    {
        juce::ComboBox   instBox;        // this track's pre-picked sound
        juce::TextButton mainButton  { "REC" };
        juce::TextButton rerecButton { "RE" };
        juce::TextButton undoButton  { "UNDO" };
        juce::TextButton clearButton { "X" };
        juce::TextButton muteButton  { "M" };
        juce::TextButton revButton   { "REV" };
        juce::Slider     panSlider;
        juce::Slider     volSlider;
        juce::Rectangle<int> ringArea;   // painted by the panel
        int chosenInstrument = -1;       // -1 = keep whatever is loaded
    };
    // 최대 트랙 수만큼 UI를 준비하고, 지금 몇 개 보일지.
    TrackUI trackUI[LoopStation::kNumTracks];
    int visibleTracks = 4;

    // 전체 재생/정지/삭제, 트랙 추가, 메트로놈/탭템포 버튼과 BPM 슬라이더.
    juce::TextButton playAllButton  { "PLAY ALL" };
    juce::TextButton stopAllButton  { "STOP ALL" };
    juce::TextButton clearAllButton { "CLEAR ALL" };
    juce::TextButton addTrackButton { "+ TRACK" };
    juce::TextButton metroButton    { "MET" };
    juce::TextButton tapButton      { "TAP" };
    juce::Slider     bpmSlider;
    // 탭 템포 계산용 상태(마지막 탭 시각·간격·횟수).
    double lastTapMs = 0.0;
    double tapIntervalMs = 0.0;
    int    tapCount = 0;

    // Pick the CURRENT sound without leaving the looper.
    // 루퍼를 벗어나지 않고 현재 소리를 고르는 콤보(엔진/악기)와 그 부착.
    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox instrumentBox;  // Featured + category submenus
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};
