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
    The LOOPER tab: an RC-505-style multi-track looper for the plugin's
    output (4 tracks visible, expandable to 6 with + TRACK).

    Each track has its own instrument picker (chosen BEFORE recording, so a
    tap on REC drops you straight into the right sound), one big pad
    (record -> set length -> overdub/play), RE-record, UNDO for the last
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
        juce::TextButton mainButton  { "REC" };
        juce::TextButton rerecButton { "RE" };
        juce::TextButton undoButton  { "UNDO" };
        juce::TextButton clearButton { "X" };
        juce::TextButton muteButton  { "M" };
        juce::TextButton revButton   { "REV" };
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
    int visibleTracks = 4;

    // Short labels on purpose. The transport row has to fit ten controls plus
    // the drag slab at the 1020px minimum width, and "PLAY ALL" was being cut
    // to "PLAY AL" - a clipped word reads as a bug, where a short one reads as
    // a label. What they act on is in the tooltip.
    juce::TextButton playAllButton  { "PLAY" };
    juce::TextButton stopAllButton  { "STOP" };
    juce::TextButton clearAllButton { "CLEAR" };
    juce::TextButton exportButton   { "EXPORT" };
    juce::TextButton addTrackButton { "+ TRACK" };
    juce::TextButton metroButton    { "MET" };
    juce::TextButton tapButton      { "TAP" };
    juce::TextButton syncButton     { "SYNC" };   // follow the host tempo

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

            auto lines = r.reduced (12.0f, 4.0f);
            auto arrow = lines.removeFromRight (22.0f);

            g.setColour (th.accentInk);
            g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold"))
                           .withExtraKerningFactor (0.06f));
            g.drawText ("HOLD & DRAG", lines.removeFromTop (lines.getHeight() * 0.56f),
                        juce::Justification::centredLeft, false);
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.setColour (th.accentInk.withAlpha (0.75f));
            g.drawText ("Drop the loop mix onto the timeline", lines,
                        juce::Justification::centredLeft, false);

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

    // Pick the CURRENT sound without leaving the looper.
    // Captions painted above the two top pickers.
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> pickCaptions;

    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox instrumentBox;  // Featured + category submenus
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;     // per-track audio import
    std::unique_ptr<juce::FileChooser> exportChooser;   // EXPORT save dialog
                                       // (separate members: replacing a live
                                       // FileChooser silently kills its dialog)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};
