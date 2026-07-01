#include "SliceGrid.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

SliceGrid::SliceGrid (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    startTimerHz (30);
}

SliceGrid::~SliceGrid()
{
    stopTimer();
}

static int columnsFor (int numPads)
{
    if (numPads <= 0) return 1;
    int cols = (int) std::ceil (std::sqrt ((double) numPads));
    return juce::jmax (1, cols);
}

juce::Rectangle<float> SliceGrid::padBounds (int index, int numPads) const
{
    const int cols = columnsFor (numPads);
    const int rows = (numPads + cols - 1) / cols;
    const int col  = index % cols;
    const int row  = index / cols;

    const float w = (float) getWidth()  / (float) cols;
    const float h = (float) getHeight() / (float) juce::jmax (1, rows);
    return juce::Rectangle<float> (col * w, row * h, w, h).reduced (4.0f);
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
        g.setColour (theme.text.withAlpha (0.5f));
        g.setFont (14.0f);
        g.drawText ("No slices — load a sample", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    for (int i = 0; i < numPads; ++i)
    {
        auto b = padBounds (i, numPads);
        const float flash = padFlash[(size_t) i];

        g.setColour (theme.knob.darker (0.4f).interpolatedWith (theme.accent, flash));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (theme.accent.withAlpha (0.4f + 0.6f * flash));
        g.drawRoundedRectangle (b, 6.0f, 1.5f);

        g.setColour (theme.text.withAlpha (0.85f));
        g.setFont (juce::jmin (18.0f, b.getHeight() * 0.4f));
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
            f = juce::jmax (0.0f, f - 0.08f);
            any = true;
        }
    }
    if (any)
        repaint();
}
