// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 소리가 아니라 '라이선스 잠금 해제 화면'이에요. 구매자가 키를 입력하면 온라인 확인
// (Gumroad, 기기 수 제한) 또는 오프라인 확인("VCS-" 키)을 하고, 성공하면 활성화를 저장하고 창을 닫습니다.
// [스레드 교훈] 온라인 확인은 네트워크 대기(블로킹)라 별도 스레드에서 합니다. 그 스레드를 detach(방치)하지
//   않고 '소유'해 소멸자에서 join(끝날 때까지 기다림) 합니다. 방치된 스레드가 플러그인보다 오래 살아남으면
//   호스트가 죽을 수 있기 때문이에요. 결과는 callAsync로 다시 메시지 스레드로 돌아와 UI를 갱신합니다.

#pragma once

#include <JuceHeader.h>
#include "../Licensing.h"
#include "ThemeManager.h"
// std::thread: 백그라운드 스레드를 만들기 위한 표준 헤더.
#include <thread>

class VocalChopAudioProcessor;

/**
    Full-window overlay where a buyer enters their license.

    Gumroad keys go online once (device-counted); "VCS-" keys verify
    offline. On success the activation is stored machine-bound and the
    overlay dismisses itself.
*/
// [클래스] UnlockPanel — 창 전체를 덮는 오버레이(뒤의 클릭/키를 막고 라이선스 입력만 받음).
class UnlockPanel : public juce::Component
{
public:
    /** finalize(email, key) must persist the activation and flip the
        processor's licensed flag; returns false if saving failed. */
    // [생성자] finalize 콜백(활성화 저장+라이선스 플래그 켜기)을 받아 저장하고, 위젯들을 만듦.
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

    // [소멸자] 진행 중인 온라인 확인 스레드를 반드시 기다렸다가(join) 정리 — 방치하면 호스트 크래시 위험.
    ~UnlockPanel() override
    {
        // Wait for any in-flight activation call: a thread that outlives the
        // editor (or the module) would crash the host.
        joinNetThread();
    }

    // [함수] resized — 카드 안에 제목/설명/이메일/키/버튼/상태를 위에서부터 차례로 배치.
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

    // [함수] paint — 뒤 화면을 어둡게 덮고, 가운데 카드(테두리 포함)를 그림.
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

    // [함수] mouseDown — 카드 바깥을 클릭하면 '나중에'로 간주해 닫음.
    void mouseDown (const juce::MouseEvent& e) override
    {
        // Click outside the card = "later".
        if (! getCardBounds().contains (e.getPosition()))
            setVisible (false);
    }

private:
    // [함수] getCardBounds — 화면 중앙에 놓일 카드 사각형 계산.
    juce::Rectangle<int> getCardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (460, getWidth() - 40), 322);
    }

    // [함수] tryActivate — 입력된 키를 검증. 오프라인 키는 즉시, 온라인(Gumroad) 키는 백그라운드 스레드로.
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
        // [스레드] 네트워크 확인은 UI를 멈추면 안 되므로 별도 스레드에서. 이전 스레드는 먼저 join으로 정리.
        joinNetThread();
        activateButton.setEnabled (false);
        showStatus ("Checking key...", true);

        // SafePointer: 스레드가 끝날 때 이 패널이 이미 사라졌을 수 있어 안전하게 참조.
        juce::Component::SafePointer<UnlockPanel> self (this);
        netThread = std::make_unique<std::thread> ([self, key]
        {
            // (백그라운드) 온라인 확인. 결과가 나오면 callAsync로 다시 메시지 스레드로 넘겨 UI를 갱신.
            const auto result = vcs::Licensing::activateOnline (key);
            juce::MessageManager::callAsync ([self, result, key]
            {
                // 패널이 사라졌으면 아무것도 안 함.
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

    // [함수] joinNetThread — 실행 중인 스레드가 있으면 끝날 때까지 기다렸다가(join) 정리(reset).
    void joinNetThread()
    {
        if (netThread != nullptr)
        {
            if (netThread->joinable())
                netThread->join();
            netThread.reset();
        }
    }

    // [함수] activated — 성공 메시지를 띄우고 1.6초 뒤 오버레이를 닫음.
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

    // [함수] showStatus — 상태 메시지를 초록(성공)/빨강(실패)으로 표시.
    void showStatus (const juce::String& text, bool good)
    {
        status.setColour (juce::Label::textColourId,
                          good ? juce::Colour (0xff34c759) : juce::Colour (0xffff453a));
        status.setText (text, juce::dontSendNotification);
    }

    // 활성화 저장 콜백과, 소유한 네트워크 스레드(소멸자에서 join).
    std::function<bool (juce::String, juce::String)> finalize;
    std::unique_ptr<std::thread> netThread;

    juce::Label      title, info, status;
    juce::TextEditor emailBox, keyBox;
    juce::TextButton activateButton { "ACTIVATE" };
    juce::TextButton laterButton    { "LATER" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnlockPanel)
};
