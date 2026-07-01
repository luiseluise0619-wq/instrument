#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

/** tanh soft-clip drive. `drive` 0..1 maps to increasing pre-gain. */
class DistortionFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, float drive);

private:
    double sampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive { 0.0f };
};

/** Room reverb wrapping juce::dsp::Reverb; `amount` 0..1 sets the wet level. */
class ReverbFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::AudioBuffer<float>& buffer, float amount);

private:
    juce::dsp::Reverb reverb;
    double sampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedAmount { 0.0f };
};

/** Stereo feedback delay; `amount` 0..1 sets the wet level. */
class DelayFX
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setTimeSeconds (float seconds) { delaySeconds = juce::jlimit (0.01f, 2.0f, seconds); updateDelay(); }
    void setFeedback (float fb)         { feedback = juce::jlimit (0.0f, 0.95f, fb); }
    void process (juce::AudioBuffer<float>& buffer, float amount);

private:
    void updateDelay();

    std::vector<float> line[2];
    int   writePos = 0;
    int   delaySamples = 22050;
    int   maxSamples   = 96000;
    float delaySeconds = 0.35f;
    float feedback     = 0.4f;
    double sampleRate  = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWet { 0.0f };
};

/**
    Master FX chain. Members are processed in the order the host wants; the
    processor calls distortion → reverb → delay by default.
*/
class FXChain
{
public:
    void prepare (juce::dsp::ProcessSpec spec);

    DistortionFX distortion;
    ReverbFX     reverb;
    DelayFX      delay;

private:
    juce::dsp::ProcessSpec spec {};
};
