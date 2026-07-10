// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 화면 아래쪽 '피아노 건반(슬라이스 트리거)'이에요. 건반은 C3부터 시작하고 슬라이스와
// 1:1로 대응합니다(노트-C3 = 슬라이스 번호). 즉 클릭한 건반 = 재생되는 조각. 마우스/컴퓨터 키보드로
// 누르면 눌린 건반이 강조색으로 빛났다가 서서히 사라지고, 물방울 튀는 연출도 있습니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// [전방 선언] Processor는 이름만 필요(소리 트리거를 넘기려고).
class VocalChopAudioProcessor;

/**
    Piano-keyboard trigger view (replaces the old square pad grid).

    Keys start at C3 and map 1:1 onto slices, exactly like incoming MIDI
    (note - C3 = slice index), so what you click is what the piano roll plays.
    Keys beyond the current slice count are shown disabled. A pressed key
    lights up with the theme accent and decays smoothly.

    Public interface is kept from the pad-grid version so the editor and
    processor wiring are unchanged.
*/
// [클래스] SliceGrid — 화면 부품 + Timer(빛/물방울 애니메이션).
class SliceGrid : public juce::Component,
                  private juce::Timer
{
public:
    explicit SliceGrid (VocalChopAudioProcessor& processor);
    ~SliceGrid() override;

    // 그리기 + 마우스 이벤트들(누름/드래그/뗌/이동/벗어남).
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Call after slices change to refresh the keyboard. */
    // 슬라이스가 바뀌면 다시 그리기.
    void refresh() { repaint(); }

    /** Lights a key from outside (computer-keyboard playing). */
    // [역할] flashKey — 바깥(컴퓨터 키보드 연주)에서 특정 건반을 빛나게 함.
    void flashKey (int semitone, float strength);

    /** Water-drop splash: ripple ring + droplets bursting from a key. */
    // [역할] spawnSplash — 물방울 튀는 연출(파문 링 + 방울들) 생성.
    void spawnSplash (int semitone, juce::Point<float> at);

private:
    void timerCallback() override;

    // 그리기 도우미들: 검은건반 판정, 그릴 반음 개수, 건반 영역/사각형, 좌표→건반 변환.
    static bool isBlackKey (int semitone);
    int  keySpan() const;                 // how many semitones we draw
    juce::Rectangle<float> keysArea() const;
    juce::Rectangle<float> keyRect (int semitone, int span) const;
    int  keyAt (juce::Point<float> position) const;

    // [역할] pressKey — 특정 건반을 눌러 소리 냄 + 빛/물방울 시작.
    void pressKey (int key, juce::Point<float> position);

    // 소리를 낼 Processor 참조.
    VocalChopAudioProcessor& proc;
    // 건반별 빛 감쇠 레벨, 현재 마우스가 올라간 건반, 마우스로 누른 건반(게이트).
    std::vector<float> keyFlash;          // per-key decay level
    int hoveredKey = -1;
    int pressedKey = -1;                  // key held by the mouse (gate)

    // Water-splash particles (rings expand, droplets arc under gravity).
    // [구조체] Drop — 물방울/파문 입자 하나(위치·속도·수명·크기, ring이면 파문 링).
    struct Drop
    {
        float x, y, vx, vy, life, size;
        bool  ring;
    };
    std::vector<Drop> drops;
    unsigned int splashSeed = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceGrid)
};
