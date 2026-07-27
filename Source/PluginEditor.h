#pragma once

#include <JuceHeader.h>
#include <vector>
#include <set>

#include "PluginProcessor.h"
#include "UI/WaveformView.h"
#include "UI/SliceGrid.h"
#include "UI/ChordBar.h"
#include "UI/FXRack.h"
#include "UI/KnobComponent.h"
#include "UI/MeterComponent.h"
#include "UI/LooperPanel.h"
#include "UI/UnlockPanel.h"
#include "UI/WelcomePanel.h"
#include "UI/AppleLookAndFeel.h"

//==============================================================================
class VocalChopAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::ChangeListener
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

    /** Syncs held typing-key notes with the OS-global key state. Runs from
        keyStateChanged AND a watchdog timer, so a release that happens while
        focus is elsewhere (combo popup, other window) can never leave a note
        stuck on. */
    bool scanTypingKeys (bool forceReleaseAll = false);
    void timerCallback() override;

    /** Host restored our state (project revert, preset switch): re-sync the
        combos, theme and cached waveform that attachments don't cover. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Lays out and paints on a fixed kBaseW x kBaseH canvas; the whole
        canvas scales uniformly with the window (see content below). */
    void layoutContent();
    void paintContent (juce::Graphics&);

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
    juce::Label      subtitleLabel;
    juce::Label      presetLabel;
    juce::ComboBox   presetBox;
    juce::ComboBox   themeBox;
    juce::TextButton loadButton   { "Load Sample" };
    juce::TextButton demoButton   { "Demo" };
    juce::TextButton looperTabButton { "LOOPER" };
    bool             showLooper = false;

    // Slicing controls.
    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox sliceModeBox;
    juce::ComboBox gridBox;
    juce::ComboBox synthWaveBox;   // Saw / Square / Sine / Triangle
    KnobComponent  sensitivityKnob { "Sensitivity" };

    // Octave shift for the whole synth (drives the synthOctave parameter).
    juce::TextButton octDownButton { "-" };
    juce::TextButton octUpButton   { "+" };
    juce::Label      octLabel;
    std::unique_ptr<juce::ParameterAttachment> octAttachment;

    // Main / grouped knobs.
    std::unique_ptr<KnobComponent> pitchKnob, formantKnob, mixKnob, widthKnob,
                                   grainKnob, attackKnob, detuneKnob;
    std::unique_ptr<KnobComponent> decayKnob, sustainKnob, releaseKnob,
                                   filterCutoffKnob, filterResoKnob, outputGainKnob,
                                   grainMixKnob;

    // Synth module knobs (Serum-style architecture controls).
    std::unique_ptr<KnobComponent> unisonKnob, spreadKnob, subKnob, noiseKnob,
                                   fmKnob, vibratoKnob, chorusKnob,
                                   lfoRateKnob, motionKnob, glideKnob;

    // Performance macros (HYPE / SPACE / DIRT).
    std::unique_ptr<KnobComponent> hypeKnob, spaceKnob, dirtKnob;

    // Grouped choice / bool controls.
    juce::ComboBox   filterTypeBox;
    juce::ComboBox   playModeBox;
    juce::ComboBox   delaySyncBox;   // delay time locked to the host tempo

    // Arp + pump: the two tempo-locked performance engines.
    juce::ComboBox   arpModeBox, arpRateBox, arpOctBox, pumpRateBox;
    std::unique_ptr<KnobComponent> arpGateKnob, pumpKnob;
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
    // Step through the instrument list without opening a 376-entry menu.
    juce::TextButton instPrevButton { "<" }, instNextButton { ">" };
    void stepInstrument (int delta);
    MeterComponent meter { processor.getOutputLevelRef() };
    SliceGrid      sliceGrid;
    FXRack         fxRack;
    LooperPanel    looperPanel { processor };
    UnlockPanel    unlockPanel { [this] (juce::String e, juce::String k)
                                 { return processor.finalizeActivation (e, k); } };
    juce::TextButton unlockButton { "UNLOCK" };

    // First-run quick start (re-openable from the toolbar "?").
    WelcomePanel     welcomePanel;
    juce::TextButton helpButton { "?" };

    // Hover help on every major control.
    juce::TooltipWindow tooltipWindow { this, 700 };

    // MY SAMPLES: item id 5000+i maps to mySamplePaths[i]. Ids resolve to a
    // path captured at menu-build time - the recents FILE reorders itself on
    // every load, so name/index lookups against it would drift.
    juce::StringArray mySamplePaths;

    // Preset menu ids: factory presets are 1..N, then the user's own, then
    // the Save entry (kept far apart so a growing factory list can't collide).
    static constexpr int kUserPresetBaseId = 4000;
    static constexpr int kSavePresetId     = 4999;
    void rebuildPresetMenu();
    void promptSavePreset();

    // Per-control captions for the slice strip. A row of six unlabelled
    // combos read as "some other preset menu" to the first tester who saw it
    // - nobody could tell which one picked the SOUND.
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> stripCaptions;
    std::set<juce::String> stripDimmed;   // captions whose control is inert
    void drawStripCaptions (juce::Graphics&) const;

    /** Adds a micro-caption above `control`, shrinking it to fit. Every combo
        in this plugin now carries one: a bare "1/16" next to a bare "1/4"
        told nobody which was the arp and which was the pump. */
    juce::Rectangle<int> captioned (juce::Rectangle<int> cell, const juce::String&);

    /** Greys out the strip controls the current engine does not use. */
    void syncEngineEnablement();

    // Cached card rectangles (populated in resized(), painted in paint()).
    juce::Rectangle<int> macroCardBounds;
    juce::Rectangle<int> sliceCardBounds;
    juce::Rectangle<int> envCardBounds;
    juce::Rectangle<int> toneCardBounds;
    juce::Rectangle<int> synthCardBounds;
    juce::Rectangle<int> filterCardBounds;
    juce::Rectangle<int> playbackCardBounds;
    juce::Rectangle<int> arpCardBounds;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Computer-keyboard note state (one flag per mapped key).
    std::array<bool, 32> typingKeyHeld {};

    // Cached Ocean Pluck scene (repainted only on resize / theme change).
    juce::Image backdropCache;
    int backdropTheme = -1;

    // Fixed-size design canvas, scaled as one unit so the window can shrink
    // to 60% without per-widget cramming. All children live inside it.
    static constexpr int kBaseW = 1080, kBaseH = 1220;
    struct ContentComp : juce::Component
    {
        explicit ContentComp (VocalChopAudioProcessorEditor& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        VocalChopAudioProcessorEditor& owner;
    };
    ContentComp content { *this };

    // Hero motion FX: on glow themes every keypress fires neon speed lines
    // and a glow pulse across the artwork band — the "riding" illusion with
    // zero frame animation. Only heroRect repaints, and only while the
    // effect is alive, so knobs and audio never feel it.
    struct SpeedLine { float x, y, len, speed, life; int hue; };
    std::vector<SpeedLine> speedLines;
    float heroGlow = 0.0f;
    juce::Rectangle<int> heroRect;
    juce::Random fxRng;
    void spawnHeroFx (float velocity);
    void drawHeroFx (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessorEditor)
};
