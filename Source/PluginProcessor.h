#pragma once

#include <JuceHeader.h>

#include "AudioEngine/SliceEngine.h"
#include "AudioEngine/SynthEngine.h"
#include "AudioEngine/VoicePool.h"
#include "AudioEngine/PitchFormant.h"
#include "AudioEngine/GranularEngine.h"
#include "AudioEngine/FXChain.h"
#include "AudioEngine/LoopStation.h"
#include "DSP/Limiter.h"
#include "Licensing.h"

//==============================================================================
class VocalChopAudioProcessor : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener,
                                public juce::ChangeBroadcaster
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

    const juce::String getName() const override { return "Slyce"; }
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
    bool loadSampleFromFile (const juce::File&, bool switchEngineToChop = true);
    bool loadSampleFromMemory (const void* data, int sizeBytes);
    bool loadDemoSample();   // embedded demo vocal, one-click start

    SliceEngine&    getSliceEngine()  { return sliceEngine; }
    VoicePool&      getVoicePool()    { return voicePool;   }
    GranularEngine& getGranular()     { return granularEngine; }
    LoopStation&    getLooper()       { return looper; }
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    double getLoadedSampleRate() const { return loadedSampleRate; }

    /** Message-thread-safe: queues a key/pad hit that the audio thread plays.
        Velocity 0..1 (keyboard clicks pass the strike position). */
    void triggerSlicePad (int sliceIndex, float velocity = 0.9f);

    /** Gate-style pad events for the on-screen / computer keyboard: press
        starts the note, release enters its release stage (like real MIDI). */
    void pressSlicePad (int sliceIndex, float velocity = 0.9f);
    void releaseSlicePad (int sliceIndex);

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

    /** Built-in synth instruments: applying one switches to Synth mode and
        dials in a designed patch (engine architecture + knob defaults). */
    static juce::StringArray getInstrumentNames();
    static juce::StringArray getInstrumentCategories();   // parallel to names
    void applyInstrument (int instrumentIndex);
    int  getCurrentInstrument() const { return currentInstrument; }

    /** Detected musical key of the loaded sample: root 0..11 (C=0) or -1
        when unknown; minor flag alongside. The chord bar follows this. */
    int  getDetectedKeyRoot() const  { return detectedKeyRoot.load(); }
    bool isDetectedKeyMinor() const  { return detectedKeyMinor.load(); }

    /** Licensing: unlicensed = demo (output mutes 2 s every minute). */
    bool isLicensed() const { return licensed.load(); }
    bool finalizeActivation (const juce::String& email, const juce::String& key)
    {
        if (! vcs::Licensing::saveActivation (email, key))
            return false;
        licensed.store (true);
        return true;
    }

