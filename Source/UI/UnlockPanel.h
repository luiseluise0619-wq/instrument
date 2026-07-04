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
        title.setText ("UNLOCK SLYCE", juce::dontSendNotification);
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

    ~UnlockPanel() override
    {
        // Wait for any in-flight activation call: a thread that outlives the
        // editor (or the module) would crash the host.
        joinNetThread();
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
            if (! vcs::Licensing::verifyOfflineKey (email, key))
                showStatus ("Key and e-mail don't match. Use the exact e-mail "
                            "the key was issued for.", false);
            else if (! finalize (email, key))
                showStatus (saveFailedMessage(), false);
            else
                activated ("Activated - welcome aboard!");
            return;
        }

        // Gumroad key: one blocking network call on a background thread. The
        // thread is OWNED (joined in the destructor), never detached — a
        // detached thread can outlive the plugin module and crash the host.
        joinNetThread();
        activateButton.setEnabled (false);
        showStatus ("Checking key...", true);

        juce::Component::SafePointer<UnlockPanel> self (this);
        netThread = std::make_unique<std::thread> ([self, key]
        {
            const auto result = vcs::Licensing::activateOnline (key);
            juce::MessageManager::callAsync ([self, result, key]
            {
                if (self == nullptr)
                    return;
                self->activateButton.setEnabled (true);
                if (! result.ok)
                    self->showStatus (result.message, false);
                else if (! self->finalize (result.email, key))
                    // The key verified (and consumed a device slot), but the
                    // licence file could not be written — say THAT, not
                    // "invalid key".
                    self->showStatus (self->saveFailedMessage(), false);
                else
                    self->activated (result.message);
            });
        });
    }

    juce::String saveFailedMessage() const
    {
        return "Key verified, but the license file could not be saved to "
               + vcs::Licensing::licenseFile().getParentDirectory().getFullPathName()
               + ". Check folder permissions and try again.";
    }

    void joinNetThread()
    {
        if (netThread != nullptr)
        {
            if (netThread->joinable())
                netThread->join();
            netThread.reset();
        }
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
    std::unique_ptr<std::thread> netThread;

    juce::Label      title, info, status;
    juce::TextEditor emailBox, keyBox;
    juce::TextButton activateButton { "ACTIVATE" };
    juce::TextButton laterButton    { "LATER" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnlockPanel)
};
