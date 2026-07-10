// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '파형 뷰'예요. 로드한 샘플의 파형을 카드 안에 부드럽게 그립니다. 슬라이스 경계선,
// 실시간 재생선(playhead), 빈 상태 안내, 그리고 오디오 파일 드래그&드롭으로 새 샘플 로드까지 담당.
// [성능] 파형 전체를 매번 그리면 무거우므로, 최소/최대 포락선(envelope)을 미리 계산해 캐시하고
// 크기 변경/새로고침 때만 다시 만듭니다.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class VocalChopAudioProcessor;

/**
    Draws the loaded sample as a smooth filled waveform inside a rounded
    "material" card in the Apple / macOS-iOS visual style.

    Visual treatment:
      - vertical gradient fill that blooms at the peaks and fades toward the
        centre line, with a crisp 1px top-contour stroke
      - optional neon glow around the contour (driven by Theme::glow)
      - a glassy low-alpha reflection of the min-envelope below the centre
      - whisper-faint horizontal guide lines for a precision-instrument look
      - slice-boundary markers with rounded nubs (soft glow dot when glowing)
      - live playheads (accent line + triangle) read from the voice pool

    Shows an empty-state prompt with an SF-symbol style glyph and accepts
    drag-and-drop of audio files to load a new sample.

    The min/max envelope is cached from the loaded sample and rebuilt on resize
    or refresh. A restrained Timer drives a near-zero, subtle animation.
*/
// [클래스] WaveformView — 화면 부품 + 파일 드래그 대상(FileDragAndDropTarget) + Timer(재생선 갱신).
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit WaveformView (VocalChopAudioProcessor& processor);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Recomputes the cached min/max envelope from the current sample. */
    // [역할] refresh — 현재 샘플에서 최소/최대 포락선을 다시 계산(캐시 갱신).
    void refresh();

    /** Fired after a dropped file loads, so the editor can refresh the
        keyboard and slice controls too. */
    // [콜백] 파일이 드롭돼 로드된 뒤 실행할 함수(에디터가 건반/슬라이스도 갱신하도록). std::function=함수를 담는 변수.
    std::function<void()> onSampleDropped;

    // 파일 드래그 관련: 관심 있는 파일인지, 들어옴/나감/드롭됨 처리.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    // 샘플에서 포락선(min/max)을 다시 만드는 내부 함수.
    void rebuildEnvelope();

    // Processor 참조, 캐시된 포락선, 미세 애니메이션 위상, 파일 호버 여부.
    VocalChopAudioProcessor& proc;
    std::vector<float> minEnv, maxEnv;
    float phase = 0.0f;
    bool  fileHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
