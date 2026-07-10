// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 소리가 아니라 '첫 실행 환영 화면'이에요. 처음 켠 사용자가 10초 안에 소리를 내도록
// 세 단계(PLAY / SOUNDS / LOOP)를 큼직하게 안내합니다. 한 번 보면 플래그 파일에 기록해 다시 안 뜨고,
// 툴바의 '?' 버튼으로 언제든 다시 열 수 있습니다.

#pragma once

#include <JuceHeader.h>
#include "ThemeManager.h"
#include "../Licensing.h"

/**
    First-run quick-start overlay: three big steps (PLAY / SOUNDS / LOOP)
    so a first-time user makes sound within ten seconds. Shown once (a
    flag file remembers), and re-openable any time from the toolbar "?".
*/
// [클래스] WelcomePanel — 창을 덮는 안내 오버레이.
class WelcomePanel : public juce::Component
{
public:
    // [생성자] '시작' 버튼을 만들고, 누르면 봤음을 기록한 뒤 닫히도록 연결.
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

    // [함수] seenFile — '봤음' 표시 플래그 파일 경로(라이선스 파일 옆에 둠).
    static juce::File seenFile()
    {
        return vcs::Licensing::licenseFile().getSiblingFile ("welcomed.flag");
    }
    // 봤는지 확인 / 봤다고 기록(파일 생성).
    static bool hasSeenWelcome()   { return seenFile().existsAsFile(); }
    static void markSeen()
    {
        auto f = seenFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText ("1");
    }

    // [함수] resized — 카드 아래쪽에 시작 버튼을 배치.
    void resized() override
    {
        auto card = getCardBounds();
        startButton.setBounds (card.removeFromBottom (64)
                                   .withSizeKeepingCentre (220, 40));
    }

    // [함수] mouseDown — 카드 바깥을 클릭하면 봤음 기록 후 닫음.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! getCardBounds().contains (e.getPosition()))
        {
            markSeen();
            setVisible (false);
        }
    }

    // [함수] paint — 어두운 배경 + 카드 + 제목/부제 + 3단계 안내 + 하단 문구를 그림.
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

        // [데이터] 3단계 안내 항목(번호/제목/본문). 아래 루프가 각 항목을 한 줄씩 그림.
        struct Step { const char* num; const char* title; const char* body; };
        const Step steps[] = {
            { "1", "PLAY",
              "Hit DEMO in the toolbar, then play your computer keys\n"
              "Z S X D C V G B H N J M  (or any MIDI keyboard)." },
            { "2", "SOUNDS",
              "Open the Instrument menu for 314 sounds - drums, 808s,\n"
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
    // [함수] getCardBounds — 화면 중앙의 카드 사각형(최대 560, 창보다 살짝 작게).
    juce::Rectangle<int> getCardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 60),
                                                       juce::jmin (560, getHeight() - 60));
    }

    // 시작 버튼.
    juce::TextButton startButton { "START MAKING BEATS" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WelcomePanel)
};
