#include "WaveformView.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

#include <cmath>
#include <vector>

namespace
{
    // Inner padding between the card edge and the waveform drawing.
    constexpr float kCardPadding = 12.0f;

    // Spec 4.3: the waveform reads as 74 mirrored bars, never a continuous
    // envelope, and at most twelve slice lanes are ever drawn.
    constexpr int   kNumBars  = 74;
    constexpr int   kMaxLanes = 12;

    // Height of the chips / hint strip along the bottom of the card.
    constexpr float kFooterH = 16.0f;

    /** Spec 2: derived accent tokens are computed, never hand-picked.
        --acc2 = color-mix(in srgb, var(--acc) 76%, #000) -> 24% toward black. */
    juce::Colour accentDeep (const Theme& t)
    {
        return t.accent.interpolatedWith (juce::Colours::black, 0.24f);
    }

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        // getStringWidth on Font is deprecated in JUCE 8; GlyphArrangement is
        // the supported way to measure a run of text.
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    /** Small rounded pill: material fill, hairline border, secondary text.
        Returns the width it used, so chips can be packed left to right. */
    float drawChip (juce::Graphics& g, const Theme& t, float x, float y, float h,
                    const juce::String& text)
    {
        const juce::Font font (juce::FontOptions (9.5f).withStyle ("Medium"));
        const float w = textWidth (font, text) + 14.0f;
        const juce::Rectangle<float> r (x, y, w, h);

        g.setColour (t.material);
        g.fillRoundedRectangle (r, h * 0.5f);
        g.setColour (t.separator);
        g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, 1.0f);
        g.setColour (t.textSecondary);
        g.setFont (font);
        g.drawText (text, r, juce::Justification::centred, false);
        return w;
    }

    juce::Font badgeFont()
    {
        return juce::Font (juce::FontOptions (9.5f).withStyle ("Semibold"));
    }

    /** Spec 4.3: the engine badge - a small accent pill, right-aligned to
        `rightX`. Measured separately so the lane numerals can dodge it. */
    juce::Rectangle<float> badgeBounds (float rightX, float y, const juce::String& text)
    {
        const float h = 15.0f;
        const float w = textWidth (badgeFont(), text) + 16.0f;
        return { rightX - w, y, w, h };
    }

    void drawBadge (juce::Graphics& g, const Theme& t,
                    juce::Rectangle<float> r, const juce::String& text)
    {
        g.setColour (t.accent);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (t.accentInk);
        g.setFont (badgeFont());
        g.drawText (text, r, juce::Justification::centred, false);
    }
}

WaveformView::WaveformView (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    // Sample-edit buttons live INSIDE the card; hidden until useful.
    trimBtn.setTooltip ("Keep only the selected part");
    cutBtn .setTooltip ("Delete the selected part and join the rest");
    fadeBtn.setTooltip ("Fade the selection (in near the start, out near the end)");
    normBtn.setTooltip ("Normalize: raise the whole sample to full volume");
    undoEditBtn.setTooltip ("Undo the last sample edit (press again = redo)");

    trimBtn.onClick     = [this] { applyEdit (0); };
    cutBtn.onClick      = [this] { applyEdit (1); };
    fadeBtn.onClick     = [this] { applyEdit (2); };
    normBtn.onClick     = [this] { applyEdit (3); };
    undoEditBtn.onClick = [this] { applyEdit (4); };

    for (auto* b : { &trimBtn, &cutBtn, &fadeBtn, &normBtn, &undoEditBtn })
        addChildComponent (*b);

    // Subtle, restrained animation. Low rate keeps the Apple look calm.
    startTimerHz (24);
}

bool WaveformView::editableNow() const
{
    // Only when the SAMPLE graph is on screen (Chop / Melody modes).
    const bool scopeMode = (proc.isSynthMode() || proc.isSamplerMode())
                           && ! proc.isMelodyMode();
    return ! scopeMode && ! maxEnv.empty();
}

