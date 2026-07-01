#pragma once

#include <JuceHeader.h>

#include "AudioEngine/SliceEngine.h"
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

    /** Message-thread-safe: queues a pad hit that the audio thread plays. */
    void triggerSlicePad (int sliceIndex);

private:
    //==========================================================================
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleMidi (const juce::MidiBuffer& midi, int numSamples);
    void drainPadQueue();
    void triggerSliceIndex (int sliceIndex, float velocity);
    void applyMasterFXChain (juce::AudioBuffer<float>&);
    void applyStereoWidth (juce::AudioBuffer<float>&);
    void reassignSampleToEngines();

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    VoicePool      voicePool;
    SliceEngine    sliceEngine;
    PitchFormant   pitchFormant;
    GranularEngine granularEngine;
    FXChain        fxChain;
    Limiter        limiter;

    std::shared_ptr<juce::AudioBuffer<float>> sampleBuffer;

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

    double currentSampleRate = 44100.0;
    double loadedSampleRate  = 44100.0;

    // MIDI note that maps to the first slice (C3).
    static constexpr int kRootNote = 48;

    // Lock-free queue of pad hits (message thread -> audio thread).
    juce::AbstractFifo padFifo { 64 };
    std::array<int, 64> padQueue {};

    // Smoothed stereo width to avoid zipper noise when the knob moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed { 1.0f };

    juce::dsp::ProcessSpec spec {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessor)
};