private:
    //==========================================================================
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleMidi (const juce::MidiBuffer& midi, int numSamples);
    void drainPadQueue();
    int  triggerSliceIndex (int sliceIndex, float velocity);
    void clearVoiceMapping (int voiceIndex);   // audio thread
    void applyMasterFXChain (juce::AudioBuffer<float>&);
    void applyStereoWidth (juce::AudioBuffer<float>&);
    void reassignSampleToEngines();
    void rescanSlices();
    void analyzeSampleKey();
    void queuePadEvent (int sliceIndex, float velocity, int type);

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    VoicePool      voicePool;
    SynthEngine    synthEngine;
    SliceEngine    sliceEngine;
    PitchFormant   pitchFormant;
    GranularEngine granularEngine;
    FXChain        fxChain;
    LoopStation    looper;
    Limiter        limiter;

    std::shared_ptr<juce::AudioBuffer<float>> sampleBuffer;
    juce::File loadedSampleFile;   // persisted in plugin state so reload restores it

    // Cached parameter pointers.
    std::atomic<float>* pitchParam   = nullptr;
    std::atomic<float>* formantParam = nullptr;
    std::atomic<float>* mixParam     = nullptr;
    std::atomic<float>* widthParam   = nullptr;
    std::atomic<float>* grainSizeParam = nullptr;
    std::atomic<float>* grainMixParam  = nullptr;
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
    std::atomic<float>* synthUnisonParam  = nullptr;
    std::atomic<float>* synthSpreadParam  = nullptr;
    std::atomic<float>* synthSubParam     = nullptr;
    std::atomic<float>* synthNoiseParam   = nullptr;
    std::atomic<float>* synthFMParam      = nullptr;
    std::atomic<float>* synthVibratoParam = nullptr;
    std::atomic<float>* synthChorusParam  = nullptr;
    std::atomic<float>* synthLfoRateParam = nullptr;
    std::atomic<float>* synthLfoAmtParam  = nullptr;
    std::atomic<float>* macroHypeParam  = nullptr;
    std::atomic<float>* macroSpaceParam = nullptr;
    std::atomic<float>* macroDirtParam  = nullptr;

    std::atomic<float> outputLevel { 0.0f };

    // Licensing (loaded once in the constructor; demo gate in processBlock).
    std::atomic<bool> licensed { false };
    int64_t demoClock = 0;

    // Detected key of the loaded sample (message thread writes, UI reads).
    std::atomic<int>  detectedKeyRoot { -1 };
    std::atomic<bool> detectedKeyMinor { false };

    // Effective FX amounts after macro offsets (audio thread only).
    float effDrive = 0.0f, effReverb = 0.0f, effDelay = 0.0f, effWidth = 1.0f;

    double currentSampleRate = 44100.0;
    double loadedSampleRate  = 44100.0;

    // MIDI note that maps to the first slice (C3).
    static constexpr int kRootNote = 48;

    // Lock-free queue of key/pad hits (message thread -> audio thread).
    // Generous size: a dropped note-off would leave a note stuck on.
    enum PadEvent { padTap = 0, padOn = 1, padOff = 2 };
    juce::AbstractFifo padFifo { 256 };
    std::array<int, 256>   padQueue {};
    std::array<float, 256> padQueueVel {};
    std::array<int, 256>   padQueueType {};

    // Which voice each held MIDI note started (-1 = none), for note-off routing.
    std::array<int, 128> noteToVoice {};

    // Same, for held on-screen / computer-keyboard pads (audio thread only).
    std::array<int, 128> padKeyToVoice {};

    // Which embedded demo vocal the Demo button loads next.
    int demoCycle = 0;

    // Engine-architecture half of an instrument (non-APVTS synth settings).
    void applyEnginePatch (int instrumentIndex);
    static int defaultInstrumentIndex();   // boot patch, found by name
    int  currentInstrument = 0;

    // "Drum Kit" instrument: every semitone is a different drum (C=kick,
    // D=snare, E=clap, F=hat...). Pieces are snapshotted on the message
    // thread when the kit is selected; note-ons swap the patch atomically
    // per hit on the audio thread (voices snapshot at start, so ringing
    // drums keep their own sound).
    struct KitPiece
    {
        int   unison = 1, wave = 2, playNote = 60;
        float spread = 0, sub = 0, noise = 0, fm = 0, fmRatio = 2,
              vibHz = 0, vibCents = 0,
              fltHz = 20000, fltEnvOct = 0, fltEnvMs = 200, filterQ = 0.71f,
              drift = 0, velFlt = 1.0f, pitchEnvOct = 0, pitchEnvMs = 60,
              atk = 0, dec = 200, sus = 0, rel = 150;
    };
    std::array<KitPiece, 12> kitPieces {};
    std::atomic<bool> kitMode { false };
    bool buildingKit = false;
    void buildKitPieces();                              // message thread
    void kitNoteOn (int note, float velocity, bool tap); // audio thread

    // Module values applyEnginePatch resolved for the current instrument.
    // applyInstrument mirrors THESE into the knob params — reading them back
    // from synthEngine.patch() would race the audio thread, which rewrites
    // the param-driven patch fields every block.
    struct ModuleDefaults
    {
        int   unison = 1;
        float spread = 0.5f, sub = 0.0f, noise = 0.0f,
              fm = 0.0f, vibCents = 0.0f, chorus = 0.0f;
    };
    ModuleDefaults moduleDefaults;

    // Smoothed stereo width to avoid zipper noise when the knob moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed { 1.0f };

    juce::dsp::ProcessSpec spec {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessor)
};
