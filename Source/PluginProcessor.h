#pragma once

#include <JuceHeader.h>

#include "AudioEngine/SliceEngine.h"
#include "AudioEngine/SynthEngine.h"
#include "AudioEngine/VoicePool.h"
#include "AudioEngine/PitchFormant.h"
#include "AudioEngine/GranularEngine.h"
#include "AudioEngine/FXChain.h"
#include "DSP/Limiter.h"

//==============================================================================
class VocalChopAudioProcessor : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener
{
public:
    VocalChopAudioProcessor();
    ~VocalChopAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VocalChop Studio"; }
    bool acceptsMidi()  const override { return true;  }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // GUI-facing API.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::shared_ptr<juce::AudioBuffer<float>> getLoadedSample() const;
    bool loadSampleFromFile (const juce::File&);
    bool loadSampleFromMemory (const void* data, int sizeBytes);

    SliceEngine&    getSliceEngine()  { return sliceEngine; }
    VoicePool&      getVoicePool()    { return voicePool;   }
    GranularEngine& getGranular()     { return granularEngine; }
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    double getLoadedSampleRate() const { return loadedSampleRate; }

    /** Message-thread-safe: queues a key/pad hit that the audio thread plays.
        Velocity 0..1 (keyboard clicks pass the strike position). */
    void triggerSlicePad (int sliceIndex, float velocity = 0.9f);

    /** Live output level (0..1, peak-ish) for the UI meter. */
    std::atomic<float>& getOutputLevelRef() { return outputLevel; }

    /** True when the Synth engine is selected (keyboard plays synth notes). */
    bool isSynthMode() const
    {
        return engineParam != nullptr && engineParam->load() >= 0.5f;
    }

    /** Applies a named factory preset's parameter values. */
    void applyPreset (int presetIndex);
    static juce::StringArray getPresetNames();

    /** A/B compare: stores the current knobs into the active slot and swaps
        to the other one. Message thread only. */
    void toggleAB();
    bool isSlotB() const { return abIsB; }

    /** Randomises the sound-design parameters (message thread only). */
    void randomizeParams();

    /** Built-in synth instruments: applying one switches to Synth mode and
        dials in a designed patch (wave/detune/octave/ADSR/filter/FX). */
    static juce::StringArray getInstrumentNames();
    void applyInstrument (int instrumentIndex);

private:
    //==========================================================================
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleMidi (const juce::MidiBuffer& midi, int numSamples);
    void drainPadQueue();
    int  triggerSliceIndex (int sliceIndex, float velocity);
    void applyMasterFXChain (juce::AudioBuffer<float>&);
    void applyStereoWidth (juce::AudioBuffer<float>&);
    void reassignSampleToEngines();

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    VoicePool      voicePool;
    SynthEngine    synthEngine;
    SliceEngine    sliceEngine;
    PitchFormant   pitchFormant;
    GranularEngine granularEngine;
    FXChain        fxChain;
    Limiter        limiter;

    std::shared_ptr<juce::AudioBuffer<float>> sampleBuffer;
    juce::File loadedSampleFile;   // persisted in plugin state so reload restores it

    // Cached parameter pointers.
    std::atomic<float>* pitchParam   = nullptr;
    std::atomic<float>* formantParam = nullptr;
    std::atomic<float>* mixParam     = nullptr;
    std::atomic<float>* widthParam   = nullptr;
    std::atomic<float>* grainSizeParam = nullptr;
    std::atomic<float>* driveParam   = nullptr;
    std::atomic<float>* reverbParam  = nullptr;
    std::atomic<float>* delayParam   = nullptr;
    std::atomic<float>* attackParam  = nullptr;
    std::atomic<float>* decayParam   = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* filterCutoffParam = nullptr;
    std::atomic<float>* filterResoParam   = nullptr;
    std::atomic<float>* filterTypeParam    = nullptr;
    std::atomic<float>* delayFeedbackParam = nullptr;
    std::atomic<float>* pingpongParam = nullptr;
    std::atomic<float>* reverseParam  = nullptr;
    std::atomic<float>* playModeParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* engineParam      = nullptr;
    std::atomic<float>* synthWaveParam   = nullptr;
    std::atomic<float>* synthDetuneParam = nullptr;
    std::atomic<float>* synthOctaveParam = nullptr;

    std::atomic<float> outputLevel { 0.0f };

    double currentSampleRate = 44100.0;
    double loadedSampleRate  = 44100.0;

    // MIDI note that maps to the first slice (C3).
    static constexpr int kRootNote = 48;

    // Lock-free queue of key/pad hits (message thread -> audio thread).
    juce::AbstractFifo padFifo { 64 };
    std::array<int, 64>   padQueue {};
    std::array<float, 64> padQueueVel {};

    // Which voice each held MIDI note started (-1 = none), for note-off routing.
    std::array<int, 128> noteToVoice {};

    // A/B compare snapshots of the parameter tree.
    juce::ValueTree snapshotA, snapshotB;
    bool abIsB = false;

    // Smoothed stereo width to avoid zipper noise when the knob moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed { 1.0f };

    juce::dsp::ProcessSpec spec {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessor)
};
