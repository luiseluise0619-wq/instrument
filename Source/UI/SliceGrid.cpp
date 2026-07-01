#include "SliceGrid.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

#include <cmath>
#include <vector>

SliceGrid::SliceGrid (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    startTimerHz (60);
}

SliceGrid::~SliceGrid()
{
    stopTimer();
}

static int columnsFor (int numPads)
{
    if (numPads <= 0) return 1;
    const int cols = (int) std::ceil (std::sqrt ((double) numPads));
    return juce::jmax (1, cols);
}

juce::Rectangle<float> SliceGrid::padBounds (int index, int numPads) const
{
    const int cols = columnsFor (numPads);
    const int rows = (numPads + cols - 1) / cols;
    const int col  = index % cols;
    const int row  = index / cols;

    // Even, generous gaps that scale gently with the cell size.
    const float cellW = (float) getWidth()  / (float) cols;
    const float cellH = (float) getHeight() / (float) juce::jmax (1, rows);
    const float gap   = juce::jlimit (5.0f, 12.0f, juce::jmin (cellW, cellH) * 0.10f);

    juce::Rectangle<float> cell ((float) col * cellW,
                                 (float) row * cellH,
                                 cellW, cellH);
    return cell.reduced (gap * 0.5f);
}

int SliceGrid::padIndexAt (juce::Point<int> p) const
{
    const int numPads = proc.getSliceEngine().getNumSlices();
    for (int i = 0; i < numPads; ++i)
        if (padBounds (i, numPads).contains (p.toFloat()))
            return i;
    return -1;
}

void SliceGrid::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    const int numPads = proc.getSliceEngine().getNumSlices();

    if ((int) padFlash.size() != numPads)
        padFlash.assign ((size_t) numPads, 0.0f);
    lastPadCount = numPads;

    if (numPads == 0)
    {
        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Medium")));
        g.drawText ("Load a sample to see slices", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    // Rounded-square pad radius, derived from the theme and clamped.
    const float radius = juce::jlimit (5.0f, 14.0f, theme.cornerRadius * 0.7f);

    for (int i = 0; i < numPads; ++i)
    {
        const auto b = padBounds (i, numPads);
        const float flash = juce::jlimit (0.0f, 1.0f, padFlash[(size_t) i]);

        // Soft, subtle per-pad drop shadow.
        {
            auto shadowRect = b.translated (0.0f, 1.5f);
            g.setColour (theme.shadow.withAlpha (0.18f + 0.22f * flash));
            g.fillRoundedRectangle (shadowRect, radius);
        }

        // Fill: base control colour interpolating toward the accent on trigger.
        const auto fill = theme.control.interpolatedWith (theme.accent, flash);
        g.setColour (fill);
        g.fillRoundedRectangle (b, radius);

        // Optional restrained accent glow while flashing.
        if (theme.glow > 0.0f && flash > 0.0f)
        {
            g.setColour (theme.accentSoft.withMultipliedAlpha (theme.glow * flash));
            g.fillRoundedRectangle (b.expanded (1.5f), radius + 1.5f);
            g.setColour (fill);
            g.fillRoundedRectangle (b, radius);
        }

        // 1px hairline border, easing toward the accent on trigger.
        const auto border = theme.separator.interpolatedWith (theme.accent, flash);
        g.setColour (border);
        g.drawRoundedRectangle (b, radius, 1.0f);

        // Slice number: secondary colour normally, white while flashing.
        const float fontSize = juce::jmin (16.0f, b.getHeight() * 0.34f);
        g.setFont (juce::Font (juce::FontOptions (fontSize).withStyle ("Medium")));
        g.setColour (theme.textSecondary.interpolatedWith (juce::Colours::white, flash));
        g.drawText (juce::String (i + 1), b, juce::Justification::centred);
    }
}

void SliceGrid::mouseDown (const juce::MouseEvent& e)
{
    const int idx = padIndexAt (e.getPosition());
    if (idx < 0)
        return;

    if (auto slice = proc.getSliceEngine().getSlice (idx))
    {
        const float attack = proc.getAPVTS().getRawParameterValue ("attack")->load();
        proc.getVoicePool().triggerVoice (slice->startSample,
                                          slice->lengthSamples,
                                          0.9f, attack,
                                          proc.getLoadedSampleRate());
        if (idx < (int) padFlash.size())
            padFlash[(size_t) idx] = 1.0f;
        repaint();
    }
}

void SliceGrid::timerCallback()
{
    bool any = false;
    for (auto& f : padFlash)
    {
        if (f > 0.0f)
        {
            // Smooth exponential-ish decay for a polished falloff.
            f = juce::jmax (0.0f, f - 0.045f);
            any = true;
        }
    }
    if (any)
        repaint();
}
