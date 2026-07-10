// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '코드(화음) 추천 바'예요. 스타일(K-Pop, EDM, Lo-Fi...)을 고르고 Generate를 누르면
// 내장 라이브러리에서 4개짜리 코드 진행을 채워 줍니다. 코드 버튼을 누르면 그 화음의 음들이
// 현재 엔진(신스/찹)으로 소리 납니다. 실제 소리는 Processor의 '락 없는' 경로로 넘어갑니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>

// [전방 선언] 실제 정의는 다른 파일에. 여기선 참조만 필요해 이름만 알려줌(컴파일 빠르게).
class VocalChopAudioProcessor;

/**
    Style-based chord suggester.

    Pick a style (K-Pop, EDM, Lo-Fi, R&B, Ballad, City Pop), hit Generate, and
    the bar fills with a 4-chord progression drawn from a curated built-in
    library (several progressions are stored per style — Generate cycles
    through them randomly). Clicking a chord button plays the whole chord
    through the active engine: in Synth mode the notes sound as synth voices,
    in Chop mode each chord tone triggers the matching slice (C3-based, the
    same mapping as the keyboard and MIDI).

    On glow themes, clicking a chord button kicks off a short neon flash that
    decays on a timer (the shared look-and-feel reads it from the button's
    "neonFlash" component property).
*/
// [클래스] ChordBar — 화면 부품 + Timer(클릭 플래시 감쇠용).
class ChordBar : public juce::Component,
                 private juce::Timer
{
public:
    // 생성자: 소리를 낼 Processor 참조를 받음.
    explicit ChordBar (VocalChopAudioProcessor& processor);
    ~ChordBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Updates the caption with the detected key ("CHORDS - Am"); call after
        a sample loads. Chord playback transposes into this key. */
    // [역할] refreshKeyLabel — 감지된 조(key)를 캡션에 표시. 코드 재생은 이 조로 옮겨(transpose) 재생됨.
    void refreshKeyLabel();

private:
    // [구조체] Chord — 코드 하나: 이름 + C3 기준 반음 오프셋 목록(찹 모드에선 슬라이스 번호로도 쓰임).
    struct Chord
    {
        const char* name;
        std::vector<int> semis;   // semitone offsets from C3 (= slice indices)
    };
    // 코드 진행 = 코드들의 목록.
    using Progression = std::vector<Chord>;

    // [구조체] Style — 스타일 하나: 이름 + 여러 코드 진행.
    struct Style
    {
        const char* name;
        std::vector<Progression> progressions;
    };

    // 준비된 스타일 목록 반환(정적).
    static const std::vector<Style>& styles();

    // 새 진행 뽑기, 코드 재생, 플래시 감쇠 타이머.
    void regenerate();
    void playChord (int buttonIndex);
    void timerCallback() override;   // decays the click-flash pulses

    // 소리를 낼 Processor 참조.
    VocalChopAudioProcessor& proc;

    // 캡션/스타일 콤보/생성 버튼/코드 버튼 4개.
    juce::Label      caption;
    juce::ComboBox   styleBox;
    juce::TextButton genButton { "Generate" };
    std::array<juce::TextButton, 4> chordButtons;

    // 현재 표시 중인 진행과, 직전에 뽑은 인덱스(연속 중복 방지), 난수기.
    Progression current;
    int lastPick = -1;
    juce::Random rng;

    // 코드 버튼별 네온 플래시 세기(0~1).
    std::array<float, 4> flashLevels {};   // per-chord-button neon flash (0..1)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordBar)
};
