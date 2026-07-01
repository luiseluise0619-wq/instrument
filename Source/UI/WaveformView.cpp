#include "WaveformView.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

WaveformView::WaveformView (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    startTimerHz (30);
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

    const int numColumns = juce::jmax (1, getWidth());
    const int numSamples = sample->getNumSamples();
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
    glowPhase += 0.05f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
        glowPhase -= juce::MathConstants<float>::twoPi;
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    auto bounds = getLocalBounds().toFloat();

    // Glass panel background.
    g.setColour (theme.panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (theme.accent.withAlpha (0.25f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    if (maxEnv.empty())
    {
        g.setColour (theme.text.withAlpha (0.5f));
        g.setFont (16.0f);
        g.drawText (fileHover ? "Release to load audio"
                              : "Drop an audio file here",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const float midY   = bounds.getCentreY();
    const float scale  = bounds.getHeight() * 0.45f;
    const float glow   = 0.6f + 0.4f * std::sin (glowPhase);

    // Waveform fill.
    juce::Path wave;
    wave.startNewSubPath (0.0f, midY);
    for (size_t x = 0; x < maxEnv.size(); ++x)
        wave.lineTo ((float) x, midY - maxEnv[x] * scale);
    for (size_t x = maxEnv.size(); x-- > 0; )
        wave.lineTo ((float) x, midY - minEnv[x] * scale);
    wave.closeSubPath();

    g.setColour (theme.waveform.withAlpha (0.75f * glow * theme.glow));
    g.fillPath (wave);
    g.setColour (theme.waveform);
    g.strokePath (wave, juce::PathStrokeType (1.0f));

    // Slice markers.
    auto sample = proc.getLoadedSample();
    if (sample != nullptr && sample->getNumSamples() > 0)
    {
        const auto& slices = proc.getSliceEngine().getSlices();
        const float widthRatio = bounds.getWidth() / (float) sample->getNumSamples();
        g.setColour (theme.highlight.withAlpha (0.6f));
        for (const auto& s : slices)
        {
            const float xPos = s.startSample * widthRatio;
            g.drawVerticalLine ((int) xPos, bounds.getY() + 2.0f, bounds.getBottom() - 2.0f);
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
