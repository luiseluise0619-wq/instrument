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

        // Scrim: a soft graded dim, not a flat black wash.
        {
            juce::ColourGradient scrim (juce::Colours::black.withAlpha (0.62f),
                                        0.0f, 0.0f,
                                        juce::Colours::black.withAlpha (0.78f),
                                        0.0f, (float) getHeight(), false);
            g.setGradientFill (scrim);
            g.fillAll();
        }

        auto card = getCardBounds().toFloat();
        const float radius = 20.0f;

        // Layered soft shadow under the sheet (cheap offset fills).
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            g.fillRoundedRectangle (card.translated (0.0f, (float) i * 3.0f)
                                        .expanded ((float) i * 1.5f),
                                    radius + (float) i * 1.5f);
        }

        // Sheet: theme ground lifted a step, hairline border, top glass line.
        g.setColour (theme.bgTop.brighter (theme.dark ? 0.16f : 0.02f));
        g.fillRoundedRectangle (card, radius);
        {
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (theme.dark ? 0.05f : 0.4f),
                                        card.getX(), card.getY(),
                                        juce::Colours::white.withAlpha (0.0f),
                                        card.getX(), card.getY() + card.getHeight() * 0.4f,
                                        false);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (card, radius);
        }
        g.setColour (theme.separator);
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);

        auto area = getCardBounds().reduced (34, 30);

        g.setColour (theme.text);
        g.setFont (juce::Font (juce::FontOptions (27.0f).withStyle ("Bold"))
                       .withExtraKerningFactor (-0.01f));
        g.drawText ("Welcome to Slyce", area.removeFromTop (36),
                    juce::Justification::centred);

        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (13.5f)));
        g.drawText ("Three steps and you're making beats.",
                    area.removeFromTop (22), juce::Justification::centred);
        area.removeFromTop (16);

        struct Step { const char* num; const char* title; const char* body; };
        const Step steps[] = {
            { "1", "Play",
              "Hit DEMO in the toolbar, then play your computer keys\n"
              "Z S X D C V G B H N J M  (or any MIDI keyboard)." },
            { "2", "Sounds",
              "Preset > Sounds loads a complete patch in one click.\n"
              "Or open INSTRUMENT for all 376 voices, by category." },
            { "3", "Loop",
              "Open LOOPER (top right). Pick a sound per track,\n"
              "hit REC and stack a whole beat from one laptop." },
        };

        const int stepH = (area.getHeight() - 74) / 3;
        int stepIndex = 0;
        for (const auto& s : steps)
        {
            auto row = area.removeFromTop (stepH);

            // Hairline between rows, iOS settings-list style.
            if (stepIndex++ > 0)
            {
                g.setColour (theme.separator.withAlpha (0.6f));
                g.fillRect (row.getX() + 46, row.getY(), row.getWidth() - 46, 1);
            }

            auto numCol = row.removeFromLeft (46);
            const float badge = 26.0f;
            auto b = juce::Rectangle<float> (badge, badge)
                         .withCentre ({ (float) numCol.getCentreX() - 2.0f,
                                        (float) row.getY() + 22.0f });
            g.setColour (theme.accent.withAlpha (0.16f));
            g.fillEllipse (b);
            g.setColour (theme.accent);
            g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
            g.drawText (s.num, b, juce::Justification::centred);

            row.removeFromTop (8);
            g.setColour (theme.text);
            g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Semibold")));
            g.drawText (s.title, row.removeFromTop (21), juce::Justification::centredLeft);

            g.setColour (theme.textSecondary);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawFittedText (s.body, row.reduced (0, 1),
                              juce::Justification::topLeft, 3);
        }

        g.setColour (theme.textSecondary.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText ("Every knob is safe to turn. Press ? in the toolbar to see this again.",
                    getCardBounds().reduced (34, 0).removeFromBottom (88).removeFromTop (18),
                    juce::Justification::centred);
    }

private:
    juce::Rectangle<int> getCardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 60),
                                                       juce::jmin (560, getHeight() - 60));
    }

    juce::TextButton startButton { "Start making beats" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WelcomePanel)
};
