#include "ChordBar.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

//==============================================================================
// Chord spellings as semitone offsets from C3 (offset 0 = C, 2 = D, 4 = E,
// 5 = F, 7 = G, 9 = A, 11 = B). These double as slice indices in Chop mode.
const std::vector<ChordBar::Style>& ChordBar::styles()
{
    // Chord building blocks. Offsets are semitones from the tonic, and the
    // bar transposes the lot into whatever key the sample turned out to be in.
    #define C_   { "C",      { 0, 4, 7 } }
    #define CM7  { "Cmaj7",  { 0, 4, 7, 11 } }
    #define C7_  { "C7",     { 0, 4, 7, 10 } }
    #define C6_  { "C6",     { 0, 4, 7, 9 } }
    #define CAD9 { "Cadd9",  { 0, 4, 7, 14 } }
    #define CS2  { "Csus2",  { 0, 2, 7 } }
    #define CS4  { "Csus4",  { 0, 5, 7 } }
    #define CM_  { "Cm",     { 0, 3, 7 } }
    #define CM7m { "Cm7",    { 0, 3, 7, 10 } }
    #define CM9m { "Cm9",    { 0, 3, 7, 10, 14 } }
    #define DM_  { "Dm",     { 2, 5, 9 } }
    #define DM7  { "Dm7",    { 2, 5, 9, 12 } }
    #define DM9  { "Dm9",    { 2, 5, 9, 12, 16 } }
    #define D7_  { "D7",     { 2, 6, 9, 12 } }
    #define DS4  { "Dsus4",  { 2, 7, 9 } }
    #define EB_  { "Eb",     { 3, 7, 10 } }
    #define EBM7 { "Ebmaj7", { 3, 7, 10, 14 } }
    #define EM_  { "Em",     { 4, 7, 11 } }
    #define EM7  { "Em7",    { 4, 7, 11, 14 } }
    #define E7_  { "E7",     { 4, 8, 11, 14 } }
    #define F_   { "F",      { 5, 9, 12 } }
    #define FM7  { "Fmaj7",  { 5, 9, 12, 16 } }
    #define F6_  { "F6",     { 5, 9, 12, 14 } }
    #define FMm  { "Fm",     { 5, 8, 12 } }
    #define FM7m { "Fm7",    { 5, 8, 12, 15 } }
    #define G_   { "G",      { 7, 11, 14 } }
    #define G7_  { "G7",     { 7, 11, 14, 17 } }
    #define GS4  { "Gsus4",  { 7, 12, 14 } }
    #define GB_  { "G/B",    { 11, 14, 19 } }
    #define AB_  { "Ab",     { 8, 12, 15 } }
    #define ABM7 { "Abmaj7", { 8, 12, 15, 19 } }
    #define AM_  { "Am",     { 9, 12, 16 } }
    #define AM7  { "Am7",    { 9, 12, 16, 19 } }
    #define AM9  { "Am9",    { 9, 12, 16, 19, 23 } }
    #define A7_  { "A7",     { 9, 13, 16, 19 } }
    #define BB_  { "Bb",     { 10, 14, 17 } }
    #define BBM7 { "Bbmaj7", { 10, 14, 17, 21 } }
    #define BDIM { "Bdim",   { 11, 14, 17 } }
    #define BM7b { "Bm7b5",  { 11, 14, 17, 21 } }

    // Added for variety. The libraries below leaned hard on one diatonic
    // region per style, so several "different" progressions were the same
    // four chords in a different order - identical pitch content, and
    // Generate sounded like it had done nothing. These open up the other
    // regions and the sus / borrowed qualities.
    #define D_   { "D",      { 2, 6, 9 } }
    #define DS2  { "Dsus2",  { 2, 4, 9 } }
    #define E_   { "E",      { 4, 8, 11 } }
    #define FS2  { "Fsus2",  { 5, 7, 12 } }
    #define FM9  { "Fmaj9",  { 5, 9, 12, 16, 19 } }
    #define GMm  { "Gm",     { 7, 10, 14 } }
    #define GM7m { "Gm7",    { 7, 10, 14, 17 } }
    #define GS2  { "Gsus2",  { 7, 9, 14 } }
    #define AS2  { "Asus2",  { 9, 11, 16 } }
    #define AS4  { "Asus4",  { 9, 14, 16 } }
    #define BM_  { "Bm",     { 11, 14, 18 } }
    #define BM7  { "Bm7",    { 11, 14, 18, 21 } }
    #define BBS2 { "Bbsus2", { 10, 12, 17 } }
    #define DB_  { "Db",     { 1, 5, 8 } }
    #define DBM7 { "Dbmaj7", { 1, 5, 8, 12 } }
    #define EBMm { "Ebm",    { 3, 6, 10 } }
    #define CM9  { "Cmaj9",  { 0, 4, 7, 11, 14 } }

    static const std::vector<Style> all = {
        { "K-Pop", {
            { F_,  G_,  EM_, AM_ },          // the royal road
            { C_,  G_,  AM_, F_  },
            { AM_, F_,  C_,  G_  },
            { F_,  G_,  AM_, G_  },
            { FM7, G_,  EM7, AM_ },
            { C_,  GB_, AM_, F_  },          // the descending bass line
            { AM_, F_,  G_,  EM_ },
            { CAD9, G_, AM7, FM7 },
        }},
        // EDM had eight entries built from exactly TWO chord sets - Am F C G
        // and Cm Ab Eb Bb - rotated. Every Generate returned the same pitches
        // in a different order, which is why a tester said the EDM chords were
        // always the same. They were. These sixteen span five tonal centres
        // and lean on the sus voicings the genre actually uses.
        { "EDM", {
            { AM_, F_,  C_,  G_  },
            { CS2, GS4, AM7, FM7 },          // sus-stack: the festival sound
            { FMm, DB_, AB_, EB_ },          // the minor anthem
            { EM_, C_,  G_,  D_  },          // E-minor region
            { DM_, BB_, F_,  C_  },          // D-minor region
            { GMm, EB_, BB_, D_  },          // G-minor region
            { BM7b, EM7, AM7, DM7 },         // descending fifths - trance
            { AM_, G_,  F_,  G_  },
            { CM_, AB_, EB_, BB_ },
            { FM9, GS2, AM7, CM9 },          // wide 9ths, big-room breakdown
            { AS2, F_,  CS2, GS4 },
            { EBMm, DB_, AB_, BB_ },
            { AM7, FM7, G7_, CM7 },
            { D_,  BM_, GMm, A7_ },
            { FMm, EB_, DB_, CM_ },          // the falling minor tetrachord
            { CS2, AS2, FS2, GS2 },          // all-sus, no thirds at all
        }},
        { "Trap", {
            { CM_, AB_, EB_, BB_ },
            { CM7m, ABM7, EBM7, FM7m },
            { AM_, F_,  DM_, EM_ },
            { CM_, EB_, AB_, G_  },
            { FM7m, CM7m, ABM7, BB_ },
            { CM9m, ABM7, BBM7, EBM7 },
        }},
        { "Drill", {
            { CM_, BDIM, AB_, G_ },          // the chromatic slide
            { CM7m, BM7b, ABM7, G7_ },
            { AM_, G_,  F_,  EM_ },
            { CM_, AB_, BB_, G_  },
            { FM7m, EBM7, ABM7, G7_ },
        }},
        { "Lo-Fi", {
            { FM7, EM7, DM7, CM7 },
            { AM7, DM7, G7_, CM7 },
            { CM7, AM7, FM7, G7_ },
            { DM7, EM7, FM7, EM7 },
            { CM7, C7_, FM7, FM7m },         // the borrowed minor-iv ache
            { AM9, DM9, G7_, CM7 },
            { FM7, G7_, EM7, AM7 },
            { DM9, G7_, CM7, AM9 },
        }},
        { "R&B", {
            { CM7, AM7, DM7, G7_ },
            { FM7, G7_, EM7, AM7 },
            { AM7, FM7, DM7, EM7 },
            { DM7, G7_, CM7, AM7 },
            { CM9m, FM7m, BBM7, EBM7 },
            { AM9, DM9, GS4, G7_ },
            { FM7, EM7, EBM7, DM7 },
            { CM7, BM7b, AM7, D7_ },
        }},
        { "House", {
            { AM7, DM7, G7_, CM7 },
            { FM7, G7_, AM7, AM7 },
            { CM_, FMm, AB_, G7_ },
            { AM_, DM_, G_,  C_  },
            { FM7m, BBM7, EBM7, ABM7 },
            { AM9, FM7, CM7, G_  },
        }},
        { "Ballad", {
            { C_,  AM_, F_,  G_  },
            { C_,  EM_, F_,  G_  },
            { C_,  G_,  F_,  G_  },
            { AM_, EM_, F_,  C_  },
            { C_,  GB_, AM_, EM_ },          // canon
            { F_,  G_,  C_,  AM_ },
            { C_,  CS4, F_,  F6_ },
            { AM_, F_,  C_,  GS4 },
        }},
        { "City Pop", {
            { FM7, E7_, AM7, C7_ },
            { DM7, G7_, CM7, AM7 },
            { FM7, G7_, AM7, AM7 },
            { CM7, E7_, AM7, G7_ },
            { CM7, BM7b, EM7, AM7 },
            { FM7, FM7m, CM7, A7_ },
            { DM9, G7_, EM7, AM9 },
        }},
        { "Jazz", {
            { DM7, G7_, CM7, CM7 },          // ii-V-I
            { DM7, G7_, EM7, A7_ },
            { CM7, A7_, DM7, G7_ },          // rhythm changes
            { BM7b, E7_, AM7, AM7 },
            { CM7, EBM7, ABM7, D7_ },        // the sidestep
            { FM7, BM7b, E7_, AM7 },
        }},
        { "Gospel", {
            { C_,  F_,  C_,  G7_ },
            { CM7, FM7, EM7, AM7 },
            { F_,  G_,  EM7, AM_ },
            { C_,  C7_, F_,  FMm },
            { AB_, BB_, C_,  C_  },
            { CM7, BB_, F_,  C_  },
        }},
        { "Afrobeats", {
            { AM_, F_,  C_,  G_  },
            { CM7, AM7, FM7, G_  },
            { FM7, EM7, AM7, AM7 },
            { AM7, G_,  F_,  EM7 },
            { CM_, EB_, FMm, G_  },
        }},
        { "Anime", {
            { F_,  G_,  EM_, AM_ },
            { F_,  G_,  AM_, AM_ },
            { C_,  G_,  AM_, EM_ },
            { DM_, EM_, F_,  G_  },
            { FM7, G_,  EM7, AM_ },
            { BB_, C_,  AM_, DM_ },
            { F_,  E7_, AM_, AM_ },          // the harmonic-minor turn
        }},
        { "Phonk", {
            { CM_, AB_, G_,  G_  },
            { AM_, F_,  E7_, AM_ },
            { CM7m, ABM7, G7_, CM7m },
            { FMm, CM_, AB_, G_  },
            { AM_, DM_, EM_, AM_ },
        }},
    };
#undef C_
    #undef CM7
    #undef C7_
    #undef C6_
    #undef CAD9
    #undef CS2
    #undef CS4
    #undef CM_
    #undef CM7m
    #undef CM9m
    #undef DM_
    #undef DM7
    #undef DM9
    #undef D7_
    #undef DS4
    #undef EB_
    #undef EBM7
    #undef EM_
    #undef EM7
    #undef E7_
    #undef F_
    #undef FM7
    #undef F6_
    #undef FMm
    #undef FM7m
    #undef G_
    #undef G7_
    #undef GS4
    #undef GB_
    #undef AB_
    #undef ABM7
    #undef AM_
    #undef AM7
    #undef AM9
    #undef A7_
    #undef BB_
    #undef BBM7
    #undef BDIM
    #undef BM7b

    return all;
}

