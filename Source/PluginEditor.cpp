#include "PluginEditor.h"
#include "UI/ThemeManager.h"

namespace
{
    // Consistent outer margin / inner padding for the Apple-style layout.
    constexpr int kMargin  = 20;
    constexpr int kPadding = 18;
    constexpr int kGap     = 16;
}

//==============================================================================
VocalChopAudioProcessorEditor::VocalChopAudioProcessorEditor (VocalChopAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      sensitivityKnob ("Sensitivity"),
      waveform (p),
      sliceGrid (p),
      fxRack (p.getAPVTS())
{
    // Restyle all buttons / combos / menus with the shared Apple look.
    setLookAndFeel (&appleLaf);

    // --- Title ---
    titleLabel.setText ("VocalChop Studio", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Semibold")));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    // --- Theme selector ---
    int themeId = 1;
    for (const auto& t : ThemeManager::themes())
        themeBox.addItem (t.name, themeId++);
    themeBox.setSelectedId (ThemeManager::current() + 1, juce::dontSendNotification);
    themeBox.setJustificationType (juce::Justification::centred);
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
    sliceModeBox.setJustificationType (juce::Justification::centred);
    sliceModeBox.onChange = [this] { applySlicing(); };
    addAndMakeVisible (sliceModeBox);

    for (int div : { 4, 8, 16, 32 })
        gridBox.addItem (juce::String (div) + " slices", div);
    gridBox.setSelectedId (16, juce::dontSendNotification);
    gridBox.setJustificationType (juce::Justification::centred);
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
    setResizeLimits (820, 560, 1700, 1200);
    setSize (980, 640);

    refreshChildren();
}

VocalChopAudioProcessorEditor::~VocalChopAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

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
void VocalChopAudioProcessorEditor::drawCard (juce::Graphics& g,
                                              juce::Rectangle<float> bounds) const
{
    const auto& theme = ThemeManager::active();
    const float radius = theme.cornerRadius;

    // Soft drop shadow.
    {
        juce::DropShadow shadow (theme.shadow.withAlpha (0.35f), 18, { 0, 6 });
        juce::Path p;
        p.addRoundedRectangle (bounds, radius);
        shadow.drawForPath (g, p);
    }

    // Material fill.
    g.setColour (theme.material);
    g.fillRoundedRectangle (bounds, radius);

    // Hairline border.
    g.setColour (theme.separator);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

void VocalChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    // Backdrop gradient.
    juce::ColourGradient bg (theme.bgTop, 0.0f, 0.0f,
                             theme.bgBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // Live label colours.
    titleLabel.setColour (juce::Label::textColourId, theme.text);

    // Hairline under the toolbar.
    const auto full = getLocalBounds().reduced (kMargin, 0);
    const int toolbarBottom = kMargin + 34 + (kGap / 2);
    g.setColour (theme.separator);
    g.fillRect (full.getX(), toolbarBottom, full.getWidth(), 1);

    // Material cards.
    if (! sliceCardBounds.isEmpty())
        drawCard (g, sliceCardBounds.toFloat());
    if (! knobCardBounds.isEmpty())
        drawCard (g, knobCardBounds.toFloat());

    // Footer hint.
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("MIDI C3 = slice 1   •   drop audio onto the waveform",
                getLocalBounds().removeFromBottom (24).reduced (kMargin, 0),
                juce::Justification::centredRight);
}

void VocalChopAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin);

    // --- Top toolbar row ---
    auto top = area.removeFromTop (34);
    titleLabel.setBounds (top.removeFromLeft (300));
    themeBox.setBounds  (top.removeFromRight (150).withSizeKeepingCentre (150, 30));
    top.removeFromRight (10);
    loadButton.setBounds (top.removeFromRight (140).withSizeKeepingCentre (140, 30));

    area.removeFromTop (kGap);

    // --- Slice control card ---
    auto sliceCard = area.removeFromTop (94);
    sliceCardBounds = sliceCard;
    {
        auto inner = sliceCard.reduced (kPadding, kPadding - 4);
        sliceModeBox.setBounds (inner.removeFromLeft (150).withSizeKeepingCentre (150, 30));
        inner.removeFromLeft (kGap);
        gridBox.setBounds (inner.removeFromLeft (150).withSizeKeepingCentre (150, 30));
        inner.removeFromLeft (kGap);
        sensitivityKnob.setBounds (inner.removeFromLeft (96));
    }

    area.removeFromTop (kGap);

    // Reserve footer space.
    area.removeFromBottom (24 + kGap / 2);

    // --- Right column: FX rack ---
    auto right = area.removeFromRight (160);
    fxRack.setBounds (right);
    area.removeFromRight (kGap);

    // --- Waveform (upper section) ---
    waveform.setBounds (area.removeFromTop (juce::jmax (140, area.getHeight() / 3)));
    area.removeFromTop (kGap);

    // --- Knob row card ---
    auto knobCard = area.removeFromTop (128);
    knobCardBounds = knobCard;
    {
        auto knobRow = knobCard.reduced (kPadding, kPadding - 6);
        KnobComponent* knobs[] = { pitchKnob.get(), formantKnob.get(), mixKnob.get(),
                                   widthKnob.get(), grainKnob.get(), attackKnob.get() };
        const int numKnobs = (int) (sizeof (knobs) / sizeof (knobs[0]));
        const int knobW = knobRow.getWidth() / numKnobs;
        for (auto* k : knobs)
            if (k != nullptr)
                k->setBounds (knobRow.removeFromLeft (knobW).reduced (6, 0));
    }

    area.removeFromTop (kGap);

    // --- Slice pad grid fills the remainder ---
    sliceGrid.setBounds (area);
}
