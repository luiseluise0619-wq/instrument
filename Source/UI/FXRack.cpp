// [파일 역할] FXRack.h의 구현. 이펙트 노브 3개(Drive/Reverb/Delay) 생성·배치·그리기.
#include "FXRack.h"
#include "ThemeManager.h"

// [생성자] 세 개의 이펙트 모듈을 파라미터 id와 함께 추가.
FXRack::FXRack (juce::AudioProcessorValueTreeState& apvts)
{
    addModule (apvts, "drive",  "Drive");
    addModule (apvts, "reverb", "Reverb");
    addModule (apvts, "delay",  "Delay");
}

// [함수] addModule — 노브를 만들고 파라미터에 연결한 뒤 모듈 목록에 추가.
void FXRack::addModule (juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& paramID, const juce::String& caption)
{
    Module m;
    m.knob = std::make_unique<KnobComponent> (caption);
    m.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, m.knob->getSlider());
    addAndMakeVisible (*m.knob);

    // Keep the rack's neon value tracks in sync while the knob moves.
    // (KnobComponent leaves onValueChange free for clients; attachments use
    // Slider::Listener, so this callback is ours.)
    // [콜백] 노브를 돌릴 때 네온 값 트랙을 다시 그리도록(글로우 테마에서만). onValueChange는 우리 몫.
    m.knob->getSlider().onValueChange = [this]
    {
        if (ThemeManager::active().glow >= 0.9f)
            repaint();
    };

    modules.push_back (std::move (m));
}

