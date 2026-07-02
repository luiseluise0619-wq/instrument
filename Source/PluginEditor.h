#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "UI/WaveformView.h"
#include "UI/SliceGrid.h"
#include "UI/ChordBar.h"
#include "UI/FXRack.h"
#include "UI/KnobComponent.h"
#include "UI/MeterComponent.h"
#include "UI/AppleLookAndFeel.h"

//==============================================================================
class VocalChopAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalChopAudioProcessorEditor (VocalChopAudioProcessor&);
    ~VocalChopAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Computer-keyboard playing (Z S X D C ... like FL Studio's typing keys).
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void addKnob (std::unique_ptr<KnobComponent>& knob,
                  const juce::String& paramID, const juce::String& caption);
    void openFileChooser();
    void applySlicing();
    void syncSliceControls();   // reflect the engine's mode/grid in the combos
    void refreshChildren();
    void grabKeysSoon();        // return keyboard focus after combo popups

    // Draws a rounded "material" card with hairline border and soft shadow.
    void drawCard (juce::Graphics&, juce::Rectangle<float> bounds) const;

    // Draws a small section caption above a card's content.
    void drawCaption (juce::Graphics&, const juce::String& text,
                      juce::Rectangle<int> cardBounds) const;

    VocalChopAudioProcessor& processor;

    // Shared Apple-style look for buttons / combos / menus.
    AppleLookAndFeel appleLaf;

    // Top bar.
    juce::Label      titleLabel;
    juce::Label      presetLabel;
    juce::ComboBox   presetBox;
    juce::ComboBox   themeBox;
    juce::TextButton loadButton   { "Load Sample" };
    juce::TextButton demoButton   { "Demo" };
    juce::TextButton abButton     { "A/B" };
    juce::TextButton randomButton { "RND" };

    // Slicing controls.
    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox sliceModeBox;
    juce::ComboBox gridBox;
    juce::ComboBox synthWaveBox;   // Saw / Square / Sine / Triangle
    KnobComponent  sensitivityKnob { "Sensitivity" };

    // Main / grouped knobs.
    std::unique_ptr<KnobComponent> pitchKnob, formantKnob, mixKnob, widthKnob,
                                   grainKnob, attackKnob, detuneKnob;
    std::unique_ptr<KnobComponent> decayKnob, sustainKnob, releaseKnob,
                                   filterCutoffKnob, filterResoKnob, outputGainKnob;

    // Grouped choice / bool controls.
    juce::ComboBox   filterTypeBox;
    juce::ComboBox   playModeBox;
    juce::ToggleButton reverseButton  { "Reverse" };
    juce::ToggleButton pingpongButton { "Ping-Pong" };

    // Attachments (kept alive as members).
    std::vector<std::unique_ptr<SliderAttachment>>   sliderAttachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>>   buttonAttachments;

    // Views.
    WaveformView   waveform;
    ChordBar       chordBar;
    juce::ComboBox instrumentBox;
    MeterComponent meter { processor.getOutputLevelRef() };
    SliceGrid      sliceGrid;
    FXRack         fxRack;

    // Cached card rectangles (populated in resized(), painted in paint()).
    juce::Rectangle<int> sliceCardBounds;
    juce::Rectangle<int> envCardBounds;
    juce::Rectangle<int> toneCardBounds;
    juce::Rectangle<int> filterCardBounds;
    juce::Rectangle<int> playbackCardBounds;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Computer-keyboard note state (one flag per mapped key).
    std::array<bool, 32> typingKeyHeld {};

    // Cached Ocean Pluck scene (repainted only on resize / theme change).
    juce::Image backdropCache;
    int backdropTheme = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessorEditor)
};
