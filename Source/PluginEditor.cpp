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

    // Cheap deterministic pseudo-random for the starfield (no <random> on
    // the paint path, same sky every frame).
    float hash01 (int n)
    {
        unsigned int u = (unsigned int) n;
        u = (u << 13) ^ u;
        u = u * (u * u * 15731u + 789221u) + 1376312589u;
        return (float) (u & 0x7fffffffu) / (float) 0x7fffffff;
    }

    // A stylised leaping dolphin in unit space (0..1, facing right, y down).
    juce::Path makeDolphinPath()
    {
        juce::Path p;

        // Body: tail joint -> back -> nose -> belly -> tail joint.
        p.startNewSubPath (0.10f, 0.64f);
        p.cubicTo (0.14f, 0.30f, 0.34f, 0.02f, 0.62f, 0.02f);   // back rising
        p.cubicTo (0.76f, 0.02f, 0.90f, 0.16f, 0.97f, 0.30f);   // head to nose
        p.cubicTo (0.88f, 0.38f, 0.74f, 0.46f, 0.58f, 0.50f);   // jaw / chest
        p.cubicTo (0.42f, 0.55f, 0.24f, 0.62f, 0.14f, 0.70f);   // belly to tail
        p.closeSubPath();

        // Tail flukes.
        p.startNewSubPath (0.12f, 0.62f);
        p.cubicTo (0.06f, 0.68f, 0.02f, 0.78f, 0.00f, 0.90f);   // lower fluke
        p.cubicTo (0.06f, 0.82f, 0.09f, 0.78f, 0.13f, 0.76f);   // notch
        p.cubicTo (0.16f, 0.82f, 0.20f, 0.86f, 0.26f, 0.88f);   // upper fluke
        p.cubicTo (0.22f, 0.78f, 0.18f, 0.70f, 0.16f, 0.64f);
        p.closeSubPath();

        // Dorsal fin.
        p.startNewSubPath (0.48f, 0.06f);
        p.cubicTo (0.50f, -0.08f, 0.56f, -0.14f, 0.64f, -0.16f);
        p.cubicTo (0.60f, -0.06f, 0.60f, 0.00f, 0.62f, 0.03f);
        p.closeSubPath();

        // Pectoral fin.
        p.startNewSubPath (0.56f, 0.40f);
        p.cubicTo (0.52f, 0.50f, 0.50f, 0.58f, 0.50f, 0.66f);
        p.cubicTo (0.56f, 0.58f, 0.62f, 0.50f, 0.66f, 0.44f);
        p.closeSubPath();

        return p;
    }

    void drawNeonDolphin (juce::Graphics& g, juce::Rectangle<float> box,
                          float angleRadians, bool flipped)
    {
        auto p = makeDolphinPath();

        auto t = juce::AffineTransform::translation (-0.5f, -0.5f)
                     .scaled (flipped ? -box.getWidth() : box.getWidth(),
                              box.getHeight())
                     .rotated (angleRadians)
                     .translated (box.getCentreX(), box.getCentreY());
        p.applyTransform (t);

        const juce::Colour cyan    (0xff00f5ff);
        const juce::Colour magenta (0xffff2daa);
        const auto pb = p.getBounds();

        // Dark glass body so the neon edge pops.
        {
            juce::ColourGradient body (juce::Colour (0xff141a3e), pb.getX(), pb.getY(),
                                       juce::Colour (0xff2a1050), pb.getRight(), pb.getBottom(),
                                       false);
            g.setGradientFill (body);
            g.fillPath (p);
        }

        // Neon rim: wide soft glow passes, then a crisp gradient edge.
        juce::ColourGradient rim (cyan, pb.getX(), pb.getY(),
                                  magenta, pb.getRight(), pb.getBottom(), false);

        g.setGradientFill (rim);
        g.setOpacity (0.08f);
        g.strokePath (p, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved));
        g.setOpacity (0.16f);
        g.strokePath (p, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved));
        g.setOpacity (0.90f);
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
        g.setOpacity (1.0f);
    }

    // The full-bleed "Ocean Pluck" scene for the default glow theme: night
    // sky, stars, neon mountains, a sun ring, the perspective sea grid and
    // two leaping neon dolphins. Painted once into a cached image.
    void paintOceanScene (juce::Graphics& g, int wi, int hi)
    {
        const float w = (float) wi;
        const float h = (float) hi;
        const float horizonY = h * 0.42f;
        const float cx = w * 0.5f;

        const juce::Colour cyan    (0xff00f5ff);
        const juce::Colour purple  (0xffb026ff);
        const juce::Colour magenta (0xffff2daa);

        // --- Sky ------------------------------------------------------------
        {
            juce::ColourGradient sky (juce::Colour (0xff050814), 0.0f, 0.0f,
                                      juce::Colour (0xff1a0b3c), 0.0f, horizonY, false);
            sky.addColour (0.55, juce::Colour (0xff0a0d2c));
            g.setGradientFill (sky);
            g.fillRect (0.0f, 0.0f, w, horizonY + 1.0f);
        }

        // --- Stars ----------------------------------------------------------
        for (int i = 0; i < 150; ++i)
        {
            const float sx = hash01 (i * 3 + 1) * w;
            const float sy = hash01 (i * 3 + 2) * horizonY * 0.92f;
            const float sr = 0.5f + hash01 (i * 3 + 3) * 1.1f;
            const float a  = 0.10f + hash01 (i * 7 + 5) * 0.55f;

            g.setColour ((i % 9 == 0 ? cyan : juce::Colours::white).withAlpha (a));
            g.fillEllipse (sx, sy, sr, sr);
        }

        // --- Sun ring (top right) --------------------------------------------
        {
            const float r  = juce::jmin (w, h) * 0.16f;
            const float ox = w * 0.82f, oy = h * 0.14f;

            g.setColour (purple.withAlpha (0.07f));
            g.drawEllipse (ox - r, oy - r, r * 2.0f, r * 2.0f, 9.0f);
            g.setColour (cyan.withAlpha (0.35f));
            g.drawEllipse (ox - r, oy - r, r * 2.0f, r * 2.0f, 1.6f);
            g.setColour (purple.withAlpha (0.30f));
            g.drawEllipse (ox - r * 0.82f, oy - r * 0.82f, r * 1.64f, r * 1.64f, 1.0f);
        }

        // --- Horizon glow ----------------------------------------------------
        {
            juce::ColourGradient glow (magenta.withAlpha (0.32f), cx, horizonY,
                                       juce::Colours::transparentBlack, cx,
                                       horizonY - h * 0.16f, false);
            g.setGradientFill (glow);
            g.fillRect (0.0f, horizonY - h * 0.16f, w, h * 0.16f);
        }

        // --- Mountains (two layers, jagged, neon ridge) -----------------------
        auto ridge = [&] (int seed, float base, float amp, juce::Colour fill,
                          juce::Colour stroke, float strokeAlpha)
        {
            juce::Path m;
            m.startNewSubPath (0.0f, base);
            const int peaks = 9;
            for (int i = 0; i <= peaks; ++i)
            {
                const float px = w * (float) i / (float) peaks;
                const float py = base - amp * (0.25f + 0.75f * hash01 (seed + i * 17));
                m.lineTo (px, py);
            }
            m.lineTo (w, base);
            m.closeSubPath();

            g.setColour (fill);
            g.fillPath (m);
            g.setColour (stroke.withAlpha (strokeAlpha * 0.25f));
            g.strokePath (m, juce::PathStrokeType (4.0f));
            g.setColour (stroke.withAlpha (strokeAlpha));
            g.strokePath (m, juce::PathStrokeType (1.2f));
        };

        ridge (91, horizonY + 1.0f, h * 0.11f, juce::Colour (0xff0a0c26),
               purple, 0.45f);
        ridge (47, horizonY + 1.0f, h * 0.055f, juce::Colour (0xff060818),
               magenta, 0.40f);

        // --- Sea: base + perspective grid -------------------------------------
        {
            juce::ColourGradient sea (juce::Colour (0xff12082e), 0.0f, horizonY,
                                      juce::Colour (0xff050814), 0.0f, h, false);
            g.setGradientFill (sea);
            g.fillRect (0.0f, horizonY, w, h - horizonY);
        }

        // Horizon line, hot.
        g.setColour (magenta.withAlpha (0.18f));
        g.fillRect (0.0f, horizonY - 2.5f, w, 5.0f);
        g.setColour (magenta.withAlpha (0.75f));
        g.fillRect (0.0f, horizonY - 0.75f, w, 1.5f);

        // Verticals converging on the vanishing point.
        for (int k = -14; k <= 14; ++k)
        {
            const float xTop = cx + (float) k * w * 0.012f;
            const float xBot = cx + (float) k * w * 0.085f;
            g.setColour (magenta.withAlpha (k == 0 ? 0.10f : 0.13f));
            g.drawLine (xTop, horizonY, xBot, h, 1.0f);
        }

        // Horizontals rushing toward the viewer.
        for (int row = 1; row <= 9; ++row)
        {
            const float t = (float) row / 9.0f;
            const float y = horizonY + (h - horizonY) * t * t * 1.04f;
            if (y > h) break;
            g.setColour (magenta.interpolatedWith (cyan, 0.25f)
                             .withAlpha (0.06f + 0.14f * t));
            g.drawLine (0.0f, y, w, y, t > 0.6f ? 1.4f : 1.0f);
        }

        // --- Dolphins ----------------------------------------------------------
        {
            const float s = juce::jmin (w, h);
            drawNeonDolphin (g, { w * 0.13f, h * 0.075f, s * 0.30f, s * 0.21f },
                             -0.32f, false);
            drawNeonDolphin (g, { w * 0.60f, h * 0.16f, s * 0.20f, s * 0.14f },
                             -0.15f, true);

            // Splash sparks where the big dolphin left the water.
            for (int i = 0; i < 14; ++i)
            {
                const float sx = w * 0.16f + hash01 (i * 5 + 3) * w * 0.14f;
                const float sy = horizonY - h * 0.02f - hash01 (i * 5 + 4) * h * 0.05f;
                g.setColour (cyan.withAlpha (0.12f + 0.30f * hash01 (i * 5 + 6)));
                g.fillEllipse (sx, sy, 2.0f, 2.0f);
            }
        }

        // --- Soft blooms + corner vignette so panels stay readable -------------
        {
            juce::ColourGradient bloom (cyan.withAlpha (0.07f), w * 0.16f, 0.0f,
                                        juce::Colours::transparentBlack,
                                        w * 0.16f, h * 0.5f, true);
            g.setGradientFill (bloom);
            g.fillRect (0.0f, 0.0f, w, h);
        }
        {
            juce::ColourGradient vig (juce::Colours::transparentBlack, cx, h * 0.45f,
                                      juce::Colour (0xff050814).withAlpha (0.55f),
                                      0.0f, h, true);
            vig.addColour (0.72, juce::Colours::transparentBlack);
            g.setGradientFill (vig);
            g.fillRect (0.0f, 0.0f, w, h);
        }
    }

    // FL-style typing keys: bottom row = C3 octave, top row = C4 octave.
    const juce::String kTypingKeys ("zsxdcvgbhnjm,q2w3er5t6y7u");

    int typingKeySemitone (int i)     { return i < 13 ? i : 12 + (i - 13); }

    bool physicalKeyDown (juce::juce_wchar c)
    {
        if (juce::KeyPress::isKeyCurrentlyDown ((int) c))
            return true;
        const juce::juce_wchar up = juce::CharacterFunctions::toUpperCase (c);
        return up != c && juce::KeyPress::isKeyCurrentlyDown ((int) up);
    }
}

