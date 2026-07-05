#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "signalsmith-stretch.h"
#include <vector>

/**
    Pitch and formant shaper backed by Signalsmith Stretch — a high-quality
    spectral (phase-vocoder-family) engine with proper formant handling.

    - Pitch  : setTransposeSemitones  (independent of formants)
    - Formant: setFormantSemitones (compensated), so shifting pitch keeps the
      natural vocal character instead of the "chipmunk" effect.

    The engine has inherent latency (reported via getLatencySamples()); the
    host is told about it, and the dry path is delay-matched so the dry/wet
    mix stays phase-aligned.
*/
class PitchFormant
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setPitch (float semitones)   { pitchSemi   = juce::jlimit (-24.0f, 24.0f, semitones); }
    void setFormant (float semitones) { formantSemi = juce::jlimit (-12.0f, 12.0f, semitones); }

    /** Processes in place, blending dry/wet by mix (0 = dry, 1 = wet). */
    void process (juce::AudioBuffer<float>& buffer,
                  float pitchSemitones, float formantSemitones, float mix);

    int getLatencySamples() const { return latency; }

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch;

    double sr = 44100.0;
    int    channels = 2;
    int    latency  = 0;

    float  pitchSemi   = 0.0f;
    float  formantSemi = 0.0f;

    juce::AudioBuffer<float> inputScratch;   // dry copy fed to the stretcher
    std::vector<float>       dryRing[2];      // latency-matched dry for the mix
    int    ringCap   = 1;
    int    ringWrite = 0;

    // TRUE bypass at pitch/formant = 0: the stretcher's ~100 ms inherent
    // latency (and its CPU) must never tax plain playing. A short fade-in
    // masks the content jump when the path is toggled mid-note.
    bool   engaged  = false;
    int    fadePos  = 1 << 20;   // >= fadeLen means "no fade running"
    static constexpr int kFadeLen = 256;
    void   applyToggleFade (juce::AudioBuffer<float>&, int numSamples, int numChannels);

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed { 1.0f };
};