float WaveformView::fracAt (float x) const
{
    const float x0 = 4.0f + kCardPadding;
    const float w  = juce::jmax (1.0f, (float) getWidth() - 8.0f - 2.0f * kCardPadding);
    return juce::jlimit (0.0f, 1.0f, (x - x0) / w);
}

float WaveformView::xOfFrac (float frac) const
{
    const float x0 = 4.0f + kCardPadding;
    const float w  = juce::jmax (1.0f, (float) getWidth() - 8.0f - 2.0f * kCardPadding);
    return x0 + frac * w;
}

int WaveformView::markerNear (float x) const
{
    // Spec 7: Chop is the only engine that shows slice markers - so it is the
    // only engine where one can be grabbed. Nothing invisible is draggable.
    if (! proc.isChopMode())
        return -1;

    auto sample = proc.getLoadedSample();
    if (sample == nullptr || sample->getNumSamples() <= 0)
        return -1;

    const auto& slices = proc.getSliceEngine().getSlices();
    const float total = (float) sample->getNumSamples();

    int best = -1;
    float bestDist = 8.0f;              // grab radius in pixels
    for (size_t i = 0; i < slices.size(); ++i)
    {
        // Slice 0 always sits at the very start; dragging it would only ever
        // trim the head, which is what TRIM is for.
        if (slices[i].startSample <= 0)
            continue;
        const float d = std::abs (x - xOfFrac (slices[i].startSample / total));
        if (d < bestDist) { bestDist = d; best = (int) i; }
    }
    return best;
}

int WaveformView::sliceAtFrac (float frac) const
{
    auto sample = proc.getLoadedSample();
    if (sample == nullptr || sample->getNumSamples() <= 0)
        return -1;

    const auto& slices = proc.getSliceEngine().getSlices();
    const int pos = (int) (frac * (float) sample->getNumSamples());
    for (int i = (int) slices.size() - 1; i >= 0; --i)
        if (pos >= slices[(size_t) i].startSample)
            return i;
    return slices.empty() ? -1 : 0;
}

void WaveformView::moveMarker (int index, float frac)
{
    auto sample = proc.getLoadedSample();
    if (sample == nullptr || sample->getNumSamples() <= 0)
        return;

    auto& engine = proc.getSliceEngine();
    const auto& slices = engine.getSlices();
    if (index <= 0 || index >= (int) slices.size())
        return;

    const int total = sample->getNumSamples();
    std::vector<int> points;
    points.reserve (slices.size());
    for (const auto& sl : slices)
        points.push_back (sl.startSample);

    // Keep it between its neighbours with a small floor, so a marker can never
    // be dragged past the next one and invert a slice.
    const int gap  = juce::jmax (64, total / 2000);
    const int lo   = points[(size_t) index - 1] + gap;
    const int hi   = (index + 1 < (int) points.size() ? points[(size_t) index + 1] : total) - gap;
    if (hi <= lo)
        return;

    points[(size_t) index] = juce::jlimit (lo, hi, (int) (frac * (float) total));
    engine.sliceByManual (points);
}

void WaveformView::addMarkerAt (float frac)
{
    auto sample = proc.getLoadedSample();
    if (sample == nullptr || sample->getNumSamples() == 0)
        return;

    auto& engine = proc.getSliceEngine();
    const int total = sample->getNumSamples();
    const int at    = juce::jlimit (0, total - 1, (int) (frac * (float) total));
    const int gap   = juce::jmax (64, total / 2000);

    std::vector<int> points;
    points.reserve (engine.getSlices().size() + 1);
    for (const auto& sl : engine.getSlices())
        points.push_back (sl.startSample);

    // Refuse a cut that would sit on top of an existing one - two markers a
    // handful of samples apart is a slice nobody can hear or grab.
    for (int p : points)
        if (std::abs (p - at) < gap)
            return;

    points.push_back (at);
    std::sort (points.begin(), points.end());
    engine.sliceByManual (points);
}

