#include "PluginEditor.h"
#include "UI/ThemeManager.h"

namespace
{
    // Consistent outer margin / inner padding for the Apple-style layout.
    constexpr int kMargin  = 22;
    constexpr int kPadding = 18;
    constexpr int kGap     = 16;

    // Toolbar / caption metrics.
    constexpr int kToolbarH   = 34;
    constexpr int kCaptionH   = 20;
    constexpr int kMeterW     = 30;

    // Restrained backdrop for full-glow themes: an Apple-dark canvas with two
    // very soft colour blooms (cyan top-left, pink top-right). No grid, no
    // horizon — the glow lives in the controls, not the wallpaper.
    void drawSynthwaveBackdrop (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const juce::Colour cyan (0xff00f5ff);
        const juce::Colour pink (0xffff2daa);

        {
            juce::ColourGradient bloom (cyan.withAlpha (0.10f),
                                        area.getX() + area.getWidth() * 0.18f,
                                        area.getY(),
                                        juce::Colours::transparentBlack,
                                        area.getX() + area.getWidth() * 0.18f,
                                        area.getY() + area.getHeight() * 0.55f, true);
            g.setGradientFill (bloom);
            g.fillRect (area);
        }
        {
            juce::ColourGradient bloom (pink.withAlpha (0.07f),
                                        area.getRight() - area.getWidth() * 0.15f,
                                        area.getY(),
                                        juce::Colours::transparentBlack,
                                        area.getRight() - area.getWidth() * 0.15f,
                                        area.getY() + area.getHeight() * 0.5f, true);
            g.setGradientFill (bloom);
            g.fillRect (area);
        }
    }
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

    // --- Preset menu (NOT an APVTS param) ---
    presetLabel.setText ("Preset", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (presetLabel);

    {
        int presetId = 1;
        for (const auto& name : VocalChopAudioProcessor::getPresetNames())
            presetBox.addItem (name, presetId++);
        presetBox.setSelectedId (1, juce::dontSendNotification);
        presetBox.setJustificationType (juce::Justification::centred);
        presetBox.onChange = [this]
        {
            processor.applyPreset (presetBox.getSelectedId() - 1);
            refreshChildren();
        };
        addAndMakeVisible (presetBox);
    }

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

    // --- A/B compare + randomise ---
    abButton.onClick = [this]
    {
        processor.toggleAB();
        abButton.setButtonText (processor.isSlotB() ? "B" : "A");
        repaint();
    };
    addAndMakeVisible (abButton);

    randomButton.onClick = [this] { processor.randomizeParams(); };
    addAndMakeVisible (randomButton);

    // --- Slice mode (initialised from the engine so restored state shows) ---
    auto& engine = processor.getSliceEngine();

    sliceModeBox.addItem ("Transient", 1);
    sliceModeBox.addItem ("Grid", 2);
    sliceModeBox.setSelectedId (engine.getMode() == SliceEngine::Grid ? 2 : 1,
                                juce::dontSendNotification);
    sliceModeBox.setJustificationType (juce::Justification::centred);
    sliceModeBox.onChange = [this] { applySlicing(); };
    addAndMakeVisible (sliceModeBox);

    for (int div : { 4, 8, 16, 32 })
        gridBox.addItem (juce::String (div) + " slices", div);
    const int restoredDiv = engine.getGridDivision();
    gridBox.setSelectedId ((restoredDiv == 4 || restoredDiv == 8
                            || restoredDiv == 16 || restoredDiv == 32) ? restoredDiv : 16,
                           juce::dontSendNotification);
    gridBox.setJustificationType (juce::Justification::centred);
    gridBox.onChange = [this] { applySlicing(); };
    addAndMakeVisible (gridBox);

    sensitivityKnob.getSlider().setRange (0.05, 0.9, 0.01);
    sensitivityKnob.getSlider().setValue (engine.getSensitivity(), juce::dontSendNotification);
    sensitivityKnob.getSlider().onValueChange = [this] { applySlicing(); };
    addAndMakeVisible (sensitivityKnob);

    // --- Envelope knobs ---
    addKnob (attackKnob,  "attack",  "Attack");
    addKnob (decayKnob,   "decay",   "Decay");
    addKnob (sustainKnob, "sustain", "Sustain");
    addKnob (releaseKnob, "release", "Release");

    // --- Pitch / Tone knobs ---
    addKnob (pitchKnob,   "pitch",     "Pitch");
    addKnob (formantKnob, "formant",   "Formant");
    addKnob (mixKnob,     "mix",       "Mix");
    addKnob (widthKnob,   "width",     "Width");
    addKnob (grainKnob,   "grainSize", "Grain");

    // --- Filter knobs + combo ---
    addKnob (filterCutoffKnob, "filterCutoff", "Cutoff");
    addKnob (filterResoKnob,   "filterReso",   "Reso");

    filterTypeBox.addItem ("Off",       1);
    filterTypeBox.addItem ("Low Pass",  2);
    filterTypeBox.addItem ("High Pass", 3);
    filterTypeBox.addItem ("Band Pass", 4);
    filterTypeBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (filterTypeBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "filterType", filterTypeBox));

