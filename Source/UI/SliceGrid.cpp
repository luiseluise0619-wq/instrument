// [파일 역할] SliceGrid.h의 구현. 피아노 건반 그리기 + 마우스 입력으로 슬라이스 트리거 + 물방울 연출.
#include "SliceGrid.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
    // 건반 위 클래식한 펠트 띠의 높이.
    constexpr float kFeltHeight = 5.0f;   // classic felt strip above the keys
}

// [생성자] Processor를 저장하고 60Hz 타이머 시작(빛/물방울 애니메이션용).
SliceGrid::SliceGrid (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    startTimerHz (60);
}

// [소멸자] 타이머 정지.
SliceGrid::~SliceGrid()
{
    stopTimer();
}

//==============================================================================
// [함수] isBlackKey — 반음 값이 검은건반(C#,D#,F#,G#,A#)인지 판정. %12로 옥타브 무관하게.
bool SliceGrid::isBlackKey (int semitone)
{
    const int s = semitone % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}

// [함수] keySpan — 그릴 건반 개수(반음 단위) 결정. 신스는 3옥타브 고정, 찹은 슬라이스 수에 맞춤.
int SliceGrid::keySpan() const
{
    // Synth mode: a three-octave keyboard (every key makes sound); the
    // OCT -/+ buttons shift the whole instrument a further +-2 octaves.
    if (proc.isSynthMode())
        return 36;

    // Chop mode: whole octaves, at least one, enough to cover every slice.
    const int numSlices = proc.getSliceEngine().getNumSlices();
    const int octaves   = juce::jlimit (1, 3, (numSlices + 11) / 12);
    return octaves * 12;
}

// [함수] keysArea — 건반들이 그려질 전체 영역(여백/펠트 제외).
juce::Rectangle<float> SliceGrid::keysArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 8.0f)
                           .withTrimmedTop (kFeltHeight + 2.0f);
}

// [함수] keyRect — 특정 건반 하나의 사각형 위치/크기 계산(흰건반은 폭 균등, 검은건반은 경계에 얹힘).
juce::Rectangle<float> SliceGrid::keyRect (int semitone, int span) const
{
    const auto area = keysArea();

    // Count white keys in the span and this key's white index.
    int whitesTotal = 0, whitesBefore = 0;
    for (int s = 0; s < span; ++s)
    {
        if (! isBlackKey (s))
        {
            if (s < semitone) ++whitesBefore;
            ++whitesTotal;
        }
    }

    const float whiteW = area.getWidth() / (float) juce::jmax (1, whitesTotal);

    if (! isBlackKey (semitone))
        return { area.getX() + whitesBefore * whiteW, area.getY(),
                 whiteW, area.getHeight() };

    // Black key: centred on the boundary after the previous white key.
    const float blackW = whiteW * 0.60f;
    const float x = area.getX() + whitesBefore * whiteW - blackW * 0.5f;
    return { x, area.getY(), blackW, area.getHeight() * 0.615f };
}

// [함수] keyAt — 마우스 좌표가 어느 건반 위인지 반환. 검은건반이 위에 있으므로 먼저 검사.
int SliceGrid::keyAt (juce::Point<float> p) const
{
    const int span = keySpan();

    // Black keys sit on top, so hit-test them first.
    for (int s = 0; s < span; ++s)
        if (isBlackKey (s) && keyRect (s, span).contains (p))
            return s;
    for (int s = 0; s < span; ++s)
        if (! isBlackKey (s) && keyRect (s, span).contains (p))
            return s;
    return -1;
}

