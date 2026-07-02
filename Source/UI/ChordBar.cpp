#include "ChordBar.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

//==============================================================================
// Chord spellings as semitone offsets from C3 (offset 0 = C, 2 = D, 4 = E,
// 5 = F, 7 = G, 9 = A, 11 = B). These double as slice indices in Chop mode.
const std::vector<ChordBar::Style>& ChordBar::styles()
{
    // Chord building blocks.
    #define C_   { "C",     { 0, 4, 7 } }
    #define CM7  { "Cmaj7", { 0, 4, 7, 11 } }
    #define C7_  { "C7",    { 0, 4, 7, 10 } }
    #define DM7  { "Dm7",   { 2, 5, 9, 12 } }
    #define EM_  { "Em",    { 4, 7, 11 } }
    #define EM7  { "Em7",   { 4, 7, 11, 14 } }
    #define E7_  { "E7",    { 4, 8, 11, 14 } }
    #define F_   { "F",     { 5, 9, 12 } }
    #define FM7  { "Fmaj7", { 5, 9, 12, 16 } }
    #define G_   { "G",     { 7, 11, 14 } }
    #define G7_  { "G7",    { 7, 11, 14, 17 } }
    #define AM_  { "Am",    { 9, 12, 16 } }
    #define AM7  { "Am7",   { 9, 12, 16, 19 } }

    static const std::vector<Style> all = {
        { "K-Pop", {
            { F_,  G_,  EM_, AM_ },      // the "royal road"
            { C_,  G_,  AM_, F_  },
            { AM_, F_,  C_,  G_  },
            { F_,  G_,  AM_, G_  },
        }},
        { "EDM", {
            { AM_, F_,  C_,  G_  },
            { AM_, C_,  G_,  F_  },
            { F_,  AM_, G_,  AM_ },
            { AM_, G_,  F_,  G_  },
        }},
        { "Lo-Fi", {
            { FM7, EM7, DM7, CM7 },
            { AM7, DM7, G7_, CM7 },
            { CM7, AM7, FM7, G7_ },
            { DM7, EM7, FM7, EM7 },
        }},
        { "R&B", {
            { CM7, AM7, DM7, G7_ },
            { FM7, G7_, EM7, AM7 },
            { AM7, FM7, DM7, EM7 },
            { DM7, G7_, CM7, AM7 },
        }},
        { "Ballad", {
            { C_,  AM_, F_,  G_  },
            { C_,  EM_, F_,  G_  },
            { C_,  G_,  F_,  G_  },
            { AM_, EM_, F_,  C_  },
        }},
        { "City Pop", {
            { FM7, E7_, AM7, C7_ },
            { DM7, G7_, CM7, AM7 },
            { FM7, G7_, AM7, AM7 },
            { CM7, E7_, AM7, G7_ },
        }},
    };

    #undef C_
    #undef CM7
    #undef C7_
    #undef DM7
    #undef EM_
    #undef EM7
    #undef E7_
    #undef F_
    #undef FM7
    #undef G_
    #undef G7_
    #undef AM_
    #undef AM7

    return all;
}

//==============================================================================
ChordBar::ChordBar (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    caption.setText ("CHORDS", juce::dontSendNotification);
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
    addAndMakeVisible (genButton);

    for (int i = 0; i < (int) chordButtons.size(); ++i)
    {
        chordButtons[(size_t) i].onClick = [this, i] { playChord (i); };
        addAndMakeVisible (chordButtons[(size_t) i]);
    }

    regenerate();
}

void ChordBar::regenerate()
{
    const auto& style = styles()[(size_t) juce::jmax (0, styleBox.getSelectedId() - 1)];
    const int count = (int) style.progressions.size();

    // Pick a progression different from the previous one when possible.
    int pick = rng.nextInt (count);
    if (count > 1 && pick == lastPick)
        pick = (pick + 1 + rng.nextInt (count - 1)) % count;
    lastPick = pick;

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
    for (int semi : current[(size_t) buttonIndex].semis)
        proc.triggerSlicePad (semi, 0.85f);
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

    caption.setBounds (area.removeFromLeft (64));
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