//==============================================================================
ChordBar::ChordBar (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    caption.setText ("CHORDS", juce::dontSendNotification);
    refreshKeyLabel();
    caption.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Semibold")));
    caption.setJustificationType (juce::Justification::centredLeft);
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);

    int id = 1;
    for (const auto& s : styles())
        styleBox.addItem (s.name, id++);
    styleBox.setSelectedId (1, juce::dontSendNotification);
    styleBox.onChange = [this] { lastPick = -1; regenerate(); };
    addAndMakeVisible (styleBox);

    genButton.onClick = [this] { regenerate(); };
    genButton.setTriggeredOnMouseDown (true);   // fire on press, not release
    addAndMakeVisible (genButton);

    for (int i = 0; i < (int) chordButtons.size(); ++i)
    {
        chordButtons[(size_t) i].onClick = [this, i] { playChord (i); };
        // Chords must SOUND the instant the mouse goes down — waiting for
        // mouse-up reads as lag on a musical control.
        chordButtons[(size_t) i].setTriggeredOnMouseDown (true);
        addAndMakeVisible (chordButtons[(size_t) i]);
    }

    regenerate();
}

ChordBar::~ChordBar()
{
    stopTimer();
}

void ChordBar::refreshKeyLabel()
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    const int root = proc.getDetectedKeyRoot();

    if (root >= 0 && root < 12)
        caption.setText (juce::String ("CHORDS - ") + names[root]
                             + (proc.isDetectedKeyMinor() ? "m" : ""),
                         juce::dontSendNotification);
    else
        caption.setText ("CHORDS", juce::dontSendNotification);
}