    // --- Playback: toggles, play mode combo, output gain knob ---
    addAndMakeVisible (reverseButton);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (
        processor.getAPVTS(), "reverse", reverseButton));

    addAndMakeVisible (pingpongButton);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (
        processor.getAPVTS(), "pingpong", pingpongButton));

    playModeBox.addItem ("Gate",     1);
    playModeBox.addItem ("One-Shot", 2);
    playModeBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (playModeBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "playMode", playModeBox));

    addKnob (outputGainKnob, "outputGain", "Output");

    // --- Views ---
    addAndMakeVisible (waveform);
    addAndMakeVisible (meter);
    addAndMakeVisible (sliceGrid);
    addAndMakeVisible (fxRack);

    setResizable (true, true);
    setResizeLimits (900, 640, 1800, 1300);
    setSize (1060, 760);

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
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (
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

void VocalChopAudioProcessorEditor::drawCaption (juce::Graphics& g,
                                                 const juce::String& text,
                                                 juce::Rectangle<int> cardBounds) const
{
    if (cardBounds.isEmpty())
        return;

    const auto& theme = ThemeManager::active();
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Semibold")));

    auto strip = cardBounds.reduced (kPadding, 0)
                           .removeFromTop (kCaptionH + 6)
                           .withTrimmedTop (6);
    g.drawText (text.toUpperCase(), strip, juce::Justification::centredLeft);
}

void VocalChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    // Backdrop gradient.
    juce::ColourGradient bg (theme.bgTop, 0.0f, 0.0f,
                             theme.bgBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // Full-glow themes get the cyber-synthwave scene on top of the gradient.
    if (theme.glow >= 0.9f)
        drawSynthwaveBackdrop (g, getLocalBounds().toFloat());

    // Live label colours.
    titleLabel.setColour (juce::Label::textColourId, theme.text);
    presetLabel.setColour (juce::Label::textColourId, theme.textSecondary);

    // Hairline under the toolbar.
    const auto full = getLocalBounds().reduced (kMargin, 0);
    const int toolbarBottom = kMargin + kToolbarH + (kGap / 2);
    g.setColour (theme.separator);
    g.fillRect (full.getX(), toolbarBottom, full.getWidth(), 1);

    // Material cards.
    drawCard (g, sliceCardBounds.toFloat());
    drawCard (g, envCardBounds.toFloat());
    drawCard (g, toneCardBounds.toFloat());
    drawCard (g, filterCardBounds.toFloat());
    drawCard (g, playbackCardBounds.toFloat());

    // Section captions.
    drawCaption (g, "Envelope",   envCardBounds);
    drawCaption (g, "Pitch / Tone", toneCardBounds);
    drawCaption (g, "Filter",     filterCardBounds);
    drawCaption (g, "Playback",   playbackCardBounds);

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
    auto top = area.removeFromTop (kToolbarH);
    titleLabel.setBounds (top.removeFromLeft (300));

    // Right-aligned: theme, load, RND, A/B, preset combo, preset label.
    themeBox.setBounds (top.removeFromRight (150).withSizeKeepingCentre (150, 30));
    top.removeFromRight (kGap / 2);
    loadButton.setBounds (top.removeFromRight (130).withSizeKeepingCentre (130, 30));
    top.removeFromRight (kGap / 2);
    randomButton.setBounds (top.removeFromRight (56).withSizeKeepingCentre (56, 30));
    top.removeFromRight (kGap / 2);
    abButton.setBounds (top.removeFromRight (56).withSizeKeepingCentre (56, 30));
    top.removeFromRight (kGap / 2);
    presetBox.setBounds (top.removeFromRight (160).withSizeKeepingCentre (160, 30));
    presetLabel.setBounds (top.removeFromRight (56).withSizeKeepingCentre (56, 30));

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

    // --- Waveform row: waveform + meter column on the right ---
    auto waveRow = area.removeFromTop (juce::jmax (150, area.getHeight() * 30 / 100));
    meter.setBounds (waveRow.removeFromRight (kMeterW));
    waveRow.removeFromRight (kGap);
    waveform.setBounds (waveRow);

    area.removeFromTop (kGap);

    // --- Slice pad grid along the bottom ---
    auto sliceGridRow = area.removeFromBottom (juce::jmax (120, area.getHeight() * 34 / 100));
    sliceGrid.setBounds (sliceGridRow);
    area.removeFromBottom (kGap);

    // --- Controls area: cards row (grouped) + FX rack side column ---
    auto controls = area;

    // FX rack as a side column on the right.
    auto fxCol = controls.removeFromRight (160);
    fxRack.setBounds (fxCol);
    controls.removeFromRight (kGap);

    // Remaining width split into four labelled cards.
    // Layout: [Envelope | Pitch/Tone] top row, [Filter | Playback] bottom row.
    const int rowGap = kGap;
    auto topCards    = controls.removeFromTop ((controls.getHeight() - rowGap) / 2);
    controls.removeFromTop (rowGap);
    auto bottomCards = controls;

    auto layoutKnobRow = [] (juce::Rectangle<int> card, std::vector<KnobComponent*> knobs)
    {
        auto inner = card.reduced (kPadding, kPadding - 4);
        inner.removeFromTop (kCaptionH);        // room for the caption
        if (knobs.empty())
            return inner;
        const int w = inner.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
            if (k != nullptr)
                k->setBounds (inner.removeFromLeft (w).reduced (6, 0));
        return inner;
    };

    // Top row: Envelope | Pitch/Tone.
    {
        const int gap = kGap;
        // Give Pitch/Tone a bit more width (5 knobs vs 4).
        auto envCard  = topCards.removeFromLeft ((topCards.getWidth() - gap) * 44 / 100);
        envCardBounds = envCard;
        topCards.removeFromLeft (gap);
        auto toneCard  = topCards;
        toneCardBounds = toneCard;

        layoutKnobRow (envCard,  { attackKnob.get(), decayKnob.get(),
                                   sustainKnob.get(), releaseKnob.get() });
        layoutKnobRow (toneCard, { pitchKnob.get(), formantKnob.get(), mixKnob.get(),
                                   widthKnob.get(), grainKnob.get() });
    }

    // Bottom row: Filter | Playback.
    {
        const int gap = kGap;
        auto filterCard  = bottomCards.removeFromLeft ((bottomCards.getWidth() - gap) * 42 / 100);
        filterCardBounds = filterCard;
        bottomCards.removeFromLeft (gap);
        auto playbackCard  = bottomCards;
        playbackCardBounds = playbackCard;

        // Filter: two knobs then the type combo underneath.
        {
            auto inner = filterCard.reduced (kPadding, kPadding - 4);
            inner.removeFromTop (kCaptionH);
            auto comboRow = inner.removeFromBottom (36);
            filterTypeBox.setBounds (comboRow.withSizeKeepingCentre (
                juce::jmin (220, comboRow.getWidth()), 30));
            inner.removeFromBottom (kGap / 2);

            KnobComponent* fk[] = { filterCutoffKnob.get(), filterResoKnob.get() };
            const int w = inner.getWidth() / 2;
            for (auto* k : fk)
                if (k != nullptr)
                    k->setBounds (inner.removeFromLeft (w).reduced (6, 0));
        }

        // Playback: toggles + play mode combo on the left, output knob on the right.
        {
            auto inner = playbackCard.reduced (kPadding, kPadding - 4);
            inner.removeFromTop (kCaptionH);

            auto knobCol = inner.removeFromRight (juce::jmin (96, inner.getWidth() / 3));
            if (outputGainKnob != nullptr)
                outputGainKnob->setBounds (knobCol.reduced (6, 0));
            inner.removeFromRight (kGap);

            auto controlsCol = inner;
            const int rowH = 30;
            reverseButton.setBounds  (controlsCol.removeFromTop (rowH));
            controlsCol.removeFromTop (kGap / 2);
            pingpongButton.setBounds (controlsCol.removeFromTop (rowH));
            controlsCol.removeFromTop (kGap / 2);
            playModeBox.setBounds    (controlsCol.removeFromTop (rowH)
                                          .withSizeKeepingCentre (
                                              juce::jmin (200, controlsCol.getWidth()), 30));
        }
    }
}