void WaveformView::removeMarker (int index)
{
    auto& engine = proc.getSliceEngine();
    const auto& slices = engine.getSlices();
    // Never the first: slice 0 starts where the audio does.
    if (index <= 0 || index >= (int) slices.size() || slices.size() <= 2)
        return;

    std::vector<int> points;
    points.reserve (slices.size() - 1);
    for (int i = 0; i < (int) slices.size(); ++i)
        if (i != index)
            points.push_back (slices[(size_t) i].startSample);

    engine.sliceByManual (points);
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click to CUT, double-click a cut to remove it. Until now the only
    // way to add a slice was to re-run the whole detector at a different
    // sensitivity, which throws away every marker you had already placed.
    if (! editableNow())
        return;

    const int m = markerNear (e.position.x);
    if (m > 0) removeMarker (m);
    else       addMarkerAt (fracAt (e.position.x));

    // The key mapping and the grid below both follow the slice list.
    // Same notification the marker drag uses: the key mapping and the grid
    // below both have to follow a changed slice list.
    refresh();
    if (onSampleDropped != nullptr)
        onSampleDropped();
    repaint();
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (! editableNow()) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    const int m = markerNear (e.position.x);
    if (m != hoverMarker)
    {
        hoverMarker = m;
        setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                               : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    if (hoverMarker >= 0) { hoverMarker = -1; repaint(); }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (! editableNow())
        return;

    // On a marker: grab it. Anywhere else: audition the slice under the
    // cursor straight away, and arm a selection in case this becomes a drag.
    dragMarker = markerNear (e.position.x);
    didDragMarker = false;
    if (dragMarker >= 0)
        return;

    const float f = fracAt (e.position.x);
    const int sl = sliceAtFrac (f);
    if (sl >= 0)
        proc.triggerSlicePad (sl, 0.9f);
            // Selecting a lane lights the matching key below - the other half
            // of the link that keys make when they are pressed.
            proc.setSelectedSlice (sl);

    selA = selB = f;
    updateEditButtons();
    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (! editableNow())
        return;

    if (dragMarker >= 0)
    {
        moveMarker (dragMarker, fracAt (e.position.x));
        didDragMarker = true;
        refresh();
        return;
    }

    if (selA < 0.0f)
        return;
    selB = fracAt (e.position.x);
    updateEditButtons();
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragMarker >= 0)
    {
        dragMarker = -1;
        // Only once the drag ends: the key mapping and the slice grid have to
        // follow the new boundaries, and doing it per mouse-move would relayout
        // the editor on every pixel.
        if (didDragMarker && onSampleDropped != nullptr)
            onSampleDropped();
        return;
    }

    // A plain click (no real drag) clears the selection.
    if (selA >= 0.0f && std::abs (selB - selA) < 0.005f)
        selA = selB = -1.0f;
    updateEditButtons();
    repaint();
}

void WaveformView::applyEdit (int op)
{
    bool ok = false;
    if (op == 4)
        ok = proc.undoSampleEdit();
    else
    {
        const float a = juce::jmin (selA, selB), b = juce::jmax (selA, selB);
        using E = VocalChopAudioProcessor::SampleEdit;
        const E ops[4] = { E::Trim, E::Cut, E::Fade, E::Normalize };
        ok = proc.editSample (ops[juce::jlimit (0, 3, op)], a, b);
    }

    if (ok)
    {
        selA = selB = -1.0f;
        refresh();
        if (onSampleDropped != nullptr)   // resync slices/keys in the editor
            onSampleDropped();
    }
    updateEditButtons();
}

void WaveformView::layoutEditButtons()
{
    updateEditButtons();
}

void WaveformView::updateEditButtons()
{
    const bool base = editableNow();
    const bool sel  = base && selA >= 0.0f && std::abs (selB - selA) > 0.005f;
    trimBtn.setVisible (sel);
    cutBtn .setVisible (sel);
    fadeBtn.setVisible (sel);
    normBtn.setVisible (base);
    undoEditBtn.setVisible (base && proc.canUndoSampleEdit());

    // Pack whichever buttons are visible into one tight strip at the
    // card's top-left, so the row never shows mid-air gaps.
    int x = 16;
    for (auto* b : { &trimBtn, &cutBtn, &fadeBtn, &normBtn, &undoEditBtn })
        if (b->isVisible())
        {
            b->setBounds (x, 12, 54, 24);
            x += 60;
        }
}

