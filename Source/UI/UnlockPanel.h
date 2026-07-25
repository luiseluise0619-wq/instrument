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
        title.setText ("Unlock Slyce", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
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

        activateButton.getProperties().set ("primaryAction", true);
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
        // Cancel any in-flight activation FIRST (the progress callback aborts
        // the connection within milliseconds), then join - so closing the
        // window never blocks the host UI for a network timeout.
        if (cancelNet != nullptr)
            cancelNet->store (true);
        joinNetThread();
    }

    void resized() override
    {
        auto card = getCardBounds();
        card = card.reduced (28, 24);

        card.removeFromTop (46);            // the lock glyph lives here
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

        // Graded scrim, matching the welcome sheet.
        {
            juce::ColourGradient scrim (juce::Colours::black.withAlpha (0.58f), 0.0f, 0.0f,
                                        juce::Colours::black.withAlpha (0.74f),
                                        0.0f, (float) getHeight(), false);
            g.setGradientFill (scrim);
            g.fillAll();
        }

        auto card = getCardBounds().toFloat();
        const float radius = 20.0f;

        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            g.fillRoundedRectangle (card.translated (0.0f, (float) i * 3.0f)
                                        .expanded ((float) i * 1.5f),
                                    radius + (float) i * 1.5f);
        }

        g.setColour (theme.bgTop.brighter (theme.dark ? 0.16f : 0.02f));
        g.fillRoundedRectangle (card, radius);
        {
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (theme.dark ? 0.05f : 0.4f),
                                        card.getX(), card.getY(),
                                        juce::Colours::white.withAlpha (0.0f),
                                        card.getX(), card.getY() + card.getHeight() * 0.4f, false);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (card, radius);
        }
        g.setColour (theme.separator);
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);

        // Accent key glyph above the title - a lock, drawn not imported.
        {
            const float cx = card.getCentreX();
            const float cy = card.getY() + 44.0f;
            g.setColour (theme.accent.withAlpha (0.16f));
            g.fillEllipse (cx - 19.0f, cy - 19.0f, 38.0f, 38.0f);
            g.setColour (theme.accent);
            juce::Rectangle<float> body (cx - 8.0f, cy - 1.0f, 16.0f, 13.0f);
            g.fillRoundedRectangle (body, 3.0f);
            juce::Path shackle;
            shackle.addCentredArc (cx, cy - 1.0f, 5.5f, 6.5f, 0.0f,
                                   -juce::MathConstants<float>::halfPi,
                                   juce::MathConstants<float>::halfPi, true);
            g.strokePath (shackle, juce::PathStrokeType (2.0f));
        }

        title.setColour (juce::Label::textColourId, theme.text);
        info.setColour (juce::Label::textColourId, theme.textSecondary);
        status.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        // Text fields follow the theme too - the stock JUCE editor shipped a
        // teal outline that belonged to no palette here.
        for (auto* ed : { &emailBox, &keyBox })
        {
            ed->setColour (juce::TextEditor::backgroundColourId,
                           theme.control.withAlpha (theme.dark ? 0.85f : 1.0f));
            ed->setColour (juce::TextEditor::outlineColourId, theme.separator);
            ed->setColour (juce::TextEditor::focusedOutlineColourId, theme.accent);
            ed->setColour (juce::TextEditor::textColourId, theme.text);
            ed->setColour (juce::TextEditor::highlightColourId, theme.accentSoft);
            ed->setColour (juce::TextEditor::highlightedTextColourId, theme.text);
            ed->setColour (juce::CaretComponent::caretColourId, theme.accent);
            ed->setFont (juce::Font (juce::FontOptions (13.5f)));
        }
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
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (460, getWidth() - 40), 372);
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
        if (cancelNet != nullptr)
            cancelNet->store (true);   // abort a previous attempt, if any
        joinNetThread();
        activateButton.setEnabled (false);
        showStatus ("Checking key...", true);

        cancelNet = std::make_shared<std::atomic<bool>> (false);
        auto cancel = cancelNet;
        juce::Component::SafePointer<UnlockPanel> self (this);
        netThread = std::make_unique<std::thread> ([self, key, cancel]
        {
            const auto result = vcs::Licensing::activateOnline (key, cancel.get());
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
    std::shared_ptr<std::atomic<bool>> cancelNet;   // aborts the HTTP attempt

    juce::Label      title, info, status;
    juce::TextEditor emailBox, keyBox;
    juce::TextButton activateButton { "Activate" };
    juce::TextButton laterButton    { "Later" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnlockPanel)
};
