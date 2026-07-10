// [파일 역할] WaveformView.h의 구현. 파형 포락선 계산 + 카드/파형/슬라이스선/재생선 그리기 + 파일 드롭.
#include "WaveformView.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

#include <cmath>
#include <vector>

namespace
{
    // 카드 가장자리와 파형 사이 안쪽 여백.
    constexpr float kCardPadding = 12.0f;
}

// [생성자] 24Hz의 낮은 빈도 타이머 시작(잔잔한 애니메이션/재생선 갱신).
WaveformView::WaveformView (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    // Subtle, restrained animation. Low rate keeps the Apple look calm.
    startTimerHz (24);
}

// [소멸자] 타이머 정지.
WaveformView::~WaveformView()
{
    stopTimer();
}

// [함수] refresh — 포락선을 다시 만들고 화면 갱신.
void WaveformView::refresh()
{
    rebuildEnvelope();
    repaint();
}

// [함수] rebuildEnvelope — 샘플을 화면 폭만큼의 세로 막대(열)로 요약해 각 열의 최소/최대값을 캐시.
//   [왜] 수십만 샘플을 매번 다 그리면 느림 → 화면 픽셀 수만큼만 미리 요약해 두면 그리기가 빨라짐.
void WaveformView::rebuildEnvelope()
{
    minEnv.clear();
    maxEnv.clear();

    // 로드된 샘플을 가져옴(없으면 그릴 것 없음).
    auto sample = proc.getLoadedSample();
    if (sample == nullptr || sample->getNumSamples() == 0)
        return;

    // Resolve envelope over the drawable area paint() actually uses: the
    // card is the local bounds reduced by 4 (shadow margin) and then by the
    // card padding — mismatched widths here skew slice markers/playheads.
    const int drawWidth = juce::jmax (1, (int) std::floor (
        (float) getWidth() - 8.0f - 2.0f * kCardPadding));
    const int numColumns = juce::jmax (1, drawWidth);
    const int numSamples  = sample->getNumSamples();
    const int numChannels = sample->getNumChannels();

    minEnv.assign ((size_t) numColumns, 0.0f);
    maxEnv.assign ((size_t) numColumns, 0.0f);

    // 한 열이 담당할 원본 샘플 수.
    const int samplesPerColumn = juce::jmax (1, numSamples / numColumns);

    // 각 열마다 담당 구간의 최소/최대값을 찾아 저장.
    for (int col = 0; col < numColumns; ++col)
    {
        const int start = col * samplesPerColumn;
        const int end   = juce::jmin (numSamples, start + samplesPerColumn);
        float mn = 0.0f, mx = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* d = sample->getReadPointer (ch);
            for (int i = start; i < end; ++i)
            {
                mn = juce::jmin (mn, d[i]);
                mx = juce::jmax (mx, d[i]);
            }
        }
        minEnv[(size_t) col] = mn;
        maxEnv[(size_t) col] = mx;
    }
}

// [함수] resized — 크기가 바뀌면 폭이 달라지므로 포락선을 다시 계산.
void WaveformView::resized()
{
    rebuildEnvelope();
}

