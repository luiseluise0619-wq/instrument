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

private:
    //==========================================================================
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleMidi (const juce::MidiBuffer& midi, int numSamples);
    void applyMasterFXChain (juce::AudioBuffer<float>&);
    void applyStereoWidth (juce::AudioBuffer<float>&, float width);
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

    juce::dsp::ProcessSpec spec {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessor)
};
