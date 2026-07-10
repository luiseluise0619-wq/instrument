// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 소리가 아니라 '버튼·콤보·메뉴의 생김새'를 담당하는 룩앤필(LookAndFeel)이에요.
// JUCE에서 룩앤필 = "이 위젯을 어떻게 그릴지" 규칙 모음. 여기에 macOS/iOS 느낌(둥근 채움, 얇은 테두리,
// 하나의 강조색, 깔끔한 글꼴)을 정의해 두면 앱의 모든 버튼/콤보가 그 스타일로 그려집니다.
// 색은 활성 Theme에서 실시간으로 읽어와, 테마를 바꾸면 즉시 다시 스타일링됩니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Shared look-and-feel that gives buttons, combo boxes and menus a macOS / iOS
    feel: rounded "material" fills, hairline borders, a single accent colour,
    and clean SF-style typography. All colours are pulled live from the active
    Theme, so switching themes restyles everything without re-instantiating.
*/
// [클래스] AppleLookAndFeel — JUCE 기본 룩앤펠(V4)을 상속해 그리기 함수들을 우리 스타일로 재정의(override).
class AppleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppleLookAndFeel();

    // 버튼 글꼴/배경/글자 그리기 재정의.
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    // 콤보박스 그리기/글꼴/텍스트 위치 재정의.
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    // 팝업 메뉴 배경/글꼴/항목 그리기 재정의.
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
};
