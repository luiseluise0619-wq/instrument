#pragma once

#include <set>
#include <limits>

#include <JuceHeader.h>

#include "AudioEngine/SliceEngine.h"
#include "AudioEngine/SynthEngine.h"
#include "AudioEngine/VoicePool.h"
#include "AudioEngine/PitchFormant.h"
#include "AudioEngine/GranularEngine.h"
#include "AudioEngine/FXChain.h"
#include "AudioEngine/LoopStation.h"
#include "AudioEngine/SamplerEngine.h"
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
    bool loadDemoSample();            // next built-in vocal in the cycle
    bool loadDemoSample (int index);  // a specific one; wraps at either end

    /** The built-in vocals, in menu order, and the section each belongs to
        (parallel arrays - index i of one matches index i of the other). */
    static juce::StringArray getDemoSampleNames();
    static juce::StringArray getDemoSampleGroups();
    static int               getNumDemoSamples();

    /** Which built-in vocal is loaded, or -1 when the sample came from a file
        of the user's own. The hero row needs this to say "3 of 36" and to know
        where stepping should go next. */
    int getCurrentDemoIndex() const { return currentDemo; }

    /** Name of whatever is currently loaded — the built-in vocal's label, or
        the file's name. Empty when nothing is loaded. The Demo button was
        indistinguishable from a no-op without this: it swaps the sample, and
        one waveform of a voice looks much like another. */
    juce::String getLoadedSampleName() const { return loadedSampleName; }

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

    /** Stop a pad NOW rather than entering its release stage. Used when the
        player moves to another note: on a pad or a bell, a natural release
        means the previous note is still sounding under the new one. */
    void chokeSlicePad (int sliceIndex);

    /** Live output level (0..1, peak-ish) for the UI meter. */
    std::atomic<float>& getOutputLevelRef() { return outputLevel; }

    /** Tempo reported by the host this block, or 0 when the host gives none
        (standalone, or a host that does not publish transport info). */
    double getHostBpm() const { return hostBpm.load(); }

    /** Mono output ring for the live oscilloscope (Synth/Sampled modes).
        Size is a power of two; readers index with (i & (size-1)). */
    const std::array<float, 2048>& getScopeRing() const { return scopeRing; }
    int getScopeWritePos() const { return scopeWritePos.load (std::memory_order_acquire); }

    /** The slice the user last touched, in either the waveform or the
        keyboard, or -1. It lives here rather than in either view because it is
        the ONE piece of state the two share: clicking a lane has to light a
        key, and pressing a key has to light a lane. Owned by the message
        thread; the audio thread never reads it. */
    int  getSelectedSlice() const        { return selectedSlice; }
    void setSelectedSlice (int s)        { selectedSlice = s; }

    /** Which slice the n-th semitone above the root plays. Public so a test
        can compare the mapping against the audio that actually comes out. */
    static int diatonicSliceIndex (int semis);   // white-key order -> slice order

    /** Where the most recent slice voice ACTUALLY started, in samples into the
        loaded audio, plus the slice index it resolved to.

        This exists because the question "does key K play slice N" could not be
        answered from outside. Both trigger entry points run through the same
        key mapping, so comparing one against the other only ever proved the
        mapping equals itself; and by the time audio leaves processBlock it has
        been through an envelope, the filter and the master chain, so every
        slice correlates best with slice 0. The sample OFFSET the voice began
        reading from is the one fact that is downstream of the mapping and
        upstream of everything that smears it.

        Written on the audio thread, read by the offline tests. */
    std::atomic<int> lastVoiceStartSample { -1 };
    std::atomic<int> lastVoiceSlice       { -1 };

    /** True when the Chop engine is selected: the loaded sample is cut into
        slices laid one per key. */
    bool isChopMode() const
    {
        return engineParam != nullptr && engineParam->load() < 0.5f;
    }

    /** True when the Synth engine is selected (keyboard plays synth notes). */
    bool isSynthMode() const
    {
        return engineParam != nullptr && engineParam->load() >= 0.5f;
    }

    /** True when the Sampled (SFZ multisample) engine is selected. */
    bool isSamplerMode() const
    {
        if (engineParam == nullptr) return false;
        const float v = engineParam->load();
        return v >= 1.5f && v < 2.5f;
    }

    /** True when Melody mode is selected: the loaded chop sample played
        chromatically across the keyboard (root = C3). */
    bool isMelodyMode() const
    {
        return engineParam != nullptr && engineParam->load() >= 2.5f;
    }

    /** Loads an SFZ multisample bank (message thread; heavy - decodes all
        samples). On success switches to Sampled mode and remembers the path
        in the session state. */
    bool loadSfzBank (const juce::File& f, juce::String& error)
    {
        if (! samplerEngine.loadSfz (f, error))
            return false;
        loadedSfzFile = f;
        if (auto* p = apvts.getParameter ("engine"))
            p->setValueNotifyingHost (p->convertTo0to1 (2.0f));   // Sampled
        rememberSfzBank (samplerEngine.getBankName(), f);
        return true;
    }
    SamplerEngine& getSampler() { return samplerEngine; }

    /** Recently loaded SFZ banks, shared across ALL sessions (stored next to
        the license file) so a loaded violin/piano never just disappears —
        they reappear under MY SAMPLES in the instrument menu. */
    static juce::StringArray getRecentSfzNames();
    static juce::StringArray getRecentSfzPaths();   // parallel to names
    static void rememberSfzBank (const juce::String& name, const juce::File& f);

    /** Destructive sample edits over a [start,end) fraction selection made
        on the waveform (message thread). One level of undo is kept. */
    enum class SampleEdit { Trim, Cut, Fade, Normalize };
    bool editSample (SampleEdit op, float startFrac, float endFrac);
    bool undoSampleEdit();
    bool canUndoSampleEdit() const { return prevSampleBuffer != nullptr; }

    /** Applies a named factory preset's parameter values. */
    void applyPreset (int presetIndex);
    static juce::StringArray getPresetNames();
    /** Presets 0..N-1 are CHOP presets (FX only, your sample keeps playing);
        everything after is a SOUND preset that loads an instrument too. */
    static int getNumChopPresets();

    /** User presets: the whole parameter tree saved under a name, stored as
        one XML file each next to the licence so every session sees them. */
    static juce::File        userPresetFolder();
    static juce::StringArray getUserPresetNames();
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);

    /** Built-in synth instruments: applying one switches to Synth mode and
        dials in a designed patch (engine architecture + knob defaults). */
    static juce::StringArray getInstrumentNames();
    static juce::StringArray getInstrumentCategories();   // parallel to names

    /** A second index over the same instruments, grouped the way someone
        building a track thinks: EDM / HIP-HOP / POP, each split into the roles
        a track needs filled. Members are instrument NAMES from the catalogue
        above - no bank owns a patch of its own, and one instrument can appear
        in several banks. */
    static juce::StringArray getGenreBankNames();
    static juce::StringArray getGenreRoleNames (int bank);
    static juce::StringArray getGenreRoleInstruments (int bank, int role);
    static int               getGenreBankSize (int bank);
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

    /** The output-FX ids that survive an instrument change once touched. */
    static const juce::StringArray& ownableFx();

    /** Hands the FX rack back to the instrument table. The editor calls this
        from the FX card's reset, so "stop following me" is undoable. */
    void releaseFxOwnership() { userOwnsFx.clear(); }
    bool userOwnsAnyFx() const { return ! userOwnsFx.empty(); }
    void handleMidi (const juce::MidiBuffer& midi, int numSamples);

    /** Every note the plugin plays goes through here, whatever its source
        (MIDI, on-screen keys, chord bar, arpeggiator). */
    void routeNoteOn (int note, float velocity, bool selfReleasing);
    void routeNoteOff (int note);

    /** Arpeggiator: while it is on, held notes feed the pattern instead of
        sounding directly, and it triggers them on the tempo grid. */
    void advanceArp (int numSamples);
    double currentBpm() const;   // host tempo, else the looper's own BPM
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
    SamplerEngine  samplerEngine;
    SamplerEngine  melodyEngine;    // the loaded chop sample, pitched per key
    std::shared_ptr<juce::AudioBuffer<float>> melodySourceRef;   // rebuild guard
    juce::File     loadedSfzFile;   // persisted so reload restores the bank
    SliceEngine    sliceEngine;
    PitchFormant   pitchFormant;
    GranularEngine granularEngine;
    FXChain        fxChain;
    LoopStation    looper;
    Limiter        limiter;

    std::shared_ptr<juce::AudioBuffer<float>> sampleBuffer;
    std::shared_ptr<juce::AudioBuffer<float>> prevSampleBuffer;   // 1-level edit undo
    double prevSampleRate = 44100.0;
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
    int selectedSlice = -1;   // message thread only
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
    std::atomic<float>* synthGlideParam   = nullptr;
    std::atomic<float>* delaySyncParam  = nullptr;
    bool lastDelayWasSynced = false;   // audio thread only
    std::atomic<float>* macroHypeParam  = nullptr;
    std::atomic<float>* macroSpaceParam = nullptr;
    std::atomic<float>* macroDirtParam  = nullptr;

    std::atomic<float> outputLevel { 0.0f };
    juce::String loadedSampleName;

    std::atomic<double> hostBpm { 0.0 };   // 0 = host published no tempo
    // Musical position at the top of the block, and whether the transport is
    // rolling. The arp and the pump PHASE-LOCK to this: without it they run
    // free and land off the DAW's grid, which is the whole point of them.
    std::atomic<double> hostPpq { -1.0 };  // < 0 = host published no position
    std::atomic<bool>   hostPlaying { false };
    int64_t arpLastStep = std::numeric_limits<int64_t>::min();

    // --- Arpeggiator (audio thread state) ---------------------------------
    std::array<bool, 128> arpHeld {};
    std::array<float, 128> arpVel {};   // how hard each held key was struck
    int   arpStepSamples = 0;     // samples left in the current step
    int   arpGateSamples = 0;     // samples left before the current note ends
    int   arpNote = -1;           // the note currently sounding
    int   arpIndex = 0;           // walk position in the held set
    int   arpDir = 1;             // for up-down
    int   arpOctave = 0;          // octave stack position
    juce::Random arpRandom;

    std::atomic<float>* arpModeParam = nullptr;
    std::atomic<float>* arpRateParam = nullptr;
    std::atomic<float>* arpGateParam = nullptr;
    std::atomic<float>* arpOctParam  = nullptr;

    // --- Sidechain pump ----------------------------------------------------
    double pumpPhase = 0.0;
    std::atomic<float>* pumpAmtParam  = nullptr;
    std::atomic<float>* pumpRateParam = nullptr;

    // Live-output oscilloscope ring (audio thread writes, UI paint reads).
    std::array<float, 2048> scopeRing {};
    std::atomic<int>        scopeWritePos { 0 };

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
    enum PadEvent { padTap = 0, padOn = 1, padOff = 2, padChoke = 3 };
    juce::AbstractFifo padFifo { 256 };
    std::array<int, 256>   padQueue {};
    std::array<float, 256> padQueueVel {};
    std::array<int, 256>   padQueueType {};

    // Which voice each held MIDI note started (-1 = none), for note-off routing.
    std::array<int, 128> noteToVoice {};

    // Same, for held on-screen / computer-keyboard pads (audio thread only).
    std::array<int, 128> padKeyToVoice {};

    // Which embedded demo vocal the Demo button loads next.
    int demoCycle  = 0;
    int currentDemo = -1;   // index into the built-in vocals, -1 for a user file

    // Which output-FX knobs the user has taken over, and whether the change
    // currently arriving is our own doing. See applyInstrument.
    std::set<juce::String> userOwnsFx;
    bool applyingPatch = false;

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
    // Double-buffered: buildKitPieces (message thread) fills the inactive
    // page and flips the index; kitNoteOn (audio thread) reads the active
    // page - rewriting one shared array under the reader was a data race.
    std::array<KitPiece, 12> kitPiecesBuf[2] {};
    std::atomic<int>         kitPage { 0 };
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
              fm = 0.0f, vibCents = 0.0f, chorus = 0.0f, glideMs = 0.0f;
    };
    ModuleDefaults moduleDefaults;

    // Smoothed stereo width to avoid zipper noise when the knob moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed { 1.0f };

    juce::dsp::ProcessSpec spec {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopAudioProcessor)
};
