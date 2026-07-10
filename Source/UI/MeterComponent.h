// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '출력 레벨 미터'(세로 막대)예요. 소리의 현재 음량을 눈으로 보여줍니다.
// 중요한 점: 이 부품은 소리를 만들지도, 값을 쓰지도 않아요. Processor가 atomic에 써 둔 레벨을
// 30~60Hz 타이머로 '읽기만' 합니다(오디오↔UI 안전 통신의 전형). 그리고 미터답게 빠르게 올라가고
// 천천히 내려오는 '탄도(ballistics)'와, 최고점을 잠깐 잡아두는 피크홀드를 표시합니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeManager.h"
#include <atomic>

/**
    A vertical bar output meter in the house Apple / macOS style.

    Reads an external linear level atomic (0..1, already peak-ish) that is
    written from elsewhere (e.g. the processor). This component never owns nor
    writes the atomic; it only reads it on a ~30 Hz timer and applies meter
    ballistics (fast attack, slow release) plus a slowly falling peak-hold
    marker. All colours are pulled live from ThemeManager::active() in paint()
    so a theme switch restyles it instantly.
*/
// [클래스] MeterComponent — 화면 부품 + Timer(주기적 갱신).
class MeterComponent : public juce::Component,
                       private juce::Timer
{
public:
    // 생성자: 읽어올 레벨 atomic의 '참조'를 받음(소유 아님, 읽기 전용).
    explicit MeterComponent (std::atomic<float>& levelSource);
    ~MeterComponent() override;

    // 막대/피크홀드/캡션을 그림.
    void paint (juce::Graphics& g) override;

private:
    // 주기 콜백: 레벨을 읽어 탄도/피크홀드 갱신 후 다시 그림.
    void timerCallback() override;

    // [스레드] 외부(Processor)가 쓰는 레벨 소스. 여기선 읽기만.
    std::atomic<float>& level;   // external source, read-only

    // 화면에 보이는 부드러운 막대 값과, 천천히 떨어지는 피크 표시(둘 다 0~1).
    float displayed = 0.0f;      // ballistic-smoothed bar value (0..1)
    float peakHold   = 0.0f;     // slowly-falling peak marker (0..1)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterComponent)
};
