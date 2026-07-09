/*  [초보자 안내]
    음정(pitch)과 포먼트(formant)를 따로따로 바꾸는 장치예요.
    포먼트 = 목소리의 '몸집' 같은 울림 특성. 음정만 올리면 다람쥐 소리가 되는데,
    포먼트를 보정하면 음만 높아지고 목소리 성격은 그대로 남습니다.
    내부는 Signalsmith Stretch 라는 스펙트럼(주파수 영역) 엔진을 씁니다.
    이런 처리는 소리를 조금 '늦게' 내보낼 수밖에 없어서(지연, latency),
    그 지연량을 DAW에 알려주고 원음 경로도 같은 만큼 늦춰 위상을 맞춥니다.
*/

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

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed { 1.0f };
};
