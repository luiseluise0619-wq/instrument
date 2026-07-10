// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '동그란 노브(회전 다이얼)' UI 부품이에요. 소리를 만들진 않고, 사용자가 값을 돌려서
// 파라미터(음정·필터 등)를 조절하게 해줍니다. 내부에 juce::Slider를 감싸고 있어서
// SliderAttachment로 파라미터에 바로 연결됩니다. 색은 ThemeManager에서 실시간으로 읽어옵니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Apple / macOS-style rotary knob.

    A refined, flat rotary control: soft drop shadow, a base disc, a thin
    inactive track ring, an accent progress arc with rounded caps, and a small
    crisp indicator near the rim. A caption label sits below the dial. Wraps a
    juce::Slider so it plugs straight into an
    AudioProcessorValueTreeState::SliderAttachment.

    All colours are pulled live from ThemeManager::active() so switching themes
    restyles the control instantly.
*/
// [클래스] KnobComponent — 화면 부품(Component) + Timer(빛 잔상 애니메이션) + Slider::Listener(값 변화 감지).
class KnobComponent : public juce::Component,
                      private juce::Timer,
                      private juce::Slider::Listener
{
public:
    // 생성자: 다이얼 아래 붙일 캡션(이름)을 받음. explicit로 실수 변환 방지.
    explicit KnobComponent (const juce::String& caption);
    ~KnobComponent() override;

    // 내부 슬라이더 참조 반환(SliderAttachment 연결용).
    juce::Slider& getSlider() { return slider; }

    // 크기 변경 시 배치, 그리기.
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    // [내부 클래스] 노브의 실제 '생김새'를 그리는 룩앤필. LookAndFeel_V4를 상속해 회전 슬라이더 그리기를 재정의.
    class KnobLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        // [역할] drawRotarySlider — 원반/링/진행 아크/표시선을 그림. JUCE가 슬라이더를 그릴 때 호출.
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;
    };

    // Light-trail: turning the knob flares the glow, which then decays.
    // (Slider::Listener is used so clients remain free to set slider.onValueChange.)
    // 값이 바뀌면 잔상(glow)을 켜고, Timer로 서서히 줄임. (onValueChange 대신 Listener를 써서 외부가 onValueChange를 쓸 여지를 남김.)
    void sliderValueChanged (juce::Slider*) override;
    void timerCallback() override;

    // 실제 슬라이더, 캡션 라벨, 이 노브 전용 룩앤필, 현재 잔상 세기.
    juce::Slider slider;
    juce::Label  label;
    KnobLookAndFeel lookAndFeel;
    float dragGlow = 0.0f;

    // 복사 금지 + 누수 감지 매크로.
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};
