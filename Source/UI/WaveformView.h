#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
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
    void refresh();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void rebuildEnvelope();

    VocalChopAudioProcessor& proc;
    std::vector<float> minEnv, maxEnv;
    float phase = 0.0f;
    bool  fileHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
