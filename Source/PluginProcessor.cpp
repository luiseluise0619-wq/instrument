#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AudioEngine/SampleLoader.h"

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

    apvts.addParameterListener ("pitch", this);
    apvts.addParameterListener ("formant", this);
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
        "drive", "Drive", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "reverb", "Reverb", Range (0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delay", "Delay", Range (0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack", Range (0.0f, 500.0f, 0.1f), 5.0f));
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
        "engine", "Engine", juce::StringArray { "Chop", "Synth" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "synthWave", "Synth Wave",
        juce::StringArray { "Saw", "Square", "Sine", "Triangle" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthDetune", "Detune", Range (0.0f, 50.0f, 0.1f), 7.0f));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "synthOctave", "Octave", -2, 2, 0));

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

    // The pitch/formant engine has inherent latency — report it to the host.
    setLatencySamples (pitchFormant.getLatencySamples());

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
                const int voice = triggerSliceIndex (note - kRootNote,
                                                     msg.getVelocity() / 127.0f);
                if (juce::isPositiveAndBelow (note, 128))
                    noteToVoice[(size_t) note] = voice;
            }
        }
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            if (synth)
            {
                synthEngine.noteOff (note);
            }
            // Gate mode: releasing the key starts that voice's release stage
            // (one-shot voices ignore this inside the pool).
            else if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
            {
                voicePool.releaseVoice (noteToVoice[(size_t) note]);
                noteToVoice[(size_t) note] = -1;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            voicePool.releaseAll();
            synthEngine.releaseAll();
            noteToVoice.fill (-1);
        }
    }
}

int VocalChopAudioProcessor::triggerSliceIndex (int sliceIndex, float velocity)
{
    // Audio thread. tryGetSlice() safely no-ops if a re-slice is in progress.
    SlicePoint slice;
    if (sliceEngine.tryGetSlice (sliceIndex, slice))
        return voicePool.triggerVoice (slice.startSample, slice.lengthSamples, velocity);
    return -1;
}

void VocalChopAudioProcessor::triggerSlicePad (int sliceIndex, float velocity)
{
    // Message thread (key click). Hand index+velocity to the audio thread
    // lock-free.
    int start1, size1, start2, size2;
    padFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        padQueue[(size_t) start1]    = sliceIndex;
        padQueueVel[(size_t) start1] = juce::jlimit (0.0f, 1.0f, velocity);
        padFifo.finishedWrite (1);
    }
}

