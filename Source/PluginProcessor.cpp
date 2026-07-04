#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AudioEngine/SampleLoader.h"
#include "BinaryData.h"

//==============================================================================
VocalChopAudioProcessor::VocalChopAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    pitchParam     = apvts.getRawParameterValue ("pitch");
    formantParam   = apvts.getRawParameterValue ("formant");
    mixParam       = apvts.getRawParameterValue ("mix");
    widthParam     = apvts.getRawParameterValue ("width");
    grainSizeParam = apvts.getRawParameterValue ("grainSize");
    grainMixParam  = apvts.getRawParameterValue ("grainMix");
    driveParam     = apvts.getRawParameterValue ("drive");
    reverbParam    = apvts.getRawParameterValue ("reverb");
    delayParam     = apvts.getRawParameterValue ("delay");
    attackParam    = apvts.getRawParameterValue ("attack");
    decayParam     = apvts.getRawParameterValue ("decay");
    sustainParam   = apvts.getRawParameterValue ("sustain");
    releaseParam   = apvts.getRawParameterValue ("release");
    filterCutoffParam  = apvts.getRawParameterValue ("filterCutoff");
    filterResoParam    = apvts.getRawParameterValue ("filterReso");
    filterTypeParam    = apvts.getRawParameterValue ("filterType");
    delayFeedbackParam = apvts.getRawParameterValue ("delayFeedback");
    pingpongParam  = apvts.getRawParameterValue ("pingpong");
    reverseParam   = apvts.getRawParameterValue ("reverse");
    playModeParam  = apvts.getRawParameterValue ("playMode");
    outputGainParam  = apvts.getRawParameterValue ("outputGain");
    engineParam      = apvts.getRawParameterValue ("engine");
    synthWaveParam   = apvts.getRawParameterValue ("synthWave");
    synthDetuneParam = apvts.getRawParameterValue ("synthDetune");
    synthOctaveParam = apvts.getRawParameterValue ("synthOctave");
    synthUnisonParam  = apvts.getRawParameterValue ("synthUnison");
    synthSpreadParam  = apvts.getRawParameterValue ("synthSpread");
    synthSubParam     = apvts.getRawParameterValue ("synthSub");
    synthNoiseParam   = apvts.getRawParameterValue ("synthNoise");
    synthFMParam      = apvts.getRawParameterValue ("synthFM");
    synthVibratoParam = apvts.getRawParameterValue ("synthVibrato");
    synthChorusParam  = apvts.getRawParameterValue ("synthChorus");
    synthLfoRateParam = apvts.getRawParameterValue ("synthLfoRate");
    synthLfoAmtParam  = apvts.getRawParameterValue ("synthLfoAmt");

    apvts.addParameterListener ("pitch", this);
    apvts.addParameterListener ("formant", this);

    // First-run experience: the default engine is Synth, so boot with a
    // designed patch (Supersaw Lead) instead of the bare init tone.
    applyEnginePatch (defaultInstrumentIndex());
}