//==============================================================================
VocalChopAudioProcessorEditor::VocalChopAudioProcessorEditor (VocalChopAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      sensitivityKnob ("Sensitivity"),
      waveform (p),
      chordBar (p),
      sliceGrid (p),
      fxRack (p.getAPVTS())
{
    // Restyle all buttons / combos / menus with the shared Apple look.
    setLookAndFeel (&appleLaf);

    // The editor itself plays notes from the computer keyboard.
    setWantsKeyboardFocus (true);

    // --- Title ---
    titleLabel.setText ("VOCALCHOP STUDIO", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (19.0f).withStyle ("Semibold"))
                            .withExtraKerningFactor (0.14f));
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

    // One-click start: load the embedded demo vocal (the processor slices it,
    // falling back to a grid when transients are sparse — reflect that here).
    demoButton.onClick = [this]
    {
        if (processor.loadDemoSample())
        {
            syncSliceControls();
            refreshChildren();
            grabKeysSoon();
        }
    };
    addAndMakeVisible (demoButton);

    // --- Engine mode: sample chopping vs. built-in synth ---
    engineBox.addItem ("Chop",  1);
    engineBox.addItem ("Synth", 2);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), "engine", engineBox));
    engineBox.onChange = [this] { refreshChildren(); grabKeysSoon(); };
    addAndMakeVisible (engineBox);

    synthWaveBox.addItem ("Saw", 1);
    synthWaveBox.addItem ("Square", 2);
    synthWaveBox.addItem ("Sine", 3);
    synthWaveBox.addItem ("Triangle", 4);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), "synthWave", synthWaveBox));
    addAndMakeVisible (synthWaveBox);

    // --- Instrument picker: designed synth patches (switches to Synth mode),
    //     grouped by category with section headings ---
    {
        const auto names = VocalChopAudioProcessor::getInstrumentNames();
        const auto cats  = VocalChopAudioProcessor::getInstrumentCategories();
        juce::String lastCat;
        for (int i = 0; i < names.size(); ++i)
        {
            if (cats[i] != lastCat)
            {
                instrumentBox.addSectionHeading (cats[i]);
                lastCat = cats[i];
            }
            instrumentBox.addItem (names[i], i + 1);
        }
    }
    instrumentBox.setTextWhenNothingSelected ("Instrument");
    if (processor.getCurrentInstrument() > 0)
        instrumentBox.setSelectedId (processor.getCurrentInstrument() + 1,
                                     juce::dontSendNotification);
    instrumentBox.onChange = [this]
    {
        if (instrumentBox.getSelectedId() > 0)
            processor.applyInstrument (instrumentBox.getSelectedId() - 1);
        refreshChildren();
        grabKeysSoon();   // pick a patch, play it immediately
    };
    addAndMakeVisible (instrumentBox);

    addAndMakeVisible (chordBar);

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
    addKnob (grainKnob,   "grainSize",   "Grain");
    addKnob (detuneKnob,  "synthDetune", "Detune");

    // --- Synth module knobs ---
    addKnob (unisonKnob,  "synthUnison",  "Unison");
    addKnob (spreadKnob,  "synthSpread",  "Spread");
    addKnob (subKnob,     "synthSub",     "Sub");
    addKnob (noiseKnob,   "synthNoise",   "Noise");
    addKnob (fmKnob,      "synthFM",      "FM");
    addKnob (vibratoKnob, "synthVibrato", "Vibrato");
    addKnob (chorusKnob,  "synthChorus",  "Chorus");

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
    waveform.onSampleDropped = [this]
    {
        syncSliceControls();
        refreshChildren();
        grabKeysSoon();
    };
    addAndMakeVisible (waveform);
    addAndMakeVisible (meter);
    addAndMakeVisible (sliceGrid);
    addAndMakeVisible (fxRack);

    setResizable (true, true);
    setResizeLimits (940, 760, 1800, 1400);
    setSize (1080, 900);

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
            syncSliceControls();
            refreshChildren();
            grabKeysSoon();
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