juce::String ChordBar::getKeyText() const
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    const int root = proc.getDetectedKeyRoot();
    if (root < 0 || root >= 12) return "no key";
    return juce::String (names[root]) + (proc.isDetectedKeyMinor() ? " minor" : " major");
}

void ChordBar::regenerate()
{
    const auto& style = styles()[(size_t) juce::jmax (0, styleBox.getSelectedId() - 1)];
    const int count = (int) style.progressions.size();

    // Reject by CONTENT, not by index. A different entry that happens to be
    // the same four chords in a different order is the same pitches, and to
    // the ear Generate did nothing - which is exactly what a tester reported.
    auto fingerprint = [] (const Progression& p)
    {
        std::set<int> pcs;
        for (const auto& c : p)
            for (int s : c.semis)
                pcs.insert (((s % 12) + 12) % 12);
        return pcs;
    };

    int pick = rng.nextInt (count);
    for (int tries = 0; tries < count && count > 1; ++tries)
    {
        const bool sameSlot  = (pick == lastPick);
        const bool samePitch = (fingerprint (style.progressions[(size_t) pick])
                                    == lastFingerprint);
        if (! sameSlot && ! samePitch)
            break;
        pick = (pick + 1) % count;
    }
    lastPick = pick;
    lastFingerprint = fingerprint (style.progressions[(size_t) pick]);

    current = style.progressions[(size_t) pick];
    for (int i = 0; i < (int) chordButtons.size(); ++i)
        chordButtons[(size_t) i].setButtonText (
            i < (int) current.size() ? current[(size_t) i].name : "-");
}

