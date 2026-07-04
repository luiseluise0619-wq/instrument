#pragma once

#include <JuceHeader.h>
#include "../Licensing.h"
#include "ThemeManager.h"
#include <thread>

class VocalChopAudioProcessor;

/**
    Full-window overlay where a buyer enters their license.

    Gumroad keys go online once (device-counted); "VCS-" keys verify
    offline. On success the activation is stored machine-bound and the
    overlay dismisses itself.
*/
class UnlockPanel : public juce::Component
{
public:
    /** finalize(email, key) must persist the activation and flip the
        processor's licensed flag; returns false if saving failed. */
    explicit UnlockPanel (std::function<bool (juce::String, juce::String)> finalizeFn)
        : finalize (std::move (finalizeFn))
    {
        title.setText ("UNLOCK VOCALCHOP STUDIO", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Semibold")));
        title.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title);

        info.setText ("Demo mode mutes the output for 2 seconds every minute.\n"
                      "Paste the license key from your Gumroad receipt.",
                      juce::dontSendNotification);
        info.setFont (juce::Font (juce::FontOptions (13.0f)));
        info.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (info);

        emailBox.setTextToShowWhenEmpty ("e-mail (for VCS- keys)", juce::Colours::grey);
        addAndMakeVisible (emailBox);

        keyBox.setTextToShowWhenEmpty ("license key", juce::Colours::grey);
        addAndMakeVisible (keyBox);

        activateButton.onClick = [this] { tryActivate(); };
        addAndMakeVisible (activateButton);

        laterButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (laterButton);

        status.setJustificationType (juce::Justification::centred);
        status.setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (status);

        setWantsKeyboardFocus (true);   // swallow clicks/keys behind the veil
    }

    void resized() override
    {
        auto card = getCardBounds();
        card = card.reduced (26, 22);

        title.setBounds (card.removeFromTop (30));
        card.removeFromTop (4);
        info.setBounds (card.removeFromTop (40));
        card.removeFromTop (12);
        emailBox.setBounds (card.removeFromTop (30));
        card.removeFromTop (10);
        keyBox.setBounds (card.removeFromTop (30));
        card.removeFromTop (14);

        auto row = card.removeFromTop (34);
        laterButton.setBounds (row.removeFromLeft (row.getWidth() / 3));
        row.removeFromLeft (10);
        activateButton.setBounds (row);

        card.removeFromTop (10);
        status.setBounds (card.removeFromTop (36));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = ThemeManager::active();

        g.fillAll (juce::Colours::black.withAlpha (0.72f));   // dim the studio

        auto card = getCardBounds().toFloat();
        g.setColour (theme.glow >= 0.9f ? juce::Colour (0xf80a0f24) : theme.bgTop);
        g.fillRoundedRectangle (card, 14.0f);
        g.setColour (theme.accent.withAlpha (0.5f));
        g.drawRoundedRectangle (card.reduced (0.5f), 14.0f, 1.4f);

        title.setColour (juce::Label::textColourId, theme.text);
        info.setColour (juce::Label::textColourId, theme.textSecondary);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Click outside the card = "later".
        if (! getCardBounds().contains (e.getPosition()))
            setVisible (false);
    }

private:
    juce::Rectangle<int> getCardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (460, getWidth() - 40), 322);
    }

    void tryActivate()
    {
        const auto email = emailBox.getText();
        const auto key   = keyBox.getText();

        if (vcs::Licensing::normKey (key).isEmpty())
        {
            showStatus ("Enter a license key first.", false);
            return;
        }

        if (vcs::Licensing::looksLikeOfflineKey (key))
        {
            if (vcs::Licensing::verifyOfflineKey (email, key) && finalize (email, key))
                activated ("Activated - welcome aboard!");
            else
                showStatus ("Key and e-mail don't match. Use the exact e-mail "
                            "the key was issued for.", false);
            return;
        }

        // Gumroad key: one blocking network call on a background thread.
        activateButton.setEnabled (false);
        showStatus ("Checking key...", true);

        juce::Component::SafePointer<UnlockPanel> self (this);
        std::thread ([self, key]
        {
            const auto result = vcs::Licensing::activateOnline (key);
            juce::MessageManager::callAsync ([self, result, key]
            {
                if (self == nullptr)
                    return;
                self->activateButton.setEnabled (true);
                if (result.ok && self->finalize (result.email, key))
                    self->activated (result.message);
                else
                    self->showStatus (result.message, false);
            });
        }).detach();
    }

    void activated (const juce::String& message)
    {
        showStatus (message, true);
        juce::Component::SafePointer<UnlockPanel> self (this);
        juce::Timer::callAfterDelay (1600, [self]
        {
            if (self != nullptr)
                self->setVisible (false);
        });
    }

    void showStatus (const juce::String& text, bool good)
    {
        status.setColour (juce::Label::textColourId,
                          good ? juce::Colour (0xff34c759) : juce::Colour (0xffff453a));
        status.setText (text, juce::dontSendNotification);
    }

    std::function<bool (juce::String, juce::String)> finalize;

    juce::Label      title, info, status;
    juce::TextEditor emailBox, keyBox;
    juce::TextButton activateButton { "ACTIVATE" };
    juce::TextButton laterButton    { "LATER" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnlockPanel)
};