//==============================================================================
// [함수] paint — 펠트/흰건반/검은건반/눌린 빛/슬라이스 번호 등을 그림(가장 긴 함수, 순수 그리기).
void SliceGrid::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    const int numSlices = proc.getSliceEngine().getNumSlices();
    const int span = keySpan();

    if ((int) keyFlash.size() != span)
        keyFlash.assign ((size_t) span, 0.0f);

    // --- Card behind the keybed -------------------------------------------
    const auto card = getLocalBounds().toFloat().reduced (2.0f);
    juce::DropShadow (theme.shadow, 10, { 0, 2 })
        .drawForRectangle (g, card.getSmallestIntegerContainer());
    g.setColour (theme.material);
    g.fillRoundedRectangle (card, theme.cornerRadius);
    g.setColour (theme.separator);
    g.drawRoundedRectangle (card.reduced (0.5f), theme.cornerRadius, 1.0f);

    const bool synthMode = proc.isSynthMode();

    if (numSlices == 0 && ! synthMode)
    {
        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Medium")));
        g.drawText ("Load a sample to play", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    const auto keys = keysArea();

    // --- Strip above the keys: neon light bar on glow themes, felt otherwise
    {
        auto felt = keys.withY (keys.getY() - kFeltHeight - 2.0f)
                        .withHeight (kFeltHeight);

        if (theme.glow >= 0.9f)
        {
            // Cyan -> pink neon tube with a soft upward glow.
            juce::ColourGradient tube (theme.accent, felt.getX(), felt.getY(),
                                       theme.waveform, felt.getRight(), felt.getY(),
                                       false);
            g.setColour (theme.accent.withAlpha (0.16f));
            g.fillRoundedRectangle (felt.expanded (2.0f, 3.0f), 3.0f);
            g.setGradientFill (tube);
            g.fillRoundedRectangle (felt, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (felt.withHeight (1.0f));
        }
        else
        {
            g.setColour (theme.waveform.darker (0.25f).withAlpha (0.90f));
            g.fillRoundedRectangle (felt, 2.0f);
            // Thin highlight so the felt reads as fabric, not a flat bar.
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (felt.withHeight (1.0f));
        }
    }

    const bool dark = theme.dark;

    // --- Key renderers -------------------------------------------------------
    auto drawWhite = [&] (int s)
    {
        auto r = keyRect (s, span).reduced (1.2f, 0.0f);
        const bool  enabled = synthMode || s < numSlices;
        const float flash   = keyFlash[(size_t) s];
        const bool  hover   = (s == hoveredKey && enabled);
        const float pressed = flash;   // 0..1 visual press amount

        juce::Path key;
        key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                 5.0f, 5.0f, false, false, true, true);

        // Accent under-glow while lit.
        if (flash > 0.02f && theme.glow > 0.0f)
        {
            g.setColour (theme.accent.withAlpha (0.45f * flash * theme.glow));
            g.strokePath (key, juce::PathStrokeType (5.0f));
        }

        // Ivory body: slightly shaded at the top (fallboard shadow), bright
        // toward the front edge. Pressing tilts the gradient darker.
        const juce::Colour ivoryTop = dark ? juce::Colour (0xffc9cfdd)
                                           : juce::Colour (0xffe9e9ee);
        const juce::Colour ivoryBot = dark ? juce::Colour (0xfff4f7ff)
                                           : juce::Colours::white;

        juce::Colour top = ivoryTop.darker (0.10f * pressed);
        juce::Colour bot = ivoryBot.darker (0.14f * pressed);
        if (! enabled) { top = top.withAlpha (0.16f); bot = bot.withAlpha (0.16f); }

        juce::ColourGradient body (top, r.getX(), r.getY(),
                                   bot, r.getX(), r.getBottom(), false);
        if (flash > 0.0f)
        {
            body.multiplyOpacity (1.0f);
            top = top.interpolatedWith (theme.accent, flash * 0.75f);
            bot = bot.interpolatedWith (theme.accent.brighter (0.2f), flash * 0.75f);
            body = juce::ColourGradient (top, r.getX(), r.getY(),
                                         bot, r.getX(), r.getBottom(), false);
        }
        g.setGradientFill (body);
        g.fillPath (key);

        // Hover: a whisper of accent.
        if (hover && flash < 0.3f)
        {
            g.setColour (theme.accent.withAlpha (0.10f));
            g.fillPath (key);
        }

        // Recessed shadow where the key meets the felt.
        juce::ColourGradient recess (juce::Colours::black.withAlpha (enabled ? 0.22f : 0.08f),
                                     r.getX(), r.getY(),
                                     juce::Colours::transparentBlack,
                                     r.getX(), r.getY() + 9.0f, false);
        g.setGradientFill (recess);
        g.fillRect (r.withHeight (9.0f));

        // Side separation: soft dark line on the right edge.
        g.setColour (juce::Colours::black.withAlpha (dark ? 0.35f : 0.15f));
        g.fillRect (juce::Rectangle<float> (r.getRight() - 0.75f, r.getY(),
                                            0.75f, r.getHeight()));

        // Front-edge lip highlight.
        g.setColour (juce::Colours::white.withAlpha (enabled ? 0.35f : 0.08f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 2.0f, r.getBottom() - 2.5f,
                                            r.getWidth() - 4.0f, 1.2f));

        // Octave labels on the Cs.
        if (s % 12 == 0 && enabled)
        {
            g.setColour (juce::Colour (0xff6a7086).withAlpha (0.9f));
            g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Semibold")));
            g.drawText ("C" + juce::String (3 + s / 12),
                        r.reduced (2.0f).removeFromBottom (16.0f),
                        juce::Justification::centred);
        }
    };

    auto drawBlack = [&] (int s)
    {
        auto r = keyRect (s, span);
        const bool  enabled = synthMode || s < numSlices;
        const float flash   = keyFlash[(size_t) s];
        const bool  hover   = (s == hoveredKey && enabled);

        // Drop shadow cast onto the white keys.
        g.setColour (juce::Colours::black.withAlpha (enabled ? 0.35f : 0.15f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f).expanded (1.2f, 0.0f), 4.5f);

        juce::Path key;
        key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                 4.0f, 4.0f, false, false, true, true);

        // Accent glow while lit.
        if (flash > 0.02f && theme.glow > 0.0f)
        {
            g.setColour (theme.accent.withAlpha (0.55f * flash * theme.glow));
            g.strokePath (key, juce::PathStrokeType (5.0f));
        }

        // Glossy lacquer body.
        juce::Colour top (0xff262a44);
        juce::Colour bot (0xff0b0d1c);
        if (flash > 0.0f)
        {
            top = top.interpolatedWith (theme.accent, flash * 0.9f);
            bot = bot.interpolatedWith (theme.accent.darker (0.2f), flash * 0.9f);
        }
        if (! enabled) { top = top.withAlpha (0.35f); bot = bot.withAlpha (0.35f); }

        juce::ColourGradient body (top, r.getX(), r.getY(),
                                   bot, r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillPath (key);

        if (hover && flash < 0.3f)
        {
            g.setColour (theme.accent.withAlpha (0.16f));
            g.fillPath (key);
        }

        // Glossy highlight down the centre-top of the lacquer.
        {
            auto gloss = r.reduced (r.getWidth() * 0.22f, 0.0f)
                          .withTrimmedTop (3.0f)
                          .withHeight (r.getHeight() * 0.45f);
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (enabled ? 0.16f : 0.05f),
                                        gloss.getX(), gloss.getY(),
                                        juce::Colours::transparentWhite,
                                        gloss.getX(), gloss.getBottom(), false);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (gloss, 2.5f);
        }

        // Front face: the lighter lip at the bottom of a real black key.
        {
            auto lip = r.withTrimmedTop (r.getHeight() - 7.0f).reduced (1.0f, 0.0f);
            g.setColour (juce::Colour (0xff353a5c).withAlpha (enabled ? 1.0f : 0.35f)
                             .interpolatedWith (theme.accent, flash * 0.6f));
            g.fillRoundedRectangle (lip, 3.0f);
        }
    };

    // Whites first, then blacks on top.
    for (int s = 0; s < span; ++s)
        if (! isBlackKey (s))
            drawWhite (s);
    for (int s = 0; s < span; ++s)
        if (isBlackKey (s))
            drawBlack (s);

    // --- Water-drop splashes on top of the keybed ---------------------------
    for (const auto& d : drops)
    {
        const float a = juce::jlimit (0.0f, 1.0f, d.life);

        if (d.ring)
        {
            // Expanding ripple: soft outer glow + crisp ring.
            g.setColour (theme.accent.withAlpha (0.18f * a));
            g.drawEllipse (d.x - d.size, d.y - d.size * 0.55f,
                           d.size * 2.0f, d.size * 1.1f, 4.0f);
            g.setColour (theme.accent.withAlpha (0.65f * a));
            g.drawEllipse (d.x - d.size, d.y - d.size * 0.55f,
                           d.size * 2.0f, d.size * 1.1f, 1.4f);
        }
        else
        {
            // Droplet: glow halo + bright core.
            g.setColour (theme.accent.withAlpha (0.25f * a));
            g.fillEllipse (d.x - d.size, d.y - d.size,
                           d.size * 2.0f, d.size * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.80f * a));
            g.fillEllipse (d.x - d.size * 0.45f, d.y - d.size * 0.45f,
                           d.size * 0.9f, d.size * 0.9f);
        }
    }
}