// [함수] paint — 카드 배경 + 'FX' 제목 + (글로우 테마면 네온 값 트랙, 아니면 얇은 구분선)을 그림.
void FXRack::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    const bool  glowTheme = theme.glow >= 0.9f;

    const auto  bounds = getLocalBounds().toFloat();
    const float radius = theme.cornerRadius;

    // Card region inset slightly so the drop shadow has room to breathe.
    const auto card = bounds.reduced (2.0f);

    // Soft drop shadow beneath the material card.
    {
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle (card, radius);
        juce::DropShadow (theme.shadow, 10, { 0, 2 }).drawForPath (g, shadowPath);
    }

    if (glowTheme)
    {
        // Dark glass fill + neon rim, matching the editor's cards.
        g.setColour (juce::Colour (0xc008102a));
        g.fillRoundedRectangle (card, radius);

        g.setColour (theme.accent.withAlpha (0.10f));
        g.drawRoundedRectangle (card.expanded (1.0f), radius + 1.0f, 2.5f);
        g.setColour (theme.accent.withAlpha (0.28f));
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);
    }
    else
    {
        // Material fill with a gentle top-to-bottom vibrancy gradient.
        juce::ColourGradient fill (theme.materialStrong, card.getX(), card.getY(),
                                   theme.material,       card.getX(), card.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (card, radius);

        // 1px hairline border, picking up a whisper of accent on glow themes.
        const juce::Colour borderColour =
            theme.glow > 0.0f ? theme.separator.interpolatedWith (theme.accentSoft, 0.35f * theme.glow)
                              : theme.separator;
        g.setColour (borderColour);
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);
    }

    // Uppercase, tracked title "FX" in the top padding strip.
    {
        const float pad = 16.0f;
        auto titleArea = card.reduced (pad, 0.0f).withTop (card.getY() + 12.0f).withHeight (16.0f);

        const juce::Colour titleColour =
            glowTheme       ? theme.accent
          : theme.glow > 0.0f ? theme.textSecondary.interpolatedWith (theme.accent, 0.30f * theme.glow)
                              : theme.textSecondary;
        g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Semibold")));

        // Draw glyph-by-glyph to add wide letter-spacing (tracking).
        const juce::String title ("FX");
        const float tracking = glowTheme ? 3.5f : 3.0f;
        const auto& font = g.getCurrentFont();

        float x = titleArea.getX();
        const float cy = titleArea.getCentreY();
        for (auto ch : title)
        {
            const juce::String s = juce::String::charToString (ch);
            const float w = font.getStringWidthFloat (s);
            const juce::Rectangle<float> glyphBox (x, cy - 8.0f, w + tracking, 16.0f);

            // Soft neon halo behind each glyph on the cyberpunk theme.
            if (glowTheme)
            {
                static const float dx[] = { -1.2f, 1.2f,  0.0f, 0.0f };
                static const float dy[] = {  0.0f, 0.0f, -1.2f, 1.2f };

                g.setColour (theme.accent.withAlpha (0.22f));
                for (int o = 0; o < 4; ++o)
                    g.drawText (s, glyphBox.translated (dx[o], dy[o]),
                                juce::Justification::centredLeft, false);
            }

            g.setColour (titleColour);
            g.drawText (s, glyphBox, juce::Justification::centredLeft, false);
            x += w + tracking;
        }
    }

    if (glowTheme)
    {
        // Each module gets a slim cyan->purple value track with a small
        // glowing dot at its current position, drawn in the gap below it.
        const float pad = 16.0f;
        const juce::Colour purple (0xffb026ff);

        for (const auto& m : modules)
        {
            const auto kb = m.knob->getBounds().toFloat();
            if (kb.isEmpty())
                continue;

            auto& s = m.knob->getSlider();
            const float pos = juce::jlimit (0.0f, 1.0f,
                                            (float) s.valueToProportionOfLength (s.getValue()));

            const float y = kb.getBottom() + moduleGap * 0.5f;
            const juce::Rectangle<float> track (card.getX() + pad, y - 1.5f,
                                                card.getWidth() - pad * 2.0f, 3.0f);

            // Inactive track base.
            g.setColour (theme.controlTrack);
            g.fillRoundedRectangle (track, 1.5f);

            // Lit portion sweeps cyan -> electric purple, like the knob arcs.
            juce::ColourGradient grad (theme.accent.withAlpha (0.60f), track.getX(),     y,
                                       purple.withAlpha (0.60f),       track.getRight(), y, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (track.withWidth (juce::jmax (3.0f, track.getWidth() * pos)), 1.5f);

            // Small glowing value dot at the current position.
            const juce::Point<float> dot (track.getX() + track.getWidth() * pos, y);
            const juce::Colour dotCol = theme.accent.interpolatedWith (purple, pos);

            g.setColour (dotCol.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (dot));
            g.setColour (dotCol.withAlpha (0.60f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
            g.setColour (juce::Colours::white.interpolatedWith (dotCol, 0.25f));
            g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (dot));
        }
    }
    else if (modules.size() > 1)
    {
        // Hairline dividers between the stacked modules.
        const float pad = 16.0f;
        g.setColour (theme.separator);

        for (size_t i = 1; i < modules.size(); ++i)
        {
            const auto b = modules[i].knob->getBounds().toFloat();
            const float y = b.getY() - (moduleGap * 0.5f);
            g.fillRect (juce::Rectangle<float> (card.getX() + pad, y,
                                                card.getWidth() - pad * 2.0f, 1.0f));
        }
    }
}

// [함수] resized — 카드 안쪽을 패딩만큼 줄이고 제목 자리를 뺀 뒤, 모듈들을 같은 높이로 세로 분배.
void FXRack::resized()
{
    auto area = getLocalBounds().reduced (2);   // match the card inset
    area = area.reduced (16);                    // generous interior padding
    area.removeFromTop (titleStrip);             // reserve the title strip

    if (modules.empty())
        return;

    // 모듈 수와 간격을 빼고 남은 높이를 균등 분배.
    const int count       = (int) modules.size();
    const int totalGaps    = (count - 1) * moduleGap;
    const int slotHeight   = (area.getHeight() - totalGaps) / count;

    for (int i = 0; i < count; ++i)
    {
        auto slot = area.removeFromTop (slotHeight);
        modules[(size_t) i].knob->setBounds (slot);

        if (i < count - 1)
            area.removeFromTop (moduleGap);
    }
}
