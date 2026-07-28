#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../AudioEngine/LoopStation.h"
#include "ThemeManager.h"
#include <functional>
#include <memory>
#include <vector>

class VocalChopAudioProcessor;

/**
    The Looper tab: an RC-505-style multi-track loop station for the plugin's
    output. All six tracks are on screen by default, one horizontal lane each.

    Each track has its own instrument picker (chosen BEFORE recording, so a
    tap on Rec drops you straight into the right sound), one big pad
    (record -> set length -> overdub/play), Re-rec, Undo for the last
    dub pass, mute, clear and volume, plus a progress ring. Track 1 defines
    the loop length; later tracks quantise to a multiple of it. A BPM
    metronome click (never recorded) keeps takes honest. The on-screen
    keyboard below stays live the whole time.
*/
class LooperPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit LooperPanel (VocalChopAudioProcessor& processor);
    ~LooperPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void populateInstrumentBox (juce::ComboBox&, bool withQuickShelf);
    void applyTrackInstrument (int track);
    void updateTrackVisibility();
    void importAudioToTrack (int track);   // file -> loop (combo's top entry)
    juce::File writeMixToTempFile();       // for the DAW drag-out

    static constexpr int kLoadAudioId = 900000;   // per-track combo item id

    VocalChopAudioProcessor& proc;

    struct TrackUI
    {
        juce::ComboBox   instBox;        // this track's pre-picked sound
        juce::TextButton mainButton  { "Rec" };
        juce::TextButton rerecButton { "Re-rec" };
        juce::TextButton undoButton  { "Undo" };
        juce::TextButton clearButton { "Clr" };
        juce::TextButton muteButton  { "Mute" };
        juce::TextButton revButton   { "Rev" };
        juce::Slider     panSlider;
        juce::Slider     volSlider;
        // All three painted by the panel. A track is one horizontal lane:
        // number, record pad, sound, the recorded audio, then its controls.
        juce::Rectangle<int> laneArea;   // the whole row, washed as one lane
        juce::Rectangle<int> indexArea;  // the track number at the far left
        juce::Rectangle<int> ringArea;   // square record pad + progress ring
        juce::Rectangle<int> waveArea;   // this track's recorded material
        int chosenInstrument = -1;       // -1 = keep whatever is loaded
    };
    TrackUI trackUI[LoopStation::kNumTracks];

    // All six lanes are on screen from the start (spec 4.9): a loop station
    // that opens showing two thirds of itself reads as a smaller machine than
    // it is, and "+ Add track" was the only way to find the rest.
    int visibleTracks = LoopStation::kNumTracks;

    // Full words, sentence case (spec 1). Nothing here is allowed to clip:
    // every one of these is measured in resized() and given the width its own
    // text needs, because a cut label reads as a broken build.
    juce::TextButton playAllButton  { "Play all" };
    juce::TextButton stopAllButton  { "Stop all" };
    juce::TextButton clearAllButton { "Clear all" };
    juce::TextButton exportButton   { "Export WAV" };
    juce::TextButton addTrackButton { "+ Add track" };
    juce::TextButton metroButton    { "Click" };
    juce::TextButton tapButton      { "Tap" };
    juce::TextButton syncButton     { "Sync" };   // follow the host tempo

    /** Press-and-drag to drop the loop mix into the DAW as a WAV. JUCE's
        external drag needs a real file on disk, so the mix is rendered to
        the temp folder the moment the drag starts. */
    struct DragOutButton : juce::TextButton
    {
        using juce::TextButton::TextButton;
        std::function<juce::File()> makeFile;

        void mouseDrag (const juce::MouseEvent&) override
        {
            if (dragging || makeFile == nullptr)
                return;

            dragging = true;
            const auto f = makeFile();
            if (f.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles (
                    { f.getFullPathName() }, false, this,
                    [this] { dragging = false; });
            else
                dragging = false;
        }

        /** Painted as a slab rather than as another button. Dropping the loop
            straight onto the DAW timeline is the best moment this thing has,
            and it looked exactly like Export and Add track next to it - three
            identical rectangles, one of which is the product. */
        void paint (juce::Graphics& g) override
        {
            const auto& th = ThemeManager::active();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool hot = isMouseOver (true) || dragging;

            g.setColour (th.accent.withAlpha (hot ? 1.0f : 0.92f));
            g.fillRoundedRectangle (r, th.cornerRadius * 0.75f);

            // Diagonal stripes, clipped to the slab: the universal "grab this
            // and pull" texture, and it costs a handful of lines.
            {
                juce::Graphics::ScopedSaveState ss (g);
                juce::Path clip;
                clip.addRoundedRectangle (r, th.cornerRadius * 0.75f);
                g.reduceClipRegion (clip);
                g.setColour (th.accentInk.withAlpha (0.13f));
                for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 10.0f)
                    g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 3.0f);
            }

            auto lines = r.reduced (14.0f, 4.0f);
            auto arrow = lines.removeFromRight (24.0f);
            auto heroRow = lines.removeFromTop (lines.getHeight() * 0.58f);

            // HOLD & DRAG is the one all-caps element the spec allows besides
            // the macro labels, so it stays shouting - but it is measured and
            // stepped down until it fits, never clipped.
            float heroSize = juce::jlimit (11.0f, 22.0f, r.getHeight() * 0.44f);
            const juce::String hero ("HOLD & DRAG");
            auto heroFont = [] (float s)
            {
                return juce::Font (juce::FontOptions (s).withStyle ("Bold"))
                           .withExtraKerningFactor (0.04f);
            };
            while (heroSize > 9.0f
                   && (float) juce::GlyphArrangement::getStringWidthInt (heroFont (heroSize), hero)
                          > heroRow.getWidth())
                heroSize -= 1.0f;

            g.setColour (th.accentInk);
            g.setFont (heroFont (heroSize));
            g.drawText (hero, heroRow, juce::Justification::centredLeft, false);

            // Same rule for the sub-line: longest wording that actually fits.
            // ASCII only - this codebase renders UTF-8 literals as Latin-1.
            const juce::Font subFont (juce::FontOptions (
                juce::jlimit (8.5f, 11.5f, r.getHeight() * 0.23f)));
            juce::String sub ("Drop the loop mix straight onto the timeline");
            for (auto* shorter : { "Drop the loop mix onto the timeline",
                                   "Drop the mix on the timeline", "" })
            {
                if ((float) juce::GlyphArrangement::getStringWidthInt (subFont, sub)
                        <= lines.getWidth())
                    break;
                sub = shorter;
            }

            g.setFont (subFont);
            g.setColour (th.accentInk.withAlpha (0.75f));
            g.drawText (sub, lines, juce::Justification::centredLeft, false);

            // Arrow pointing out of the plugin.
            g.setColour (th.accentInk.withAlpha (hot ? 1.0f : 0.8f));
            const float cx = arrow.getCentreX(), cy = arrow.getCentreY();
            g.drawLine (cx - 7.0f, cy, cx + 7.0f, cy, 2.0f);
            g.drawLine (cx + 2.0f, cy - 5.0f, cx + 7.0f, cy, 2.0f);
            g.drawLine (cx + 2.0f, cy + 5.0f, cx + 7.0f, cy, 2.0f);
        }

        void mouseEnter (const juce::MouseEvent&) override
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            repaint();
        }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        bool dragging = false;
    };
    DragOutButton dragButton { "DRAG" };
    juce::Slider     bpmSlider;
    double lastTapMs = 0.0;
    double tapIntervalMs = 0.0;
    int    tapCount = 0;

    // Panel header (spec 4.9) - the loop station is one of the three
    // accent-forward surfaces, so the tick beside the title is accent.
    juce::Rectangle<int> headerTickArea, headerTitleArea, headerSubArea;

    // The tempo reads as a NUMERAL, in mono, next to its drag slider:
    // "124.0" is the value, the slider is only the way to change it.
    juce::Rectangle<int> bpmValueArea, bpmUnitArea;
    float shownBpm = -1.0f;          // repaint trigger for the numeral

    juce::Rectangle<int> howToArea;  // painted hint line above the footer

    // Pick the CURRENT sound without leaving the looper.
    // Captions painted above the two top pickers.
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> pickCaptions;

    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox instrumentBox;  // Featured + category submenus
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;     // per-track audio import
    std::unique_ptr<juce::FileChooser> exportChooser;   // Export WAV save dialog
                                       // (separate members: replacing a live
                                       // FileChooser silently kills its dialog)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};
