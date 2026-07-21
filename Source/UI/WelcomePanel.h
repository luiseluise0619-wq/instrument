#pragma once

#include <JuceHeader.h>
#include "ThemeManager.h"
#include "../Licensing.h"

/**
    First-run quick-start overlay: three big steps (PLAY / SOUNDS / LOOP)
    so a first-time user makes sound within ten seconds. Shown once (a
    flag file remembers), and re-openable any time from the toolbar "?".
*/
class WelcomePanel : public juce::Component
{
public:
    WelcomePanel()
    {
        startButton.onClick = [this]
        {
            markSeen();
            setVisible (false);
        };
        addAndMakeVisible (startButton);
        setWantsKeyboardFocus (true);   // swallow keys behind the veil
    }

    static juce::File seenFile()
    {
        return vcs::Licensing::licenseFile().getSiblingFile ("welcomed.flag");
    }
    static bool hasSeenWelcome()   { return seenFile().existsAsFile(); }
    static void markSeen()
    {
        auto f = seenFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText ("1");
    }

    void resized() override
    {
        auto card = getCardBounds();
        startButton.setBounds (card.removeFromBottom (64)
                                   .withSizeKeepingCentre (220, 40));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! getCardBounds().contains (e.getPosition()))
        {
            markSeen();
            setVisible (false);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = ThemeManager::active();

        g.fillAll (juce::Colours::black.withAlpha (0.78f));

        auto card = getCardBounds().toFloat();
        g.setColour (theme.glow >= 0.9f ? juce::Colour (0xf80a0f24) : theme.bgTop);
        g.fillRoundedRectangle (card, 16.0f);
        g.setColour (theme.accent.withAlpha (0.5f));
        g.drawRoundedRectangle (card.reduced (0.5f), 16.0f, 1.4f);

        auto area = getCardBounds().reduced (36, 28);

        g.setColour (theme.text);
        g.setFont (juce::Font (juce::FontOptions (26.0f).withStyle ("Bold")));
        g.drawText ("welcome to slyce", area.removeFromTop (40),
                    juce::Justification::centred);
        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText ("Three steps and you're making beats.",
                    area.removeFromTop (24), juce::Justification::centred);
        area.removeFromTop (14);

        struct Step { const char* num; const char* title; const char* body; };
        const Step steps[] = {
            { "1", "PLAY",
              "Hit DEMO in the toolbar, then play your computer keys\n"
              "Z S X D C V G B H N J M  (or any MIDI keyboard)." },
            { "2", "SOUNDS",
              "Open the Instrument menu for 343 sounds - drums, 808s,\n"
              "vocals, pianos. Start with the FEATURED shelf." },
            { "3", "LOOP",
              "Open LOOPER (top right). Pick a sound per track,\n"
              "hit REC and stack a whole beat from one laptop." },
        };

        const int stepH = (area.getHeight() - 70) / 3;
        for (const auto& s : steps)
        {
            auto row = area.removeFromTop (stepH);
            auto numBox = row.removeFromLeft (56);

            g.setColour (theme.accent);
            g.setFont (juce::Font (juce::FontOptions (30.0f).withStyle ("Bold")));
            g.drawText (s.num, numBox.removeFromTop (40), juce::Justification::centred);

            g.setColour (theme.text);
            g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Semibold")));
            g.drawText (s.title, row.removeFromTop (22), juce::Justification::centredLeft);

            g.setColour (theme.textSecondary);
            g.setFont (juce::Font (juce::FontOptions (13.5f)));
            g.drawFittedText (s.body, row.reduced (0, 2),
                              juce::Justification::topLeft, 3);
            area.removeFromTop (4);
        }

        g.setColour (theme.textSecondary.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Every knob is safe to turn. Press ? in the toolbar to see this again.",
                    getCardBounds().reduced (36, 0).removeFromBottom (86).removeFromTop (18),
                    juce::Justification::centred);
    }

private:
    juce::Rectangle<int> getCardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 60),
                                                       juce::jmin (560, getHeight() - 60));
    }

    juce::TextButton startButton { "START MAKING BEATS" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WelcomePanel)
};