//==============================================================================
// [함수] pressKey — 건반 하나를 눌러 소리 냄. 누른 위치로 세기(velocity)를 정하고 게이트로 잡아둠.
void SliceGrid::pressKey (int key, juce::Point<float> position)
{
    if (key < 0)
        return;
    // Keys past the last slice still play — the engine wraps them onto the
    // available slices, so the top of the keybed is never dead.
    if (! proc.isSynthMode() && proc.getSliceEngine().getNumSlices() <= 0)
        return;

    // Velocity from the strike position, like a real keybed: clicking near
    // the front edge of the key plays louder than up by the felt.
    const auto  r = keyRect (key, keySpan());
    const float posInKey = juce::jlimit (0.0f, 1.0f,
                                         (position.y - r.getY()) / juce::jmax (1.0f, r.getHeight()));
    const float velocity = 0.35f + 0.65f * posInKey;

    // Same mapping as MIDI: key semitone offset == slice index. The key is
    // gated — it sounds until the mouse button is released, like a real key.
    // Only one mouse-held key is tracked, so a second press (multi-touch)
    // must let go of the first or its note would never receive a release.
    // [게이트] 마우스로 잡는 건반은 한 번에 하나만 추적. 새 건반을 누르면 이전 것을 먼저 놓아야 음이 안 물림.
    if (pressedKey >= 0 && pressedKey != key)
        proc.releaseSlicePad (pressedKey);

    proc.pressSlicePad (key, velocity);
    pressedKey = key;

    if (key < (int) keyFlash.size())
        keyFlash[(size_t) key] = 0.55f + 0.45f * velocity;   // light follows strength

    spawnSplash (key, position);   // water-drop splash where the key was struck
    repaint();
}