void ChordBar::playChord (int buttonIndex)
{
    if (buttonIndex < 0 || buttonIndex >= (int) current.size())
        return;

    // Each chord tone goes through the same lock-free path as a key press.
    // Progressions are stored in C; transpose them into the detected key of
    // the loaded sample so suggestions always fit what the user dropped in.
    const int root = juce::jmax (0, proc.getDetectedKeyRoot());

    for (int semi : current[(size_t) buttonIndex].semis)
    {
        int t = semi + root;
        while (t > 35) t -= 12;   // stay inside the 3-octave keyboard
        proc.triggerSlicePad (t, 0.85f);
    }

    // Kick off the neon flash pulse on the clicked button (glow themes only;
    // the shared look-and-feel reads the "neonFlash" property when drawing).
    if (ThemeManager::active().glow >= 0.9f)
    {
        flashLevels[(size_t) buttonIndex] = 1.0f;
        chordButtons[(size_t) buttonIndex].getProperties().set ("neonFlash", 1.0f);
        chordButtons[(size_t) buttonIndex].repaint();

        if (! isTimerRunning())
            startTimerHz (30);
    }
}

void ChordBar::timerCallback()
{
    // Decay each active flash and repaint just that button.
    bool anyActive = false;

    for (size_t i = 0; i < chordButtons.size(); ++i)
    {
        if (flashLevels[i] <= 0.0f)
            continue;

        flashLevels[i] = juce::jmax (0.0f, flashLevels[i] - 0.08f);
        chordButtons[i].getProperties().set ("neonFlash", flashLevels[i]);
        chordButtons[i].repaint();

        if (flashLevels[i] > 0.0f)
            anyActive = true;
    }

    if (! anyActive)
        stopTimer();
}

//==============================================================================
void ChordBar::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    caption.setColour (juce::Label::textColourId, theme.textSecondary);
    juce::ignoreUnused (g);
}

void ChordBar::resized()
{
    auto area = getLocalBounds();

    caption.setBounds (area.removeFromLeft (96));   // fits "CHORDS - A#m"
    styleBox.setBounds (area.removeFromLeft (120).withSizeKeepingCentre (120, 30));
    area.removeFromLeft (8);
    genButton.setBounds (area.removeFromLeft (96).withSizeKeepingCentre (96, 30));
    area.removeFromLeft (12);

    const int gap = 8;
    const int w = (area.getWidth() - gap * 3) / 4;
    for (auto& b : chordButtons)
    {
        b.setBounds (area.removeFromLeft (w).withSizeKeepingCentre (w, 30));
        area.removeFromLeft (gap);
    }
}
