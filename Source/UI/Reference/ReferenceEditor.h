#pragma once
#include <JuceHeader.h>
#include <memory>
class VocalChopAudioProcessor;

/** Dedicated reference editor. This, not the legacy PluginEditor, is the
    processor's default editor. Static reference-derived material is a skin;
    ALL controls, labels, audio displays and notes are rendered and hit-tested
    by real JUCE Components above it. */
class SlyceReferenceEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SlyceReferenceEditor (VocalChopAudioProcessor&);
    ~SlyceReferenceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void setReferenceDark (bool); // also used by the real-JUCE screenshot tool
    void setReferenceTheme (int); // 0 Light, 1 Noir, 2 Mint, 3 Rose, 4 Amber, 5 Azure, 6 Lavender, 7 Champagne
private:
    class Surface;
    std::unique_ptr<Surface> surface;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlyceReferenceEditor)
};
