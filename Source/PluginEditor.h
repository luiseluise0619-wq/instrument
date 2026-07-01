#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "UI/WaveformView.h"
#include "UI/SliceGrid.h"
#include "UI/FXRack.h"
#include "UI/KnobComponent.h"

//==============================================================================
class VocalChopAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalChopAudioProcessorEditor (VocalChopAudioProcessor&);
    ~VocalChopAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void addKnob (std::unique_ptr<KnobComponent>& knob,
                  const juce::String& paramID, const juce::String& caption);
    void openFileChooser();
    void applySlicing();
    void refreshChildren();
    void styleComboBox (juce::ComboBox&);

    VocalChopAudioProcessor& processor;

    // Top bar.
    juce::Label    titleLabel;
    juce::ComboBox themeBox;
    juce::TextButton loadButton { "Load Sample" };

    // Slicing controls.
    juce::ComboBox sliceModeBox;
    juce::ComboBox gridBox;
    KnobComponent  sensitivityKnob { "Sensitivity" };

    // Main knobs.
    std::unique_ptr<KnobComponent> pitchKnob, formantKnob, mixKnob, widthKnob,
                                   grainKnob, attackKnob;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;

    // Views.
    WaveformView waveform;
    SliceGrid    sliceGrid;
    FXRack       fxRack;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessorEditor)
};