// [함수] mouseDown — 누른 지점의 건반을 재생.
void SliceGrid::mouseDown (const juce::MouseEvent& e)
{
    pressKey (keyAt (e.position), e.position);
}

// [함수] mouseDrag — 글리산도: 건반 위를 미끄러지면 이전 건반을 놓고 손 아래 건반을 재생.
void SliceGrid::mouseDrag (const juce::MouseEvent& e)
{
    // Glissando: sliding across the keybed releases the old key and plays
    // the one under the pointer.
    const int key = keyAt (e.position);
    if (key == pressedKey)
        return;

    if (pressedKey >= 0)
        proc.releaseSlicePad (pressedKey);
    pressedKey = -1;

    pressKey (key, e.position);
}

// [함수] mouseUp — 마우스를 떼면 잡고 있던 건반을 놓음(음 끝).
void SliceGrid::mouseUp (const juce::MouseEvent&)
{
    if (pressedKey >= 0)
    {
        proc.releaseSlicePad (pressedKey);
        pressedKey = -1;
    }
}

// [함수] flashKey — 바깥(컴퓨터 키보드 등)에서 건반을 빛나게 + 물방울 연출.
void SliceGrid::flashKey (int semitone, float strength)
{
    if (juce::isPositiveAndBelow (semitone, (int) keyFlash.size()))
    {
        keyFlash[(size_t) semitone] = juce::jlimit (0.0f, 1.0f, strength);

        const auto r = keyRect (semitone, keySpan());
        spawnSplash (semitone, { r.getCentreX(), r.getY() + r.getHeight() * 0.30f });
        repaint();
    }
}

