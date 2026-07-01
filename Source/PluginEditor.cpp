#include "PluginEditor.h"
#include "UI/ThemeManager.h"

//==============================================================================
VocalChopAudioProcessorEditor::VocalChopAudioProcessorEditor (VocalChopAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      sensitivityKnob ("Sensitivity"),
      waveform (p),
      sliceGrid (p),
      fxRack (p.getAPVTS())
{
    // --- Title ---
    titleLabel.setText ("VocalChop Studio", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (24.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    // --- Theme selector ---
    int themeId = 1;
    for (const auto& t : ThemeManager::themes())
        themeBox.addItem (t.name, themeId++);
    themeBox.setSelectedId (ThemeManager::current() + 1, juce::dontSendNotification);
    styleComboBox (themeBox);
    themeBox.onChange = [this]
    {
        ThemeManager::setIndex (themeBox.getSelectedId() - 1);
        refreshChildren();
        repaint();
    };
    addAndMakeVisible (themeBox);

    // --- Load button ---
    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    // --- Slice mode ---
    sliceModeBox.addItem ("Transient", 1);
    sliceModeBox.addItem ("Grid", 2);
    sliceModeBox.setSelectedId (1, juce::dontSendNotification);
    styleComboBox (sliceModeBox);
    sliceModeBox.onChange = [this] { applySlicing(); };
    addAndMakeVisible (sliceModeBox);

    for (int div : { 4, 8, 16, 32 })
        gridBox.addItem (juce::String (div) + " slices", div);
    gridBox.setSelectedId (16, juce::dontSendNotification);
    styleComboBox (gridBox);
    gridBox.onChange = [this] { applySlicing(); };
    addAndMakeVisible (gridBox);

    sensitivityKnob.getSlider().setRange (0.05, 0.9, 0.01);
    sensitivityKnob.getSlider().setValue (0.3, juce::dontSendNotification);
    sensitivityKnob.getSlider().onValueChange = [this] { applySlicing(); };
    addAndMakeVisible (sensitivityKnob);

    // --- Main knobs ---
    addKnob (pitchKnob,   "pitch",     "Pitch");
    addKnob (formantKnob, "formant",   "Formant");
    addKnob (mixKnob,     "mix",       "Mix");
    addKnob (widthKnob,   "width",     "Width");
    addKnob (grainKnob,   "grainSize", "Grain");
    addKnob (attackKnob,  "attack",    "Attack");

    // --- Views ---
    addAndMakeVisible (waveform);
    addAndMakeVisible (sliceGrid);
    addAndMakeVisible (fxRack);

    setResizable (true, true);
    setResizeLimits (760, 520, 1600, 1100);
    setSize (960, 620);

    refreshChildren();
}

VocalChopAudioProcessorEditor::~VocalChopAudioProcessorEditor() = default;

//==============================================================================
void VocalChopAudioProcessorEditor::addKnob (std::unique_ptr<KnobComponent>& knob,
                                             const juce::String& paramID,
                                             const juce::String& caption)
{
    knob = std::make_unique<KnobComponent> (caption);
    attachments.push_back (std::make_unique<SliderAttachment> (
        processor.getAPVTS(), paramID, knob->getSlider()));
    addAndMakeVisible (*knob);
}

void VocalChopAudioProcessorEditor::styleComboBox (juce::ComboBox& box)
{
    box.setJustificationType (juce::Justification::centred);
}

void VocalChopAudioProcessorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Select an audio file to chop",
        juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile() && processor.loadSampleFromFile (file))
        {
            applySlicing();
            refreshChildren();
        }
    });
}

void VocalChopAudioProcessorEditor::applySlicing()
{
    auto& engine = processor.getSliceEngine();
    engine.setMode (sliceModeBox.getSelectedId() == 2 ? SliceEngine::Grid
                                                      : SliceEngine::Transient);
    engine.setGridDivision (gridBox.getSelectedId());
    engine.setSensitivity ((float) sensitivityKnob.getSlider().getValue());
    engine.rebuildSlices();
    refreshChildren();
}

void VocalChopAudioProcessorEditor::refreshChildren()
{
    waveform.refresh();
    sliceGrid.refresh();
    repaint();
}

//==============================================================================
void VocalChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    juce::ColourGradient bg (theme.bgTop, 0, 0,
                             theme.bgBottom, 0, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    titleLabel.setColour (juce::Label::textColourId, theme.text);

    g.setColour (theme.text.withAlpha (0.6f));
    g.setFont (12.0f);
    g.drawText ("MIDI C3 = slice 1   •   drop audio onto the waveform",
                getLocalBounds().removeFromBottom (20).reduced (12, 0),
                juce::Justification::centredRight);
}

void VocalChopAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    // Top bar.
    auto top = area.removeFromTop (36);
    titleLabel.setBounds (top.removeFromLeft (260));
    themeBox.setBounds (top.removeFromRight (140).reduced (2));
    loadButton.setBounds (top.removeFromRight (130).reduced (2));

    area.removeFromTop (8);

    // Slice control strip.
    auto sliceBar = area.removeFromTop (70);
    sliceModeBox.setBounds (sliceBar.removeFromLeft (130).reduced (4).withSizeKeepingCentre (122, 26));
    gridBox.setBounds (sliceBar.removeFromLeft (130).reduced (4).withSizeKeepingCentre (122, 26));
    sensitivityKnob.setBounds (sliceBar.removeFromLeft (80).reduced (2));

    area.removeFromTop (8);

    // Right column: FX rack.
    auto right = area.removeFromRight (140);
    fxRack.setBounds (right);
    area.removeFromRight (10);

    // Waveform occupies the upper portion.
    waveform.setBounds (area.removeFromTop (juce::jmax (120, area.getHeight() / 3)));
    area.removeFromTop (10);

    // Knob row.
    auto knobRow = area.removeFromTop (100);
    KnobComponent* knobs[] = { pitchKnob.get(), formantKnob.get(), mixKnob.get(),
                               widthKnob.get(), grainKnob.get(), attackKnob.get() };
    const int numKnobs = (int) (sizeof (knobs) / sizeof (knobs[0]));
    const int knobW = knobRow.getWidth() / numKnobs;
    for (auto* k : knobs)
        if (k != nullptr)
            k->setBounds (knobRow.removeFromLeft (knobW).reduced (4));

    area.removeFromTop (10);

    // Slice pad grid fills the remainder.
    sliceGrid.setBounds (area);
}
