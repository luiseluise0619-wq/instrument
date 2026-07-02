#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

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
class SliceGrid : public juce::Component,
                  private juce::Timer
{
public:
    explicit SliceGrid (VocalChopAudioProcessor& processor);
    ~SliceGrid() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Call after slices change to refresh the keyboard. */
    void refresh() { repaint(); }

    /** Lights a key from outside (computer-keyboard playing). */
    void flashKey (int semitone, float strength);

    /** Water-drop splash: ripple ring + droplets bursting from a key. */
    void spawnSplash (int semitone, juce::Point<float> at);

private:
    void timerCallback() override;

    static bool isBlackKey (int semitone);
    int  keySpan() const;                 // how many semitones we draw
    juce::Rectangle<float> keysArea() const;
    juce::Rectangle<float> keyRect (int semitone, int span) const;
    int  keyAt (juce::Point<float> position) const;

    void pressKey (int key, juce::Point<float> position);

    VocalChopAudioProcessor& proc;
    std::vector<float> keyFlash;          // per-key decay level
    int hoveredKey = -1;
    int pressedKey = -1;                  // key held by the mouse (gate)

    // Water-splash particles (rings expand, droplets arc under gravity).
    struct Drop
    {
        float x, y, vx, vy, life, size;
        bool  ring;
    };
    std::vector<Drop> drops;
    unsigned int splashSeed = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceGrid)
};
