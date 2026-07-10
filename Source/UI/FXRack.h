// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 화면 오른쪽의 'FX 랙'(세로로 쌓인 이펙트 노브 묶음)이에요. Drive/Reverb/Delay의
// '양(amount)' 노브를 세로로 배치합니다. 실제 이펙트 처리 순서는 오디오 엔진(FXChain)이 고정으로
// 정하고, 이 랙은 그 값을 조절하는 '조종석'일 뿐입니다.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "KnobComponent.h"
#include <memory>
#include <vector>

/**
    Vertical rack of FX modules (Drive / Reverb / Delay), each a captioned knob
    bound to its parameter. The processing order in the audio engine is fixed
    (distortion → reverb → delay); this rack is the UI for their amounts.
*/
// [클래스] FXRack — 화면 부품. 여러 이펙트 노브(모듈)를 담아 세로로 그림.
class FXRack : public juce::Component
{
public:
    // 생성자: 파라미터 저장소(APVTS)를 받아 각 노브를 해당 파라미터에 연결.
    explicit FXRack (juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // [내부 구조체] Module — 노브 하나 + 그 노브를 파라미터에 잇는 부착. 둘 다 unique_ptr로 소유.
    struct Module
    {
        std::unique_ptr<KnobComponent> knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // [도우미] addModule — 노브 하나 만들고 파라미터에 연결해 목록에 추가.
    void addModule (juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const juce::String& caption);

    // 이 랙이 담고 있는 모듈들.
    std::vector<Module> modules;

    // Layout metrics (kept in sync between resized() and paint()).
    // 배치 상수: 상단 'FX' 제목 높이, 모듈 사이 간격. resized()와 paint()가 같은 값을 씀.
    static constexpr int titleStrip = 26; // reserved height for the "FX" title
    static constexpr int moduleGap  = 16; // vertical space between modules / divider

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXRack)
};