VocalChopAudioProcessor::~VocalChopAudioProcessor()
{
    apvts.removeParameterListener ("pitch", this);
    apvts.removeParameterListener ("formant", this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocalChopAudioProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "pitch", "Pitch", Range (-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "formant", "Formant", Range (-12.0f, 12.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix", Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width", "Width", Range (0.0f, 2.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "grainSize", "Grain Size", Range (20.0f, 500.0f, 1.0f), 80.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "grainMix", "Grain Mix", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "drive", "Drive", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "reverb", "Reverb", Range (0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delay", "Delay", Range (0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack", Range (0.0f, 1000.0f, 0.1f), 5.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "decay", "Decay", Range (0.0f, 2000.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "sustain", "Sustain", Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release", Range (0.0f, 2000.0f, 0.1f), 20.0f));

    // Filter cutoff with a musical (logarithmic) response.
    juce::NormalisableRange<float> cutoffRange (20.0f, 20000.0f, 1.0f);
    cutoffRange.setSkewForCentre (1000.0f);
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "filterCutoff", "Filter Cutoff", cutoffRange, 20000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "filterReso", "Filter Reso", Range (0.1f, 8.0f, 0.01f), 0.707f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "filterType", "Filter Type",
        juce::StringArray { "Off", "Low Pass", "High Pass", "Band Pass" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayFeedback", "Delay FB", Range (0.0f, 0.95f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "pingpong", "Ping-Pong", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "reverse", "Reverse", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "playMode", "Play Mode", juce::StringArray { "Gate", "One-Shot" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputGain", "Output", Range (-24.0f, 6.0f, 0.1f), 0.0f));

    // Synth engine mode: play oscillators instead of sample slices.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "engine", "Engine", juce::StringArray { "Chop", "Synth" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "synthWave", "Synth Wave",
        juce::StringArray { "Saw", "Square", "Sine", "Triangle" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthDetune", "Detune", Range (0.0f, 50.0f, 0.1f), 7.0f));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "synthOctave", "Octave", -2, 2, 0));

    // Synth architecture modules, adjustable like a proper wavetable synth.
    // Defaults match the boot patch (Supersaw Lead).
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "synthUnison", "Unison", 1, 7, 7));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthSpread", "Spread", Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthSub", "Sub Osc", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthNoise", "Noise", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthFM", "FM Amount", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthVibrato", "Vibrato", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthChorus", "Chorus", Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthLfoRate", "LFO Rate", Range (0.05f, 8.0f, 0.01f, 0.5f), 2.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthLfoAmt", "Motion", Range (0.0f, 1.0f, 0.001f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void VocalChopAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;

    voicePool.prepare (spec);
    synthEngine.prepare (spec);
    pitchFormant.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    granularEngine.prepare (spec);
    fxChain.prepare (spec);
    limiter.prepare (sampleRate, samplesPerBlock);

    widthSmoothed.reset (sampleRate, 0.02);
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr ? widthParam->load() : 1.0f);

    noteToVoice.fill (-1);
    padKeyToVoice.fill (-1);

    // Total plugin latency: the pitch/formant engine's inherent latency plus
    // the limiter's look-ahead delay.
    setLatencySamples (pitchFormant.getLatencySamples()
                       + limiter.getLatencySamples());

    reassignSampleToEngines();
}

void VocalChopAudioProcessor::releaseResources()
{
    voicePool.releaseAll();
}

bool VocalChopAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void VocalChopAudioProcessor::reassignSampleToEngines()
{
    sliceEngine.setSample (sampleBuffer, loadedSampleRate);
    voicePool.setSource (sampleBuffer, loadedSampleRate);
}

void VocalChopAudioProcessor::rescanSlices()
{
    sliceEngine.rebuildSlices();

    // Transient detection on pads / sustained material can yield almost no
    // slices, which reads as "the keyboard is broken". Fall back to an even
    // 16-part grid so a fresh load is always playable.
    if (sliceEngine.getMode() == SliceEngine::Transient
        && sliceEngine.getNumSlices() < 4)
    {
        sliceEngine.setMode (SliceEngine::Grid);
        sliceEngine.setGridDivision (16);
        sliceEngine.rebuildSlices();
    }
}

//==============================================================================
void VocalChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    // Push per-block voice settings so new triggers use the current ADSR / mode.
    voicePool.setEnvelope (attackParam->load(), decayParam->load(),
                           sustainParam->load(), releaseParam->load());
    voicePool.setReverse (reverseParam->load() >= 0.5f);
    voicePool.setPlayMode (playModeParam->load() >= 0.5f);

    synthEngine.setEnvelope (attackParam->load(), decayParam->load(),
                             sustainParam->load(), releaseParam->load());
    synthEngine.setWave ((int) synthWaveParam->load());
    synthEngine.setDetuneCents (synthDetuneParam->load());
    synthEngine.setOctave ((int) synthOctaveParam->load());

    // Adjustable synth modules: the knobs drive the patch every block, so
    // tweaking Unison / Sub / FM / Chorus reshapes the sound live.
    {
        auto& pt = synthEngine.patch();
        pt.unison        = juce::jlimit (1, 7, (int) synthUnisonParam->load());
        pt.stereoSpread  = synthSpreadParam->load();
        pt.subLevel      = synthSubParam->load();
        pt.noiseLevel    = synthNoiseParam->load();
        pt.fmAmount      = synthFMParam->load();
        pt.vibDepthCents = synthVibratoParam->load() * 30.0f;
        pt.chorusMix     = synthChorusParam->load();
        pt.lfoRateHz     = synthLfoRateParam->load();
        pt.lfoDepthOct   = synthLfoAmtParam->load() * 2.0f;
    }

    // 1) MIDI + pad-queue → slice / synth triggers.
    handleMidi (midi, numSamples);
    drainPadQueue();

    // 2) Render both sources (the unused engine is simply silent, so
    //    switching modes mid-note never clicks).
    voicePool.renderNextBlock (buffer, numSamples);
    synthEngine.render (buffer, numSamples);

    // 3) Pitch / formant transformation.
    pitchFormant.process (buffer,
                          pitchParam->load(),
                          formantParam->load(),
                          mixParam->load());

    // 4) Granular texture (bypassed unless its mix is raised).
    granularEngine.setGrainSize (grainSizeParam->load());
    granularEngine.setMix (grainMixParam->load());
    granularEngine.process (buffer);

    // 5) Master FX chain.
    applyMasterFXChain (buffer);

    // 6) Stereo width.
    applyStereoWidth (buffer);

    // 7) Output gain trim (before the limiter so it protects the boosted signal).
    const float gain = juce::Decibels::decibelsToGain (outputGainParam->load());
    buffer.applyGain (gain);

    // 8) Brick-wall limiter.
    limiter.process (buffer);

    // Publish the output level for the UI meter (meter applies its own ballistics).
    outputLevel.store (buffer.getMagnitude (0, numSamples), std::memory_order_relaxed);
}

void VocalChopAudioProcessor::handleMidi (const juce::MidiBuffer& midi, int /*numSamples*/)
{
    for (const auto meta : midi)
    {
        // Note/CC messages only — building a MidiMessage from a large SysEx
        // event would heap-allocate on the audio thread.
        if (meta.numBytes > 3)
            continue;

        const auto msg = meta.getMessage();

        const int  note  = msg.getNoteNumber();
        const bool synth = isSynthMode();

        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            if (synth)
            {
                synthEngine.noteOn (note, msg.getVelocity() / 127.0f);
            }
            else
            {
                // Retriggering a still-held note releases its old voice so
                // the previous hit doesn't ring on as an orphan.
                if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
                    voicePool.releaseVoice (noteToVoice[(size_t) note]);

                const int voice = triggerSliceIndex (note - kRootNote,
                                                     msg.getVelocity() / 127.0f);
                if (juce::isPositiveAndBelow (note, 128))
                    noteToVoice[(size_t) note] = voice;
            }
        }
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            // Release BOTH engines regardless of the current mode: the note
            // may have started before an engine switch, and each call is a
            // safe no-op when that engine holds nothing for this note.
            synthEngine.noteOff (note);

            if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
            {
                voicePool.releaseVoice (noteToVoice[(size_t) note]);
                noteToVoice[(size_t) note] = -1;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            // All Sound Off is an emergency stop: it must silence even
            // one-shot voices, which ignore ordinary releases by design.
            if (msg.isAllSoundOff())
                voicePool.stopAll();
            else
                voicePool.releaseAll();

            synthEngine.releaseAll();
            noteToVoice.fill (-1);
            padKeyToVoice.fill (-1);
        }
    }
}

void VocalChopAudioProcessor::clearVoiceMapping (int voiceIndex)
{
    // Audio thread. A voice slot that self-finished (or was stolen) may still
    // be referenced by another held note's mapping; releasing that note later
    // would then cut whichever NEW note reused the slot. Scrub stale entries
    // whenever a slot is (re)assigned.
    if (voiceIndex < 0)
        return;

    for (auto& m : noteToVoice)
        if (m == voiceIndex)
            m = -1;
    for (auto& m : padKeyToVoice)
        if (m == voiceIndex)
            m = -1;
}

int VocalChopAudioProcessor::triggerSliceIndex (int sliceIndex, float velocity)
{
    // Audio thread. tryGetSlice() safely no-ops if a re-slice is in progress.
    SlicePoint slice;
    if (sliceEngine.tryGetSlice (sliceIndex, slice))
    {
        const int voice = voicePool.triggerVoice (slice.startSample,
                                                  slice.lengthSamples, velocity);
        clearVoiceMapping (voice);
        return voice;
    }
    return -1;
}

void VocalChopAudioProcessor::queuePadEvent (int sliceIndex, float velocity, int type)
{
    // Message thread (key click / computer keyboard). Hand the event to the
    // audio thread lock-free.
    int start1, size1, start2, size2;
    padFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        padQueue[(size_t) start1]     = sliceIndex;
        padQueueVel[(size_t) start1]  = juce::jlimit (0.0f, 1.0f, velocity);
        padQueueType[(size_t) start1] = type;
        padFifo.finishedWrite (1);
    }
}

void VocalChopAudioProcessor::triggerSlicePad (int sliceIndex, float velocity)
{
    queuePadEvent (sliceIndex, velocity, padTap);
}

void VocalChopAudioProcessor::pressSlicePad (int sliceIndex, float velocity)
{
    queuePadEvent (sliceIndex, velocity, padOn);
}

void VocalChopAudioProcessor::releaseSlicePad (int sliceIndex)
{
    queuePadEvent (sliceIndex, 0.0f, padOff);
}

void VocalChopAudioProcessor::drainPadQueue()
{
    int start1, size1, start2, size2;
    padFifo.prepareToRead (padFifo.getNumReady(), start1, size1, start2, size2);

    const bool synth = isSynthMode();

    auto fire = [this, synth] (int idx, float vel, int type)
    {
        const int note = kRootNote + idx;   // same key mapping as MIDI

        if (type == padOff)
        {
            // Release both engines: the pad may have been pressed before an
            // engine switch (each call safely no-ops when not applicable).
            synthEngine.noteOff (note);

            if (juce::isPositiveAndBelow (idx, 128) && padKeyToVoice[(size_t) idx] >= 0)
            {
                voicePool.releaseVoice (padKeyToVoice[(size_t) idx]);
                padKeyToVoice[(size_t) idx] = -1;
            }
            return;
        }

        if (synth)
        {
            if (type == padOn) synthEngine.noteOn (note, vel);
            else               synthEngine.tapNote (note, vel);
        }
        else
        {
            const int voice = triggerSliceIndex (idx, vel);
            if (type == padOn && juce::isPositiveAndBelow (idx, 128))
                padKeyToVoice[(size_t) idx] = voice;
        }
    };

    for (int i = 0; i < size1; ++i)
        fire (padQueue[(size_t) (start1 + i)], padQueueVel[(size_t) (start1 + i)],
              padQueueType[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        fire (padQueue[(size_t) (start2 + i)], padQueueVel[(size_t) (start2 + i)],
              padQueueType[(size_t) (start2 + i)]);

    padFifo.finishedRead (size1 + size2);
}

void VocalChopAudioProcessor::applyMasterFXChain (juce::AudioBuffer<float>& buffer)
{
    fxChain.filter.process     (buffer, filterCutoffParam->load(),
                                filterResoParam->load(),
                                (int) filterTypeParam->load());
    fxChain.distortion.process (buffer, driveParam->load());
    fxChain.reverb.process     (buffer, reverbParam->load());

    fxChain.delay.setFeedback (delayFeedbackParam->load());
    fxChain.delay.setPingpong (pingpongParam->load() >= 0.5f);
    fxChain.delay.process      (buffer, delayParam->load());
}

void VocalChopAudioProcessor::applyStereoWidth (juce::AudioBuffer<float>& buffer)
{
    widthSmoothed.setTargetValue (widthParam->load());

    if (buffer.getNumChannels() < 2)
    {
        widthSmoothed.skip (buffer.getNumSamples());
        return;
    }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float width = widthSmoothed.getNextValue();
        const float mid  = (L[i] + R[i]) * 0.5f;
        const float side = (L[i] - R[i]) * 0.5f * width;
        L[i] = mid + side;
        R[i] = mid - side;
    }
}

//==============================================================================
std::shared_ptr<juce::AudioBuffer<float>> VocalChopAudioProcessor::getLoadedSample() const
{
    return sampleBuffer;
}

bool VocalChopAudioProcessor::loadSampleFromFile (const juce::File& file,
                                                  bool switchEngineToChop)
{
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (file, sr);
    if (buffer == nullptr)
        return false;

    sampleBuffer     = buffer;
    loadedSampleRate = sr;
    loadedSampleFile = file;
    reassignSampleToEngines();
    rescanSlices();

    // Loading a sample means the user wants to chop it - switch engines so
    // the keyboard immediately plays slices (state restore passes false).
    if (switchEngineToChop)
        if (auto* p = apvts.getParameter ("engine"))
            p->setValueNotifyingHost (0.0f);

    return true;
}

bool VocalChopAudioProcessor::loadDemoSample()
{
    return loadSampleFromMemory (BinaryData::vocal_chop_demo_wav,
                                 BinaryData::vocal_chop_demo_wavSize);
}

bool VocalChopAudioProcessor::loadSampleFromMemory (const void* data, int sizeBytes)
{
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (data, sizeBytes, sr);
    if (buffer == nullptr)
        return false;

    sampleBuffer     = buffer;
    loadedSampleRate = sr;
    reassignSampleToEngines();
    rescanSlices();

    if (auto* p = apvts.getParameter ("engine"))
        p->setValueNotifyingHost (0.0f);
    return true;
}

//==============================================================================
void VocalChopAudioProcessor::parameterChanged (const juce::String& id, float newValue)
{
    if (id == "pitch")   pitchFormant.setPitch (newValue);
    if (id == "formant") pitchFormant.setFormant (newValue);
}

//==============================================================================
juce::StringArray VocalChopAudioProcessor::getPresetNames()
{
    return { "Init", "Clean Chops", "Vocal Shimmer", "Lo-Fi Tape",
             "Reverse Swell", "Hard Stutter" };
}

void VocalChopAudioProcessor::applyPreset (int presetIndex)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Start every preset from a known baseline, then apply the character.
    set ("pitch", 0.0f);      set ("formant", 0.0f);   set ("mix", 1.0f);
    set ("width", 1.0f);      set ("grainSize", 80.0f);
    set ("drive", 0.0f);      set ("reverb", 0.0f);    set ("delay", 0.0f);
    set ("attack", 5.0f);     set ("decay", 0.0f);     set ("sustain", 1.0f);
    set ("release", 20.0f);   set ("filterType", 0.0f); set ("filterCutoff", 20000.0f);
    set ("filterReso", 0.707f); set ("delayFeedback", 0.4f); set ("pingpong", 0.0f);
    set ("reverse", 0.0f);    set ("playMode", 0.0f);  set ("outputGain", 0.0f);

    switch (presetIndex)
    {
        case 1: // Clean Chops
            set ("attack", 2.0f); set ("reverb", 0.15f); set ("delay", 0.1f);
            break;

        case 2: // Vocal Shimmer
            set ("formant", 3.0f); set ("reverb", 0.5f); set ("delay", 0.35f);
            set ("pingpong", 1.0f); set ("release", 200.0f);
            set ("filterType", 1.0f); set ("filterCutoff", 9000.0f);
            break;

        case 3: // Lo-Fi Tape
            set ("drive", 0.45f); set ("filterType", 1.0f); set ("filterCutoff", 3500.0f);
            set ("filterReso", 1.0f); set ("width", 0.8f); set ("reverb", 0.2f);
            set ("pitch", -0.15f); // slight detune wobble feel
            break;

        case 4: // Reverse Swell
            set ("reverse", 1.0f); set ("attack", 200.0f); set ("release", 300.0f);
            set ("reverb", 0.6f); set ("delay", 0.3f); set ("playMode", 1.0f);
            break;

        case 5: // Hard Stutter
            set ("attack", 0.0f); set ("decay", 40.0f); set ("sustain", 0.0f);
            set ("release", 5.0f); set ("drive", 0.5f); set ("grainSize", 40.0f);
            set ("playMode", 1.0f);
            break;

        case 0: // Init (baseline already applied)
        default:
            break;
    }
}

//==============================================================================
namespace
{
    /** One designed instrument: the synth-engine architecture plus the public
        knob defaults. Kept as plain data so adding instruments is one line. */
    struct InstrumentDef
    {
        const char* category;
        const char* name;
        // Engine patch --------------------------------------------------------
        int   unison;  float spread, sub, noise, fm, fmRatio, vibHz, vibCents;
        float fltHz, fltEnvOct, fltEnvMs;
        // Public knobs --------------------------------------------------------
        int   wave, octave; float detune;
        float atk, dec, sus, rel;
        float drive, reverb, delay, pingpong, width;
    };

    // wave: 0 Saw, 1 Square, 2 Sine, 3 Triangle
    static const InstrumentDef kInstruments[] = {
    // cat      name              uni sprd  sub   noise fm    fmRat vibHz vibC  fltHz  envOct envMs | wav oct det   atk   dec   sus   rel   drv   rev   dly   pp   wid
    { "INIT",  "Init Synth",       1, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 20000, 0.0f,  200, 0,  0,  7.0f,  5,   120, 0.75f,  60, 0.00f, 0.15f, 0.0f, 0, 1.0f },

    { "BASS",  "Neon Bass",        3, 0.5f, 0.6f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   220, 3.0f,  140, 0, -1, 14.0f,  0,   220, 0.80f,  80, 0.30f, 0.00f, 0.0f, 0, 0.9f },
    { "BASS",  "Sub 808",          1, 0.0f, 1.0f, 0.03f,0.0f, 2.0f, 0.0f, 0.0f,   900, 2.0f,   60, 2, -2,  0.0f,  0,   900, 0.00f, 300, 0.45f, 0.00f, 0.0f, 0, 0.6f },
    { "BASS",  "Reese Bass",       5, 0.85f,0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   450, 1.0f,  400, 0, -1, 28.0f, 10,   300, 0.90f, 120, 0.25f, 0.10f, 0.0f, 0, 1.0f },
    { "BASS",  "Wobble Growl",     3, 0.6f, 0.4f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   300, 2.5f,  500, 1, -1, 18.0f,  5,   350, 0.85f, 120, 0.40f, 0.05f, 0.0f, 0, 1.0f },
    { "BASS",  "Pluck Bass",       1, 0.0f, 0.4f, 0.05f,0.0f, 2.0f, 0.0f, 0.0f,   250, 3.5f,   90, 0, -1,  5.0f,  0,   160, 0.20f,  80, 0.20f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Analog Warm",      1, 0.0f, 0.5f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   700, 1.5f,  250, 3, -1,  4.0f,  2,   250, 0.70f, 120, 0.10f, 0.10f, 0.0f, 0, 0.9f },
    { "BASS",  "FM Knock",         1, 0.0f, 0.3f, 0.0f, 0.6f, 2.0f, 0.0f, 0.0f,   500, 2.0f,   80, 2, -1,  0.0f,  0,   200, 0.00f, 100, 0.20f, 0.05f, 0.0f, 0, 0.8f },
    { "BASS",  "Moog Bass",        1, 0.0f, 0.6f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   480, 2.2f,  180, 0, -1,  4.0f,  1,   180, 0.70f,  90, 0.12f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Future Bass",      5, 0.7f, 0.3f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  1200, 1.6f,  220, 0, -1, 22.0f,  5,   300, 0.50f, 150, 0.15f, 0.20f, 0.10f, 0, 1.3f },
    { "BASS",  "Deep House",       1, 0.0f, 0.8f, 0.02f,0.0f, 2.0f, 0.0f, 0.0f,   350, 1.8f,  120, 1, -1,  0.0f,  2,   240, 0.35f, 110, 0.08f, 0.05f, 0.0f, 0, 0.85f },
    { "BASS",  "Neuro Bass",       1, 0.0f, 0.4f, 0.00f, 0.70f, 3.5f, 0.0f, 0.0f,   320, 3.2f,  260, 2, -1,  0.0f,  2,   300, 0.60f, 120, 0.45f, 0.05f, 0.0f, 0, 0.8f },
    { "BASS",  "Garage Sub",       1, 0.0f, 1.0f, 0.00f, 0.25f, 1.0f, 0.0f, 0.0f,   240, 1.6f,  140, 2, -2,  0.0f,  1,   350, 0.55f, 140, 0.20f, 0.03f, 0.0f, 0, 0.6f },
    { "BASS",  "Slap Funk",        1, 0.0f, 0.5f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,   500, 3.8f,   90, 1, -1,  3.0f,  1,   220, 0.15f,  80, 0.30f, 0.06f, 0.0f, 0, 0.7f },
    { "BASS",  "Dark Reese",       4, 0.6f, 0.3f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   650, 0.8f,  600, 0, -1, 28.0f,  3,   400, 0.80f, 150, 0.35f, 0.08f, 0.0f, 0, 0.9f },
    { "BASS",  "Rubber Bass",      1, 0.0f, 0.5f, 0.00f, 0.50f, 2.0f, 0.0f, 0.0f,   420, 3.0f,  130, 2, -1,  0.0f,  1,   260, 0.20f,  90, 0.25f, 0.05f, 0.0f, 0, 0.7f },
    { "BASS",  "Acid Bass",        1, 0.0f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   220, 3.6f,  160, 0, -1,  0.0f,  1,   240, 0.30f,  70, 0.40f, 0.04f, 0.0f, 0, 0.6f },
    { "BASS",  "Gnarl Bass",    2, 0.3f, 0.4f, 0.00f, 0.65f, 2.0f, 4.5f,  8.0f,  300, 2.5f,  420, 3, -1, 10.0f,  1,  380, 0.60f, 140, 0.50f, 0.05f, 0.0f, 0, 1.0f },
    { "BASS",  "Memphis 808",   1, 0.0f, 0.8f, 0.00f, 0.50f, 1.0f, 0.0f,  0.0f,  700, 1.5f,  300, 2, -2,  0.0f,  0,  900, 0.30f, 300, 0.60f, 0.04f, 0.0f, 0, 0.8f },
    { "BASS",  "Trap Knock",    1, 0.0f, 1.0f, 0.00f, 0.30f, 3.0f, 0.0f,  0.0f,  200, 2.0f,   80, 2, -2,  0.0f,  0, 1200, 0.40f, 250, 0.25f, 0.03f, 0.0f, 0, 0.7f },
    { "BASS",  "Outrun Bass",   2, 0.2f, 0.5f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f,  900, 2.0f,  160, 0, -1,  6.0f,  1,  260, 0.20f, 120, 0.22f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Octave Disco",  1, 0.0f, 0.4f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 1200, 1.8f,  120, 1, -1,  0.0f,  1,  200, 0.15f, 100, 0.18f, 0.04f, 0.0f, 0, 0.8f },
    { "BASS",  "Psy Stomp",     1, 0.0f, 0.3f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f,  500, 1.8f,   90, 0, -1,  0.0f,  0,  140, 0.00f,  70, 0.30f, 0.03f, 0.0f, 0, 0.7f },
    { "BASS",  "Dusty Bass",    1, 0.0f, 0.5f, 0.04f, 0.00f, 2.0f, 0.0f,  0.0f,  600, 1.0f,  350, 3, -1,  0.0f,  8,  500, 0.60f, 220, 0.15f, 0.10f, 0.0f, 0, 0.8f },
    { "BASS",  "Lately Bass",   1, 0.0f, 0.4f, 0.00f, 0.70f, 1.0f, 0.0f,  0.0f,  800, 2.5f,  120, 2, -1,  0.0f,  0,  350, 0.35f, 150, 0.20f, 0.06f, 0.0f, 0, 0.8f },
    { "BASS",  "Talk Bass",     1, 0.0f, 0.3f, 0.00f, 0.50f, 3.0f, 5.0f, 12.0f,  900, 1.2f,  250, 3, -1,  0.0f,  2,  300, 0.65f, 130, 0.30f, 0.05f, 0.0f, 0, 0.9f },
    { "BASS",  "Pedal Organ",   1, 0.0f, 0.9f, 0.00f, 0.20f, 2.0f, 0.0f,  0.0f, 1400, 0.5f,  100, 2, -1,  0.0f,  2,  120, 1.00f,  90, 0.15f, 0.08f, 0.0f, 0, 0.9f },
    { "BASS",  "Rust Bass",     3, 0.3f, 0.3f, 0.20f, 0.00f, 2.0f, 0.0f,  0.0f,  700, 1.5f,  300, 0, -1, 15.0f,  1,  400, 0.55f, 180, 0.70f, 0.06f, 0.0f, 0, 1.0f },
    { "BASS",  "Liquid Sub",    1, 0.0f, 0.6f, 0.00f, 0.10f, 2.0f, 0.0f,  0.0f,  300, 0.8f,  400, 2, -2,  0.0f,  5,  350, 0.85f, 250, 0.08f, 0.06f, 0.0f, 0, 0.7f },
    { "BASS",  "Syn Jazz Bass",  1, 0.0f, 0.5f, 0.05f, 0.00f, 2.0f, 0.0f,  0.0f,  350, 1.5f,  180, 3, -1,  0.0f,  3,  700, 0.25f, 150, 0.10f, 0.12f, 0.0f, 0, 0.8f },
    { "BASS",  "Rage 808",      1, 0.0f, 1.0f, 0.05f, 0.30f, 1.0f, 0.0f, 0.0f,   500, 2.5f,  150, 2, -2,  0.0f,  0, 1400, 0.30f, 250, 0.55f, 0.05f, 0.0f, 0, 0.8f },
    { "BASS",  "Growl 808",     1, 0.0f, 0.7f, 0.00f, 0.60f, 2.5f, 0.0f, 0.0f,   400, 3.0f,  300, 2, -2,  0.0f,  0, 1000, 0.40f, 220, 0.60f, 0.05f, 0.0f, 0, 0.85f },
    { "BASS",  "Bounce Bass",   1, 0.0f, 0.6f, 0.03f, 0.00f, 2.0f, 0.0f, 0.0f,   700, 2.8f,  110, 1, -1,  4.0f,  0,  300, 0.20f, 120, 0.35f, 0.08f, 0.0f, 0, 0.9f },
    { "BASS",  "Seoul Bass",    1, 0.0f, 0.75f,0.00f, 0.15f, 1.0f, 0.0f, 0.0f,   450, 1.5f,  250, 2, -1,  0.0f,  2,  450, 0.50f, 200, 0.15f, 0.10f, 0.0f, 0, 0.85f },

    { "LEAD",  "Supersaw Lead",    7, 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  9000, 0.0f,  200, 0,  0, 22.0f,  2,   150, 0.85f, 200, 0.00f, 0.30f, 0.20f, 0, 1.6f },
    { "LEAD",  "Retro Lead",       1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 5.5f, 14.0f, 7000, 0.0f,  200, 1,  0,  6.0f,  3,   100, 0.70f, 150, 0.00f, 0.15f, 0.25f, 0, 1.0f },
    { "LEAD",  "Acid Lead",        1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   700, 3.0f,  200, 0,  0,  3.0f,  0,   180, 0.55f,  90, 0.35f, 0.10f, 0.15f, 0, 1.0f },
    { "LEAD",  "Chip Lead",        1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 6.0f,  8.0f, 20000, 0.0f, 200, 1,  0,  0.0f,  0,    80, 0.60f,  60, 0.00f, 0.10f, 0.15f, 0, 1.0f },
    { "LEAD",  "Scream Lead",      5, 0.6f, 0.0f, 0.0f, 0.0f, 2.0f, 5.0f, 10.0f, 4000, 1.0f,  800, 0,  0, 20.0f,  5,   200, 0.80f, 200, 0.50f, 0.25f, 0.20f, 0, 1.3f },
    { "LEAD",  "PWM Lead",         3, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 4.8f,  6.0f, 5200, 0.8f,  300, 1,  0, 12.0f,  8,   200, 0.75f, 180, 0.10f, 0.20f, 0.18f, 0, 1.2f },
    { "LEAD",  "Velvet Lead",      1, 0.0f, 0.15f,0.0f, 0.0f, 2.0f, 4.2f,  7.0f, 2600, 0.6f,  400, 3,  0,  4.0f, 25,   300, 0.85f, 260, 0.00f, 0.30f, 0.15f, 0, 1.05f },
    { "LEAD",  "Hard Lead",        5, 0.7f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2600, 1.5f,  300, 1,  0, 22.0f,  2,   250, 0.80f, 180, 0.60f, 0.20f, 0.15f, 0, 1.2f },
    { "LEAD",  "Italo Lead",       2, 0.4f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 10.0f, 3200, 0.8f,  250, 1,  0,  9.0f,  4,   200, 0.75f, 220, 0.15f, 0.25f, 0.35f, 1, 1.2f },
    { "LEAD",  "Flute Lead",       1, 0.0f, 0.0f, 0.15f, 0.00f, 2.0f, 5.0f, 14.0f, 4000, 0.5f,  200, 2,  0,  0.0f, 40,   300, 0.80f, 260, 0.05f, 0.30f, 0.20f, 0, 0.9f },
    { "LEAD",  "Rude Lead",        2, 0.2f, 0.1f, 0.00f, 0.00f, 2.0f, 4.5f,  8.0f, 1800, 2.0f,  400, 0,  0,  6.0f,  2,   300, 0.70f, 160, 0.70f, 0.15f, 0.10f, 0, 0.8f },
    { "LEAD",  "Dream Lead",       6, 0.9f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 0.6f,  500, 0,  0, 16.0f, 25,   350, 0.70f, 500, 0.10f, 0.40f, 0.35f, 1, 1.6f },
    { "LEAD",  "Wire Lead",        1, 0.0f, 0.0f, 0.00f, 0.55f, 7.0f, 4.8f,  6.0f, 5000, 1.0f,  350, 2,  0,  0.0f,  3,   300, 0.65f, 240, 0.20f, 0.25f, 0.30f, 1, 1.1f },
    { "LEAD",  "Midnight Lead", 3, 0.5f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 10.0f, 2800, 1.0f,  250, 0,  0, 12.0f,  5,  300, 0.75f, 250, 0.15f, 0.25f, 0.35f, 1, 1.3f },
    { "LEAD",  "Idol Pluck",    4, 0.6f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 1200, 2.5f,  200, 0,  0, 10.0f,  1,  260, 0.00f, 200, 0.10f, 0.25f, 0.30f, 1, 1.4f },
    { "LEAD",  "Funk Worm",     1, 0.0f, 0.2f, 0.00f, 0.00f, 2.0f, 5.5f, 15.0f,  600, 2.8f,  180, 1,  0,  0.0f,  2,  220, 0.70f, 120, 0.25f, 0.08f, 0.10f, 0, 1.0f },
    { "LEAD",  "Grime Hoover",  6, 0.7f, 0.2f, 0.05f, 0.00f, 2.0f, 0.0f,  0.0f, 1500, 1.5f,  300, 0, -1, 30.0f,  3,  400, 0.80f, 200, 0.50f, 0.15f, 0.15f, 0, 1.5f },
    { "LEAD",  "Haze Lead",     1, 0.0f, 0.0f, 0.00f, 0.15f, 2.0f, 4.5f,  6.0f, 1800, 0.5f,  600, 2,  0,  0.0f, 200,  500, 0.80f, 800, 0.05f, 0.60f, 0.30f, 1, 1.4f },
    { "LEAD",  "Glide Solo",    1, 0.0f, 0.1f, 0.00f, 0.00f, 2.0f, 5.8f, 14.0f, 2200, 1.2f,  300, 0,  0,  0.0f, 15,  350, 0.85f, 180, 0.20f, 0.20f, 0.25f, 0, 1.0f },
    { "LEAD",  "Solo Brass",    2, 0.2f, 0.0f, 0.00f, 0.00f, 2.0f, 5.0f,  8.0f,  900, 2.0f,  350, 0,  0,  8.0f, 40,  400, 0.80f, 300, 0.30f, 0.25f, 0.10f, 0, 1.1f },
    { "LEAD",  "NES Round",     1, 0.0f, 0.0f, 0.00f, 0.00f, 2.0f, 6.0f, 12.0f, 4000, 0.0f,  100, 3,  0,  0.0f,  1,  150, 0.90f, 100, 0.05f, 0.10f, 0.15f, 0, 0.8f },
    { "LEAD",  "Goa Lead",      2, 0.3f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 1800, 2.0f,  220, 0,  0, 10.0f,  1,  240, 0.40f, 160, 0.30f, 0.20f, 0.40f, 1, 1.2f },
    { "LEAD",  "Mainstage",     7, 0.8f, 0.1f, 0.00f, 0.00f, 2.0f, 0.0f,  0.0f, 6000, 0.5f,  200, 0,  0, 35.0f,  4,  300, 0.85f, 350, 0.20f, 0.30f, 0.30f, 1, 1.7f },
    { "LEAD",  "Silk Lead",     1, 0.0f, 0.0f, 0.00f, 0.25f, 2.0f, 5.5f, 12.0f, 2000, 0.8f,  400, 2,  0,  0.0f, 10,  350, 0.80f, 400, 0.08f, 0.35f, 0.30f, 1, 1.2f },
    { "LEAD",  "Epic Horn",     3, 0.3f, 0.1f, 0.00f, 0.00f, 2.0f, 4.5f,  6.0f,  700, 1.8f,  600, 0, -1,  6.0f, 60,  700, 0.90f, 500, 0.20f, 0.45f, 0.15f, 0, 1.3f },
    { "LEAD",  "Vowel Lead",    1, 0.0f, 0.0f, 0.00f, 0.55f, 5.0f, 5.2f, 14.0f, 1100, 1.2f,  400, 3,  0,  0.0f, 20,  400, 0.75f, 300, 0.15f, 0.30f, 0.25f, 1, 1.2f },
    { "LEAD",  "Rage Bell",     5, 0.8f, 0.0f, 0.05f, 0.35f, 3.5f, 0.0f, 0.0f,  3500, 1.0f,  400, 1,  0, 30.0f,  1,  600, 0.30f, 400, 0.45f, 0.35f, 0.25f, 1, 1.5f },
    { "LEAD",  "Rage Lead",     7, 0.9f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2800, 1.5f,  300, 0,  0, 38.0f,  3,  350, 0.55f, 250, 0.55f, 0.25f, 0.20f, 0, 1.5f },

    { "SYNTH", "Analog Poly",      3, 0.6f, 0.2f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  1800, 1.2f,  300, 0,  0, 12.0f,  8,   300, 0.65f, 300, 0.05f, 0.25f, 0.10f, 0, 1.2f },
    { "SYNTH", "PWM Strings",      5, 0.8f, 0.0f, 0.0f, 0.0f, 2.0f, 4.0f,  4.0f, 3000, 0.0f,  200, 1,  0, 18.0f, 120,  400, 0.80f, 600, 0.00f, 0.35f, 0.00f, 0, 1.4f },
    { "SYNTH", "Hoover",           5, 0.9f, 0.3f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2500, 1.0f,  500, 0, -1, 35.0f, 15,   250, 0.75f, 350, 0.15f, 0.25f, 0.10f, 0, 1.4f },
    { "SYNTH", "80s Poly",         3, 0.7f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2200, 1.5f,  250, 1,  0, 14.0f, 10,   350, 0.60f, 400, 0.05f, 0.30f, 0.12f, 0, 1.3f },
    { "SYNTH", "FM Digital",       1, 0.0f, 0.0f, 0.0f, 0.45f,3.0f, 0.0f, 0.0f,  8000, 0.0f,  200, 2,  0,  4.0f,  3,   500, 0.35f, 350, 0.00f, 0.30f, 0.10f, 0, 1.1f },
    { "SYNTH", "Trance Saw",       7, 0.9f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  3800, 1.2f,  350, 0,  0, 18.0f,  6,   400, 0.55f, 240, 0.10f, 0.25f, 0.30f, 1, 1.5f },
    { "SYNTH", "Rave Stab",        3, 0.6f, 0.2f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2400, 2.4f,  140, 0,  0, 14.0f,  0,   220, 0.15f, 120, 0.18f, 0.20f, 0.10f, 0, 1.25f },
    { "SYNTH", "Techno Stab",      3, 0.5f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   480, 3.0f,  150, 0, -1, 14.0f,  1,   280, 0.05f, 200, 0.35f, 0.30f, 0.20f, 1, 1.1f },
    { "SYNTH", "French Chord",     5, 0.8f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1100, 1.8f,  180, 0,  0, 15.0f,  1,   320, 0.15f, 160, 0.20f, 0.15f, 0.10f, 0, 1.5f },
    { "SYNTH", "Ambient Drone",    5, 0.7f, 0.4f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,   850, 0.0f, 3000, 0, -1, 10.0f, 450,  800, 0.90f, 1800, 0.10f, 0.65f, 0.25f, 1, 1.7f },
    { "SYNTH", "Glass Keys",       1, 0.0f, 0.0f, 0.00f, 0.50f, 4.0f, 0.0f, 0.0f,  6000, 0.8f,  400, 2,  0,  0.0f,  1,   700, 0.25f, 500, 0.05f, 0.35f, 0.25f, 1, 1.3f },
    { "SYNTH", "Ice Pluck",        2, 0.4f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 4.0f,  110, 3,  0,  7.0f,  1,   240, 0.00f, 300, 0.05f, 0.35f, 0.45f, 1, 1.4f },
    { "SYNTH", "Soft Organ",       1, 0.0f, 0.7f, 0.00f, 0.00f, 2.0f, 6.0f,  5.0f, 3500, 0.0f,  200, 2,  0,  0.0f,  5,   100, 0.95f, 120, 0.10f, 0.25f, 0.00f, 0, 1.0f },
    { "SYNTH", "Cold Strings",     6, 0.8f, 0.0f, 0.04f, 0.00f, 2.0f, 0.0f, 0.0f,  1600, 0.4f,  900, 0,  0, 13.0f, 220,  600, 0.80f, 900, 0.10f, 0.45f, 0.15f, 0, 1.6f },
    { "SYNTH", "Lo-Fi Keys",       2, 0.3f, 0.0f, 0.07f, 0.00f, 2.0f, 0.8f,  6.0f, 1400, 1.2f,  350, 3,  0,  5.0f,  2,   850, 0.35f, 400, 0.15f, 0.30f, 0.20f, 0, 1.1f },
    { "SYNTH", "Modular Blip",   1, 0.0f, 0.0f, 0.00f, 0.30f, 3.0f, 0.0f, 0.0f,   900, 3.0f,   90, 2,  0,  0,  1.0f,  140, 0.00f,  120, 0.05f, 0.10f, 0.45f, 1, 1.0f },
    { "SYNTH", "Squelch 303",    1, 0.0f, 0.1f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   420, 3.5f,  300, 1, -1,  0,  0.5f,  260, 0.35f,  120, 0.55f, 0.08f, 0.15f, 0, 0.8f },
    { "SYNTH", "Bigroom Stab",   7, 0.8f, 0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  5000, 1.5f,  200, 0,  0, 30,  1.0f,  300, 0.10f,  200, 0.25f, 0.30f, 0.10f, 0, 1.4f },
    { "SYNTH", "2-Step Chord",   3, 0.6f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 1.2f,  180, 1,  0, 10,  1.0f,  220, 0.15f,  180, 0.10f, 0.20f, 0.25f, 1, 1.2f },
    { "SYNTH", "8-Bit Poly",     1, 0.0f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 8.0f,  8000, 0.0f,  200, 1,  0,  0,  1.0f,  400, 0.60f,  100, 0.10f, 0.15f, 0.20f, 1, 0.9f },
    { "SYNTH", "Mall Keys",      2, 0.5f, 0.1f, 0.00f, 0.45f, 2.0f, 0.8f, 10.0f, 3200, 0.8f,  500, 2,  0,  8,  2.0f,  700, 0.50f,  500, 0.05f, 0.35f, 0.30f, 1, 1.5f },
    { "SYNTH", "CS-80 Brass",    4, 0.6f, 0.1f, 0.00f, 0.00f, 2.0f, 5.0f, 6.0f,  1400, 1.0f,  800, 0,  0, 14, 90.0f,  600, 0.80f,  400, 0.30f, 0.35f, 0.10f, 0, 1.3f },
    { "SYNTH", "Gabber Stab",    1, 0.0f, 0.4f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 2.5f,  150, 1, -1,  0,  0.5f,  250, 0.10f,  150, 0.85f, 0.15f, 0.05f, 0, 0.8f },
    { "SYNTH", "Tech Blip",      1, 0.0f, 0.0f, 0.00f, 0.20f, 1.0f, 0.0f, 0.0f,  2500, 1.5f,   60, 2,  0,  0,  0.5f,   90, 0.00f,   80, 0.05f, 0.12f, 0.35f, 1, 1.0f },
    { "SYNTH", "Gate Dream",     7, 0.9f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  4500, 0.8f,  400, 0,  0, 22,  2.0f,  300, 0.65f,  300, 0.10f, 0.30f, 0.45f, 1, 1.5f },
    { "SYNTH", "Italo Arp",      2, 0.4f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 1.8f,  220, 0,  0,  8,  1.0f,  260, 0.20f,  180, 0.10f, 0.20f, 0.40f, 1, 1.1f },
    { "SYNTH", "Night Drive",    3, 0.5f, 0.3f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1000, 0.6f,  600, 0, -1, 10,  8.0f,  500, 0.75f,  350, 0.20f, 0.30f, 0.20f, 0, 1.2f },
    { "SYNTH", "Dembow Pluck",   3, 0.5f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1800, 2.0f,  160, 0,  0, 12,  1.0f,  200, 0.10f,  160, 0.12f, 0.18f, 0.22f, 1, 1.2f },
    { "SYNTH", "Hyper Saw",      7, 1.0f, 0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  9000, 0.5f,  300, 0,  0, 40,  1.0f,  400, 0.70f,  250, 0.35f, 0.25f, 0.15f, 0, 1.6f },
    { "SYNTH", "Dark Rage",      5, 0.7f, 0.3f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  1200, 1.8f,  500, 0, -1, 26.0f,  5,  500, 0.45f, 350, 0.50f, 0.30f, 0.15f, 0, 1.4f },
    { "SYNTH", "Lov3 Chords",    3, 0.5f, 0.1f, 0.00f, 0.20f, 1.0f, 0.0f, 0.0f,  2200, 0.6f,  400, 3,  0,  8.0f, 10,  700, 0.45f, 400, 0.05f, 0.35f, 0.15f, 0, 1.35f },

    { "PIANO", "Syn Grand",      1, 0.0f, 0.05f,0.02f,0.0f, 2.0f, 0.0f, 0.0f,  3200, 1.2f,  700, 3,  0,  3.0f,  1,   900, 0.22f, 260, 0.00f, 0.20f, 0.00f, 0, 1.0f },
    { "PIANO", "Syn Bright",     1, 0.0f, 0.0f, 0.03f,0.12f,1.0f, 0.0f, 0.0f,  5200, 1.0f,  500, 3,  0,  4.0f,  1,   750, 0.28f, 220, 0.05f, 0.18f, 0.00f, 0, 1.05f },
    { "PIANO", "Syn Soft Key",       1, 0.0f, 0.08f,0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2000, 0.8f,  900, 2,  0,  2.0f,  2,  1100, 0.18f, 350, 0.00f, 0.30f, 0.00f, 0, 1.0f },
    { "PIANO", "House Keys",      3, 0.4f, 0.0f, 0.0f, 0.10f,2.0f, 0.0f, 0.0f,  4500, 1.0f,  300, 0,  0,  8.0f,  1,   500, 0.40f, 180, 0.08f, 0.20f, 0.10f, 0, 1.15f },
    { "PIANO", "Syn Upright",    2, 0.2f, 0.10f,0.05f,0.12f,3.0f, 0.0f, 0.0f,  2200, 1.6f,  350, 0,  0,  4.0f,  2,  1000, 0.22f, 380, 0.10f, 0.25f, 0.00f, 0, 1.1f },
    { "PIANO", "Syn Felt",       2, 0.2f, 0.20f,0.08f,0.05f,2.0f, 0.0f, 0.0f,  1200, 0.9f,  500, 3,  0,  3.0f,  4,  1100, 0.25f, 600, 0.00f, 0.40f, 0.08f, 0, 1.15f },
    { "PIANO", "Syn Honky",     3, 0.3f, 0.08f,0.06f,0.15f,3.0f, 0.0f, 0.0f,  2600, 1.7f,  320, 0,  0, 14.0f,  3,   950, 0.20f,  350, 0.15f, 0.22f, 0.00f, 0, 1.15f },
    { "PIANO", "Noir Keys",     2, 0.2f, 0.18f,0.04f,0.10f,2.0f, 0.0f, 0.0f,   900, 1.2f,  500, 0,  0,  4.0f,  4,  1300, 0.15f,  900, 0.08f, 0.50f, 0.12f, 0, 1.2f },
    { "PIANO", "Syn Pop Key",      3, 0.25f,0.12f,0.05f,0.18f,3.0f, 0.0f, 0.0f,  3200, 1.8f,  300, 0,  0,  8.0f,  2,   900, 0.25f,  320, 0.12f, 0.28f, 0.08f, 0, 1.25f },
    { "PIANO", "Ballad Keys",   2, 0.2f, 0.12f,0.03f,0.08f,2.0f, 0.0f, 0.0f,  1800, 1.4f,  420, 0,  0,  3.0f,  4,  1200, 0.30f,  700, 0.05f, 0.40f, 0.10f, 0, 1.1f },
    { "PIANO", "Syn Tack",     2, 0.2f, 0.05f,0.12f,0.25f,5.0f, 0.0f, 0.0f,  4200, 2.0f,  180, 0,  0, 10.0f,  1,   700, 0.12f,  250, 0.20f, 0.18f, 0.00f, 0, 1.0f },
    { "PIANO", "Ghost Keys",    2, 0.35f,0.15f,0.06f,0.10f,2.0f, 0.8f, 6.0f,  1400, 1.3f,  600, 0,  0,  6.0f,  5,  1400, 0.30f, 1600, 0.05f, 0.75f, 0.35f, 1, 1.5f },

    { "GUITAR","Syn Nylon",     1, 0.0f, 0.0f, 0.04f,0.0f, 2.0f, 0.0f, 0.0f,  1200, 2.0f,  140, 3,  0,  3.0f,  0,   380, 0.10f, 200, 0.00f, 0.25f, 0.08f, 0, 1.0f },
    { "GUITAR","Syn Steel",     1, 0.0f, 0.0f, 0.05f,0.15f,2.0f, 0.0f, 0.0f,  2600, 1.8f,  160, 0,  0,  5.0f,  0,   420, 0.12f, 220, 0.05f, 0.22f, 0.06f, 0, 1.05f },
    { "GUITAR","Syn Clean Gtr",     1, 0.0f, 0.0f, 0.0f, 0.08f,1.0f, 0.0f, 0.0f,  2400, 1.2f,  250, 3,  0,  2.0f,  1,   550, 0.30f, 250, 0.06f, 0.18f, 0.10f, 0, 1.1f },
    { "GUITAR","Syn Mute Gtr",     1, 0.0f, 0.10f,0.02f,0.0f, 2.0f, 0.0f, 0.0f,   900, 1.5f,   60, 3,  0,  0.0f,  0,   140, 0.05f,  90, 0.10f, 0.08f, 0.00f, 0, 0.95f },
    { "GUITAR","Funk Gtr Syn",      1, 0.0f, 0.0f, 0.02f,0.0f, 2.0f, 0.0f, 0.0f,  1600, 2.2f,   80, 1,  0,  2.0f,  0,   160, 0.08f, 100, 0.12f, 0.10f, 0.05f, 0, 1.0f },
    { "GUITAR","Syn 12-String",        4, 0.5f, 0.0f, 0.07f,0.10f,2.0f, 0.0f, 0.0f,  2600, 1.8f,  180, 3,  0,  9.0f,  1,   850, 0.12f, 400, 0.08f, 0.30f, 0.10f, 0, 1.35f },
    { "GUITAR","Syn Jazz Gtr",         1, 0.0f, 0.25f,0.03f,0.0f, 2.0f, 0.0f, 0.0f,  1100, 1.3f,  150, 3,  0,  0.0f,  1,   700, 0.15f, 300, 0.12f, 0.18f, 0.00f, 0, 1.0f },
    { "GUITAR","Syn Spanish",  2, 0.3f, 0.10f,0.08f,0.30f,3.0f, 0.0f, 0.0f,  2400, 1.9f,  120, 3,  0,  6.0f,  0,   500, 0.10f,  260, 0.10f, 0.30f, 0.00f, 0, 1.1f },
    { "GUITAR","Chorus Gtr",   2, 0.4f, 0.08f,0.02f,0.35f,2.0f, 0.9f, 8.0f,  3000, 1.5f,  200, 3,  0, 10.0f,  0,   800, 0.25f,  400, 0.05f, 0.30f, 0.15f, 1, 1.4f },
    { "GUITAR","Crunch Syn",  2, 0.25f,0.10f,0.04f,0.10f,2.0f, 0.0f, 0.0f,  1600, 1.6f,  140, 0,  0,  8.0f,  0,   450, 0.20f,  200, 0.60f, 0.15f, 0.00f, 0, 1.0f },
    { "GUITAR","Drive Lead Gtr", 1, 0.0f, 0.15f,0.03f,0.12f,2.0f, 5.5f,12.0f,  1300, 1.4f,  220, 0,  0,  0.0f,  0,   600, 0.30f,  300, 0.70f, 0.25f, 0.20f, 1, 0.9f },
    { "GUITAR","Syn Slide",   1, 0.0f, 0.12f,0.02f,0.40f,2.0f, 4.5f,18.0f,  2000, 1.5f,  250, 3,  0,  0.0f,  5,   900, 0.30f,  500, 0.20f, 0.35f, 0.10f, 0, 1.0f },
    { "GUITAR","Chime Syn", 2, 0.3f, 0.05f,0.02f,0.50f,3.5f, 0.0f, 0.0f,  5000, 1.8f,   90, 3,  1,  5.0f,  0,   700, 0.05f,  500, 0.05f, 0.40f, 0.20f, 1, 1.3f },
    { "GUITAR","Syn Bass Gtr",    1, 0.0f, 0.40f,0.03f,0.30f,2.0f, 0.0f, 0.0f,   700, 1.7f,  150, 3, -1,  0.0f,  0,   500, 0.25f,  180, 0.25f, 0.05f, 0.00f, 0, 0.7f },

    { "PAD",   "Dream Pad",        5, 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2600, 0.0f,  200, 0,  0, 18.0f, 450,  800, 0.80f, 1000, 0.00f, 0.60f, 0.00f, 0, 1.6f },
    { "PAD",   "Warm Strings",     5, 0.7f, 0.0f, 0.0f, 0.0f, 2.0f, 4.5f,  6.0f, 3400, 0.0f,  200, 0,  0, 12.0f, 220,  500, 0.85f, 500, 0.00f, 0.45f, 0.00f, 0, 1.3f },
    { "PAD",   "Dark Pad",         5, 0.8f, 0.3f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   900, 0.0f,  200, 0, -1, 15.0f, 600, 1000, 0.85f, 1200, 0.00f, 0.70f, 0.00f, 0, 1.4f },
    { "PAD",   "Glass Pad",        3, 0.8f, 0.0f, 0.0f, 0.35f,2.0f, 0.0f, 0.0f,  5000, 0.0f,  200, 2,  0, 10.0f, 400,  800, 0.75f, 900, 0.00f, 0.65f, 0.10f, 0, 1.5f },
    { "PAD",   "Choir Air",        5, 0.9f, 0.0f, 0.06f,0.0f, 2.0f, 4.0f,  5.0f, 3000, 0.0f,  200, 2,  0, 14.0f, 350,  700, 0.85f, 800, 0.00f, 0.70f, 0.00f, 0, 1.5f },
    { "PAD",   "Analog Sweep",     5, 0.8f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   400, 2.5f, 3000, 0,  0, 16.0f, 500,  900, 0.85f, 1200, 0.10f, 0.55f, 0.00f, 0, 1.5f },
    { "PAD",   "Ocean Pad",        3, 0.9f, 0.2f, 0.10f,0.0f, 2.0f, 0.0f, 0.0f,  1800, 0.0f,  200, 2,  0, 10.0f, 800,  900, 0.85f, 1400, 0.00f, 0.70f, 0.15f, 0, 1.6f },
    { "PAD",   "Cinema Strings",   7, 0.8f, 0.0f, 0.0f, 0.0f, 2.0f, 4.6f,  5.0f, 3800, 0.0f,  200, 0,  0,  9.0f, 350,  600, 0.90f, 800, 0.00f, 0.50f, 0.00f, 0, 1.45f },
    { "PAD",   "Vapor Pad",        5, 0.85f,0.0f, 0.04f,0.0f, 2.0f, 0.0f, 0.0f,  1500, 0.5f, 1500, 1,  0, 16.0f, 600,  800, 0.80f, 1200, 0.05f, 0.60f, 0.35f, 1, 1.55f },
    { "PAD",   "Shimmer Pad",      5, 0.9f, 0.10f,0.05f,0.25f,3.0f, 0.0f, 0.0f,  3200, 1.0f, 2500, 0,  0, 14.0f, 350,  900, 0.85f, 1800, 0.00f, 0.70f, 0.30f, 1, 1.7f },
    { "PAD",   "Tape Strings",     4, 0.7f, 0.15f,0.12f,0.0f, 2.0f, 0.5f,  8.0f, 2400, 0.5f, 1200, 0,  0, 10.0f, 280,  800, 0.80f, 1200, 0.15f, 0.50f, 0.10f, 0, 1.4f },
    { "PAD",   "Cathedral",        6, 0.8f, 0.20f,0.06f,0.08f,2.0f, 4.5f,  6.0f, 1400, 0.8f, 2000, 3,  0,  8.0f, 450, 1000, 0.90f, 2000, 0.00f, 0.70f, 0.05f, 0, 1.6f },
    { "PAD",   "Velvet Pad",       3, 0.6f, 0.40f,0.0f, 0.0f, 2.0f, 0.0f, 0.0f,   900, 0.4f, 1500, 3, -1,  7.0f, 320,  900, 0.85f, 1500, 0.10f, 0.50f, 0.00f, 0, 1.3f },
    { "PAD",   "Aurora Pad",       5, 0.85f,0.10f,0.04f,0.15f,2.0f, 0.3f,  5.0f, 1600, 1.5f, 2800, 1,  0, 12.0f, 400, 1100, 0.80f, 1700, 0.00f, 0.60f, 0.35f, 1, 1.7f },
    { "PAD",   "Solar Winds",      2, 0.5f, 0.15f,0.35f,0.0f, 2.0f, 0.0f, 0.0f,  5000, 0.0f,  200, 2,  0,  6.0f, 500, 1000, 0.75f, 2000, 0.00f, 0.65f, 0.20f, 1, 1.8f },
    { "PAD",   "Ensemble Str",   5, 0.8f, 0.05f,0.05f, 0.00f, 2.0f, 5.5f, 5.0f,  2600, 0.0f,  200, 0,  0, 16, 350.0f, 600, 0.85f,  900, 0.00f, 0.50f, 0.10f, 0, 1.5f },
    { "PAD",   "Soft Brass",     4, 0.6f, 0.1f, 0.00f, 0.00f, 2.0f, 4.5f, 4.0f,  1500, 0.8f,  900, 0,  0, 12, 250.0f, 700, 0.80f,  700, 0.15f, 0.45f, 0.10f, 0, 1.3f },
    { "PAD",   "Boys Choir",     3, 0.6f, 0.0f, 0.08f, 0.25f, 5.0f, 4.5f, 7.0f,  2000, 0.0f,  200, 3,  0, 10, 400.0f, 700, 0.85f, 1200, 0.00f, 0.60f, 0.10f, 0, 1.4f },
    { "PAD",   "Grain Cloud",    6, 1.0f, 0.0f, 0.25f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 0.0f,  200, 3,  0, 28, 700.0f, 900, 0.80f, 1600, 0.00f, 0.65f, 0.30f, 1, 1.7f },
    { "PAD",   "Frost Pad",      4, 0.8f, 0.0f, 0.12f, 0.00f, 2.0f, 0.0f, 0.0f,  1300, 0.0f,  200, 3,  0, 18, 500.0f, 800, 0.80f, 1400, 0.00f, 0.60f, 0.25f, 1, 1.6f },
    { "PAD",   "Juno Warmth",    3, 0.7f, 0.25f,0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1700, 0.3f,  800, 0,  0, 12, 300.0f, 600, 0.85f,  900, 0.05f, 0.45f, 0.10f, 0, 1.4f },
    { "PAD",   "Hollow Glass",   2, 0.6f, 0.0f, 0.00f, 0.50f, 3.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2,  0,  8, 400.0f, 800, 0.75f, 1300, 0.00f, 0.55f, 0.30f, 1, 1.5f },
    { "PAD",   "Nebula Drone",   5, 0.9f, 0.3f, 0.15f, 0.00f, 2.0f, 0.0f, 0.0f,  1100, 0.0f,  200, 3, -1, 20, 900.0f, 1000, 0.90f, 2000, 0.00f, 0.70f, 0.35f, 0, 1.7f },
    { "PAD",   "E-Bow Swell",    2, 0.5f, 0.0f, 0.00f, 0.30f, 2.0f, 0.0f, 0.0f,  2400, 0.6f, 1500, 3,  0,  6, 800.0f, 900, 0.80f, 1100, 0.10f, 0.55f, 0.30f, 1, 1.3f },
    { "PAD",   "Seraph Pad",     5, 0.8f, 0.0f, 0.05f, 0.00f, 2.0f, 5.0f, 5.0f,  3500, 0.0f,  200, 0,  0, 16, 500.0f, 800, 0.85f, 1600, 0.00f, 0.65f, 0.15f, 0, 1.6f },
    { "PAD",   "Tension Bed",    4, 0.7f, 0.1f, 0.20f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 1.2f, 2500, 0, -1, 26, 600.0f, 800, 0.85f, 1200, 0.15f, 0.55f, 0.10f, 0, 1.4f },
    { "PAD",   "Submerged",      3, 0.7f, 0.35f,0.00f, 0.30f, 0.5f, 0.6f, 12.0f,  700, 0.0f,  200, 2,  0, 10, 600.0f, 800, 0.85f, 1500, 0.00f, 0.65f, 0.30f, 0, 1.5f },
    { "PAD",   "Sunset Haze",    4, 0.7f, 0.2f, 0.00f, 0.00f, 2.0f, 0.7f, 6.0f,  1900, 0.0f,  200, 3,  0, 14, 400.0f, 700, 0.85f, 1300, 0.05f, 0.50f, 0.20f, 0, 1.5f },
    { "PAD",   "Retro Cosmos",   3, 0.6f, 0.0f, 0.00f, 0.00f, 2.0f, 5.5f, 12.0f, 2100, 0.4f, 1200, 1,  0, 12, 350.0f, 700, 0.80f, 1000, 0.10f, 0.55f, 0.35f, 1, 1.4f },
    { "PAD",   "Bass Pad",       3, 0.5f, 0.8f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,   600, 0.3f,  200, 0, -2, 10.0f, 350,  800, 0.85f, 1200, 0.10f, 0.35f, 0.00f, 0, 1.1f },
    { "PAD",   "Sub Drone",      2, 0.4f, 1.0f, 0.03f, 0.00f, 2.0f, 0.0f, 0.0f,   350, 0.0f,  200, 2, -2,  6.0f, 450,  900, 0.90f, 1500, 0.05f, 0.30f, 0.00f, 0, 1.0f },
    { "PAD",   "Lov3 Pad",       4, 0.7f, 0.2f, 0.05f, 0.15f, 1.0f, 0.0f, 0.0f,  1600, 0.4f,  900, 3,  0,  9.0f, 400,  800, 0.85f, 1300, 0.00f, 0.55f, 0.15f, 0, 1.5f },

    { "PLUCK", "Crystal Pluck",    1, 0.0f, 0.0f, 0.10f,0.0f, 2.0f, 0.0f, 0.0f,   500, 4.0f,  120, 3,  0,  7.0f,  0,   200, 0.00f, 140, 0.00f, 0.25f, 0.30f, 1, 1.2f },
    { "PLUCK", "Syn Kalimba",          1, 0.0f, 0.0f, 0.02f,0.5f, 4.2f, 0.0f, 0.0f,  3000, 2.0f,  100, 2,  0,  0.0f,  0,   250, 0.00f, 150, 0.00f, 0.30f, 0.10f, 0, 1.1f },
    { "PLUCK", "Syn Marimba",          1, 0.0f, 0.0f, 0.0f, 0.25f,3.0f, 0.0f, 0.0f,  2500, 2.0f,   90, 2,  0,  0.0f,  0,   220, 0.00f, 160, 0.00f, 0.30f, 0.05f, 0, 1.0f },
    { "PLUCK", "Trance Pluck",     1, 0.0f, 0.0f, 0.03f,0.0f, 2.0f, 0.0f, 0.0f,   700, 3.6f,  110, 0,  0,  6.0f,  0,   190, 0.00f, 150, 0.08f, 0.25f, 0.35f, 1, 1.2f },
    { "PLUCK", "Syn Harp",             1, 0.0f, 0.0f, 0.05f,0.10f,2.0f, 0.0f, 0.0f,  2200, 1.8f,  300, 3,  0,  3.0f,  0,   600, 0.00f, 420, 0.00f, 0.35f, 0.10f, 0, 1.15f },
    { "PLUCK", "Syn Koto",             1, 0.0f, 0.0f, 0.08f,0.18f,3.0f, 0.0f, 0.0f,  1800, 2.5f,  160, 3,  0,  0.0f,  1,   380, 0.00f, 320, 0.10f, 0.30f, 0.12f, 0, 1.1f },
    { "PLUCK", "Syn Pizz",        2, 0.3f, 0.10f,0.06f,0.0f, 2.0f, 0.0f, 0.0f,  1200, 2.0f,  120, 0,  0,  5.0f,  1,   260, 0.00f, 240, 0.05f, 0.35f, 0.00f, 0, 1.2f },
    { "PLUCK", "Syn Music Box",        1, 0.0f, 0.0f, 0.0f, 0.40f,3.5f, 0.0f, 0.0f,  6000, 1.5f,  200, 2,  1,  0.0f,  0,   550, 0.00f, 600, 0.00f, 0.45f, 0.15f, 1, 1.2f },
    { "PLUCK", "Neon Pluck",       3, 0.6f, 0.20f,0.0f, 0.12f,2.0f, 0.0f, 0.0f,   900, 3.5f,   90, 1,  0,  8.0f,  0,   300, 0.00f, 260, 0.20f, 0.25f, 0.40f, 1, 1.4f },
    { "PLUCK", "Syn Dulcimer",         2, 0.30f,0.00f,0.04f,0.15f,3.0f, 0.0f, 0.0f,  3400, 2.2f,  220, 3,  0,  6.0f,  0,   620, 0.00f, 500, 0.05f, 0.30f, 0.12f, 0, 1.20f },
    { "PLUCK", "Syn Steel Pan",        1, 0.00f,0.00f,0.00f,0.50f,2.0f, 5.0f, 6.0f,  2600, 2.0f,  180, 2,  0,  0.0f,  0,   550, 0.00f, 480, 0.08f, 0.35f, 0.10f, 0, 1.10f },
    { "PLUCK", "Syn Banjo",      1, 0.00f,0.00f,0.10f,0.18f,5.0f, 0.0f, 0.0f,  3800, 3.2f,   90, 0,  0,  0.0f,  0,   320, 0.00f, 240, 0.15f, 0.15f, 0.00f, 0, 1.00f },
    { "PLUCK", "Syn Sitar",       2, 0.35f,0.00f,0.06f,0.20f,5.0f, 0.0f, 0.0f,  2400, 3.0f,  260, 0,  0,  9.0f,  0,   680, 0.10f, 600, 0.45f, 0.30f, 0.20f, 1, 1.20f },
    { "PLUCK", "Tropic Pluck",     3, 0.60f,0.10f,0.00f,0.00f,1.0f, 0.0f, 0.0f,   800, 3.2f,  140, 1,  0,  8.0f,  0,   380, 0.00f, 300, 0.05f, 0.25f, 0.35f, 1, 1.30f },
    { "PLUCK", "Mallet Choir",     4, 0.50f,0.00f,0.00f,0.30f,4.0f, 0.0f, 0.0f,  2800, 1.8f,  200, 3,  0, 10.0f,  0,   480, 0.00f, 420, 0.00f, 0.40f, 0.12f, 0, 1.35f },
    { "PLUCK", "Water Drop",       1, 0.00f,0.00f,0.00f,0.30f,0.5f, 0.0f, 0.0f,   600, 4.0f,   60, 2,  0,  0.0f,  0,   200, 0.00f, 180, 0.00f, 0.30f, 0.30f, 1, 1.10f },
    { "PLUCK", "Rubber Toy",       1, 0.00f,0.10f,0.00f,0.50f,1.5f, 0.0f, 0.0f,   900, 2.2f,  110, 3,  0,  0.0f,  0,   260, 0.05f, 200, 0.20f, 0.15f, 0.08f, 0, 1.00f },
    { "PLUCK", "Syn Gamelan",          1, 0.00f,0.00f,0.00f,0.60f,3.5f, 0.0f, 0.0f,  3200, 1.6f,  240, 2,  0,  0.0f,  0,   700, 0.00f, 950, 0.00f, 0.45f, 0.10f, 0, 1.25f },
    { "PLUCK", "Felt Mallet",      1, 0.00f,0.20f,0.02f,0.10f,4.0f, 0.0f, 0.0f,  1100, 1.5f,  160, 3,  0,  0.0f,  2,   520, 0.05f, 480, 0.00f, 0.35f, 0.08f, 0, 1.10f },
    { "PLUCK", "Organ Pluck",      2, 0.40f,0.50f,0.00f,0.00f,1.0f, 0.0f, 0.0f,  1300, 2.0f,  130, 1,  0,  5.0f,  0,   300, 0.15f, 220, 0.10f, 0.20f, 0.18f, 1, 1.15f },
    { "PLUCK", "Lov3 Pluck",       1, 0.00f,0.05f,0.06f,0.20f,2.0f, 0.0f, 0.0f,  1500, 1.8f,  180, 3,  0,  4.0f,  0,   450, 0.05f, 300, 0.05f, 0.30f, 0.18f, 1, 1.2f },

    { "KEYS",  "EP Keys",          1, 0.0f, 0.0f, 0.0f, 0.45f,1.0f, 0.0f, 0.0f,  6500, 0.0f,  200, 2,  0,  4.0f,  2,   450, 0.40f, 260, 0.00f, 0.30f, 0.12f, 0, 1.1f },
    { "KEYS",  "Soft Keys",        1, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  5200, 0.0f,  200, 3,  0,  5.0f,  5,   300, 0.60f, 250, 0.00f, 0.30f, 0.00f, 0, 1.0f },
    { "KEYS",  "House Organ",      1, 0.0f, 0.8f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  20000, 0.0f, 200, 1,  0,  4.0f,  0,    50, 1.00f,  60, 0.05f, 0.15f, 0.10f, 0, 1.1f },
    { "KEYS",  "Retro Organ",      1, 0.0f, 0.8f, 0.0f, 0.0f, 2.0f, 6.5f,  4.0f, 20000, 0.0f, 200, 1,  0,  5.0f,  2,    50, 1.00f,  90, 0.15f, 0.20f, 0.00f, 0, 1.1f },
    { "KEYS",  "Funk Clav",        1, 0.0f, 0.0f, 0.03f,0.0f, 2.0f, 0.0f, 0.0f,  1800, 2.5f,   60, 0,  0,  5.0f,  0,   180, 0.30f,  60, 0.30f, 0.10f, 0.05f, 0, 1.0f },
    { "KEYS",  "Tine EP",          1, 0.0f, 0.0f, 0.0f, 0.30f,7.0f, 0.0f, 0.0f,  5600, 0.5f,  350, 3,  0,  3.0f,  1,   650, 0.30f, 300, 0.05f, 0.28f, 0.10f, 0, 1.1f },
    { "KEYS",  "Dusty Keys",       2, 0.3f, 0.15f,0.10f,0.20f,3.0f, 5.0f,  4.0f, 2800, 0.6f,  400, 3,  0,  6.0f,  2,   700, 0.35f, 350, 0.25f, 0.35f, 0.15f, 0, 1.1f },
    { "KEYS",  "Wurli EP",         1, 0.0f, 0.10f,0.0f, 0.18f,1.0f, 5.5f,  3.0f, 3400, 0.7f,  300, 2,  0,  0.0f,  2,   750, 0.30f, 280, 0.40f, 0.20f, 0.05f, 0, 1.0f },
    { "KEYS",  "Pipe Organ",       2, 0.4f, 0.50f,0.05f,0.10f,2.0f, 0.0f, 0.0f,  4500, 0.0f,  200, 2,  0,  4.0f, 30,   100, 1.00f, 250, 0.05f, 0.50f, 0.00f, 0, 1.3f },
    { "KEYS",  "DX Piano",         1, 0.00f,0.00f,0.00f,0.50f,14.0f,0.0f, 0.0f,  8000, 0.8f,  300, 2,  0,  0.0f,  1,   900, 0.35f, 400, 0.05f, 0.25f, 0.15f, 0, 1.20f },
    { "KEYS",  "Wah Clav",         1, 0.00f,0.00f,0.03f,0.00f,1.0f, 0.0f, 0.0f,   600, 3.0f,  350, 0,  0,  0.0f,  0,   700, 0.25f, 150, 0.30f, 0.10f, 0.10f, 0, 1.00f },
    { "KEYS",  "Soft Celeste",     1, 0.00f,0.10f,0.00f,0.25f,4.0f, 0.0f, 0.0f,  4500, 0.6f,  400, 2,  1,  0.0f,  2,  1200, 0.20f, 800, 0.00f, 0.45f, 0.10f, 0, 1.20f },
    { "KEYS",  "Toy Keys",         1, 0.00f,0.00f,0.02f,0.45f,4.0f, 0.0f, 0.0f,  3000, 1.2f,  250, 2,  0,  0.0f,  0,   800, 0.15f, 350, 0.08f, 0.30f, 0.10f, 0, 1.10f },
    { "KEYS",  "Syn Accordion",        3, 0.40f,0.00f,0.00f,0.00f,1.0f, 5.5f, 8.0f,  2200, 0.3f,  500, 1,  0,  8.0f, 25,   200, 0.90f, 180, 0.05f, 0.20f, 0.00f, 0, 1.15f },
    { "KEYS",  "Syn Harmonium",        2, 0.30f,0.10f,0.05f,0.00f,1.0f, 0.0f, 0.0f,  1600, 0.2f,  600, 0,  0,  6.0f, 60,   300, 0.92f, 300, 0.05f, 0.30f, 0.00f, 0, 1.10f },
    { "KEYS",  "Full Organ",       5, 0.50f,0.60f,0.00f,0.00f,1.0f, 0.0f, 0.0f,  5000, 0.0f,   40, 1,  0,  7.0f, 10,   100, 1.00f, 450, 0.10f, 0.55f, 0.00f, 0, 1.40f },
    { "KEYS",  "Perc Organ",       1, 0.00f,0.50f,0.00f,0.20f,3.0f, 0.0f, 0.0f,  2000, 2.2f,   80, 2,  0,  0.0f,  0,   150, 0.85f, 120, 0.25f, 0.15f, 0.00f, 0, 1.00f },
    { "KEYS",  "Glass CP80",       2, 0.30f,0.00f,0.00f,0.20f,7.0f, 0.0f, 0.0f,  7500, 0.7f,  350, 0,  0,  4.0f,  0,  1100, 0.30f, 420, 0.08f, 0.30f, 0.10f, 0, 1.25f },
    { "KEYS",  "Suitcase EP",      1, 0.00f,0.05f,0.00f,0.22f,7.0f, 0.0f, 0.0f,  3200, 0.5f,  380, 2,  0,  0.0f,  3,   950, 0.35f, 380, 0.05f, 0.35f, 0.10f, 0, 1.15f },
    { "KEYS",  "Syn Harpsi",      2, 0.20f,0.00f,0.02f,0.00f,1.0f, 0.0f, 0.0f,  5200, 0.9f,  200, 0,  0,  4.0f,  0,   850, 0.40f,  90, 0.10f, 0.25f, 0.05f, 0, 1.10f },
    { "KEYS",  "Lov3 Keys",        1, 0.00f,0.05f,0.00f,0.35f,2.0f, 0.0f, 0.0f,  3800, 0.5f,  400, 3,  0,  5.0f,  2,   900, 0.30f, 350, 0.08f, 0.35f, 0.12f, 0, 1.2f },
    { "KEYS",  "K-RnB Keys",       2, 0.30f,0.00f,0.10f,0.25f,5.5f, 0.0f, 0.0f,  3000, 0.6f,  350, 2,  0,  6.0f,  3,   800, 0.35f, 320, 0.05f, 0.30f, 0.15f, 1, 1.25f },

    { "BELL",  "Glass Bell",       1, 0.0f, 0.0f, 0.0f, 0.85f,3.5f, 0.0f, 0.0f,  20000, 0.0f, 200, 2,  0,  4.0f,  2,   700, 0.15f, 800, 0.00f, 0.50f, 0.00f, 0, 1.3f },
    { "BELL",  "Deep Bell",        1, 0.0f, 0.0f, 0.0f, 0.90f,2.76f,0.0f, 0.0f,  20000, 0.0f, 200, 2,  0,  3.0f,  3,  1500, 0.00f, 1500, 0.00f, 0.60f, 0.00f, 0, 1.3f },
    { "BELL",  "Syn Celesta",          1, 0.0f, 0.0f, 0.0f, 0.35f,4.0f, 0.0f, 0.0f,  6000, 0.0f,  200, 2,  0,  2.0f,  0,   900, 0.00f, 600, 0.00f, 0.45f, 0.08f, 0, 1.2f },
    { "BELL",  "Syn Tubular",     1, 0.0f, 0.10f,0.02f,0.60f,3.5f, 0.0f, 0.0f,  4200, 1.2f,  900, 2,  0,  0.0f,  0,  1500, 0.00f, 1400, 0.00f, 0.55f, 0.10f, 0, 1.3f },
    { "BELL",  "Syn Gong",   1, 0.0f, 0.15f,0.02f,0.80f,2.7f, 0.0f, 0.0f,  2600, 1.0f, 1200, 2,  0,  0.0f,  0,  1600, 0.00f, 1600, 0.10f, 0.50f, 0.05f, 0, 1.2f },
    { "BELL",  "Syn Handbell",      1, 0.0f, 0.08f,0.02f,0.55f,3.0f, 0.0f, 0.0f,  5000, 1.2f,  700, 2,  0,  0.0f,  0,  1100, 0.00f, 1100, 0.00f, 0.45f, 0.05f, 0, 1.1f },
    { "BELL",  "Fairy Bell",     1, 0.0f, 0.10f,0.02f,0.65f,5.2f, 0.5f, 5.0f,  3000, 1.0f,  500, 2,  0,  0.0f,  0,   900, 0.00f, 1200, 0.00f, 0.70f, 0.30f, 1, 1.5f },
    { "BELL",  "Syn Church",    1, 0.0f, 0.15f,0.02f,0.75f,2.5f, 0.0f, 0.0f,  3600, 1.1f, 1400, 2,  0,  0.0f,  0,  1600, 0.00f, 1800, 0.05f, 0.65f, 0.10f, 0, 1.3f },
    { "BELL",  "Syn Vibes",     1, 0.0f, 0.12f,0.01f,0.35f,4.0f, 5.0f,10.0f,  3200, 0.8f,  600, 2,  0,  0.0f,  0,  1200, 0.00f,  900, 0.00f, 0.40f, 0.08f, 0, 1.2f },
    { "BELL",  "Syn Glock",   1, 0.0f, 0.05f,0.01f,0.60f,5.5f, 0.0f, 0.0f,  7500, 1.4f,  400, 2,  0,  0.0f,  0,   700, 0.00f,  700, 0.00f, 0.35f, 0.05f, 0, 1.0f },

    { "MISC",  "Syn Flute",       1, 0.0f, 0.0f, 0.12f,0.0f, 2.0f, 5.0f, 10.0f, 4000, 0.0f,  200, 2,  1,  0.0f, 90,   200, 0.80f, 220, 0.00f, 0.35f, 0.00f, 0, 1.0f },
    { "MISC",  "Synth Brass",      3, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  1500, 1.5f,  300, 0,  0, 10.0f, 40,   200, 0.90f, 150, 0.20f, 0.20f, 0.00f, 0, 1.2f },
    { "MISC",  "Brass Stab",       5, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,  2000, 2.0f,  180, 0,  0, 12.0f,  3,   260, 0.25f, 140, 0.20f, 0.18f, 0.05f, 0, 1.25f },
    { "MISC",  "Rave Hit",         6, 0.7f, 0.40f,0.15f,0.10f,2.0f, 0.0f, 0.0f,  1500, 2.2f,  260, 0, -1, 15.0f,  0,   500, 0.00f, 500, 0.30f, 0.50f, 0.10f, 0, 1.5f },
    { "MISC",  "Noise Riser",      1, 0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f,   300, 4.0f, 2500, 2,  1,  0.0f, 500,  500, 1.00f, 500, 0.00f, 0.40f, 0.20f, 0, 1.5f },
    { "MISC",  "Cinema Braam",   5, 0.6f, 0.30f,0.05f,0.10f,2.0f, 0.0f, 0.0f,   700, 1.5f, 1500, 0, -1, 22.0f,200,  1500, 0.80f,  900, 0.50f, 0.60f, 0.10f, 0, 1.4f },
    { "MISC",  "Sub Drop",       1, 0.0f, 0.50f,0.02f,0.00f,2.0f, 0.0f, 0.0f,    80, 3.0f,  800, 2, -2,  0.0f,  0,  1200, 0.00f,  400, 0.20f, 0.10f, 0.00f, 0, 0.6f },
    { "MISC",  "Vinyl Keys",     2, 0.25f,0.10f,0.35f,0.20f,2.0f, 0.6f, 4.0f,  1600, 1.0f,  400, 3,  0,  5.0f, 10,   800, 0.40f,  500, 0.10f, 0.40f, 0.20f, 0, 1.1f },
    { "MISC",  "Tonal Wind",     3, 0.5f, 0.10f,0.70f,0.00f,2.0f, 0.3f, 8.0f,  1200, 0.5f, 1000, 2,  0, 12.0f,600,  1000, 0.90f, 1500, 0.05f, 0.80f, 0.10f, 0, 1.6f },
    { "MISC",  "Sci-Fi Sweep",   4, 0.5f, 0.10f,0.06f,0.00f,2.0f, 6.0f,20.0f,   300, 4.5f, 2500, 0,  0, 18.0f,100,  2000, 0.60f,  800, 0.25f, 0.50f, 0.40f, 1, 1.5f },
    };

    constexpr int kNumInstruments = (int) (sizeof (kInstruments) / sizeof (kInstruments[0]));
}

int VocalChopAudioProcessor::defaultInstrumentIndex()
{
    for (int i = 0; i < kNumInstruments; ++i)
        if (juce::String (kInstruments[i].name) == "Supersaw Lead")
            return i;
    return 0;
}

juce::StringArray VocalChopAudioProcessor::getInstrumentNames()
{
    juce::StringArray names;
    for (const auto& d : kInstruments)
        names.add (d.name);
    return names;
}

juce::StringArray VocalChopAudioProcessor::getInstrumentCategories()
{
    juce::StringArray cats;
    for (const auto& d : kInstruments)
        cats.add (d.category);
    return cats;
}

void VocalChopAudioProcessor::applyEnginePatch (int i)
{
    i = juce::jlimit (0, kNumInstruments - 1, i);
    const auto& d = kInstruments[i];

    auto& p = synthEngine.patch();
    p.resetToInit();
    p.unison        = d.unison;
    p.stereoSpread  = d.spread;
    p.subLevel      = d.sub;
    p.noiseLevel    = d.noise;
    p.fmAmount      = d.fm;
    p.fmRatio       = d.fmRatio;
    // Keep a musical LFO rate even when the patch ships without vibrato, so
    // raising the Vibrato knob always does something.
    p.vibRateHz     = d.vibHz > 0.01f ? d.vibHz : 5.0f;
    p.vibDepthCents = d.vibCents;
    p.filterCutoff  = d.fltHz;
    p.filterEnvOct  = d.fltEnvOct;
    p.filterEnvMs   = d.fltEnvMs;

    // --- Lushness settings by category, with a few name-specific accents ----
    // Chorus is computed into a local: the patch field is param-driven and
    // rewritten by the audio thread every block, so it can't be read back.
    const juce::String cat (d.category);
    const juce::String name (d.name);
    float chorus = 0.12f;

    if (cat == "PAD")         { chorus = 0.50f; p.driftCents = 4.0f;  p.velToFilterOct = 0.4f; }
    else if (cat == "LEAD")   { chorus = 0.28f; p.driftCents = 3.0f;  p.velToFilterOct = 0.6f; }
    else if (cat == "SYNTH")  { chorus = 0.35f; p.driftCents = 3.5f;  p.velToFilterOct = 0.6f; }
    else if (cat == "PIANO")  { chorus = 0.06f; p.driftCents = 1.0f;  p.velToFilterOct = 1.4f; }
    else if (cat == "GUITAR") { chorus = 0.08f; p.driftCents = 1.0f;  p.velToFilterOct = 1.3f;
                                p.filterQ = 1.2f; }
    else if (cat == "KEYS")   { chorus = 0.25f; p.driftCents = 2.0f;  p.velToFilterOct = 1.1f; }
    else if (cat == "BELL")   { chorus = 0.22f; p.driftCents = 1.5f;  p.velToFilterOct = 0.8f; }
    else if (cat == "PLUCK")  { chorus = 0.18f; p.driftCents = 2.0f;  p.velToFilterOct = 1.2f;
                                p.filterQ = 1.5f; }
    else if (cat == "BASS")   { chorus = 0.0f;  p.driftCents = 1.5f;  p.velToFilterOct = 1.0f;
                                p.satAmount = 0.30f; }
    else /* MISC / INIT */    { chorus = 0.12f; p.driftCents = 2.5f;  p.velToFilterOct = 0.6f; }

    if (name == "Acid Lead")     { p.filterQ = 5.5f; p.satAmount = 0.35f; }
    if (name == "Wobble Growl")  p.filterQ = 2.2f;
    if (name == "Neon Bass")     p.filterQ = 1.4f;
    if (name == "Supersaw Lead") chorus = 0.40f;
    if (name == "Chip Lead")     { chorus = 0.0f; p.driftCents = 0.0f; }
    if (name == "Sub 808")       { p.driftCents = 0.5f; chorus = 0.0f; }
    if (name == "Synth Brass")   chorus = 0.30f;
    if (name == "Syn Flute")    chorus = 0.20f;

    p.chorusMix = chorus;

    // Publish the resolved module values for applyInstrument to mirror into
    // the knobs (race-free snapshot; see ModuleDefaults in the header).
    moduleDefaults = { d.unison, d.spread, d.sub, d.noise, d.fm,
                       d.vibCents, chorus };

    currentInstrument = i;
}

void VocalChopAudioProcessor::applyInstrument (int instrumentIndex)
{
    applyEnginePatch (instrumentIndex);
    const auto& d = kInstruments[currentInstrument];

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Common baseline, then the instrument's knob values.
    set ("engine",       1.0f);          // Synth mode
    set ("filterType",   0.0f);   set ("filterCutoff", 20000.0f);
    set ("filterReso",   0.707f);
    set ("mix",          1.0f);
    set ("pitch",        0.0f);   set ("formant", 0.0f);

    set ("synthWave",    (float) d.wave);
    set ("synthOctave",  (float) d.octave);
    set ("synthDetune",  d.detune);

    // Mirror the resolved architecture into the module knobs (the knobs take
    // over from here, so every module stays hand-adjustable).
    set ("synthUnison",  (float) moduleDefaults.unison);
    set ("synthSpread",  moduleDefaults.spread);
    set ("synthSub",     moduleDefaults.sub);
    set ("synthNoise",   moduleDefaults.noise);
    set ("synthFM",      moduleDefaults.fm);
    set ("synthVibrato", juce::jlimit (0.0f, 1.0f, moduleDefaults.vibCents / 30.0f));
    set ("synthChorus",  moduleDefaults.chorus);
    set ("synthLfoAmt",  0.0f);   // motion is a per-user seasoning, not baked in
    set ("attack",       d.atk);
    set ("decay",        d.dec);
    set ("sustain",      d.sus);
    set ("release",      d.rel);
    set ("drive",        d.drive);
    set ("reverb",       d.reverb);
    set ("delay",        d.delay);
    set ("pingpong",     d.pingpong);
    set ("width",        d.width);
}

//==============================================================================
void VocalChopAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Full session state: parameters + sample path + slicing + theme, so that
    // reopening the project restores everything, not just the knobs.
    juce::XmlElement root ("VocalChopState");
    root.setAttribute ("samplePath",  loadedSampleFile.getFullPathName());
    root.setAttribute ("sliceMode",   (int) sliceEngine.getMode());
    root.setAttribute ("gridDiv",     sliceEngine.getGridDivision());
    root.setAttribute ("sensitivity", (double) sliceEngine.getSensitivity());
    root.setAttribute ("theme",       ThemeManager::current());
    root.setAttribute ("instrument",  currentInstrument);
    // The name is authoritative across plugin versions — the table grows and
    // indices drift, but "Syn Grand" is forever.
    root.setAttribute ("instrumentName", getInstrumentNames()[currentInstrument]);

    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    copyXmlToBinary (root, destData);
}

void VocalChopAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    // Legacy format: bare parameter tree.
    if (xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        return;
    }

    if (! xml->hasTagName ("VocalChopState"))
        return;

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    ThemeManager::setIndex (xml->getIntAttribute ("theme", ThemeManager::current()));

    // Restore the instrument's engine architecture only — the knob values come
    // from the restored parameter tree above, not the instrument defaults.
    // Prefer the saved NAME (indices drift as the table grows across
    // versions); fall back to the index for old sessions.
    {
        int idx = xml->getIntAttribute ("instrument", 0);
        const auto savedName = xml->getStringAttribute ("instrumentName");
        if (savedName.isNotEmpty())
        {
            const auto names = getInstrumentNames();
            const int byName = names.indexOf (savedName);
            if (byName >= 0)
                idx = byName;
        }
        applyEnginePatch (idx);
    }

    sliceEngine.setMode ((SliceEngine::Mode) xml->getIntAttribute ("sliceMode",
                                                                   (int) SliceEngine::Transient));
    sliceEngine.setGridDivision (xml->getIntAttribute ("gridDiv", 16));
    sliceEngine.setSensitivity ((float) xml->getDoubleAttribute ("sensitivity", 0.3));

    // Reload the sample from disk; loadSampleFromFile re-slices with the
    // settings restored above. If the file moved/was deleted, keep running
    // with no sample rather than failing state restore.
    const juce::File sample (xml->getStringAttribute ("samplePath"));
    if (sample.existsAsFile())
        loadSampleFromFile (sample, false);
    else
        sliceEngine.rebuildSlices();

    // Let an open editor re-sync its combos / theme / cached waveform
    // (sendChangeMessage is async and safe from any thread).
    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* VocalChopAudioProcessor::createEditor()
{
    return new VocalChopAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalChopAudioProcessor();
}
