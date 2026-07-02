#include "WaveformView.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

#include <cmath>
#include <vector>

namespace
{
    // Inner padding between the card edge and the waveform drawing.
    constexpr float kCardPadding = 12.0f;
}

WaveformView::WaveformView (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    // Subtle, restrained animation. Low rate keeps the Apple look calm.
    startTimerHz (24);
}

WaveformView::~WaveformView()
{
    stopTimer();
}

void WaveformView::refresh()
{
    rebuildEnvelope();
    repaint();
}

void WaveformView::rebuildEnvelope()
{
    minEnv.clear();
    maxEnv.clear();

    auto sample = proc.getLoadedSample();
    if (sample == nullptr || sample->getNumSamples() == 0)
        return;

    // Resolve envelope over the drawable area inside the card padding.
    const int drawWidth = juce::jmax (1, (int) std::floor (getWidth() - 2.0f * kCardPadding));
    const int numColumns = juce::jmax (1, drawWidth);
    const int numSamples  = sample->getNumSamples();
    const int numChannels = sample->getNumChannels();

    minEnv.assign ((size_t) numColumns, 0.0f);
    maxEnv.assign ((size_t) numColumns, 0.0f);

    const int samplesPerColumn = juce::jmax (1, numSamples / numColumns);

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

void WaveformView::resized()
{
    rebuildEnvelope();
}

void WaveformView::timerCallback()
{
    // Very slow breathing phase; used only for a near-invisible shimmer.
    phase += 0.015f;
    if (phase > juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    const float radius = theme.cornerRadius;
    const float glow   = juce::jlimit (0.0f, 1.0f, theme.glow);

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
    g.setColour (theme.materialStrong);
    g.fillRoundedRectangle (card, radius);

    // Border tints toward accent while a file is dragged over the view.
    const juce::Colour border = fileHover ? theme.accent
                                          : theme.separator;
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);

    // --- Empty state ---------------------------------------------------------
    if (maxEnv.empty())
    {
        auto centre = card.getCentre();

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

        // --- Playheads (active voices) --------------------------------------
        float heads[VoicePool::kMaxVoices];
        const int nHeads = proc.getVoicePool().copyPlayheads (heads, VoicePool::kMaxVoices);
        for (int h = 0; h < nHeads; ++h)
        {
            const float xPos = inner.getX()
                             + juce::jlimit (0.0f, 1.0f, heads[h]) * inner.getWidth();

            // Slight glow stroke so playback feels alive; scales with theme glow
            // but keeps a whisper even on flat themes.
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

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
            || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")
            || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3"))
            return true;
    return false;
}

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

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    fileHover = false;
    if (files.isEmpty())
        return;

    if (proc.loadSampleFromFile (juce::File (files[0])))
        refresh();
}