// [함수] spawnSplash — 물방울 연출 생성: 퍼지는 파문 링 1개 + 위로 튀었다 떨어지는 방울 6개.
void SliceGrid::spawnSplash (int /*semitone*/, juce::Point<float> at)
{
    // [람다] rnd — 간단한 결정론적 난수(0~1). 방울의 무작위 방향/크기에 사용.
    auto rnd = [this]
    {
        splashSeed = splashSeed * 1664525u + 1013904223u;
        return (float) ((splashSeed >> 8) & 0xffff) / 65535.0f;
    };

    // One expanding ripple ring...
    drops.push_back ({ at.x, at.y, 0.0f, 0.0f, 1.0f, 5.0f, true });

    // ...and a burst of droplets that arc up and fall under gravity.
    for (int i = 0; i < 6; ++i)
        drops.push_back ({ at.x, at.y,
                           (rnd() - 0.5f) * 4.0f,
                           -(2.0f + 3.0f * rnd()),
                           1.0f,
                           1.6f + 2.2f * rnd(),
                           false });

    // Hard cap so mashing the keyboard can't grow the list unbounded.
    if (drops.size() > 140)
        drops.erase (drops.begin(), drops.begin() + (long) (drops.size() - 140));
}

// [함수] mouseMove — 마우스가 올라간 건반이 바뀌면 다시 그림(호버 표시).
void SliceGrid::mouseMove (const juce::MouseEvent& e)
{
    const int key = keyAt (e.position);
    if (key != hoveredKey)
    {
        hoveredKey = key;
        repaint();
    }
}

// [함수] mouseExit — 마우스가 영역을 벗어나면 호버 해제.
void SliceGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoveredKey != -1)
    {
        hoveredKey = -1;
        repaint();
    }
}

// [함수] timerCallback — 눌린 건반은 계속 빛나게 두고, 놓인 건반의 빛과 물방울을 서서히 줄임.
void SliceGrid::timerCallback()
{
    bool any = false;
    for (size_t i = 0; i < keyFlash.size(); ++i)
    {
        // A held key stays lit; released keys fade out.
        if ((int) i == pressedKey)
            continue;

        if (keyFlash[i] > 0.0f)
        {
            keyFlash[i] = juce::jmax (0.0f, keyFlash[i] - 0.05f);
            any = true;
        }
    }

    // Advance the water-splash particles.
    if (! drops.empty())
    {
        for (auto& d : drops)
        {
            if (d.ring)
            {
                d.size += 2.2f;           // ripple expands
                d.life -= 0.055f;
            }
            else
            {
                d.x += d.vx;
                d.y += d.vy;
                d.vy += 0.38f;            // gravity pulls the droplet back down
                d.life -= 0.035f;
            }
        }
        drops.erase (std::remove_if (drops.begin(), drops.end(),
                                     [] (const Drop& d) { return d.life <= 0.0f; }),
                     drops.end());
        any = true;
    }

    if (any)
        repaint();
}
