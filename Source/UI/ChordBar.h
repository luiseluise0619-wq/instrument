#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>

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
class ChordBar : public juce::Component,
                 private juce::Timer
{
public:
    explicit ChordBar (VocalChopAudioProcessor& processor);
    ~ChordBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Updates the caption with the detected key ("CHORDS - Am"); call after
        a sample loads. Chord playback transposes into this key. */
    void refreshKeyLabel();

private:
    struct Chord
    {
        const char* name;
        std::vector<int> semis;   // semitone offsets from C3 (= slice indices)
    };
    using Progression = std::vector<Chord>;

    struct Style
    {
        const char* name;
        std::vector<Progression> progressions;
    };

    static const std::vector<Style>& styles();

    void regenerate();
    void playChord (int buttonIndex);
    void timerCallback() override;   // decays the click-flash pulses

    VocalChopAudioProcessor& proc;

    juce::Label      caption;
    juce::ComboBox   styleBox;
    juce::TextButton genButton { "Generate" };
    std::array<juce::TextButton, 4> chordButtons;

    Progression current;
    int lastPick = -1;
    juce::Random rng;

    std::array<float, 4> flashLevels {};   // per-chord-button neon flash (0..1)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordBar)
};
