#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class VocalChopAudioProcessor;

/**
    Draws the loaded sample as 74 mirrored bars inside a rounded "material"
    card in the Apple / macOS-iOS visual style (design spec 4.3).

    Visual treatment:
      - 74 rounded, centre-mirrored bars with a small gap, filled with a
        vertical gradient: accent on the centre line, a computed darker accent
        (accent 76% + black) at the tips
      - optional soft glow behind the bars (driven by Theme::glow)
      - whisper-faint horizontal guide lines for a precision-instrument look
      - an engine-gated accent badge, engine-gated hint line, and file-name /
        "Normalised" chips along the bottom
      - slice-boundary markers with rounded nubs and numbered lane tabs -
        CHOP MODE ONLY, at most twelve lanes
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

    /** Fired after a dropped file loads, so the editor can refresh the
        keyboard and slice controls too. */
    std::function<void()> onSampleDropped;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Sample editing: drag a selection on the waveform, then TRIM / CUT /
    // FADE / NORM / UNDO via the small overlay buttons.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void rebuildEnvelope();
    void layoutEditButtons();
    void updateEditButtons();
    void applyEdit (int op);            // 0 Trim, 1 Cut, 2 Fade, 3 Norm, 4 Undo
    float fracAt (float x) const;       // pixel -> 0..1 sample position
    bool  editableNow() const;          // sample graph visible & sample loaded

    VocalChopAudioProcessor& proc;
    std::vector<float> minEnv, maxEnv;
    float phase = 0.0f;
    bool  fileHover = false;

    // Slice markers are draggable and the waveform auditions on click. The
    // markers were previously the one thing on screen that looked adjustable
    // and was not.
    int  markerNear (float x) const;    // index of a marker within grab range, or -1
    void addMarkerAt (float frac);      // double-click: cut here
    void removeMarker (int index);      // double-click ON a marker: uncut
    int  sliceAtFrac (float frac) const;
    void moveMarker (int index, float frac);
    float xOfFrac (float frac) const;

    int  dragMarker = -1;               // marker being dragged (-1 = none)
    int  hoverMarker = -1;
    bool didDragMarker = false;

    float selA = -1.0f, selB = -1.0f;   // selection fractions (-1 = none)
    juce::TextButton trimBtn { "TRIM" }, cutBtn { "CUT" }, fadeBtn { "FADE" },
                     normBtn { "NORM" }, undoEditBtn { "UNDO" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