// [함수] timerCallback — 아주 느린 위상 전진(미세 반짝임) + 재생선 갱신을 위해 다시 그림.
void WaveformView::timerCallback()
{
    // Very slow breathing phase; used only for a near-invisible shimmer.
    phase += 0.015f;
    if (phase > juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;
    repaint();
}

// [함수] paint — 카드 배경 + 파형(채움/윤곽/반사) + 슬라이스 경계선 + 실시간 재생선 + 빈 상태 안내를 그림.
//   재생선 위치는 VoicePool이 atomic으로 게시한 값을 (Processor 경유로) 읽어와 그립니다(오디오↔UI 안전 통신).
void WaveformView::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    const float radius = theme.cornerRadius;
    const float glow   = juce::jlimit (0.0f, 1.0f, theme.glow);
    // High-glow themes ("Neon Ocean") get the full cyberpunk treatment.
    const bool  cyber  = theme.glow >= 0.9f;

    auto full = getLocalBounds().toFloat();
    // Reserve room for the drop shadow so the card doesn't touch the edges.
    auto card = full.reduced (4.0f);

    // --- Soft drop shadow behind the card ------------------------------------
    {
        juce::Path cardPath;
        cardPath.addRoundedRectangle (card, radius);
        juce::DropShadow (theme.shadow, 10, { 0, 2 }).drawForPath (g, cardPath);
    }

    // --- Material card fill + hairline border --------------------------------
    if (cyber)
    {
        // Dark-glass fill + cyan neon rim, matching the editor cards.
        g.setColour (juce::Colour (0xc008102a));
        g.fillRoundedRectangle (card, radius);

        g.setColour (theme.accent.withAlpha (0.10f));
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 3.0f);
        g.setColour (theme.accent.withAlpha (fileHover ? 0.9f : 0.55f));
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);

        if (fileHover)
        {
            // Neon dashed border marks the drop target; a slow alpha pulse
            // gives it an animated feel without moving the dashes.
            juce::Path outline;
            outline.addRoundedRectangle (card.reduced (4.0f), radius - 3.0f);
            juce::Path dashed;
            const float dashPattern[2] = { 7.0f, 5.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline,
                                                            dashPattern, 2);
            g.setColour (theme.accent.withAlpha (0.7f + 0.2f * std::sin (phase * 3.0f)));
            g.fillPath (dashed);
        }
    }
    else
    {
        g.setColour (theme.materialStrong);
        g.fillRoundedRectangle (card, radius);

        // Border tints toward accent while a file is dragged over the view.
        const juce::Colour border = fileHover ? theme.accent
                                              : theme.separator;
        g.setColour (border);
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);
    }

    // --- Empty state ---------------------------------------------------------
    if (maxEnv.empty())
    {
        auto centre = card.getCentre();

        if (cyber)
        {
            // Faint HUD-style corner brackets: four L-shapes in cyan.
            const float arm = 14.0f;
            auto b = card.reduced (10.0f);

            juce::Path hud;
            hud.startNewSubPath (b.getX(),         b.getY() + arm);       // top-left
            hud.lineTo          (b.getX(),         b.getY());
            hud.lineTo          (b.getX() + arm,   b.getY());
            hud.startNewSubPath (b.getRight() - arm, b.getY());           // top-right
            hud.lineTo          (b.getRight(),       b.getY());
            hud.lineTo          (b.getRight(),       b.getY() + arm);
            hud.startNewSubPath (b.getRight(),     b.getBottom() - arm);  // bottom-right
            hud.lineTo          (b.getRight(),     b.getBottom());
            hud.lineTo          (b.getRight() - arm, b.getBottom());
            hud.startNewSubPath (b.getX() + arm,   b.getBottom());        // bottom-left
            hud.lineTo          (b.getX(),         b.getBottom());
            hud.lineTo          (b.getX(),         b.getBottom() - arm);

            g.setColour (theme.accent.withAlpha (0.3f));
            g.strokePath (hud, juce::PathStrokeType (1.5f));
        }

        // SF-symbol-style glyph: a downward arrow into an open tray/box.
        const float gs = 22.0f;                      // glyph size
        const float gx = centre.x;
        const float gy = centre.y - 16.0f;

        juce::Path glyph;

        // Arrow shaft.
        glyph.startNewSubPath (gx, gy - gs * 0.55f);
        glyph.lineTo          (gx, gy + gs * 0.15f);
        // Arrow head.
        glyph.startNewSubPath (gx - gs * 0.28f, gy - gs * 0.1f);
        glyph.lineTo          (gx,              gy + gs * 0.2f);
        glyph.lineTo          (gx + gs * 0.28f, gy - gs * 0.1f);

        // Tray / box below the arrow.
        juce::Path tray;
        const float tw = gs * 0.9f;
        const float ty = gy + gs * 0.42f;
        tray.startNewSubPath (gx - tw, ty);
        tray.lineTo          (gx - tw, ty + gs * 0.42f);
        tray.lineTo          (gx + tw, ty + gs * 0.42f);
        tray.lineTo          (gx + tw, ty);

        const juce::Colour glyphColour = fileHover ? theme.accent
                                                   : theme.textSecondary;
        g.setColour (glyphColour);
        g.strokePath (glyph, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.strokePath (tray,  juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour (fileHover ? theme.accent : theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Medium")));
        auto textArea = card.withTop (centre.y + 8.0f).withHeight (24.0f);
        g.drawText (fileHover ? "Release to load" : "Drop audio to load",
                    textArea, juce::Justification::centred);
        return;
    }

    // --- Waveform ------------------------------------------------------------
    auto inner = card.reduced (kCardPadding);

    // Clip everything below to the rounded card so the fill stays inside.
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path clipPath;
        clipPath.addRoundedRectangle (card.reduced (1.0f), radius - 1.0f);
        g.reduceClipRegion (clipPath);

        const float left  = inner.getX();
        const float midY  = inner.getCentreY();
        const float scale = inner.getHeight() * 0.46f;

        const size_t n = maxEnv.size();

        // --- Grid whisper: three faint horizontal guides ----------------------
        // Placed at the quarter lines and the top of the waveform band; barely
        // there, purely for a precision-instrument feel.
        {
            g.setColour (theme.separator.withMultipliedAlpha (0.28f));
            const float guides[3] = { midY - scale * 0.75f,
                                      midY - scale * 0.375f,
                                      midY + scale * 0.5f };
            for (float gy : guides)
                g.drawLine (inner.getX(), gy, inner.getRight(), gy, 1.0f);
        }

        // Centre line.
        g.setColour (theme.separator.withMultipliedAlpha (0.8f));
        g.drawLine (inner.getX(), midY, inner.getRight(), midY, 1.0f);

        // Subtle, near-zero shimmer breathing on the fill (Apple = restrained).
        const float shimmer = 1.0f + 0.05f * std::sin (phase) * glow;

        // Upper body: top contour down to the centre line.
        juce::Path upperFill;
        upperFill.startNewSubPath (left, midY - maxEnv[0] * scale);
        for (size_t x = 1; x < n; ++x)
            upperFill.lineTo (left + (float) x, midY - maxEnv[x] * scale);
        upperFill.lineTo (left + (float) (n - 1), midY);
        upperFill.lineTo (left, midY);
        upperFill.closeSubPath();

        // Vertical gradient: bright band at the peaks, melting away to almost
        // nothing at the centre line.
        if (cyber)
        {
            // Hot pink core at the peaks -> purple mid -> transparent centre.
            const juce::Colour purple (0xffb026ff);
            juce::ColourGradient grad (
                theme.waveform.withAlpha (juce::jlimit (0.0f, 1.0f, 0.95f * shimmer)),
                { left, midY - scale },
                purple.withAlpha (0.0f),
                { left, midY },
                false);
            grad.addColour (0.45, purple.withAlpha (
                                      juce::jlimit (0.0f, 1.0f, 0.55f * shimmer)));
            g.setGradientFill (grad);
            g.fillPath (upperFill);
        }
        else
        {
            juce::ColourGradient grad (
                theme.waveform.withAlpha (juce::jlimit (0.0f, 1.0f, 0.85f * shimmer)),
                { left, midY - scale },
                theme.waveform.withAlpha (0.05f),
                { left, midY },
                false);
            grad.addColour (0.35, theme.waveform.withAlpha (
                                      juce::jlimit (0.0f, 1.0f, 0.55f * shimmer)));
            g.setGradientFill (grad);
            g.fillPath (upperFill);
        }

        // --- Glassy reflection: mirrored min-envelope below the centre --------
        juce::Path lowerFill;
        lowerFill.startNewSubPath (left, midY);
        lowerFill.lineTo (left, midY - minEnv[0] * scale);
        for (size_t x = 1; x < n; ++x)
            lowerFill.lineTo (left + (float) x, midY - minEnv[x] * scale);
        lowerFill.lineTo (left + (float) (n - 1), midY);
        lowerFill.closeSubPath();

        {
            juce::ColourGradient reflGrad (
                theme.waveform.withAlpha (0.30f),
                { left, midY },
                theme.waveform.withAlpha (0.02f),
                { left, midY + scale },
                false);
            g.setGradientFill (reflGrad);
            g.fillPath (lowerFill);
        }

        // Faint contour on the reflection so it reads as glass, not fog.
        juce::Path bottomStroke;
        bottomStroke.startNewSubPath (left, midY - minEnv[0] * scale);
        for (size_t x = 1; x < n; ++x)
            bottomStroke.lineTo (left + (float) x, midY - minEnv[x] * scale);
        g.setColour (theme.waveform.withAlpha (0.18f));
        g.strokePath (bottomStroke, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

        // Live playhead positions, read once; the top contour reacts to them.
        float heads[VoicePool::kMaxVoices];
        const int nHeads = proc.getVoicePool().copyPlayheads (heads, VoicePool::kMaxVoices);

        // --- Top contour: glow halo + crisp 1px stroke -------------------------
        juce::Path topStroke;
        topStroke.startNewSubPath (left, midY - maxEnv[0] * scale);
        for (size_t x = 1; x < n; ++x)
            topStroke.lineTo (left + (float) x, midY - maxEnv[x] * scale);

        if (glow > 0.0f)
        {
            // Widening, fading passes so the peaks emit light.
            const float haloW[3]     = { 2.5f, 4.5f, 7.0f };
            const float haloAlpha[3] = { 0.22f, 0.11f, 0.05f };
            for (int p = 0; p < 3; ++p)
            {
                g.setColour (theme.waveform.withAlpha (haloAlpha[p] * glow * shimmer));
                g.strokePath (topStroke, juce::PathStrokeType (haloW[p],
                                                               juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
            }
        }

        g.setColour (theme.waveform.brighter (0.35f));
        g.strokePath (topStroke, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

        if (cyber)
        {
            // Thin cyan contour riding the peaks...
            g.setColour (theme.accent.withAlpha (0.28f));
            g.strokePath (topStroke, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

            // ...that heats up around each live playhead.
            for (int h = 0; h < nHeads; ++h)
            {
                const float hx = juce::jlimit (0.0f, 1.0f, heads[h]) * inner.getWidth();
                const int c0 = juce::jlimit (0, (int) n - 1, (int) hx - 24);
                const int c1 = juce::jlimit (0, (int) n - 1, (int) hx + 24);
                if (c1 <= c0)
                    continue;

                juce::Path seg;
                seg.startNewSubPath (left + (float) c0, midY - maxEnv[(size_t) c0] * scale);
                for (int x = c0 + 1; x <= c1; ++x)
                    seg.lineTo (left + (float) x, midY - maxEnv[(size_t) x] * scale);

                g.setColour (theme.accent.withAlpha (0.22f));
                g.strokePath (seg, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
                g.setColour (theme.accent.withAlpha (0.85f));
                g.strokePath (seg, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
            }
        }

        // --- Slice markers ---------------------------------------------------
        auto sample = proc.getLoadedSample();
        if (sample != nullptr && sample->getNumSamples() > 0)
        {
            const auto& slices = proc.getSliceEngine().getSlices();
            const float widthRatio = inner.getWidth() / (float) sample->getNumSamples();

            for (const auto& s : slices)
            {
                const float xPos = left + s.startSample * widthRatio;
                if (xPos < inner.getX() - 0.5f || xPos > inner.getRight() + 0.5f)
                    continue;

                // Thin accent hairline.
                g.setColour (theme.accent.withAlpha (0.5f));
                g.drawLine (xPos, inner.getY() + 3.0f, xPos, inner.getBottom(), 1.0f);

                if (cyber)
                {
                    // Small glowing diamond nub at the top of the marker.
                    const float cx = xPos;
                    const float cy = inner.getY() + 4.0f;
                    const float r  = 3.5f;

                    g.setColour (theme.accent.withAlpha (0.30f));
                    g.fillEllipse (cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
                    g.setColour (theme.accent.withAlpha (0.12f));
                    g.fillEllipse (cx - 11.0f, cy - 11.0f, 22.0f, 22.0f);

                    juce::Path diamond;
                    diamond.addQuadrilateral (cx,     cy - r,
                                              cx + r, cy,
                                              cx,     cy + r,
                                              cx - r, cy);
                    g.setColour (theme.accent);
                    g.fillPath (diamond);
                }
                else
                {
                    // Small rounded handle / nub at the top of the marker.
                    const float nubW = 6.0f;
                    const float nubH = 6.0f;
                    juce::Rectangle<float> nub (xPos - nubW * 0.5f, inner.getY(), nubW, nubH);

                    if (glow > 0.0f)
                    {
                        // Soft glow dot bloom behind the nub.
                        const auto dot = nub.getCentre();
                        g.setColour (theme.accent.withAlpha (0.30f * glow));
                        g.fillEllipse (dot.x - 6.0f, dot.y - 6.0f, 12.0f, 12.0f);
                        g.setColour (theme.accent.withAlpha (0.12f * glow));
                        g.fillEllipse (dot.x - 10.0f, dot.y - 10.0f, 20.0f, 20.0f);
                    }

                    g.setColour (theme.accent);
                    g.fillRoundedRectangle (nub, 2.0f);
                }
            }
        }

        // --- Playheads (active voices) --------------------------------------
        for (int h = 0; h < nHeads; ++h)
        {
            const float xPos = inner.getX()
                             + juce::jlimit (0.0f, 1.0f, heads[h]) * inner.getWidth();

            if (cyber)
            {
                // Short fading trail: a leftward gradient wash behind the head.
                const float trailW = juce::jmin (26.0f, xPos - inner.getX());
                if (trailW > 1.0f)
                {
                    juce::ColourGradient trail (theme.accent.withAlpha (0.0f),
                                                { xPos - trailW, midY },
                                                theme.accent.withAlpha (0.25f),
                                                { xPos, midY },
                                                false);
                    g.setGradientFill (trail);
                    g.fillRect (juce::Rectangle<float> (xPos - trailW, inner.getY(),
                                                        trailW, inner.getHeight()));
                }

                // Neon vertical line: wide halo + crisp core.
                g.setColour (theme.accent.withAlpha (0.35f));
                g.drawLine (xPos, inner.getY(), xPos, inner.getBottom(), 4.0f);
                g.setColour (theme.accent.withAlpha (0.95f));
                g.drawLine (xPos, inner.getY(), xPos, inner.getBottom(), 1.5f);

                // Glowing head dot where the line crosses the top contour.
                const int col = juce::jlimit (0, (int) n - 1, (int) (xPos - left));
                const float dotY = midY - maxEnv[(size_t) col] * scale;
                g.setColour (theme.accent.withAlpha (0.25f));
                g.fillEllipse (xPos - 6.0f, dotY - 6.0f, 12.0f, 12.0f);
                g.setColour (theme.accent);
                g.fillEllipse (xPos - 2.5f, dotY - 2.5f, 5.0f, 5.0f);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.fillEllipse (xPos - 1.0f, dotY - 1.0f, 2.0f, 2.0f);
            }
            else
            {
                // Slight glow stroke so playback feels alive; scales with theme
                // glow but keeps a whisper even on flat themes.
                const float headGlow = 0.15f + 0.35f * glow;
                g.setColour (theme.accent.withAlpha (headGlow));
                g.drawLine (xPos, inner.getY(), xPos, inner.getBottom(), 4.0f);

                g.setColour (theme.accent.withAlpha (0.95f));
                g.drawLine (xPos, inner.getY(), xPos, inner.getBottom(), 1.5f);

                juce::Path tri;
                tri.addTriangle (xPos - 4.0f, inner.getY(),
                                 xPos + 4.0f, inner.getY(),
                                 xPos,        inner.getY() + 6.0f);
                g.setColour (theme.accent);
                g.fillPath (tri);
            }
        }
    }
}

// [함수] isInterestedInFileDrag — 드래그된 파일 중 지원 오디오 확장자가 있으면 받겠다고(true) 함.
bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
            || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")
            || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3"))
            return true;
    return false;
}

// [함수] fileDragEnter/Exit — 파일을 끌고 들어오거나 나갈 때 강조 표시(fileHover) 토글.
void WaveformView::fileDragEnter (const juce::StringArray&, int, int)
{
    fileHover = true;
    repaint();
}

void WaveformView::fileDragExit (const juce::StringArray&)
{
    fileHover = false;
    repaint();
}

// [함수] filesDropped — 파일을 놓으면 첫 파일을 로드하고, 성공 시 새로고침 + 콜백 실행(에디터도 갱신).
void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    fileHover = false;
    if (files.isEmpty())
        return;

    if (proc.loadSampleFromFile (juce::File (files[0])))
    {
        refresh();
        // 드롭 후 처리 콜백이 등록돼 있으면 호출(건반/슬라이스 컨트롤 갱신용).
        if (onSampleDropped != nullptr)
            onSampleDropped();
    }
}