void VocalChopAudioProcessorEditor::syncSliceControls()
{
    auto& engine = processor.getSliceEngine();
    sliceModeBox.setSelectedId (engine.getMode() == SliceEngine::Grid ? 2 : 1,
                                juce::dontSendNotification);

    const int div = engine.getGridDivision();
    if (div == 4 || div == 8 || div == 16 || div == 32)
        gridBox.setSelectedId (div, juce::dontSendNotification);
}

void VocalChopAudioProcessorEditor::refreshChildren()
{
    waveform.refresh();
    sliceGrid.refresh();
    repaint();
}

//==============================================================================
void VocalChopAudioProcessorEditor::grabKeysSoon()
{
    // Combo popups steal focus; take it back once they've closed so the
    // computer keyboard plays notes right away.
    juce::Component::SafePointer<VocalChopAudioProcessorEditor> safe (this);
    juce::Timer::callAfterDelay (120, [safe]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });
}

void VocalChopAudioProcessorEditor::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool VocalChopAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Swallow mapped musical keys (sound is driven by keyStateChanged, which
    // also sees releases); everything else passes through.
    const auto c = juce::CharacterFunctions::toLowerCase (
                       (juce::juce_wchar) key.getTextCharacter());
    return kTypingKeys.indexOfChar (c) >= 0;
}