void VocalChopAudioProcessor::drainPadQueue()
{
    int start1, size1, start2, size2;
    padFifo.prepareToRead (padFifo.getNumReady(), start1, size1, start2, size2);

    const bool synth = isSynthMode();

    auto fire = [this, synth] (int idx, float vel)
    {
        if (synth)
            synthEngine.tapNote (kRootNote + idx, vel);   // same key mapping
        else
            triggerSliceIndex (idx, vel);
    };

    for (int i = 0; i < size1; ++i)
        fire (padQueue[(size_t) (start1 + i)], padQueueVel[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        fire (padQueue[(size_t) (start2 + i)], padQueueVel[(size_t) (start2 + i)]);

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

bool VocalChopAudioProcessor::loadSampleFromFile (const juce::File& file)
{
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (file, sr);
    if (buffer == nullptr)
        return false;

    sampleBuffer     = buffer;
    loadedSampleRate = sr;
    loadedSampleFile = file;
    reassignSampleToEngines();
    return true;
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
juce::StringArray VocalChopAudioProcessor::getInstrumentNames()
{
    return { "Init Synth", "Neon Bass", "Dream Pad", "Crystal Pluck",
             "Retro Lead", "Glass Bell", "Soft Keys" };
}

void VocalChopAudioProcessor::applyInstrument (int instrumentIndex)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Every instrument starts from a clean synth baseline.
    set ("engine",       1.0f);          // Synth mode
    set ("synthWave",    0.0f);          // Saw
    set ("synthDetune",  7.0f);
    set ("synthOctave",  0.0f);
    set ("attack",       5.0f);   set ("decay",   120.0f);
    set ("sustain",      0.75f);  set ("release", 60.0f);
    set ("filterType",   0.0f);   set ("filterCutoff", 20000.0f);
    set ("filterReso",   0.707f);
    set ("drive",        0.0f);   set ("reverb", 0.15f);
    set ("delay",        0.0f);   set ("pingpong", 0.0f);
    set ("width",        1.0f);   set ("mix", 1.0f);
    set ("pitch",        0.0f);   set ("formant", 0.0f);

    switch (instrumentIndex)
    {
        case 1: // Neon Bass — fat detuned saw an octave down, dark filter
            set ("synthOctave", -1.0f); set ("synthDetune", 12.0f);
            set ("filterType", 1.0f);   set ("filterCutoff", 500.0f);
            set ("filterReso", 1.2f);   set ("drive", 0.3f);
            set ("attack", 0.0f); set ("decay", 200.0f);
            set ("sustain", 0.8f); set ("release", 80.0f);
            set ("reverb", 0.0f); set ("width", 0.9f);
            break;

        case 2: // Dream Pad — wide slow saw wash
            set ("synthDetune", 18.0f);
            set ("attack", 400.0f); set ("decay", 800.0f);
            set ("sustain", 0.8f);  set ("release", 900.0f);
            set ("filterType", 1.0f); set ("filterCutoff", 3200.0f);
            set ("reverb", 0.6f); set ("width", 1.5f);
            break;

        case 3: // Crystal Pluck — snappy triangle with echo
            set ("synthWave", 3.0f);
            set ("attack", 0.0f);  set ("decay", 180.0f);
            set ("sustain", 0.0f); set ("release", 120.0f);
            set ("filterType", 1.0f); set ("filterCutoff", 6000.0f);
            set ("filterReso", 1.8f);
            set ("delay", 0.3f); set ("pingpong", 1.0f);
            set ("reverb", 0.25f);
            break;

        case 4: // Retro Lead — square with slap-back
            set ("synthWave", 1.0f); set ("synthDetune", 10.0f);
            set ("attack", 3.0f);  set ("decay", 100.0f);
            set ("sustain", 0.7f); set ("release", 150.0f);
            set ("filterType", 1.0f); set ("filterCutoff", 8000.0f);
            set ("delay", 0.25f);
            break;

        case 5: // Glass Bell — pure sine an octave up, long shimmer
            set ("synthWave", 2.0f); set ("synthOctave", 1.0f);
            set ("synthDetune", 4.0f);
            set ("attack", 2.0f);  set ("decay", 600.0f);
            set ("sustain", 0.2f); set ("release", 700.0f);
            set ("reverb", 0.5f);  set ("width", 1.3f);
            break;

        case 6: // Soft Keys — mellow triangle EP feel
            set ("synthWave", 3.0f);
            set ("attack", 5.0f);  set ("decay", 300.0f);
            set ("sustain", 0.6f); set ("release", 250.0f);
            set ("filterType", 1.0f); set ("filterCutoff", 5500.0f);
            set ("reverb", 0.3f);
            break;

        case 0: // Init Synth (baseline already applied)
        default:
            break;
    }
}

//==============================================================================
void VocalChopAudioProcessor::toggleAB()
{
    // Save the current knobs into the slot we're leaving, then load the other
    // slot (first toggle simply carries the current sound across).
    auto current = apvts.copyState();

    if (abIsB)
    {
        snapshotB = current;
        if (snapshotA.isValid())
            apvts.replaceState (snapshotA.createCopy());
    }
    else
    {
        snapshotA = current;
        if (snapshotB.isValid())
            apvts.replaceState (snapshotB.createCopy());
    }

    abIsB = ! abIsB;
}

void VocalChopAudioProcessor::randomizeParams()
{
    auto& rng = juce::Random::getSystemRandom();

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    auto uni = [&rng] (float lo, float hi) { return lo + rng.nextFloat() * (hi - lo); };

    // Musical-but-surprising ranges; envelope stays playable on purpose.
    set ("pitch",        uni (-12.0f, 12.0f));
    set ("formant",      uni (-6.0f, 6.0f));
    set ("grainSize",    uni (30.0f, 300.0f));
    set ("drive",        uni (0.0f, 0.6f));
    set ("reverb",       uni (0.0f, 0.7f));
    set ("delay",        uni (0.0f, 0.6f));
    set ("delayFeedback",uni (0.2f, 0.7f));
    set ("pingpong",     rng.nextBool() ? 1.0f : 0.0f);
    set ("width",        uni (0.6f, 1.6f));
    set ("filterType",   (float) rng.nextInt (4));
    set ("filterCutoff", uni (300.0f, 18000.0f));
    set ("filterReso",   uni (0.5f, 4.0f));
    set ("reverse",      rng.nextFloat() < 0.25f ? 1.0f : 0.0f);
    set ("attack",       uni (0.0f, 80.0f));
    set ("decay",        uni (0.0f, 400.0f));
    set ("sustain",      uni (0.4f, 1.0f));
    set ("release",      uni (10.0f, 400.0f));
    set ("synthWave",    (float) rng.nextInt (4));
    set ("synthDetune",  uni (0.0f, 25.0f));
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

    sliceEngine.setMode ((SliceEngine::Mode) xml->getIntAttribute ("sliceMode",
                                                                   (int) SliceEngine::Transient));
    sliceEngine.setGridDivision (xml->getIntAttribute ("gridDiv", 16));
    sliceEngine.setSensitivity ((float) xml->getDoubleAttribute ("sensitivity", 0.3));

    // Reload the sample from disk; loadSampleFromFile re-slices with the
    // settings restored above. If the file moved/was deleted, keep running
    // with no sample rather than failing state restore.
    const juce::File sample (xml->getStringAttribute ("samplePath"));
    if (sample.existsAsFile())
        loadSampleFromFile (sample);
    else
        sliceEngine.rebuildSlices();
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