WaveformView::~WaveformView()
{
    stopTimer();
}

void WaveformView::refresh()
{
    // Any selection was made on the PREVIOUS sample - keeping it would let
    // one click TRIM the fresh sample at stale positions.
    selA = selB = -1.0f;
    rebuildEnvelope();
    updateEditButtons();
    repaint();
}

void WaveformView::rebuildEnvelope()
{
    minEnv.clear();
    maxEnv.clear();

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
    layoutEditButtons();
}

void WaveformView::timerCallback()
{
    // Very slow breathing phase; used only for a near-invisible shimmer.
    phase += 0.015f;
    if (phase > juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;
    updateEditButtons();   // mode switches show/hide the edit strip
    repaint();
}

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

    // --- Engine-gated copy (spec 4.3) ----------------------------------------
    // isSynthMode() is true for every non-Chop engine, so Melody and Sampled
    // have to be asked about first.
    const bool chopMode    = proc.isChopMode();
    const bool melodyMode  = proc.isMelodyMode();
    const bool sampledMode = proc.isSamplerMode();

    juce::String badgeText, hintText;
    if (chopMode)
    {
        auto& engine = proc.getSliceEngine();
        badgeText = juce::String (engine.getNumSlices()) + " slices  -  "
                  + (engine.getMode() == SliceEngine::Grid ? "beats" : "transient");
        hintText  = "Drag a marker to move the cut  -  drop audio anywhere";
    }
    else if (melodyMode)
    {
        badgeText = "Root C3  -  loop region";
        hintText  = "Drag the region edges to set the loop";
    }
    else if (sampledMode)
    {
        badgeText = "SFZ zone";
        hintText  = "Drag the region edges to set the loop";
    }
    else
    {
        badgeText = "Oscillator preview";
        hintText  = "Live output  -  drop audio to switch to Chop";
    }

    // --- Soft drop shadow behind the card ------------------------------------
    // Two offset fills: this view repaints 24x a second, and a gaussian blur
    // that often is exactly what made the UI feel heavy.
    g.setColour (theme.shadow.withAlpha (0.16f));
    g.fillRoundedRectangle (card.translated (0.0f, 4.0f).expanded (1.5f), radius + 1.5f);
    g.setColour (theme.shadow.withAlpha (0.10f));
    g.fillRoundedRectangle (card.translated (0.0f, 2.0f), radius);

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

    // --- Synth / Sampled modes: live output oscilloscope ---------------------
    // The static sample waveform is a CHOP-mode tool; while playing
    // instruments this card shows what you actually hear, in real time.
    if ((proc.isSynthMode() || proc.isSamplerMode()) && ! proc.isMelodyMode())
    {
        auto area = card.reduced (kCardPadding);

        // Bottom strip: the engine hint (spec 4.3). No file chips here - the
        // scope shows live output, not the loaded file.
        {
            auto footer = area.removeFromBottom (kFooterH);
            area.removeFromBottom (3.0f);

            g.setColour (theme.textSecondary);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (hintText, footer, juce::Justification::centredLeft, false);
        }

        const auto& ring = proc.getScopeRing();
        const int   ringSize = (int) ring.size();
        const int   writePos = proc.getScopeWritePos();
        const int   span = juce::jmin (1024, ringSize - 256);

        // Simple rising-zero-cross trigger so the trace holds still.
        int start = writePos - span;
        for (int back = 0; back < 256; ++back)
        {
            const float a = ring[(size_t) ((start - back - 1) & (ringSize - 1))];
            const float b = ring[(size_t) ((start - back)     & (ringSize - 1))];
            if (a <= 0.0f && b > 0.0f)
            {
                start -= back;
                break;
            }
        }

        // Centre line.
        g.setColour (theme.separator);
        g.fillRect (area.getX(), area.getCentreY() - 0.5f, area.getWidth(), 1.0f);

        juce::Path trace;
        const int points = juce::jmax (2, (int) (area.getWidth() / 2.0f));
        for (int i = 0; i < points; ++i)
        {
            const int idx = start + (i * span) / points;
            const float v = juce::jlimit (-1.0f, 1.0f,
                                          ring[(size_t) (idx & (ringSize - 1))]);
            const float px = area.getX() + area.getWidth() * (float) i / (float) (points - 1);
            const float py = area.getCentreY() - v * area.getHeight() * 0.46f;
            if (i == 0) trace.startNewSubPath (px, py);
            else        trace.lineTo (px, py);
        }

        if (glow > 0.5f)
        {
            g.setColour (theme.waveform.withAlpha (0.25f));
            g.strokePath (trace, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
        }
        g.setColour (theme.waveform);
        g.strokePath (trace, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

        // The accent badge says what this card is showing, so the old
        // "LIVE OUTPUT" caption would only repeat it.
        drawBadge (g, theme, badgeBounds (area.getRight(), area.getY(), badgeText),
                   badgeText);
        return;
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

        // Dashed drop zone while a file hovers - the target is explicit, the
        // way a Finder drop target is.
        if (fileHover)
        {
            juce::Path zone;
            zone.addRoundedRectangle (card.reduced (14.0f), theme.cornerRadius);
            const float dashes[] = { 7.0f, 6.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, zone, dashes, 2);
            g.setColour (theme.accent.withAlpha (0.55f));
            g.fillPath (dashed);

            g.setColour (theme.accent.withAlpha (0.06f));
            g.fillRoundedRectangle (card.reduced (14.0f), theme.cornerRadius);
        }

        g.setColour (fileHover ? theme.accent : theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (14.5f).withStyle ("Medium")));
        auto textArea = card.withTop (centre.y + 8.0f).withHeight (22.0f);
        g.drawText (fileHover ? "Release to load" : "Drop audio here",
                    textArea, juce::Justification::centred);

        if (! fileHover)
        {
            g.setColour (theme.textSecondary.withAlpha (0.65f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText ("or press Demo for a built-in vocal",
                        card.withTop (centre.y + 30.0f).withHeight (18.0f),
                        juce::Justification::centred);
        }
        return;
    }

    // --- Waveform ------------------------------------------------------------
    auto inner = card.reduced (kCardPadding);

    // Measured up front: the lane numerals below have to step around it.
    const auto badgeRect = badgeBounds (inner.getRight(), inner.getY(), badgeText);

    // --- Bottom strip: chips on the left, engine hint on the right (spec 4.3)
    {
        auto footer = inner.removeFromBottom (kFooterH);
        inner.removeFromBottom (3.0f);          // breathing room above the chips

        float chipX = footer.getX();
        const auto sampleName = proc.getLoadedSampleName();
        if (sampleName.isNotEmpty())
        {
            const auto shown = sampleName.length() > 24
                                 ? sampleName.substring (0, 23) + juce::String::charToString (0x2026)
                                 : sampleName;
            chipX += drawChip (g, theme, chipX, footer.getY(), footer.getHeight(), shown) + 6.0f;
        }
        chipX += drawChip (g, theme, chipX, footer.getY(), footer.getHeight(), "Normalised") + 8.0f;

        if (chipX < footer.getRight())
        {
            g.setColour (theme.textSecondary);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (hintText, footer.withLeft (chipX),
                        juce::Justification::centredRight, false);
        }
    }

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

        // Subtle, near-zero shimmer breathing on the bars (Apple = restrained).
        const float shimmer = 1.0f + 0.05f * std::sin (phase) * glow;

        // Live playhead positions, read once.
        float heads[VoicePool::kMaxVoices];
        const int nHeads = proc.getVoicePool().copyPlayheads (heads, VoicePool::kMaxVoices);

        // --- 74 mirrored bars (spec 4.3) --------------------------------------
        // The cached min/max envelope is still the data source; it is only
        // resampled into 74 buckets and drawn as rounded, centred bars instead
        // of a continuous contour.
        const float slotW = inner.getWidth() / (float) kNumBars;
        const float gapW  = juce::jlimit (1.0f, 3.0f, slotW * 0.22f);
        const float barW  = juce::jmax (1.5f, slotW - gapW);
        const float maxH  = juce::jmax (2.0f, scale * 2.0f);

        std::vector<juce::Rectangle<float>> bars;
        bars.reserve ((size_t) kNumBars);

        for (int b = 0; b < kNumBars; ++b)
        {
            const size_t c0 = ((size_t) b * n) / (size_t) kNumBars;
            if (c0 >= n)
                break;
            const size_t c1 = juce::jmin (n, juce::jmax (c0 + 1,
                                  ((size_t) (b + 1) * n) / (size_t) kNumBars));

            float peak = 0.0f;
            for (size_t c = c0; c < c1; ++c)
                peak = juce::jmax (peak, juce::jmax (maxEnv[c], -minEnv[c]));

            // Mirrored: the bar is centred on the middle line, so half of its
            // height sits above it and half below.
            const float h = juce::jlimit (2.0f, maxH, peak * scale * shimmer * 2.0f);
            bars.push_back ({ left + (float) b * slotW + gapW * 0.5f,
                              midY - h * 0.5f, barW, h });
        }

        // The bars are drawn in four passes rather than one fill. A single
        // gradient rectangle per bar is a bar chart; what makes this read as an
        // instrument is the light coming OFF it - a bloom under the loud ones,
        // a lit tip where the energy is, and a spine holding the row together.

        // Loudest bar in the row, so the bloom and the caps can key off energy
        // rather than being applied evenly - an even glow is just a blur.
        float loudest = 1.0f;
        for (const auto& bar : bars)
            loudest = juce::jmax (loudest, bar.getHeight());

        // (1) BLOOM. Two widening passes at low alpha under the bars, weighted
        // by how tall each one is, so a transient throws light and a quiet
        // passage does not. Cheap: two rounded rects per bar, no blur.
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                const float grow = 2.5f + (float) pass * 5.0f;
                const float base = (pass == 0 ? 0.26f : 0.13f) * (0.6f + 0.4f * glow);
                for (const auto& bar : bars)
                {
                    const float e = bar.getHeight() / loudest;          // 0..1 energy
                    if (e < 0.12f) continue;
                    g.setColour (theme.accent.withAlpha (base * e * e));
                    g.fillRoundedRectangle (bar.expanded (grow),
                                            juce::jmin (barW, bar.getHeight()) * 0.5f + grow);
                }
            }
        }

        // (2) BODY. Accent on the centre line, --acc2 at the tips, so tall bars
        // darken as they reach out - the spec's gradient.
        {
            const juce::Colour tip = accentDeep (theme);
            juce::ColourGradient barGrad (tip, { left, midY - scale },
                                          tip, { left, midY + scale }, false);
            barGrad.addColour (0.5, theme.accent);
            g.setGradientFill (barGrad);

            for (const auto& bar : bars)
                g.fillRoundedRectangle (bar, juce::jmin (barW, bar.getHeight()) * 0.5f);
        }

        // (3) PEAK CAPS. A short bright segment at both ends of each bar, in
        // the light end of the accent. This is the detail that makes a level
        // display look alive rather than printed - the eye reads the caps as
        // the moving part.
        {
            const juce::Colour capCol = theme.accent
                                            .interpolatedWith (juce::Colours::white,
                                                               theme.dark ? 0.45f : 0.18f);
            // Small and only on the loud ones. The first attempt used a cap
            // of barW*0.9 at up to 0.85 alpha, which on a short bar is half
            // its length - every bar came out as a dumbbell with two bright
            // ends and a dark middle. A highlight has to be smaller than the
            // thing it is highlighting.
            for (const auto& bar : bars)
            {
                const float e = bar.getHeight() / loudest;
                if (e < 0.30f) continue;
                const float capH = juce::jmin (bar.getHeight() * 0.20f, 3.0f);
                if (capH < 1.0f) continue;
                const float r = juce::jmin (barW, capH) * 0.5f;
                g.setColour (capCol.withAlpha (0.18f + 0.30f * e));
                g.fillRoundedRectangle (bar.getX(), bar.getY(), barW, capH, r);
                g.fillRoundedRectangle (bar.getX(), bar.getBottom() - capH, barW, capH, r);
            }
        }

        // (4) SPINE. A hairline down the centre, brightest where the bars are.
        // Without it the mirrored halves read as two separate rows.
        {
            g.setColour (theme.accent.withAlpha (theme.dark ? 0.30f : 0.22f));
            g.fillRect (left, midY - 0.5f, inner.getWidth(), 1.0f);
        }

        // --- Slice markers ---------------------------------------------------
        // Spec 7: Chop is the ONLY engine that shows slice markers. Melody
        // plays the whole sample chromatically, so cut lines there described a
        // structure that nothing was using.
        auto sample = proc.getLoadedSample();
        if (chopMode && sample != nullptr && sample->getNumSamples() > 0)
        {
            const auto& slices = proc.getSliceEngine().getSlices();
            const float widthRatio = inner.getWidth() / (float) sample->getNumSamples();

            // The selected lane, washed in accent behind everything else. It
            // is the other end of the link the keyboard draws: pressing key 04
            // lights lane 04 and vice versa, so a still screenshot shows which
            // piece of audio a key plays.
            const int selSlice = proc.getSelectedSlice();
            if (selSlice >= 0 && selSlice < (int) slices.size())
            {
                const float x0 = left + slices[(size_t) selSlice].startSample * widthRatio;
                const float x1 = (selSlice + 1 < (int) slices.size())
                                   ? left + slices[(size_t) selSlice + 1].startSample * widthRatio
                                   : inner.getRight();
                auto lane = juce::Rectangle<float> (x0, inner.getY(),
                                                    juce::jmax (2.0f, x1 - x0),
                                                    inner.getHeight())
                                .getIntersection (inner);
                g.setColour (theme.accent.withAlpha (0.15f));
                g.fillRect (lane);
                g.setColour (theme.accent.withAlpha (0.9f));
                g.fillRect (lane.getX(), lane.getY(), lane.getWidth(), 2.0f);
            }

            // --- Numbered lane tabs (spec 4.3) --------------------------------
            // A small accent numeral at the top-left of each lane; at most
            // twelve are drawn, past that they stop being readable.
            {
                // The sample-edit buttons sit at the card's top-left, so a tab
                // that would hide under them drops below the strip instead.
                float stripRight = 0.0f, stripBottom = 0.0f;
                for (auto* b : { &trimBtn, &cutBtn, &fadeBtn, &normBtn, &undoEditBtn })
                    if (b->isVisible())
                    {
                        stripRight  = juce::jmax (stripRight,  (float) b->getRight());
                        stripBottom = juce::jmax (stripBottom, (float) b->getBottom());
                    }

                // Spec 1: the mono face is reserved for numerals - which is
                // exactly what a lane tab is.
                const juce::Font tabFont (juce::FontOptions (
                    juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold));
                const int laneCount = juce::jmin ((int) slices.size(), kMaxLanes);

                for (int i = 0; i < laneCount; ++i)
                {
                    const float x0 = left + slices[(size_t) i].startSample * widthRatio;
                    const float x1 = (i + 1 < (int) slices.size())
                                       ? left + slices[(size_t) i + 1].startSample * widthRatio
                                       : inner.getRight();
                    if (x1 - x0 < 21.0f || x0 > inner.getRight() - 19.0f)
                        continue;               // lane too narrow for a numeral

                    const juce::Rectangle<float> tab (
                        x0 + 2.0f,
                        (x0 + 2.0f < stripRight + 4.0f ? stripBottom + 4.0f
                                                       : inner.getY() + 1.0f),
                        17.0f, 12.0f);
                    if (tab.getBottom() > inner.getBottom()
                        || tab.expanded (3.0f).intersects (badgeRect))
                        continue;               // the engine badge owns that corner

                    const bool on = (i == selSlice);
                    g.setColour (on ? theme.accent : theme.accentSoft);
                    g.fillRoundedRectangle (tab, 3.5f);
                    g.setColour (on ? theme.accentInk : theme.accent);
                    g.setFont (tabFont);
                    g.drawText (juce::String (i + 1).paddedLeft ('0', 2), tab,
                                juce::Justification::centred, false);
                }
            }

            int sliceIdx = -1;
            for (const auto& s : slices)
            {
                ++sliceIdx;
                const float xPos = left + s.startSample * widthRatio;
                if (xPos < inner.getX() - 0.5f || xPos > inner.getRight() + 0.5f)
                    continue;

                // The marker being pointed at (or dragged) gets a grab handle,
                // because "you can move this" has to be visible before the
                // pointer is already on it.
                const bool live = (sliceIdx == hoverMarker || sliceIdx == dragMarker);
                if (live)
                {
                    g.setColour (theme.accent);
                    g.fillRect (xPos - 1.0f, inner.getY() + 3.0f, 2.0f,
                                inner.getHeight() - 3.0f);
                    const float cy = inner.getY() + 7.0f;
                    g.fillRoundedRectangle (xPos - 5.0f, cy - 5.0f, 10.0f, 10.0f, 2.5f);
                    g.setColour (theme.bgTop.withAlpha (0.9f));
                    for (float dx : { -1.6f, 1.6f })
                        g.fillRect (xPos + dx - 0.5f, cy - 2.5f, 1.0f, 5.0f);
                    continue;
                }

                // Hairline that fades downward: the cut is announced at the
                // top, and the waveform underneath stays readable.
                {
                    juce::ColourGradient line (theme.accent.withAlpha (0.62f),
                                               xPos, inner.getY() + 3.0f,
                                               theme.accent.withAlpha (0.14f),
                                               xPos, inner.getBottom(), false);
                    g.setGradientFill (line);
                    g.fillRect (xPos - 0.5f, inner.getY() + 3.0f, 1.0f,
                                inner.getHeight() - 3.0f);
                }

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
                    const float nubW = 5.0f;
                    const float nubH = 8.0f;
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

                    juce::ColourGradient pill (theme.accent.brighter (0.25f),
                                               nub.getX(), nub.getY(),
                                               theme.accent.darker (0.15f),
                                               nub.getX(), nub.getBottom(), false);
                    g.setGradientFill (pill);
                    g.fillRoundedRectangle (nub, nubW * 0.5f);
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

    // --- Edit selection overlay (drag on the waveform to select) -------------
    if (selA >= 0.0f && ! maxEnv.empty())
    {
        const float x0 = 4.0f + kCardPadding;
        const float w  = (float) getWidth() - 8.0f - 2.0f * kCardPadding;
        const float xa = x0 + juce::jmin (selA, selB) * w;
        const float xb = x0 + juce::jmax (selA, selB) * w;

        auto selRect = juce::Rectangle<float> (xa, card.getY() + 4.0f,
                                               juce::jmax (1.0f, xb - xa),
                                               card.getHeight() - 8.0f);
        g.setColour (theme.accent.withAlpha (0.14f));
        g.fillRect (selRect);
        g.setColour (theme.accent.withAlpha (0.85f));
        g.fillRect (xa - 0.75f, selRect.getY(), 1.5f, selRect.getHeight());
        g.fillRect (xb - 0.75f, selRect.getY(), 1.5f, selRect.getHeight());
    }

    // --- Engine badge, on top of everything (spec 4.3) -----------------------
    drawBadge (g, theme, badgeRect, badgeText);
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
    {
        refresh();
        if (onSampleDropped != nullptr)
            onSampleDropped();
    }
}
