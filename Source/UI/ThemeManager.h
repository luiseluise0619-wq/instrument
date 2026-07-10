// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 소리가 아니라 '색(테마)'을 담당해요. 앱 전체의 배경/카드/강조색 같은 디자인 값을
// 한곳(Theme 구조체)에 모아 두고, 모든 UI 부품이 여기서 색을 읽어가 화면을 일관되게 유지합니다.
// macOS/iOS 감성의 '디자인 토큰' 모음이라고 보면 됩니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Apple-style design tokens.

    One theme = one cohesive palette in the spirit of macOS / iOS: a soft
    backdrop gradient, translucent "material" cards, hairline separators, a
    single system accent colour, and a restrained (often zero) glow. Components
    read these tokens so the whole UI stays visually consistent.
*/
// [구조체] Theme — 테마 하나 = 한 벌의 색/모양 값 묶음.
struct Theme
{
    // 테마 이름과 어두운 테마 여부.
    juce::String name;
    bool  dark = true;

    // 창 배경 그라디언트(위/아래 색).
    juce::Colour bgTop, bgBottom;     // window backdrop gradient
    // 반투명 카드 채움색(유리 느낌)과 좀 더 불투명한 버전.
    juce::Colour material;            // translucent card fill (vibrancy)
    juce::Colour materialStrong;      // more opaque card fill
    // 얇은 구분선/테두리 색.
    juce::Colour separator;           // hairline dividers / borders

    // 노브/패드 기본 채움색과 비활성 트랙(링) 색.
    juce::Colour control;             // knob / pad base fill
    juce::Colour controlTrack;        // inactive ring / track

    // 시스템 강조색과 그 연한(투명도 낮춘) 버전.
    juce::Colour accent;              // the single system accent
    juce::Colour accentSoft;          // accent at low alpha (fills, glows)

    // 파형 색, 기본/보조 글자색, 그림자색.
    juce::Colour waveform;
    juce::Colour text;                // primary label
    juce::Colour textSecondary;       // secondary / muted label
    juce::Colour shadow;              // drop-shadow colour

    // 카드 모서리 둥근 정도와 글로우 세기(0=평평한 애플룩, >0=은은한 빛번짐).
    float cornerRadius = 12.0f;
    float glow = 0.0f;                // 0 = flat Apple look, >0 = subtle bloom
};

// [클래스] ThemeManager — 테마 목록과 '현재 선택된 테마'를 정적(static)으로 관리. 전역 한 벌.
class ThemeManager
{
public:
    // 준비된 테마 개수.
    static constexpr int kNumThemes = 15;

    // 전체 목록, 현재 인덱스 조회/설정, 현재 활성 테마 참조.
    static const std::array<Theme, kNumThemes>& themes() { return all; }
    static int  current()          { return idx; }
    static void setIndex (int i)   { idx = juce::jlimit (0, kNumThemes - 1, i); }
    static const Theme& active()   { return all[(size_t) idx]; }

private:
    // [정적 멤버] 실제 테마 데이터와 현재 인덱스. 정의는 .cpp에 있음(정적 멤버는 한 곳에서 정의).
    static std::array<Theme, kNumThemes> all;
    static int idx;
};