bool VocalChopAudioProcessorEditor::keyStateChanged (bool)
{
    bool handled = false;

    for (int i = 0; i < kTypingKeys.length(); ++i)
    {
        const bool down = physicalKeyDown (kTypingKeys[i]);
        if (down == typingKeyHeld[(size_t) i])
            continue;

        typingKeyHeld[(size_t) i] = down;
        const int semitone = typingKeySemitone (i);

        if (down)
        {
            processor.pressSlicePad (semitone, 0.85f);
            sliceGrid.flashKey (semitone, 0.9f);
        }
        else
        {
            processor.releaseSlicePad (semitone);
        }
        handled = true;
    }

    return handled;
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

    // Material fill. On the glow theme the panels are darker glass so the
    // scene shows through without fighting the controls.
    if (theme.glow >= 0.9f)
    {
        g.setColour (juce::Colour (0xc008102a));
        g.fillRoundedRectangle (bounds, radius);

        // Faint neon rim.
        g.setColour (theme.accent.withAlpha (0.10f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), radius + 1.0f, 2.5f);
        g.setColour (theme.accent.withAlpha (0.28f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
        return;
    }

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

    if (theme.glow >= 0.9f)
    {
        // Full-glow default theme: the Ocean Pluck scene, cached so knob
        // repaints don't re-render the artwork.
        if (backdropCache.getWidth()  != getWidth()
         || backdropCache.getHeight() != getHeight()
         || backdropTheme != ThemeManager::current())
        {
            backdropCache = juce::Image (juce::Image::ARGB,
                                         juce::jmax (1, getWidth()),
                                         juce::jmax (1, getHeight()), true);
            juce::Graphics ig (backdropCache);
            paintOceanScene (ig, getWidth(), getHeight());
            backdropTheme = ThemeManager::current();
        }
        g.drawImageAt (backdropCache, 0, 0);
    }
    else
    {
        juce::ColourGradient bg (theme.bgTop, 0.0f, 0.0f,
                                 theme.bgBottom, 0.0f, (float) getHeight(), false);
        g.setGradientFill (bg);
        g.fillAll();
    }

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
    drawCard (g, synthCardBounds.toFloat());
    drawCard (g, filterCardBounds.toFloat());
    drawCard (g, playbackCardBounds.toFloat());

    // Section captions.
    drawCaption (g, "Envelope",   envCardBounds);
    drawCaption (g, "Pitch / Tone", toneCardBounds);
    drawCaption (g, "Synth",      synthCardBounds);
    drawCaption (g, "Filter",     filterCardBounds);
    drawCaption (g, "Playback",   playbackCardBounds);

    // Footer hint.
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (juce::String ("Play: MIDI / click keys / type Z S X D C V ...   •   drop audio to chop   •   v")
                    + JucePlugin_VersionString,
                getLocalBounds().removeFromBottom (24).reduced (kMargin, 0),
                juce::Justification::centredRight);
}

void VocalChopAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin);

    // --- Top toolbar row ---
    auto top = area.removeFromTop (kToolbarH);
    titleLabel.setBounds (top.removeFromLeft (300));

    // Right-aligned: theme, load, demo, preset combo, preset label.
    themeBox.setBounds (top.removeFromRight (150).withSizeKeepingCentre (150, 30));
    top.removeFromRight (kGap / 2);
    loadButton.setBounds (top.removeFromRight (130).withSizeKeepingCentre (130, 30));
    top.removeFromRight (kGap / 2);
    demoButton.setBounds (top.removeFromRight (70).withSizeKeepingCentre (70, 30));
    top.removeFromRight (kGap / 2);
    presetBox.setBounds (top.removeFromRight (160).withSizeKeepingCentre (160, 30));
    presetLabel.setBounds (top.removeFromRight (56).withSizeKeepingCentre (56, 30));

    area.removeFromTop (kGap);

    // --- Slice control card ---
    auto sliceCard = area.removeFromTop (94);
    sliceCardBounds = sliceCard;
    {
        auto inner = sliceCard.reduced (kPadding, kPadding - 4);
        engineBox.setBounds (inner.removeFromLeft (120).withSizeKeepingCentre (120, 30));
        inner.removeFromLeft (kGap);
        sliceModeBox.setBounds (inner.removeFromLeft (150).withSizeKeepingCentre (150, 30));
        inner.removeFromLeft (kGap);
        gridBox.setBounds (inner.removeFromLeft (150).withSizeKeepingCentre (150, 30));
        inner.removeFromLeft (kGap);
        sensitivityKnob.setBounds (inner.removeFromLeft (96));
        inner.removeFromLeft (kGap);
        synthWaveBox.setBounds (inner.removeFromLeft (130).withSizeKeepingCentre (130, 30));
        inner.removeFromLeft (kGap);
        instrumentBox.setBounds (inner.removeFromLeft (150).withSizeKeepingCentre (150, 30));
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

    // --- Keyboard along the bottom, chord bar just above it ---
    auto sliceGridRow = area.removeFromBottom (juce::jmax (120, area.getHeight() * 32 / 100));
    sliceGrid.setBounds (sliceGridRow);
    area.removeFromBottom (kGap / 2);
    chordBar.setBounds (area.removeFromBottom (36));
    area.removeFromBottom (kGap);

    // --- Controls area: cards row (grouped) + FX rack side column ---
    auto controls = area;

    // FX rack as a side column on the right.
    auto fxCol = controls.removeFromRight (160);
    fxRack.setBounds (fxCol);
    controls.removeFromRight (kGap);

    // Remaining width split into labelled cards over three rows:
    // [Envelope | Pitch/Tone], [Synth modules], [Filter | Playback].
    const int rowGap = kGap;
    const int rowH   = (controls.getHeight() - rowGap * 2) / 3;
    auto topCards   = controls.removeFromTop (rowH);
    controls.removeFromTop (rowGap);
    auto synthCard  = controls.removeFromTop (rowH);
    controls.removeFromTop (rowGap);
    auto bottomCards = controls;
    synthCardBounds  = synthCard;

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
                                   widthKnob.get(), grainKnob.get(), detuneKnob.get() });
    }

    // Middle row: the synth architecture modules.
    layoutKnobRow (synthCard, { unisonKnob.get(), spreadKnob.get(), subKnob.get(),
                                noiseKnob.get(), fmKnob.get(), vibratoKnob.get(),
                                chorusKnob.get() });

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
