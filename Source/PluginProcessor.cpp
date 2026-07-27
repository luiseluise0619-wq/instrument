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
   #if SLYCE_DEMO_GATE
    licensed.store (vcs::Licensing::loadActivation());
   #else
    licensed.store (true);   // demo gate disabled at build time: fully open
   #endif

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
    delaySyncParam     = apvts.getRawParameterValue ("delaySync");
    arpModeParam   = apvts.getRawParameterValue ("arpMode");
    arpRateParam   = apvts.getRawParameterValue ("arpRate");
    arpGateParam   = apvts.getRawParameterValue ("arpGate");
    arpOctParam    = apvts.getRawParameterValue ("arpOct");
    pumpAmtParam   = apvts.getRawParameterValue ("pumpAmt");
    pumpRateParam  = apvts.getRawParameterValue ("pumpRate");
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
    synthGlideParam   = apvts.getRawParameterValue ("synthGlide");
    macroHypeParam  = apvts.getRawParameterValue ("macroHype");
    macroSpaceParam = apvts.getRawParameterValue ("macroSpace");
    macroDirtParam  = apvts.getRawParameterValue ("macroDirt");

    apvts.addParameterListener ("pitch", this);
    apvts.addParameterListener ("formant", this);

    // Synth mode boots with a designed patch (Supersaw Lead) whenever the
    // user flips the engine over.
    applyEnginePatch (defaultInstrumentIndex());

    // First-run experience = the name promise: decode the embedded demo
    // vocal and slice it, so the very first key press CHOPS. No parameter
    // writes here (hosts dislike notifications mid-construction) — the
    // engine parameter's default is already Chop.
    {
        double sr = currentSampleRate;
        if (auto demo = SampleLoader::decode (BinaryData::vocal_chop_demo_wav,
                                              BinaryData::vocal_chop_demo_wavSize, sr))
        {
            sampleBuffer     = demo;
            loadedSampleRate = sr;
            reassignSampleToEngines();
            rescanSlices();
            analyzeSampleKey();
        }
    }
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

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "delaySync", "Delay Sync",
        juce::StringArray { "Free", "1/4", "1/4.", "1/8", "1/8.", "1/16", "1/32" }, 0));
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
        "engine", "Engine", juce::StringArray { "Chop", "Synth", "Sampled", "Melody" }, 0));
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

    // Performance macros: one knob each for "the hook hits harder", "wetter
    // space" and "dirtier texture". They OFFSET the underlying values in the
    // audio thread without touching the parameters they shadow.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "macroHype", "HYPE", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "macroSpace", "SPACE", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "macroDirt", "DIRT", Range (0.0f, 1.0f, 0.001f), 0.0f));

    // --- v3 additions -------------------------------------------------------
    // APPENDED, never inserted. Hosts that store automation by parameter INDEX
    // instead of by ID would re-point every lane after an insertion, so a v2
    // project would come back with the wrong knobs automated.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "arpMode", "Arp",
        juce::StringArray { "Off", "Up", "Down", "Up-Down", "Random" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "arpRate", "Arp Rate",
        juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "arpGate", "Arp Gate", Range (0.05f, 1.0f, 0.01f), 0.6f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "arpOct", "Arp Octaves", juce::StringArray { "1", "2", "3" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "pumpAmt", "Pump", Range (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "pumpRate", "Pump Rate",
        juce::StringArray { "1 bar", "1/2", "1/4", "1/8" }, 2));

    // Portamento. Skewed low: everything musical lives under 300 ms, and the
    // long end is there for 808 slides and dub sirens.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "synthGlide", "Glide", Range (0.0f, 1500.0f, 1.0f, 0.35f), 0.0f));

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
    samplerEngine.prepare (sampleRate, samplesPerBlock);
    melodyEngine.prepare (sampleRate, samplesPerBlock);
    pitchFormant.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    granularEngine.prepare (spec);
    fxChain.prepare (spec);
    looper.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate, samplesPerBlock);

    widthSmoothed.reset (sampleRate, 0.02);
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr ? widthParam->load() : 1.0f);

    noteToVoice.fill (-1);
    padKeyToVoice.fill (-1);

    // Transport/device changes must not resurrect an arp pattern or leave the
    // pump mid-duck (a stale phase makes the first bar after a restart lopsided).
    arpHeld.fill (false);
    arpVel.fill (0.0f);
    arpNote = -1;
    arpStepSamples = 0;
    arpGateSamples = 0;
    arpIndex = 0; arpDir = 1; arpOctave = 0;
    pumpPhase = 0.0;

    // Total plugin latency: the pitch/formant engine's inherent latency plus
    // the limiter's look-ahead delay.
    // Report only the limiter's tiny look-ahead. The stretch engine is fully
    // BYPASSED at neutral pitch/formant, so a live player feels ~2 ms; when a
    // pitch knob is in use its latency is audible but not host-compensated
    // (re-reporting latency mid-play makes hosts glitch far worse).
    setLatencySamples (limiter.getLatencySamples());

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

    // Melody mode plays this same sample chromatically. Rebuild only when the
    // buffer actually changed (this also runs from prepareToPlay).
    if (sampleBuffer != melodySourceRef)
    {
        melodySourceRef = sampleBuffer;
        if (sampleBuffer != nullptr)
            melodyEngine.loadFromBuffer (*sampleBuffer, loadedSampleRate, "Melody",
                                         kRootNote);   // first keybed key = original pitch
    }
}

void VocalChopAudioProcessor::analyzeSampleKey()
{
    // Message thread. Chroma-based key estimate: average FFT magnitudes
    // folded onto 12 pitch classes, correlated against the Krumhansl-Kessler
    // major/minor profiles in all 12 rotations.
    detectedKeyRoot.store (-1);

    auto sample = sampleBuffer;
    if (sample == nullptr || sample->getNumSamples() < 8192)
        return;

    constexpr int order = 12;
    const int fftSize = 1 << order;             // 4096
    juce::dsp::FFT fft (order);

    std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                      * (float) i / (float) (fftSize - 1)));

    double chroma[12] = {};
    const int numCh   = sample->getNumChannels();
    const int total   = juce::jmin (sample->getNumSamples(),
                                    (int) (loadedSampleRate * 15.0));   // first 15 s
    const double sr   = loadedSampleRate;

    for (int start = 0; start + fftSize <= total; start += fftSize)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                mono += sample->getSample (ch, start + i);
            fftData[(size_t) i] = (mono / (float) numCh) * window[(size_t) i];
        }
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        for (int bin = 2; bin < fftSize / 2; ++bin)
        {
            const double freq = (double) bin * sr / (double) fftSize;
            if (freq < 60.0 || freq > 4500.0)
                continue;
            const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
            chroma[pc] += (double) fftData[(size_t) bin];
        }
    }

    // Krumhansl-Kessler key profiles.
    static const double majorP[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                       2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                       2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double bestScore = -1.0;
    int bestRoot = -1;
    bool bestMinor = false;

    for (int root = 0; root < 12; ++root)
    {
        double sMaj = 0.0, sMin = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double c = chroma[(root + i) % 12];
            sMaj += c * majorP[i];
            sMin += c * minorP[i];
        }
        if (sMaj > bestScore) { bestScore = sMaj; bestRoot = root; bestMinor = false; }
        if (sMin > bestScore) { bestScore = sMin; bestRoot = root; bestMinor = true; }
    }

    detectedKeyMinor.store (bestMinor);
    detectedKeyRoot.store (bestRoot);
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
    // Host transport: the looper metronome and the synced delay follow it.
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm > 20.0 && *bpm < 400.0 ? *bpm : 0.0);

            hostPlaying.store (pos->getIsPlaying());
            if (auto ppq = pos->getPpqPosition())
                hostPpq.store (*ppq);
            else
                hostPpq.store (-1.0);
        }
    }

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
    synthEngine.setWave (kitMode.load() ? (int) SynthEngine::Sine
                                        : (int) synthWaveParam->load());
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
        pt.glideMs       = synthGlideParam->load();

        // Performance macros offset the modules they shadow (never the
        // parameters themselves, so host automation stays untouched).
        const float hype = macroHypeParam->load();
        const float dirt = macroDirtParam->load();
        if (hype > 0.0f)
        {
            pt.stereoSpread = juce::jmin (1.0f, synthSpreadParam->load() + 0.5f * hype);
            pt.lfoDepthOct  = juce::jmin (2.0f, pt.lfoDepthOct.load() + 0.5f * hype);
        }
        if (dirt > 0.0f)
            pt.noiseLevel = juce::jmin (1.0f, synthNoiseParam->load() + 0.30f * dirt);
    }

    // Effective FX amounts = knob + macro offsets (clamped in the FX).
    {
        const float hype  = macroHypeParam->load();
        const float space = macroSpaceParam->load();
        const float dirt  = macroDirtParam->load();
        effDrive  = juce::jlimit (0.0f, 1.0f, driveParam->load()  + 0.35f * hype + 0.40f * dirt);
        effReverb = juce::jlimit (0.0f, 1.0f, reverbParam->load() + 0.50f * space);
        effDelay  = juce::jlimit (0.0f, 1.0f, delayParam->load()  + 0.35f * space);
        effWidth  = juce::jlimit (0.0f, 2.0f, widthParam->load()  + 0.50f * space);
    }

    // 1) MIDI + pad-queue → slice / synth triggers.
    handleMidi (midi, numSamples);
    drainPadQueue();
    advanceArp (numSamples);

    // 2) Render both sources (the unused engine is simply silent, so
    //    switching modes mid-note never clicks).
    voicePool.renderNextBlock (buffer, numSamples);
    synthEngine.render (buffer, numSamples);
    // Leaving Sampled mode must actually silence it - long piano samples
    // otherwise keep ringing over the newly picked instrument.
    if (! isSamplerMode())
        samplerEngine.releaseAll();
    samplerEngine.render (buffer, numSamples);
    if (! isMelodyMode())
        melodyEngine.releaseAll();
    melodyEngine.render (buffer, numSamples);

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

    // 8) Loop station: records the performance, then adds the stacked loop
    //    (the limiter after us protects the sum).
    looper.process (buffer, numSamples);

    // 9) Brick-wall limiter.
    limiter.process (buffer);

    // 9b) Sidechain pump: the EDM signature. A tempo-locked duck applied to
    //     the whole mix - no external sidechain routing needed.
    {
        const float amt = pumpAmtParam != nullptr
                            ? juce::jlimit (0.0f, 1.0f, pumpAmtParam->load()) : 0.0f;
        if (amt > 0.001f)
        {
            static const double cycleBeats[] = { 4.0, 2.0, 1.0, 0.5 };
            const int rate = pumpRateParam != nullptr
                               ? juce::jlimit (0, 3, (int) pumpRateParam->load()) : 2;
            const double beat = 60.0 / juce::jmax (20.0, currentBpm());
            const double cycle = juce::jmax (1.0, beat * cycleBeats[rate] * currentSampleRate);
            const double inc = 1.0 / cycle;

            // Phase-lock to the transport. Free-running, the duck landed
            // wherever the plugin happened to start - the one thing a
            // sidechain pump must never do is drift off the bar line.
            const double ppq = hostPpq.load();
            if (ppq >= 0.0 && hostPlaying.load())
            {
                const double cyc = ppq / cycleBeats[rate];
                pumpPhase = cyc - std::floor (cyc);
            }

            for (int n = 0; n < numSamples; ++n)
            {
                // Ducked hard on the beat, recovering with a curve - the
                // classic pumped compressor shape.
                const float rise = (float) pumpPhase;
                const float duck = 1.0f - amt * (1.0f - rise) * (1.0f - rise);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[n] *= duck;

                pumpPhase += inc;
                if (pumpPhase >= 1.0)
                    pumpPhase -= 1.0;
            }
        }
        else
            pumpPhase = 0.0;
    }

    // 10) Demo gate: without a license the output mutes for 2 s every
    //     minute (short fades at the window edges so there is no click).
    if (! licensed.load (std::memory_order_relaxed))
    {
        const auto period  = (int64_t) (60.0 * currentSampleRate);
        const auto muteLen = (int64_t) ( 2.0 * currentSampleRate);
        for (int n = 0; n < numSamples; ++n)
        {
            const auto ph = (demoClock + n) % period;
            if (ph >= period - muteLen)
            {
                const auto into   = ph - (period - muteLen);
                const auto remain = muteLen - into;
                float mute = 0.0f;
                if (into < 256)        mute = 1.0f - (float) into / 256.0f;
                else if (remain < 256) mute = 1.0f - (float) remain / 256.0f;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[n] *= mute;
            }
        }
        demoClock += numSamples;
    }

    // Feed the oscilloscope ring (mono average of the final output). The UI
    // reads it lock-free; only the write position needs release ordering.
    {
        const int   chans = juce::jmax (1, buffer.getNumChannels());
        const float norm  = 1.0f / (float) chans;
        int w = scopeWritePos.load (std::memory_order_relaxed);
        for (int n = 0; n < numSamples; ++n)
        {
            float s = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                s += buffer.getReadPointer (ch)[n];
            scopeRing[(size_t) (w & (int) (scopeRing.size() - 1))] = s * norm;
            ++w;
        }
        // Mask to a large power-of-two multiple of the ring size: the counter
        // stays positive forever and every (x & (size-1)) read stays valid.
        scopeWritePos.store (w & 0x3fffffff, std::memory_order_release);
    }

    // Publish the output level as a PEAK LATCH: keep the maximum until the
    // meter consumes it (exchange-to-zero), so no transient between two UI
    // frames is ever missed and the bar reacts on the very next frame.
    {
        const float mag = buffer.getMagnitude (0, numSamples);
        float prev = outputLevel.load (std::memory_order_relaxed);
        while (prev < mag
               && ! outputLevel.compare_exchange_weak (prev, mag,
                                                       std::memory_order_relaxed)) {}
    }
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

        const int note = msg.getNoteNumber();

        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            // With the arp on, held notes feed the PATTERN instead of
            // sounding directly; advanceArp() plays them on the grid.
            if (arpModeParam != nullptr && (int) arpModeParam->load() > 0)
            {
                if (juce::isPositiveAndBelow (note, 128))
                {
                    arpHeld[(size_t) note] = true;
                    arpVel[(size_t) note]  = msg.getVelocity() / 127.0f;
                }
            }
            else
                routeNoteOn (note, msg.getVelocity() / 127.0f, false);
        }
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            if (juce::isPositiveAndBelow (note, 128))
                arpHeld[(size_t) note] = false;
            routeNoteOff (note);
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
            samplerEngine.releaseAll();
            melodyEngine.releaseAll();
            noteToVoice.fill (-1);
            padKeyToVoice.fill (-1);
            arpHeld.fill (false);
            arpVel.fill (0.0f);
            arpNote = -1;
        }
    }
}

void VocalChopAudioProcessor::routeNoteOn (int note, float velocity, bool selfReleasing)
{
    // Sampled/Melody modes without a loaded bank fall through to the synth
    // branch (isSynthMode() is true there too): keys must NEVER be silent.
    if (isMelodyMode() && melodyEngine.hasBank())
    {
        if (selfReleasing) melodyEngine.tapNote (note, velocity);
        else               melodyEngine.noteOn  (note, velocity);
    }
    else if (isSamplerMode() && samplerEngine.hasBank())
    {
        if (selfReleasing) samplerEngine.tapNote (note, velocity);
        else               samplerEngine.noteOn  (note, velocity);
    }
    else if (isSynthMode() || sliceEngine.getNumSlices() == 0)
    {
        if (isSynthMode() && kitMode.load (std::memory_order_relaxed))
            kitNoteOn (note, velocity, selfReleasing);
        else if (selfReleasing) synthEngine.tapNote (note, velocity);
        else                    synthEngine.noteOn  (note, velocity);
    }
    else
    {
        // Retriggering a still-held note releases its old voice so the
        // previous hit doesn't ring on as an orphan.
        if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
            voicePool.releaseVoice (noteToVoice[(size_t) note]);

        const int voice = triggerSliceIndex (diatonicSliceIndex (note - kRootNote), velocity);
        if (juce::isPositiveAndBelow (note, 128))
            noteToVoice[(size_t) note] = voice;
    }
}

void VocalChopAudioProcessor::routeNoteOff (int note)
{
    // Release EVERY engine regardless of the current mode: the note may have
    // started before an engine switch, and each call safely no-ops when that
    // engine holds nothing for this note.
    synthEngine.noteOff (note);
    samplerEngine.noteOff (note);
    melodyEngine.noteOff (note);

    if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
    {
        voicePool.releaseVoice (noteToVoice[(size_t) note]);
        noteToVoice[(size_t) note] = -1;
    }
}

double VocalChopAudioProcessor::currentBpm() const
{
    const double host = hostBpm.load();
    return host > 0.0 ? host : (double) looper.getMetroBpm();
}

void VocalChopAudioProcessor::advanceArp (int numSamples)
{
    const int mode = arpModeParam != nullptr ? (int) arpModeParam->load() : 0;

    if (mode <= 0)
    {
        // Turning the arp off must not leave its last note hanging.
        if (arpNote >= 0) { routeNoteOff (arpNote); arpNote = -1; }
        arpStepSamples = 0;
        return;
    }

    // Collect the held notes, lowest first.
    int notes[128];
    int count = 0;
    for (int n = 0; n < 128 && count < 128; ++n)
        if (arpHeld[(size_t) n])
            notes[count++] = n;

    if (count == 0)
    {
        if (arpNote >= 0) { routeNoteOff (arpNote); arpNote = -1; }
        arpStepSamples = 0;       // the next held note starts a step at once
        arpIndex = 0; arpDir = 1; arpOctave = 0;
        return;
    }

    // Step length from the tempo grid.
    static const double rateMult[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    const int  rate = arpRateParam != nullptr
                        ? juce::jlimit (0, 5, (int) arpRateParam->load()) : 3;
    const double beat = 60.0 / juce::jmax (20.0, currentBpm());
    const int stepLen = juce::jmax (32, (int) (beat * rateMult[rate] * currentSampleRate));

    const float gate = arpGateParam != nullptr
                         ? juce::jlimit (0.05f, 1.0f, arpGateParam->load()) : 0.6f;
    const int octaves = arpOctParam != nullptr
                          ? juce::jlimit (1, 3, (int) arpOctParam->load() + 1) : 1;

    // Note-off for the current step's note (block granularity, like the pads).
    if (arpNote >= 0)
    {
        arpGateSamples -= numSamples;
        if (arpGateSamples <= 0)
        {
            routeNoteOff (arpNote);
            arpNote = -1;
        }
    }

    // Step boundary. With a rolling transport the grid comes from the host's
    // musical position, so the pattern sits on the DAW's beats no matter when
    // the chord was pressed; otherwise fall back to a free-running counter.
    const double ppq = hostPpq.load();
    if (ppq >= 0.0 && hostPlaying.load())
    {
        const auto step = (int64_t) std::floor (ppq / rateMult[rate]);
        if (step == arpLastStep)
            return;
        arpLastStep = step;
    }
    else
    {
        arpLastStep = std::numeric_limits<int64_t>::min();

        arpStepSamples -= numSamples;
        if (arpStepSamples > 0)
            return;

        arpStepSamples += stepLen;
        if (arpStepSamples <= 0)      // very long blocks / very fast rates
            arpStepSamples = stepLen;
    }

    // Pick the next note in the pattern.
    int pick = 0;
    switch (mode)
    {
        case 1:  pick = arpIndex % count; ++arpIndex; break;                    // Up
        case 2:  pick = (count - 1) - (arpIndex % count); ++arpIndex; break;    // Down
        case 3:                                                                 // Up-Down
            pick = juce::jlimit (0, count - 1, arpIndex);
            arpIndex += arpDir;
            if (arpIndex >= count) { arpIndex = juce::jmax (0, count - 2); arpDir = -1; }
            else if (arpIndex < 0) { arpIndex = juce::jmin (count - 1, 1);  arpDir =  1; }
            break;
        default: pick = arpRandom.nextInt (count); break;                       // Random
    }

    if (mode != 3 && arpIndex >= count * 4)
        arpIndex %= count;            // keep the counter small

    // Octave stack: every full pass climbs, then wraps. Random has no "full
    // pass" to hang that on, so it just picks an octave per step.
    if (octaves > 1)
        arpOctave = mode == 4 ? arpRandom.nextInt (octaves)
                              : (pick == 0 ? (arpOctave + 1) % octaves : arpOctave);
    else
        arpOctave = 0;

    const int note = juce::jlimit (0, 127, notes[pick] + arpOctave * 12);

    if (arpNote >= 0)
        routeNoteOff (arpNote);

    // Play it as hard as the key that fed it, so the arp still responds to
    // touch instead of flattening every pattern to one velocity.
    routeNoteOn (note, juce::jlimit (0.05f, 1.0f, arpVel[(size_t) notes[pick]]), false);
    arpNote = note;
    arpGateSamples = juce::jmax (16, (int) (stepLen * gate));
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

int VocalChopAudioProcessor::diatonicSliceIndex (int semis)
{
    // Do-re-mi plays slices in cut order: the n-th WHITE key above the root C
    // triggers slice n (so 도레미파솔라시도 walks slices 0..7 in sequence).
    // A black key doubles its lower white neighbour instead of going silent.
    static const int white[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const int oct = semis >= 0 ? semis / 12 : (semis - 11) / 12;
    const int pos = semis - oct * 12;
    return oct * 7 + white[pos];
}

int VocalChopAudioProcessor::triggerSliceIndex (int sliceIndex, float velocity)
{
    // Audio thread. Keys past the last slice WRAP instead of going silent —
    // a demo with 8 slices must still sound on la/si/do (A, B, high C).
    const int numSlices = sliceEngine.getNumSlices();
    if (numSlices > 0)
        sliceIndex = ((sliceIndex % numSlices) + numSlices) % numSlices;

    // tryGetSlice() safely no-ops if a re-slice is in progress.
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
            if (juce::isPositiveAndBelow (note, 128))
                arpHeld[(size_t) note] = false;

            routeNoteOff (note);

            if (juce::isPositiveAndBelow (idx, 128) && padKeyToVoice[(size_t) idx] >= 0)
            {
                voicePool.releaseVoice (padKeyToVoice[(size_t) idx]);
                padKeyToVoice[(size_t) idx] = -1;
            }
            return;
        }

        // With the arp on, a HELD pad feeds the pattern like a held key does.
        // A tap does not: the chord bar fires taps with no matching release,
        // so latching them left the chord stuck in the arp forever. Taps stay
        // one-shot hits and play straight through.
        if (type == padOn && arpModeParam != nullptr && (int) arpModeParam->load() > 0
            && juce::isPositiveAndBelow (note, 128))
        {
            arpHeld[(size_t) note] = true;
            arpVel[(size_t) note]  = juce::jlimit (0.0f, 1.0f, vel);
            return;
        }

        routeNoteOn (note, vel, type != padOn);

        // Chop voices are tracked per pad so a release can stop them.
        if (type == padOn && ! isSynthMode() && sliceEngine.getNumSlices() > 0
            && juce::isPositiveAndBelow (idx, 128))
            padKeyToVoice[(size_t) idx] = noteToVoice[(size_t) note];
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
    fxChain.distortion.process (buffer, effDrive);
    fxChain.reverb.process     (buffer, effReverb);

    // Musical delay divisions, resolved against the host tempo.
    {
        const int   div = delaySyncParam != nullptr ? (int) delaySyncParam->load() : 0;
        const double bpm = hostBpm.load();
        if (div > 0 && bpm > 0.0)
        {
            const double beat = 60.0 / bpm;                    // a quarter note
            static const double mult[] = { 0.0, 1.0, 0.75, 0.5, 0.375, 0.25, 0.125 };
            const double secs = beat * mult[juce::jlimit (0, 6, div)];
            fxChain.delay.setTimeSeconds ((float) secs);
        }
        else if (div == 0 && lastDelayWasSynced)
            fxChain.delay.setTimeSeconds (0.35f);              // back to free
        lastDelayWasSynced = (div > 0 && bpm > 0.0);
    }

    fxChain.delay.setFeedback (delayFeedbackParam->load());
    fxChain.delay.setPingpong (pingpongParam->load() >= 0.5f);
    fxChain.delay.process      (buffer, effDelay);
}

void VocalChopAudioProcessor::applyStereoWidth (juce::AudioBuffer<float>& buffer)
{
    widthSmoothed.setTargetValue (effWidth);

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
    prevSampleBuffer.reset();   // edit-undo must not resurrect the OLD sample
    reassignSampleToEngines();
    rescanSlices();
    analyzeSampleKey();

    // Loading a sample means the user wants to chop it - switch engines so
    // the keyboard immediately plays slices (state restore passes false).
    if (switchEngineToChop)
        if (auto* p = apvts.getParameter ("engine"))
            p->setValueNotifyingHost (0.0f);

    return true;
}

bool VocalChopAudioProcessor::loadDemoSample()
{
    // Built-in vocals; every Demo press cycles to the next one. The last is
    // a human-beatbox loop: sliced, it turns keys into mouth drums.
    struct Embedded { const void* data; int size; };
    static const Embedded demos[] = {
        { BinaryData::vocal_chop_demo_wav, BinaryData::vocal_chop_demo_wavSize },
        { BinaryData::vox_air_wav,         BinaryData::vox_air_wavSize },
        { BinaryData::vox_rage_wav,        BinaryData::vox_rage_wavSize },
        { BinaryData::vox_beatbox_wav,     BinaryData::vox_beatbox_wavSize },
    };
    constexpr int numDemos = (int) (sizeof (demos) / sizeof (demos[0]));

    const auto& d = demos[demoCycle % numDemos];
    ++demoCycle;
    return loadSampleFromMemory (d.data, d.size);
}

bool VocalChopAudioProcessor::loadSampleFromMemory (const void* data, int sizeBytes)
{
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (data, sizeBytes, sr);
    if (buffer == nullptr)
        return false;

    sampleBuffer     = buffer;
    loadedSampleRate = sr;
    loadedSampleFile = juce::File();   // the DEMO is now playing: saving the
                                       // old path would restore a different
                                       // sample than the one heard
    prevSampleBuffer.reset();   // edit-undo must not resurrect the OLD sample
    reassignSampleToEngines();
    rescanSlices();
    analyzeSampleKey();

    if (auto* p = apvts.getParameter ("engine"))
        p->setValueNotifyingHost (0.0f);
    return true;
}

//==============================================================================
bool VocalChopAudioProcessor::editSample (SampleEdit op, float a, float b)
{
    auto src = sampleBuffer;
    if (src == nullptr || src->getNumSamples() < 64)
        return false;

    const int total = src->getNumSamples();
    const int chans = src->getNumChannels();
    const int s = juce::jlimit (0, total, juce::roundToInt (juce::jmin (a, b) * (float) total));
    const int e = juce::jlimit (0, total, juce::roundToInt (juce::jmax (a, b) * (float) total));

    if (op != SampleEdit::Normalize && e - s < 32)
        return false;

    std::shared_ptr<juce::AudioBuffer<float>> out;

    if (op == SampleEdit::Trim)          // keep only the selection
    {
        out = std::make_shared<juce::AudioBuffer<float>> (chans, e - s);
        for (int ch = 0; ch < chans; ++ch)
            out->copyFrom (ch, 0, *src, ch, s, e - s);
    }
    else if (op == SampleEdit::Cut)      // delete the selection, join the rest
    {
        const int len = total - (e - s);
        if (len < 32)
            return false;
        out = std::make_shared<juce::AudioBuffer<float>> (chans, len);
        for (int ch = 0; ch < chans; ++ch)
        {
            if (s > 0)
                out->copyFrom (ch, 0, *src, ch, 0, s);
            if (total - e > 0)
                out->copyFrom (ch, s, *src, ch, e, total - e);
        }
    }
    else if (op == SampleEdit::Fade)
    {
        // Selection in the first half fades IN across it (and silences what
        // comes before); in the second half it fades OUT (and silences the
        // tail) - matching what selecting a head or a tail means.
        out = std::make_shared<juce::AudioBuffer<float>> (chans, total);
        for (int ch = 0; ch < chans; ++ch)
            out->copyFrom (ch, 0, *src, ch, 0, total);

        const bool fadeIn = (s + e) / 2 < total / 2;
        for (int ch = 0; ch < chans; ++ch)
        {
            float* d = out->getWritePointer (ch);
            for (int i = s; i < e; ++i)
            {
                const float t = (float) (i - s) / (float) juce::jmax (1, e - s - 1);
                d[i] *= fadeIn ? t : 1.0f - t;
            }
            if (fadeIn)  for (int i = 0; i < s; ++i)     d[i] = 0.0f;
            else         for (int i = e; i < total; ++i) d[i] = 0.0f;
        }
    }
    else   // Normalize: whole sample to -0.2 dB-ish
    {
        out = std::make_shared<juce::AudioBuffer<float>> (chans, total);
        for (int ch = 0; ch < chans; ++ch)
            out->copyFrom (ch, 0, *src, ch, 0, total);
        const float peak = out->getMagnitude (0, total);
        if (peak > 1.0e-6f)
            out->applyGain (0.98f / peak);
    }

    prevSampleBuffer = sampleBuffer;
    prevSampleRate   = loadedSampleRate;
    sampleBuffer     = out;
    reassignSampleToEngines();
    rescanSlices();
    analyzeSampleKey();
    sendChangeMessage();
    return true;
}

bool VocalChopAudioProcessor::undoSampleEdit()
{
    if (prevSampleBuffer == nullptr)
        return false;
    std::swap (sampleBuffer, prevSampleBuffer);
    std::swap (loadedSampleRate, prevSampleRate);
    reassignSampleToEngines();
    rescanSlices();
    analyzeSampleKey();
    sendChangeMessage();
    return true;
}

//==============================================================================
void VocalChopAudioProcessor::parameterChanged (const juce::String& id, float newValue)
{
    if (id == "pitch")   pitchFormant.setPitch (newValue);
    if (id == "formant") pitchFormant.setFormant (newValue);
}

//==============================================================================
namespace
{
/** A complete patch: which instrument, and how it is dressed. Kept as data so
    adding a preset is one line and can never fall out of sync with the name
    list (getPresetNames builds itself from this table). */
struct SoundPreset
{
    const char* name;
    const char* instrument;
    float drive, reverb, delay, width, pump;
    int   delaySync;   // 0 Free, 1 1/4, 2 1/4., 3 1/8, 4 1/8., 5 1/16, 6 1/32
    int   arpMode, arpRate, arpOct;
};

const SoundPreset kSoundPresets[] = {
//    name                instrument         drv   rev   dly   wid  pump  sync arp rate oct
    { "Festival Supersaw", "Supersaw Lead",  0.15f, 0.30f, 0.22f, 1.0f, 0.70f, 3,  0, 3, 0 },
    { "Club Sub",          "Sub 808",        0.22f, 0.05f, 0.00f, 0.4f, 0.55f, 0,  0, 3, 0 },
    { "Crystal Arp",       "Crystal Pluck",  0.00f, 0.35f, 0.30f, 1.0f, 0.00f, 3,  1, 3, 1 },
    { "Choir Pad",         "Vox Ahh",        0.00f, 0.55f, 0.18f, 1.0f, 0.35f, 3,  0, 3, 0 },
    { "Slide 808",         "Drill Slide",    0.28f, 0.08f, 0.00f, 0.5f, 0.00f, 0,  0, 3, 0 },
    { "Future Chords",     "Future Chords",  0.10f, 0.32f, 0.20f, 1.0f, 0.55f, 3,  0, 3, 0 },
    { "Rave Stab",         "Rave Stab",      0.35f, 0.22f, 0.28f, 0.9f, 0.60f, 5,  0, 3, 0 },
    { "Trap Bell",         "Cloud Bell",     0.05f, 0.42f, 0.34f, 1.0f, 0.00f, 3,  0, 3, 0 },
    { "Amapiano Log",      "Amapiano Log",   0.08f, 0.26f, 0.22f, 0.9f, 0.30f, 3,  0, 3, 0 },
    { "Acid Runner",       "Acid Lead",      0.30f, 0.18f, 0.26f, 0.8f, 0.45f, 5,  1, 3, 1 },
};
} // namespace

juce::StringArray VocalChopAudioProcessor::getPresetNames()
{
    juce::StringArray names { "Init", "Clean Chops", "Vocal Shimmer", "Lo-Fi Tape",
                              "Reverse Swell", "Hard Stutter" };
    for (const auto& sp : kSoundPresets)
        names.add (sp.name);
    return names;
}

int VocalChopAudioProcessor::getNumChopPresets()
{
    return 6;   // Init .. Hard Stutter: FX only, they keep your sample
}

void VocalChopAudioProcessor::applyPreset (int presetIndex)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // SOUND presets are complete patches: they switch the engine to Synth,
    // load an instrument and then dress it. The first tester of the preset
    // menu could not work out why picking a preset never changed the sound -
    // because until now no preset touched the instrument at all.
    const int sound = presetIndex - getNumChopPresets();
    if (sound >= 0 && sound < (int) (sizeof (kSoundPresets) / sizeof (kSoundPresets[0])))
    {
        const auto& sp = kSoundPresets[(size_t) sound];

        set ("engine", 1.0f);                       // Synth
        const int inst = getInstrumentNames().indexOf (sp.instrument);
        if (inst >= 0)
            applyInstrument (inst);                 // sets the voice AND its envelope

        // Dress it. Deliberately applied AFTER applyInstrument so the preset's
        // character wins, and the envelope the instrument chose is left alone.
        set ("drive",   sp.drive);
        set ("reverb",  sp.reverb);
        set ("delay",   sp.delay);
        set ("delaySync", (float) sp.delaySync);
        set ("width",   sp.width);
        set ("pumpAmt", sp.pump);
        set ("pumpRate", 2.0f);                     // 1/4
        set ("arpMode", (float) sp.arpMode);
        set ("arpRate", (float) sp.arpRate);
        set ("arpOct",  (float) sp.arpOct);
        set ("filterType", 0.0f);
        set ("filterCutoff", 20000.0f);
        set ("grainMix", 0.0f);
        set ("reverse", 0.0f);
        set ("playMode", 0.0f);
        set ("pitch", 0.0f);
        set ("formant", 0.0f);
        set ("mix", 1.0f);
        return;
    }

    // Start every preset from a known baseline, then apply the character.
    set ("pitch", 0.0f);      set ("formant", 0.0f);   set ("mix", 1.0f);
    set ("width", 1.0f);      set ("grainSize", 80.0f); set ("grainMix", 0.0f);
    set ("drive", 0.0f);      set ("reverb", 0.0f);    set ("delay", 0.0f);
    set ("attack", 5.0f);     set ("decay", 0.0f);     set ("sustain", 1.0f);
    set ("release", 20.0f);   set ("filterType", 0.0f); set ("filterCutoff", 20000.0f);
    set ("filterReso", 0.707f); set ("delayFeedback", 0.4f); set ("pingpong", 0.0f);
    set ("reverse", 0.0f);    set ("playMode", 0.0f);  set ("outputGain", 0.0f);
    set ("delaySync", 0.0f);   // factory presets start on a free-running delay
    set ("arpMode", 0.0f);     set ("pumpAmt", 0.0f);

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

    { "BASS",  "Donk Bass",     1, 0.00f,0.3f, 0.00f, 0.75f, 1.0f, 0.0f, 0.0f,  1400, 2.5f,   90, 2, -1,  0.0f,   0,  220, 0.10f,  90, 0.30f, 0.06f, 0.00f, 0, 0.8f },
    { "BASS",  "Hoover Bass",   5, 0.80f,0.4f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,   800, 1.5f,  300, 0, -1, 26.0f,   5,  350, 0.75f, 160, 0.35f, 0.10f, 0.00f, 0, 1.0f },
    { "BASS",  "UK Bass",       2, 0.30f,0.7f, 0.00f, 0.30f, 2.0f, 0.0f, 0.0f,   500, 2.0f,  180, 1, -1,  8.0f,   1,  300, 0.45f, 130, 0.25f, 0.06f, 0.00f, 0, 0.8f },
    { "BASS",  "Growl Sub",     1, 0.00f,0.9f, 0.00f, 0.40f, 2.0f, 0.0f, 0.0f,   350, 1.2f,  450, 2, -2,  0.0f,   3,  500, 0.70f, 220, 0.30f, 0.05f, 0.00f, 0, 0.65f },
    { "BASS",  "Metal Bass",    3, 0.40f,0.3f, 0.00f, 0.55f, 3.5f, 0.0f, 0.0f,   600, 2.2f,  240, 0, -1, 14.0f,   1,  320, 0.60f, 140, 0.50f, 0.08f, 0.00f, 0, 0.95f },
    { "BASS",  "Drift Phonk",    1, 0.00f,0.9f, 0.03f, 0.40f, 1.0f, 0.0f, 0.0f,   500, 1.2f,  400, 2, -2,  0.0f,  0, 1000, 0.50f, 400, 0.55f, 0.10f, 0.00f, 0, 0.8f },
    { "BASS",  "Jersey Boom",    1, 0.00f,1.0f, 0.00f, 0.35f, 2.0f, 0.0f, 0.0f,   300, 2.2f,  100, 2, -2,  0.0f,  0,  700, 0.25f, 260, 0.35f, 0.06f, 0.00f, 0, 0.7f },
    { "BASS",  "Hyper Sub",      2, 0.20f,1.0f, 0.00f, 0.20f, 2.0f, 0.0f, 0.0f,   600, 1.8f,  150, 2, -2,  8.0f,  0,  500, 0.60f, 200, 0.30f, 0.05f, 0.00f, 0, 0.8f },
    { "BASS",  "Afro Log Bass",  1, 0.00f,0.85f,0.02f, 0.30f, 1.0f, 0.0f, 0.0f,   420, 1.6f,  180, 2, -2,  0.0f,  0,  600, 0.20f, 320, 0.20f, 0.10f, 0.00f, 0, 0.8f },
    { "BASS",  "Reggaeton Sub",  1, 0.00f,1.00f,0.00f, 0.15f, 2.0f, 0.0f, 0.0f,   260, 1.4f,  120, 2, -2,  0.0f,  0,  520, 0.45f, 240, 0.25f, 0.05f, 0.00f, 0, 0.7f },
    { "BASS",  "Club Rumble",    2, 0.15f,0.90f,0.02f, 0.20f, 2.0f, 0.0f, 0.0f,   340, 1.8f,  260, 2, -2,  5.0f,  0,  800, 0.55f, 380, 0.30f, 0.08f, 0.00f, 0, 0.75f },
    { "BASS",  "Drill Slide",    1, 0.00f,0.95f,0.00f, 0.40f, 1.0f, 5.5f, 22.0f,  300, 2.0f,  200, 2, -2,  0.0f,  0, 1000, 0.45f, 400, 0.35f, 0.06f, 0.00f, 0, 0.7f },
    { "DRUMS", "Drum Kit",     1, 0.00f,0.0f, 0.02f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2, -1,  0.0f,   0,  500, 0.00f, 200, 0.30f, 0.02f, 0.00f, 0, 0.7f },
    { "DRUMS", "Kick 808",      1, 0.0f, 0.0f, 0.02f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2, -1,  0.0f,  0,  500, 0.00f, 200, 0.30f, 0.02f, 0.0f, 0, 0.7f },
    { "DRUMS", "Kick Punch",    1, 0.0f, 0.0f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  4000, 0.0f,  200, 2, -1,  0.0f,  0,  260, 0.00f, 120, 0.45f, 0.02f, 0.0f, 0, 0.7f },
    { "DRUMS", "Snare 808",     1, 0.0f, 0.15f,0.85f, 0.00f, 2.0f, 0.0f, 0.0f,  7000, 0.6f,  120, 2,  0,  0.0f,  0,  220, 0.00f, 140, 0.25f, 0.12f, 0.0f, 0, 1.0f },
    { "DRUMS", "Snare Tight",   1, 0.0f, 0.10f,0.90f, 0.00f, 2.0f, 0.0f, 0.0f,  9000, 0.4f,   90, 2,  0,  0.0f,  0,  150, 0.00f, 100, 0.30f, 0.08f, 0.0f, 0, 1.0f },
    { "DRUMS", "Clap",          1, 0.0f, 0.0f, 1.00f, 0.00f, 2.0f, 0.0f, 0.0f,  6500, 0.3f,  110, 2,  0,  0.0f,  8,  190, 0.00f, 160, 0.20f, 0.22f, 0.0f, 0, 1.15f },
    { "DRUMS", "Hat Closed",    1, 0.0f, 0.0f, 0.75f, 0.90f, 7.31f,0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0,   55, 0.00f,  45, 0.15f, 0.03f, 0.0f, 0, 1.0f },
    { "DRUMS", "Hat Open",      1, 0.0f, 0.0f, 0.75f, 0.90f, 7.31f,0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0,  420, 0.00f, 320, 0.15f, 0.08f, 0.0f, 0, 1.1f },
    { "DRUMS", "Rim Perc",      1, 0.0f, 0.20f,0.35f, 0.60f, 3.7f, 0.0f, 0.0f,  8000, 0.8f,   60, 2,  0,  0.0f,  0,   90, 0.00f,  70, 0.25f, 0.10f, 0.0f, 0, 1.0f },

    { "DRUMS", "Tom Low",       1, 0.00f,0.0f, 0.08f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 2, -1,  0.0f,   0,  420, 0.00f, 180, 0.15f, 0.10f, 0.00f, 0, 0.85f },
    { "DRUMS", "Tom High",      1, 0.00f,0.0f, 0.08f, 0.00f, 2.0f, 0.0f, 0.0f,  3500, 0.0f,  200, 2,  0,  0.0f,   0,  300, 0.00f, 140, 0.12f, 0.10f, 0.00f, 0, 0.9f },
    { "DRUMS", "Perc 909",      1, 0.00f,0.0f, 0.95f, 0.00f, 2.0f, 0.0f, 0.0f,  5000, 0.4f,   90, 2,  0,  0.0f,   0,  160, 0.00f, 110, 0.10f, 0.12f, 0.00f, 0, 1.05f },
    { "DRUMS", "Cowbell 808",   1, 0.00f,0.0f, 0.05f, 0.60f, 1.48f,0.0f, 0.0f,  5500, 0.0f,  200, 1,  0,  0.0f,   0,  180, 0.00f, 120, 0.10f, 0.06f, 0.00f, 0, 1.0f },
    { "DRUMS", "Shaker",        1, 0.00f,0.0f, 1.00f, 0.00f, 2.0f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,   5,   90, 0.05f,  70, 0.00f, 0.06f, 0.00f, 0, 1.0f },
    { "DRUMS", "Tambo Jingle",  1, 0.00f,0.0f, 0.95f, 0.55f, 7.3f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,   0,  170, 0.00f, 130, 0.00f, 0.15f, 0.00f, 0, 1.1f },
    { "DRUMS", "Syn Conga",     1, 0.00f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2800, 0.0f,  200, 2,  0,  0.0f,   0,  210, 0.00f, 110, 0.08f, 0.10f, 0.00f, 0, 0.9f },
    { "DRUMS", "Clave",         1, 0.00f,0.0f, 0.04f, 0.00f, 2.0f, 0.0f, 0.0f,  2500, 0.0f,  200, 2,  1,  0.0f,   0,   90, 0.00f,  70, 0.05f, 0.10f, 0.00f, 0, 0.9f },
    { "DRUMS", "Woodblock",      1, 0.00f,0.0f, 0.10f, 0.30f, 5.0f, 0.0f, 0.0f,  3500, 0.8f,   40, 3,  0,  0.0f,  0,   90, 0.00f,  70, 0.10f, 0.10f, 0.00f, 0, 0.9f },
    { "DRUMS", "Finger Snap",    1, 0.00f,0.0f, 0.85f, 0.00f, 2.0f, 0.0f, 0.0f,  2800, 1.2f,   50, 2,  0,  0.0f,  0,  110, 0.00f,  90, 0.15f, 0.12f, 0.00f, 0, 1.0f },
    { "DRUMS", "Hat Tight",      1, 0.00f,0.0f, 0.70f, 0.95f, 8.7f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0,   32, 0.00f,  26, 0.18f, 0.02f, 0.00f, 0, 0.9f },
    { "DRUMS", "Hat Roll",       1, 0.00f,0.0f, 0.72f, 0.92f, 9.3f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  2,  0.0f,  0,   22, 0.00f,  18, 0.20f, 0.02f, 0.00f, 0, 0.85f },
    { "DRUMS", "Hat Pedal",      1, 0.00f,0.0f, 0.65f, 0.85f, 6.1f, 0.0f, 0.0f, 12000, 0.6f,   80, 2,  1,  0.0f,  0,   70, 0.00f,  55, 0.14f, 0.03f, 0.00f, 0, 0.9f },
    { "DRUMS", "Hat Sizzle",     1, 0.00f,0.0f, 0.80f, 0.88f, 11.0f,0.0f, 0.0f, 20000, 0.0f,  200, 2,  2,  0.0f,  0,  700, 0.10f, 520, 0.10f, 0.14f, 0.05f, 0, 1.2f },
    { "DRUMS", "Hat Trap Open",  1, 0.00f,0.0f, 0.78f, 0.90f, 7.9f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0,  620, 0.00f, 480, 0.22f, 0.10f, 0.00f, 0, 1.15f },
    { "DRUMS", "Ride Ping",      1, 0.00f,0.0f, 0.45f, 0.80f, 5.4f, 0.0f, 0.0f, 14000, 0.3f,  400, 2,  1,  0.0f,  0,  900, 0.20f, 800, 0.08f, 0.20f, 0.05f, 0, 1.2f },
    { "DRUMS", "Crash Splash",   1, 0.00f,0.0f, 0.95f, 0.70f, 6.8f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0, 1400, 0.05f,1200, 0.12f, 0.30f, 0.10f, 0, 1.4f },
    { "DRUMS", "Tambourine",     1, 0.00f,0.0f, 0.88f, 0.60f, 9.9f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,  0,  160, 0.00f, 130, 0.10f, 0.12f, 0.00f, 0, 1.1f },
    { "DRUMS", "Perc Click",     1, 0.00f,0.0f, 0.25f, 0.55f, 6.5f, 0.0f, 0.0f, 11000, 1.0f,   30, 2,  1,  0.0f,  0,   40, 0.00f,  32, 0.20f, 0.06f, 0.00f, 0, 0.95f },
    { "DRUMS", "Rim Snap",       1, 0.00f,0.10f,0.55f, 0.45f, 4.2f, 0.0f, 0.0f,  9000, 0.9f,   45, 2,  0,  0.0f,  0,   80, 0.00f,  60, 0.28f, 0.10f, 0.00f, 0, 1.0f },
    { "DRUMS", "Amapiano Shaker",1, 0.00f,0.0f, 0.92f, 0.40f, 8.1f, 0.0f, 0.0f, 18000, 0.0f,  200, 2,  1,  0.0f,  0,  120, 0.00f,  95, 0.08f, 0.10f, 0.00f, 0, 1.1f },
    { "DRUMS", "Afro Conga",     1, 0.00f,0.25f,0.20f, 0.35f, 2.4f, 0.0f, 0.0f,  2600, 1.4f,   90, 2, -1,  0.0f,  0,  260, 0.00f, 200, 0.18f, 0.10f, 0.00f, 0, 0.95f },
    { "DRUMS", "Reggaeton Perc", 1, 0.00f,0.15f,0.30f, 0.50f, 3.1f, 0.0f, 0.0f,  5200, 1.1f,   70, 2,  0,  0.0f,  0,  150, 0.00f, 120, 0.22f, 0.12f, 0.00f, 0, 1.0f },
    { "VOCAL", "Vox Choir",     5, 0.85f,0.0f, 0.08f, 0.00f, 2.0f, 4.5f, 8.0f,  1800, 0.4f,  500, 0,  0, 12.0f, 260,  700, 0.85f, 700, 0.00f, 0.55f, 0.00f, 0, 1.5f },
    { "VOCAL", "Vox Ahh",       4, 0.70f,0.0f, 0.06f, 0.00f, 2.0f, 5.0f,10.0f,  2600, 0.6f,  300, 0,  0, 10.0f, 120,  500, 0.85f, 450, 0.00f, 0.45f, 0.00f, 0, 1.35f },
    { "VOCAL", "Vox Ooh",       3, 0.60f,0.0f, 0.05f, 0.00f, 2.0f, 4.5f, 8.0f,   950, 0.4f,  350, 3,  0,  8.0f, 150,  600, 0.85f, 500, 0.00f, 0.45f, 0.00f, 0, 1.25f },
    { "VOCAL", "Vox Lead",      2, 0.30f,0.0f, 0.04f, 0.15f, 2.0f, 5.5f,14.0f,  2800, 1.0f,  220, 0,  0,  7.0f,  25,  300, 0.90f, 260, 0.10f, 0.30f, 0.10f, 0, 1.1f },
    { "VOCAL", "Vox Pluck",     3, 0.50f,0.0f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  2400, 2.0f,  120, 0,  0,  8.0f,   0,  240, 0.05f, 220, 0.05f, 0.35f, 0.12f, 0, 1.2f },
    { "VOCAL", "Vox Stab",      5, 0.70f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2600, 1.5f,  180, 0,  0, 12.0f,   2,  320, 0.15f, 240, 0.05f, 0.40f, 0.15f, 0, 1.3f },
    { "VOCAL", "Chop Vox",      4, 0.60f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 1.8f,  140, 0,  0, 10.0f,   2,  180, 0.10f, 160, 0.08f, 0.30f, 0.20f, 0, 1.25f },
    { "VOCAL", "Robot Vox",     1, 0.00f,0.0f, 0.02f, 0.55f, 1.0f, 0.0f, 0.0f,  1600, 1.0f,  260, 1,  0,  0.0f,   5,  260, 0.70f, 180, 0.25f, 0.20f, 0.10f, 0, 1.0f },
    { "VOCAL", "Talkbox Vox",   1, 0.00f,0.1f, 0.00f, 0.50f, 3.0f, 5.0f,10.0f,  1200, 1.2f,  250, 3,  0,  0.0f,   8,  300, 0.65f, 200, 0.30f, 0.25f, 0.05f, 0, 1.0f },
    { "VOCAL", "Vox Hum",       1, 0.00f,0.15f,0.04f, 0.00f, 2.0f, 4.0f, 6.0f,   750, 0.3f,  400, 2,  0,  0.0f, 200,  600, 0.90f, 500, 0.00f, 0.35f, 0.00f, 0, 1.0f },
    { "VOCAL", "Whisper Air",   2, 0.60f,0.0f, 0.60f, 0.00f, 2.0f, 0.0f, 0.0f,  3000, 0.0f,  200, 0,  0, 10.0f, 300,  900, 0.80f, 900, 0.00f, 0.70f, 0.10f, 0, 1.6f },
    { "VOCAL", "Angel Choir",   6, 0.90f,0.0f, 0.10f, 0.00f, 2.0f, 4.0f, 7.0f,  2200, 0.3f,  700, 0,  0, 14.0f, 500, 1000, 0.85f,1100, 0.00f, 0.75f, 0.10f, 0, 1.6f },
    { "VOCAL", "Beatbox Kick",  1, 0.00f,0.0f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,  2500, 0.0f,  200, 2, -1,  0.0f,   0,  320, 0.00f, 140, 0.20f, 0.03f, 0.00f, 0, 0.7f },
    { "VOCAL", "Beatbox Snare", 1, 0.00f,0.1f, 0.95f, 0.00f, 2.0f, 0.0f, 0.0f,  4500, 0.5f,  100, 2,  0,  0.0f,   0,  200, 0.00f, 120, 0.10f, 0.10f, 0.00f, 0, 1.0f },
    { "VOCAL", "Beatbox Hat",   1, 0.00f,0.0f, 1.00f, 0.00f, 2.0f, 0.0f, 0.0f, 20000, 0.0f,  200, 2,  1,  0.0f,   0,   70, 0.00f,  55, 0.05f, 0.02f, 0.00f, 0, 1.0f },

    { "VOCAL", "Diva Vox",      5, 0.75f,0.0f, 0.05f, 0.00f, 2.0f, 5.5f,12.0f,  3200, 0.8f,  250, 0,  0, 11.0f,  60,  400, 0.85f, 380, 0.05f, 0.40f, 0.10f, 0, 1.4f },
    { "VOCAL", "Deep Choir",    5, 0.85f,0.15f,0.07f, 0.00f, 2.0f, 4.0f, 7.0f,  1200, 0.3f,  600, 0, -1, 12.0f, 350,  800, 0.85f, 850, 0.00f, 0.60f, 0.00f, 0, 1.5f },
    { "VOCAL", "Vox Ahh Wide",   6, 0.90f,0.0f, 0.07f, 0.00f, 2.0f, 4.8f, 9.0f,  2400, 0.5f,  450, 0,  0, 14.0f, 200,  650, 0.85f, 600, 0.00f, 0.50f, 0.05f, 0, 1.55f },
    { "VOCAL", "Vox Hum Ooh",    2, 0.30f,0.2f, 0.05f, 0.00f, 2.0f, 4.2f, 6.0f,   800, 0.3f,  500, 2,  0,  4.0f, 250,  700, 0.90f, 600, 0.00f, 0.35f, 0.00f, 0, 1.1f },
    { "VOCAL", "Vox Chant Low",  4, 0.65f,0.2f, 0.06f, 0.00f, 2.0f, 4.0f, 6.0f,  1400, 0.4f,  550, 0, -1, 10.0f, 300,  750, 0.85f, 700, 0.00f, 0.50f, 0.00f, 0, 1.4f },
    { "VOCAL", "Vox Doo Choir",  4, 0.70f,0.1f, 0.05f, 0.00f, 2.0f, 4.5f, 8.0f,  1100, 0.5f,  350, 3,  0,  9.0f, 100,  550, 0.85f, 450, 0.00f, 0.40f, 0.05f, 0, 1.3f },
    { "VOCAL", "Vox Siren Air",  3, 0.60f,0.0f, 0.30f, 0.00f, 2.0f, 5.5f,20.0f,  2800, 0.6f,  300, 0,  1, 12.0f, 150,  800, 0.80f, 750, 0.00f, 0.60f, 0.15f, 0, 1.5f },
    { "HITS",  "Neon 84 Lead",  5, 0.70f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  4500, 0.6f,  250, 0,  0, 14.0f,   5,  300, 0.80f, 250, 0.15f, 0.35f, 0.20f, 0, 1.4f },
    { "HITS",  "Trap Flute",    1, 0.00f,0.0f, 0.10f, 0.00f, 2.0f, 4.5f, 9.0f,  3200, 0.3f,  300, 2,  0,  0.0f,  60,  250, 0.85f, 300, 0.00f, 0.40f, 0.15f, 0, 1.1f },
    { "HITS",  "Moody Keys",    2, 0.30f,0.05f,0.02f, 0.15f, 2.0f, 0.0f, 0.0f,  1800, 0.9f,  320, 3,  0,  6.0f,   8,  900, 0.35f, 700, 0.05f, 0.50f, 0.20f, 0, 1.25f },
    { "HITS",  "Isla Pluck",    2, 0.30f,0.0f, 0.02f, 0.35f, 3.0f, 0.0f, 0.0f,  2800, 2.0f,  120, 0,  0,  7.0f,   0,  260, 0.05f, 220, 0.08f, 0.30f, 0.15f, 0, 1.15f },
    { "HITS",  "Tropic Flute",  1, 0.00f,0.0f, 0.08f, 0.30f, 2.0f, 4.0f, 7.0f,  2600, 0.5f,  220, 2,  0,  0.0f,  25,  300, 0.75f, 260, 0.00f, 0.35f, 0.20f, 1, 1.2f },
    { "HITS",  "Whisper Bass",  1, 0.00f,0.9f, 0.02f, 0.20f, 1.0f, 0.0f, 0.0f,   300, 1.0f,  300, 2, -2,  0.0f,   4,  500, 0.60f, 250, 0.10f, 0.05f, 0.00f, 0, 0.7f },
    { "HITS",  "Chart 808",     1, 0.00f,0.9f, 0.00f, 0.40f, 1.0f, 0.0f, 0.0f,   500, 1.8f,  200, 2, -2,  0.0f,   0, 1000, 0.35f, 350, 0.35f, 0.05f, 0.00f, 0, 0.75f },
    { "HITS",  "Emo Lead",      3, 0.50f,0.0f, 0.03f, 0.20f, 2.0f, 5.0f,10.0f,  2600, 0.8f,  300, 0,  0, 12.0f,  30,  400, 0.80f, 400, 0.12f, 0.45f, 0.25f, 0, 1.3f },
    { "HITS",  "Funk Brass",    4, 0.40f,0.1f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 2.2f,  160, 0,  0, 10.0f,   4,  280, 0.55f, 180, 0.25f, 0.20f, 0.05f, 0, 1.2f },
    { "HITS",  "Boogie Bass",   1, 0.00f,0.5f, 0.00f, 0.45f, 2.0f, 0.0f, 0.0f,   900, 2.6f,  110, 1, -1,  0.0f,   1,  220, 0.20f, 100, 0.20f, 0.06f, 0.00f, 0, 0.85f },
    { "HITS",  "Sunny Pluck",   3, 0.50f,0.0f, 0.00f, 0.25f, 2.0f, 0.0f, 0.0f,  3000, 1.8f,  140, 3,  0,  9.0f,   2,  320, 0.10f, 280, 0.05f, 0.40f, 0.25f, 0, 1.3f },
    { "HITS",  "Stadium Lead",  6, 0.80f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  5500, 0.8f,  220, 0,  0, 18.0f,   6,  280, 0.80f, 260, 0.18f, 0.35f, 0.20f, 0, 1.5f },
    { "HITS",  "Wave Keys",     2, 0.40f,0.1f, 0.03f, 0.20f, 1.0f, 0.0f, 0.0f,  1500, 0.7f,  400, 3,  0,  8.0f,  15,  700, 0.50f, 550, 0.06f, 0.45f, 0.15f, 0, 1.25f },
    { "HITS",  "Retro Pop Poly",4, 0.60f,0.1f, 0.02f, 0.00f, 2.0f, 4.0f, 5.0f,  2400, 0.4f,  450, 0,  0, 12.0f,  90,  600, 0.80f, 600, 0.00f, 0.45f, 0.10f, 0, 1.4f },
    { "HITS",  "Anthem Brass",  5, 0.50f,0.15f,0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  1800, 1.8f,  220, 0,  0, 14.0f,   8,  320, 0.75f, 220, 0.30f, 0.25f, 0.05f, 0, 1.3f },
    { "HITS",  "Smooth EP",     1, 0.00f,0.1f, 0.00f, 0.35f, 1.0f, 0.0f, 0.0f,  1700, 0.9f,  300, 2,  0,  4.0f,   6,  850, 0.45f, 480, 0.04f, 0.30f, 0.10f, 0, 1.15f },
    { "HITS",  "Hit Pad",       5, 0.80f,0.1f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  1600, 0.3f,  700, 0,  0, 13.0f, 350,  900, 0.85f, 900, 0.00f, 0.60f, 0.10f, 0, 1.5f },
    { "HITS",  "K-Pop Pluck",   4, 0.60f,0.0f, 0.02f, 0.30f, 3.0f, 0.0f, 0.0f,  3400, 2.2f,  110, 0,  0, 10.0f,   0,  240, 0.08f, 200, 0.10f, 0.35f, 0.25f, 1, 1.35f },

    { "HITS",  "Hook Marimba",  1, 0.00f,0.0f, 0.02f, 0.50f, 4.0f, 0.0f, 0.0f,  2800, 1.5f,  120, 2,  0,  0.0f,   0,  300, 0.05f, 240, 0.05f, 0.30f, 0.12f, 0, 1.2f },
    { "HITS",  "Log Drum",      1, 0.00f,0.6f, 0.00f, 0.50f, 1.0f, 0.0f, 0.0f,   800, 2.0f,   90, 2, -1,  0.0f,   0,  350, 0.10f, 200, 0.15f, 0.10f, 0.00f, 0, 0.9f },
    { "HITS",  "Future Chords", 7, 0.90f,0.1f, 0.02f, 0.00f, 2.0f, 0.0f, 0.0f,  3500, 0.8f,  300, 0,  0, 20.0f,  15,  400, 0.60f, 350, 0.10f, 0.40f, 0.15f, 0, 1.6f },
    { "HITS",  "Cloud Bell",    2, 0.30f,0.0f, 0.02f, 0.45f, 3.5f, 0.0f, 0.0f,  4000, 0.3f,  400, 2,  0,  6.0f,   4,  900, 0.10f, 800, 0.00f, 0.60f, 0.25f, 1, 1.4f },
    { "HITS",  "Phonk Cowbell", 1, 0.00f,0.1f, 0.04f, 0.60f, 1.48f,0.0f, 0.0f,  3500, 0.4f,  150, 1,  0,  0.0f,   0,  200, 0.08f, 130, 0.30f, 0.10f, 0.05f, 0, 1.0f },
    { "HITS",  "Piano Stab",    2, 0.30f,0.05f,0.02f, 0.20f, 2.0f, 0.0f, 0.0f,  3000, 1.2f,  200, 3,  0,  6.0f,   2,  350, 0.10f, 260, 0.08f, 0.30f, 0.10f, 0, 1.25f },
    { "HITS",  "Dancehall Pluck",1,0.00f,0.1f, 0.02f, 0.30f, 2.0f, 0.0f, 0.0f,  2400, 1.8f,  130, 1,  0,  0.0f,   0,  220, 0.06f, 180, 0.10f, 0.25f, 0.15f, 0, 1.1f },
    { "HITS",  "Drill 808",     1, 0.00f,0.9f, 0.00f, 0.35f, 1.0f, 0.0f, 0.0f,   450, 1.5f,  250, 2, -2,  0.0f,   0,  900, 0.40f, 320, 0.30f, 0.04f, 0.00f, 0, 0.75f },
    { "HITS",  "Slap House",    1, 0.00f,0.7f, 0.00f, 0.40f, 2.0f, 0.0f, 0.0f,   700, 2.2f,  130, 1, -1,  0.0f,   1,  240, 0.20f, 110, 0.25f, 0.06f, 0.00f, 0, 0.85f },
    { "HITS",  "Sped-Up Pluck", 3, 0.50f,0.0f, 0.02f, 0.30f, 2.0f, 0.0f, 0.0f,  3400, 2.4f,   90, 0,  0,  9.0f,   0,  160, 0.05f, 140, 0.10f, 0.30f, 0.20f, 1, 1.3f },
    { "HITS",  "Tape Rewind",    2, 0.40f,0.0f, 0.15f, 0.50f, 3.5f, 6.0f,25.0f,  2000, 1.5f,  600, 0,  0, 20.0f, 100,  700, 0.30f, 500, 0.20f, 0.40f, 0.20f, 1, 1.3f },
    { "HITS",  "Laser Zap",      1, 0.00f,0.0f, 0.05f, 0.80f, 7.0f, 0.0f, 0.0f,  6000, 3.0f,   90, 2,  1,  0.0f,  0,  200, 0.00f, 150, 0.15f, 0.25f, 0.15f, 1, 1.1f },
    { "HITS",  "Riser Sweep",    4, 0.70f,0.00f,0.35f, 0.20f, 2.0f, 7.0f, 40.0f, 6000, 3.0f, 1500, 0,  0, 20.0f,900, 1800, 0.60f,1200, 0.20f, 0.45f, 0.20f, 1, 1.45f },
    { "HITS",  "Downlifter",     3, 0.60f,0.30f,0.30f, 0.25f, 2.0f, 0.0f,  0.0f, 4000, 4.0f,  900, 0, -1, 18.0f,  0, 1400, 0.20f, 900, 0.25f, 0.40f, 0.15f, 1, 1.4f },
    { "HITS",  "Vocal Chop Hit", 4, 0.65f,0.00f,0.06f, 0.00f, 2.0f, 5.0f, 10.0f, 2600, 1.6f,  200, 0,  0, 12.0f,  2,  300, 0.20f, 240, 0.08f, 0.38f, 0.18f, 1, 1.3f },
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

    { "LEAD",  "Laser Lead",    1, 0.00f,0.0f, 0.00f, 0.50f, 4.0f, 6.0f,10.0f,  6000, 2.0f,  150, 0,  0,  4.0f,   1,  140, 0.65f, 110, 0.20f, 0.18f, 0.20f, 0, 1.0f },
    { "LEAD",  "Whistle Lead",  1, 0.00f,0.0f, 0.02f, 0.00f, 2.0f, 5.5f,12.0f,  8000, 0.0f,  200, 2,  1,  0.0f,  40,  200, 0.85f, 180, 0.00f, 0.25f, 0.10f, 0, 1.0f },
    { "LEAD",  "Saw Stack",     7, 0.90f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  7000, 0.5f,  250, 0,  0, 20.0f,   8,  220, 0.80f, 220, 0.15f, 0.25f, 0.15f, 0, 1.5f },
    { "LEAD",  "Festival Lead", 6, 0.80f,0.3f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  5000, 1.0f,  200, 0,  0, 18.0f,   3,  260, 0.75f, 240, 0.20f, 0.30f, 0.20f, 0, 1.45f },
    { "LEAD",  "Neon Lead",     3, 0.50f,0.0f, 0.00f, 0.25f, 2.0f, 5.0f, 8.0f,  3500, 1.2f,  220, 1,  0, 10.0f,  15,  300, 0.80f, 260, 0.15f, 0.30f, 0.25f, 0, 1.2f },
    { "LEAD",  "Phonk Whistle",  1, 0.00f,0.0f, 0.00f, 0.30f, 4.0f, 5.5f,18.0f,  4500, 0.6f,  200, 2,  1,  0.0f, 10,  300, 0.80f, 250, 0.10f, 0.35f, 0.15f, 0, 1.0f },
    { "LEAD",  "Drill Flute",    1, 0.00f,0.0f, 0.08f, 0.15f, 2.0f, 5.0f,10.0f,  3200, 0.4f,  250, 3,  1,  0.0f, 30,  350, 0.75f, 300, 0.05f, 0.40f, 0.20f, 0, 1.1f },
    { "LEAD",  "Rave Hoover",    5, 0.80f,0.2f, 0.05f, 0.00f, 2.0f, 0.0f, 0.0f,  1500, 1.8f,  350, 0,  0, 35.0f,  5,  400, 0.70f, 250, 0.45f, 0.20f, 0.10f, 0, 1.3f },
    { "LEAD",  "Afro Marimba",   2, 0.25f,0.05f,0.02f, 0.55f, 3.0f, 0.0f, 0.0f,  3200, 1.6f,  180, 2,  0,  5.0f,  0,  420, 0.05f, 320, 0.05f, 0.28f, 0.12f, 0, 1.15f },
    { "LEAD",  "Amapiano Lead",  3, 0.45f,0.10f,0.03f, 0.30f, 2.0f, 4.5f,  6.0f, 2600, 1.2f,  260, 0,  0, 12.0f,  6,  380, 0.55f, 300, 0.10f, 0.35f, 0.18f, 1, 1.3f },
    { "LEAD",  "K-Pop Saw",      6, 0.80f,0.15f,0.02f, 0.00f, 2.0f, 4.8f,  7.0f, 3600, 1.4f,  240, 0,  0, 24.0f,  4,  300, 0.75f, 260, 0.22f, 0.30f, 0.15f, 0, 1.45f },
    { "LEAD",  "Hyperpop Squeak",2, 0.30f,0.00f,0.02f, 0.70f, 5.0f, 6.5f, 26.0f, 5200, 1.0f,  160, 1,  1,  9.0f,  2,  220, 0.60f, 180, 0.35f, 0.30f, 0.22f, 1, 1.25f },
    { "LEAD",  "Drill Bell Lead",2, 0.25f,0.00f,0.01f, 0.75f, 7.0f, 0.0f,  0.0f, 4800, 0.9f,  300, 2,  1,  6.0f,  0,  700, 0.10f, 560, 0.08f, 0.40f, 0.20f, 1, 1.3f },
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
    { "SYNTH", "Soul Chords",    3, 0.5f, 0.1f, 0.00f, 0.20f, 1.0f, 0.0f, 0.0f,  2200, 0.6f,  400, 3,  0,  8.0f, 10,  700, 0.45f, 400, 0.05f, 0.35f, 0.15f, 0, 1.35f },

    { "SYNTH", "Analog Strings",5, 0.80f,0.0f, 0.04f, 0.00f, 2.0f, 4.5f, 6.0f,  2600, 0.2f,  400, 0,  0, 14.0f, 280,  700, 0.85f, 700, 0.00f, 0.45f, 0.00f, 0, 1.45f },
    { "SYNTH", "Digital Ice",   3, 0.60f,0.0f, 0.03f, 0.45f, 6.0f, 0.0f, 0.0f,  6000, 0.3f,  300, 2,  1,  8.0f,  40,  500, 0.70f, 500, 0.00f, 0.50f, 0.25f, 1, 1.4f },
    { "SYNTH", "Glass Sync",    2, 0.40f,0.0f, 0.00f, 0.55f, 3.0f, 0.0f, 0.0f,  4500, 1.5f,  280, 1,  0,  9.0f,  10,  380, 0.60f, 300, 0.20f, 0.35f, 0.15f, 0, 1.2f },
    { "SYNTH", "Warm Poly",     3, 0.50f,0.15f,0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2200, 0.6f,  320, 0,  0, 10.0f,  45,  450, 0.80f, 420, 0.05f, 0.30f, 0.10f, 0, 1.25f },
    { "SYNTH", "Hard Sync",     2, 0.30f,0.0f, 0.00f, 0.65f, 2.5f, 0.0f, 0.0f,  3000, 2.2f,  240, 0,  0, 12.0f,   2,  260, 0.55f, 180, 0.35f, 0.20f, 0.10f, 0, 1.1f },
    { "SYNTH", "Trance Gate",    5, 0.70f,0.1f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2500, 1.2f,  200, 0,  0, 18.0f,  2,  180, 0.60f, 160, 0.15f, 0.30f, 0.25f, 1, 1.3f },
    { "SYNTH", "Vapor Wash",     4, 0.85f,0.1f, 0.10f, 0.00f, 2.0f, 3.5f, 9.0f,  1400, 0.5f,  700, 3,  0, 16.0f, 400,  900, 0.80f, 900, 0.05f, 0.65f, 0.20f, 1, 1.5f },
    { "SYNTH", "Future Chord",   5, 0.75f,0.10f,0.02f, 0.20f, 2.0f, 4.2f,  6.0f, 2800, 1.5f,  260, 0,  0, 20.0f,  6,  420, 0.65f, 340, 0.18f, 0.35f, 0.15f, 1, 1.4f },
    { "SYNTH", "Drill Dark Pad", 4, 0.70f,0.20f,0.05f, 0.15f, 2.0f, 3.0f,  5.0f, 1200, 0.6f,  700, 0, -1, 16.0f,300, 1000, 0.80f, 900, 0.10f, 0.50f, 0.10f, 0, 1.4f },
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

    { "PIANO", "Royal Grand",   2, 0.15f,0.08f,0.015f,0.10f, 3.5f, 0.0f, 0.0f,  3200, 1.3f,  260, 3,  0,  2.5f,   1, 1400, 0.22f, 500, 0.02f, 0.30f, 0.04f, 0, 1.2f },
    { "PIANO", "Concert Bright",2, 0.20f,0.05f,0.02f, 0.14f, 3.5f, 0.0f, 0.0f,  4200, 1.5f,  220, 3,  0,  3.0f,   1, 1200, 0.20f, 450, 0.05f, 0.32f, 0.05f, 0, 1.25f },
    { "PIANO", "Felt Piano",     1, 0.10f,0.10f,0.03f, 0.10f, 2.0f, 0.0f, 0.0f,  1500, 1.2f,  400, 3,  0,  2.0f,  1,  900, 0.30f, 500, 0.00f, 0.35f, 0.05f, 0, 1.0f },
    { "PIANO", "Toy Piano",      1, 0.00f,0.0f, 0.02f, 0.55f, 5.0f, 0.0f, 0.0f,  4000, 1.0f,  200, 2,  1,  0.0f,  0,  600, 0.10f, 400, 0.05f, 0.30f, 0.10f, 0, 0.9f },
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

    { "GUITAR","Pop Mute Gtr",  1, 0.00f,0.05f,0.03f, 0.30f, 2.0f, 0.0f, 0.0f,  2100, 1.6f,  110, 3,  0,  0.0f,   0,  190, 0.04f, 150, 0.10f, 0.18f, 0.08f, 0, 1.05f },
    { "GUITAR","Tropic Gtr",    2, 0.25f,0.0f, 0.02f, 0.35f, 2.0f, 0.0f, 0.0f,  2600, 1.4f,  140, 3,  0,  5.0f,   0,  260, 0.06f, 220, 0.06f, 0.28f, 0.12f, 0, 1.15f },
    { "GUITAR","Syn Acoustic",  2, 0.20f,0.0f, 0.04f, 0.30f, 3.0f, 0.0f, 0.0f,  3400, 1.1f,  180, 3,  0,  4.0f,   1,  700, 0.12f, 380, 0.03f, 0.25f, 0.06f, 0, 1.2f },
    { "GUITAR","Muted Chug",     1, 0.00f,0.2f, 0.03f, 0.25f, 2.0f, 0.0f, 0.0f,   900, 2.5f,   70, 0, -1,  3.0f,  0,  120, 0.10f,  90, 0.45f, 0.05f, 0.00f, 0, 0.8f },
    { "GUITAR","Nylon Soft",     1, 0.10f,0.05f,0.02f, 0.20f, 2.0f, 0.0f, 0.0f,  1800, 1.5f,  200, 3,  0,  2.0f,  1,  700, 0.15f, 450, 0.00f, 0.30f, 0.06f, 0, 1.0f },
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
    { "PAD",   "Soul Pad",       4, 0.7f, 0.2f, 0.05f, 0.15f, 1.0f, 0.0f, 0.0f,  1600, 0.4f,  900, 3,  0,  9.0f, 400,  800, 0.85f, 1300, 0.00f, 0.55f, 0.15f, 0, 1.5f },

    { "PAD",   "Nebula Pad",    6, 0.90f,0.1f, 0.08f, 0.00f, 2.0f, 0.0f, 0.0f,  1800, 0.3f,  900, 0,  0, 16.0f, 600, 1200, 0.85f,1200, 0.00f, 0.75f, 0.15f, 1, 1.6f },
    { "PAD",   "Ice Pad",       4, 0.70f,0.0f, 0.06f, 0.35f, 6.0f, 0.0f, 0.0f,  4000, 0.0f,  200, 2,  1, 10.0f, 500, 1000, 0.85f,1100, 0.00f, 0.70f, 0.20f, 1, 1.55f },
    { "PAD",   "Analog Wash",   5, 0.85f,0.2f, 0.05f, 0.00f, 2.0f, 4.0f, 5.0f,  1500, 0.2f,  800, 0,  0, 14.0f, 450,  900, 0.85f, 950, 0.00f, 0.65f, 0.10f, 0, 1.5f },
    { "PAD",   "Deep Space",    3, 0.70f,0.5f, 0.10f, 0.00f, 2.0f, 0.0f, 0.0f,   900, 0.4f, 1200, 0, -1, 12.0f, 700, 1500, 0.85f,1400, 0.00f, 0.80f, 0.20f, 0, 1.55f },
    { "PAD",   "Dusk Pad",      4, 0.75f,0.1f, 0.05f, 0.15f, 2.0f, 4.5f, 6.0f,  1400, 0.3f,  700, 3,  0, 11.0f, 400,  900, 0.85f, 900, 0.00f, 0.60f, 0.10f, 0, 1.45f },
    { "PAD",   "Polar Lights",     6, 0.90f,0.1f, 0.08f, 0.00f, 2.0f, 3.0f, 6.0f,  1600, 0.4f,  900, 0,  0, 20.0f, 900, 1400, 0.85f,1600, 0.00f, 0.70f, 0.15f, 1, 1.6f },
    { "PAD",   "Cinema Swell",   5, 0.80f,0.2f, 0.06f, 0.00f, 2.0f, 0.0f, 0.0f,   800, 1.5f, 1600, 0, -1, 15.0f,1400, 1800, 0.90f,1500, 0.10f, 0.65f, 0.10f, 0, 1.5f },
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
    { "PLUCK", "Soul Pluck",       1, 0.00f,0.05f,0.06f,0.20f,2.0f, 0.0f, 0.0f,  1500, 1.8f,  180, 3,  0,  4.0f,  0,   450, 0.05f, 300, 0.05f, 0.30f, 0.18f, 1, 1.2f },

    { "PLUCK", "Dance Pluck",   3, 0.50f,0.0f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2600, 2.2f,  110, 0,  0, 10.0f,   0,  200, 0.00f, 170, 0.10f, 0.30f, 0.20f, 0, 1.2f },
    { "PLUCK", "Water Pluck",   2, 0.40f,0.0f, 0.02f, 0.40f, 3.0f, 0.0f, 0.0f,  3200, 2.5f,   90, 2,  0,  7.0f,   0,  240, 0.00f, 220, 0.00f, 0.40f, 0.25f, 1, 1.25f },
    { "PLUCK", "Bubble Pluck",  1, 0.00f,0.0f, 0.00f, 0.55f, 2.0f, 0.0f, 0.0f,  2000, 3.0f,   70, 2,  0,  0.0f,   0,  160, 0.00f, 140, 0.05f, 0.30f, 0.20f, 0, 1.1f },
    { "PLUCK", "Glass Pluck",   2, 0.30f,0.0f, 0.00f, 0.50f, 5.0f, 0.0f, 0.0f,  5000, 1.8f,  120, 2,  1,  6.0f,   0,  300, 0.00f, 260, 0.00f, 0.45f, 0.25f, 0, 1.3f },
    { "PLUCK", "Amapiano Log",   1, 0.00f,0.6f, 0.02f, 0.40f, 2.0f, 0.0f, 0.0f,   900, 2.2f,  140, 2, -1,  0.0f,  0,  450, 0.10f, 250, 0.25f, 0.20f, 0.00f, 0, 0.9f },
    { "PLUCK", "Droplet Pluck",    2, 0.40f,0.0f, 0.03f, 0.55f, 3.5f, 0.0f, 0.0f,  2600, 2.5f,  100, 2,  0,  6.0f,  0,  300, 0.05f, 260, 0.05f, 0.45f, 0.25f, 1, 1.2f },
    { "PLUCK", "Jersey Bounce",  2, 0.30f,0.30f,0.02f, 0.45f, 2.0f, 0.0f, 0.0f,  1800, 2.4f,  120, 2, -1,  6.0f,  0,  260, 0.05f, 200, 0.25f, 0.18f, 0.08f, 0, 1.0f },
    { "PLUCK", "Afro Kalimba",   2, 0.35f,0.00f,0.02f, 0.60f, 4.0f, 0.0f, 0.0f,  3400, 1.8f,  160, 2,  0,  5.0f,  0,  520, 0.05f, 400, 0.05f, 0.32f, 0.15f, 1, 1.2f },
    { "PLUCK", "Latin Guitar Pl",2, 0.20f,0.05f,0.03f, 0.25f, 2.0f, 0.0f, 0.0f,  2200, 2.0f,  140, 3,  0,  4.0f,  1,  340, 0.10f, 260, 0.10f, 0.25f, 0.10f, 0, 1.05f },
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
    { "KEYS",  "Soul Keys",        1, 0.00f,0.05f,0.00f,0.35f,2.0f, 0.0f, 0.0f,  3800, 0.5f,  400, 3,  0,  5.0f,  2,   900, 0.30f, 350, 0.08f, 0.35f, 0.12f, 0, 1.2f },
    { "KEYS",  "K-RnB Keys",       2, 0.30f,0.00f,0.10f,0.25f,5.5f, 0.0f, 0.0f,  3000, 0.6f,  350, 2,  0,  6.0f,  3,   800, 0.35f, 320, 0.05f, 0.30f, 0.15f, 1, 1.25f },

    { "KEYS",  "Dream Keys",    3, 0.50f,0.05f,0.02f, 0.10f, 2.0f, 4.0f, 4.0f,  2000, 0.8f,  350, 3,  0,  8.0f,  15,  700, 0.55f, 600, 0.00f, 0.50f, 0.20f, 0, 1.3f },
    { "KEYS",  "Soft EP",       1, 0.00f,0.1f, 0.00f, 0.30f, 1.0f, 0.0f, 0.0f,  1600, 1.0f,  300, 2,  0,  4.0f,   8,  900, 0.40f, 500, 0.05f, 0.30f, 0.10f, 0, 1.1f },
    { "KEYS",  "Night Keys",    2, 0.30f,0.1f, 0.03f, 0.20f, 2.0f, 0.0f, 0.0f,  1400, 0.8f,  400, 3,  0,  6.0f,  12,  800, 0.45f, 550, 0.08f, 0.40f, 0.15f, 0, 1.2f },
    { "KEYS",  "Chord Keys",    4, 0.60f,0.05f,0.00f, 0.15f, 2.0f, 0.0f, 0.0f,  2400, 1.0f,  280, 0,  0, 12.0f,   5,  500, 0.50f, 350, 0.10f, 0.30f, 0.15f, 0, 1.3f },
    { "KEYS",  "Dream Organ",    3, 0.50f,0.35f,0.03f, 0.25f, 1.0f, 0.0f, 0.0f,  2000, 0.3f,  300, 2,  0,  6.0f, 40,  200, 1.00f, 300, 0.05f, 0.45f, 0.10f, 0, 1.3f },
    { "KEYS",  "Amapiano Keys",  3, 0.50f,0.10f,0.02f, 0.30f, 2.0f, 3.5f,  5.0f, 2400, 0.8f,  400, 2,  0,  8.0f, 25,  700, 0.70f, 500, 0.05f, 0.40f, 0.12f, 0, 1.3f },
    { "KEYS",  "RnB Rhodes",     2, 0.35f,0.08f,0.02f, 0.45f, 2.0f, 4.0f,  4.0f, 1900, 1.0f,  350, 2,  0,  5.0f, 12,  900, 0.60f, 650, 0.05f, 0.35f, 0.10f, 0, 1.25f },
    { "KEYS",  "Gospel Organ",   3, 0.45f,0.30f,0.02f, 0.20f, 1.0f, 5.0f,  6.0f, 3000, 0.4f,  200, 2,  0,  6.0f,  8,  200, 1.00f, 180, 0.15f, 0.30f, 0.08f, 0, 1.25f },
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

    { "BELL",  "Crystal Bell",  1, 0.00f,0.0f, 0.00f, 0.55f, 7.0f, 0.0f, 0.0f,  8000, 0.0f,  200, 2,  1,  5.0f,   0,  900, 0.00f, 900, 0.00f, 0.55f, 0.25f, 0, 1.35f },
    { "BELL",  "Toy Bell",      1, 0.00f,0.0f, 0.02f, 0.45f, 4.0f, 0.0f, 0.0f,  6000, 0.0f,  200, 2,  1,  4.0f,   0,  500, 0.00f, 450, 0.00f, 0.35f, 0.15f, 0, 1.15f },
    { "BELL",  "Night Bell",    2, 0.30f,0.0f, 0.00f, 0.40f, 3.5f, 0.0f, 0.0f,  4000, 0.3f,  400, 2,  0,  7.0f,   5, 1100, 0.00f,1000, 0.00f, 0.60f, 0.30f, 1, 1.4f },
    { "BELL",  "Ice Bell",       2, 0.40f,0.0f, 0.02f, 0.65f, 7.0f, 0.0f, 0.0f,  5000, 0.8f,  300, 2,  1,  8.0f,  0, 1200, 0.05f, 900, 0.00f, 0.55f, 0.20f, 1, 1.4f },
    { "BELL",  "Gamelan Glow",   2, 0.30f,0.1f, 0.03f, 0.70f, 3.5f, 0.0f, 0.0f,  3200, 0.6f,  400, 2,  0,  5.0f,  0, 1500, 0.10f,1100, 0.05f, 0.50f, 0.15f, 0, 1.2f },
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
    
    { "MISC",  "Air Horn",      3, 0.40f,0.2f, 0.00f, 0.00f, 2.0f, 0.0f, 0.0f,  2500, 0.5f,  200, 0,  0, 16.0f,  10,  400, 0.90f, 300, 0.40f, 0.25f, 0.10f, 0, 1.2f },
    { "MISC",  "Horror Drone",  4, 0.80f,0.4f, 0.15f, 0.20f, 2.5f, 0.0f, 0.0f,   700, 0.3f, 1500, 0, -1, 22.0f, 800, 1500, 0.90f,1500, 0.20f, 0.70f, 0.20f, 0, 1.5f },
    { "MISC",  "Impact Hit",    2, 0.50f,0.6f, 0.30f, 0.00f, 2.0f, 0.0f, 0.0f,  1200, 1.8f,  300, 0, -1, 18.0f,   0,  900, 0.00f, 700, 0.30f, 0.55f, 0.10f, 0, 1.3f },};

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

void VocalChopAudioProcessor::buildKitPieces()
{
    // C=kick up to B=cowbell; the layout repeats every octave.
    static const char* pieceNames[12] = {
        "Kick 808", "Kick Punch", "Snare 808", "Snare Tight", "Clap",
        "Hat Closed", "Hat Open", "Tom Low", "Tom High", "Rim Perc",
        "Shaker", "Cowbell 808" };

    const auto names = getInstrumentNames();
    auto& p = synthEngine.patch();

    // Write into the INACTIVE page; flip only when every piece is complete.
    const int page = 1 - kitPage.load (std::memory_order_relaxed);

    for (int k = 0; k < 12; ++k)
    {
        const int idx = names.indexOf (pieceNames[k]);
        if (idx < 0)
            continue;

        applyEnginePatch (idx);   // writes the piece into the live patch...
        const auto& d = kInstruments[idx];
        auto& kp = kitPiecesBuf[page][(size_t) k];

        // ...which we snapshot into plain floats the audio thread can use.
        kp.unison  = p.unison.load();        kp.spread   = p.stereoSpread.load();
        kp.sub     = p.subLevel.load();      kp.noise    = p.noiseLevel.load();
        kp.fm      = p.fmAmount.load();      kp.fmRatio  = p.fmRatio.load();
        kp.vibHz   = p.vibRateHz.load();     kp.vibCents = p.vibDepthCents.load();
        kp.fltHz   = p.filterCutoff.load();  kp.fltEnvOct= p.filterEnvOct.load();
        kp.fltEnvMs= p.filterEnvMs.load();   kp.filterQ  = p.filterQ.load();
        kp.drift   = p.driftCents.load();    kp.velFlt   = p.velToFilterOct.load();
        kp.pitchEnvOct = p.pitchEnvOct.load();
        kp.pitchEnvMs  = p.pitchEnvMs.load();
        kp.wave     = d.wave;
        kp.playNote = 60 + d.octave * 12;    // natural drum pitch, key-independent
        kp.atk = d.atk; kp.dec = d.dec; kp.sus = d.sus; kp.rel = d.rel;
    }

    kitPage.store (page, std::memory_order_release);
}

void VocalChopAudioProcessor::kitNoteOn (int note, float velocity, bool tap)
{
    // Audio thread: atomic stores only, no allocations. The voice snapshots
    // everything at start, so each drum keeps its sound while others ring.
    const auto& kp = kitPiecesBuf[kitPage.load (std::memory_order_acquire)]
                                 [(size_t) (((note % 12) + 12) % 12)];
    auto& p = synthEngine.patch();

    p.unison.store (kp.unison);          p.stereoSpread.store (kp.spread);
    p.subLevel.store (kp.sub);           p.noiseLevel.store (kp.noise);
    p.fmAmount.store (kp.fm);            p.fmRatio.store (kp.fmRatio);
    p.vibRateHz.store (kp.vibHz);        p.vibDepthCents.store (kp.vibCents);
    p.filterCutoff.store (kp.fltHz);     p.filterEnvOct.store (kp.fltEnvOct);
    p.filterEnvMs.store (kp.fltEnvMs);   p.filterQ.store (kp.filterQ);
    p.driftCents.store (kp.drift);       p.velToFilterOct.store (kp.velFlt);
    p.pitchEnvOct.store (kp.pitchEnvOct);
    p.pitchEnvMs.store (kp.pitchEnvMs);

    synthEngine.setWave (kp.wave);
    synthEngine.setEnvelope (kp.atk, kp.dec, kp.sus, kp.rel);

    if (tap) synthEngine.tapNote (note, velocity, kp.playNote);
    else     synthEngine.noteOn (note, velocity, kp.playNote);
}

void VocalChopAudioProcessor::applyEnginePatch (int i)
{
    // Drum Kit: build the 12 per-key piece snapshots first (recursive calls
    // guarded), then fall through and apply this row's base patch normally.
    {
        const juce::String nm (kInstruments[juce::jlimit (0, (int) (sizeof (kInstruments) / sizeof (kInstruments[0])) - 1, i)].name);
        if (! buildingKit)
        {
            if (nm == "Drum Kit")
            {
                // Suspend kit mode FIRST: buildKitPieces snapshots each piece
                // by writing the LIVE patch atomics, and a pad hit landing
                // mid-rebuild would start a voice made of two different drums.
                kitMode.store (false);
                buildingKit = true;
                buildKitPieces();
                buildingKit = false;
            }
            kitMode.store (nm == "Drum Kit");
        }
    }

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
    else if (cat == "DRUMS")  { chorus = 0.0f;  p.driftCents = 0.0f;  p.velToFilterOct = 1.2f;
                                p.satAmount = 0.25f; }
    else if (cat == "VOCAL")
    {
        chorus = 0.45f; p.driftCents = 3.5f; p.velToFilterOct = 0.4f;
        p.filterQ = 1.6f;

        // TRUE vowel formants (three bandpass resonances in the synth bus)
        // are what turn "resonant saw" into "voice". Vowel picked from the
        // name; amount leaves ~45% dry so the note keeps its body.
        int vowel = 0;                                              // "ah"
        if      (name.containsIgnoreCase ("Choir"))  vowel = 3;     // "oh"
        else if (name.containsIgnoreCase ("Air")
              || name.containsIgnoreCase ("Breath")
              || name.containsIgnoreCase ("Ooh"))    vowel = 4;     // "oo"
        else if (name.containsIgnoreCase ("Pluck")
              || name.containsIgnoreCase ("Chant"))  vowel = 1;     // "eh"
        else if (name.containsIgnoreCase ("Robot")
              || name.containsIgnoreCase ("Bit"))    vowel = 2;     // "ee"
        p.formantVowel  = vowel;
        p.formantAmount = 0.55f;

        // A voice always carries a little breath and a slow, humane vibrato.
        if (p.noiseLevel.load() < 0.03f) p.noiseLevel = 0.03f;
        if (p.vibDepthCents.load() < 6.0f) { p.vibRateHz = 5.1f; p.vibDepthCents = 6.0f; }
    }
    else if (cat == "HITS")   { chorus = 0.30f; p.driftCents = 3.0f;  p.velToFilterOct = 0.7f; }
    else /* MISC / INIT */    { chorus = 0.12f; p.driftCents = 2.5f;  p.velToFilterOct = 0.6f; }

    if (name == "Acid Lead")     { p.filterQ = 5.5f; p.satAmount = 0.35f; }
    if (name == "Wobble Growl")  p.filterQ = 2.2f;
    if (name == "Neon Bass")     p.filterQ = 1.4f;
    if (name == "Supersaw Lead") chorus = 0.40f;
    if (name == "Chip Lead")     { chorus = 0.0f; p.driftCents = 0.0f; }
    if (name == "Sub 808")       { p.driftCents = 0.5f; chorus = 0.0f; }
    if (name == "Synth Brass")   chorus = 0.30f;
    if (name == "Syn Flute")    chorus = 0.20f;
    if (name == "Robot Vox")    { chorus = 0.10f; p.driftCents = 0.0f; }   // machines don't drift
    if (name.startsWith ("Beatbox")) { chorus = 0.0f; p.driftCents = 0.0f;
                                       p.velToFilterOct = 1.2f; }

    // Percussion pitch drops (the 808 "boo" and snare thwack).
    if (name == "Kick 808")    { p.pitchEnvOct = 2.2f; p.pitchEnvMs = 42.0f; }
    if (name == "Kick Punch")  { p.pitchEnvOct = 3.0f; p.pitchEnvMs = 26.0f; }
    if (name == "Snare 808")   { p.pitchEnvOct = 1.2f; p.pitchEnvMs = 34.0f; }
    if (name == "Snare Tight") { p.pitchEnvOct = 1.5f; p.pitchEnvMs = 22.0f; }
    if (name == "Rim Perc")    { p.pitchEnvOct = 1.8f; p.pitchEnvMs = 16.0f; }
    if (name == "Beatbox Kick")  { p.pitchEnvOct = 1.7f; p.pitchEnvMs = 55.0f; }   // the 'buh' drop
    if (name == "Beatbox Snare") { p.pitchEnvOct = 0.8f; p.pitchEnvMs = 40.0f; }
    if (name == "Tom Low")     { p.pitchEnvOct = 1.0f; p.pitchEnvMs = 70.0f; }
    if (name == "Tom High")    { p.pitchEnvOct = 1.0f; p.pitchEnvMs = 55.0f; }
    if (name == "Syn Conga")   { p.pitchEnvOct = 0.5f; p.pitchEnvMs = 28.0f; }
    if (name == "Clave")       { p.pitchEnvOct = 0.3f; p.pitchEnvMs = 10.0f; }
    if (name == "Chart 808")   { p.pitchEnvOct = 1.8f; p.pitchEnvMs = 50.0f; }
    if (name == "Log Drum")    { p.pitchEnvOct = 0.8f; p.pitchEnvMs = 60.0f; }
    if (name == "Drill 808")   { p.pitchEnvOct = 1.2f; p.pitchEnvMs = 80.0f; }
    if (name == "Slap House")  { p.pitchEnvOct = 0.6f; p.pitchEnvMs = 45.0f; }

    // Flagship grands: hammer chirp, hard velocity->brightness, no wobble.
    if (name == "Royal Grand" || name == "Concert Bright")
    {
        p.pitchEnvOct = 0.04f; p.pitchEnvMs = 6.0f;    // hammer strike transient
        p.velToFilterOct = 1.8f;                        // soft = felt, hard = glass
        p.driftCents = 0.6f;
        chorus = 0.04f;
        p.filterQ = 0.8f;
    }
    if (name == "Impact Hit")  { p.pitchEnvOct = 1.5f; p.pitchEnvMs = 150.0f; }

    // --- New chart-facing voices -------------------------------------------
    // Hats and metal percussion: no drift, no chorus - machines are rigid.
    if (name.startsWith ("Hat ") || name == "Ride Ping" || name == "Crash Splash"
        || name == "Tambourine" || name == "Perc Click" || name == "Amapiano Shaker")
    {
        chorus = 0.0f; p.driftCents = 0.0f; p.velToFilterOct = 1.4f;
    }
    if (name == "Rim Snap")        { p.pitchEnvOct = 1.6f; p.pitchEnvMs = 14.0f; }
    if (name == "Afro Conga")      { p.pitchEnvOct = 0.6f; p.pitchEnvMs = 30.0f; }
    if (name == "Reggaeton Perc")  { p.pitchEnvOct = 0.9f; p.pitchEnvMs = 22.0f; }

    // Sub-bass families keep the 808 "boo" drop.
    if (name == "Afro Log Bass")   { p.pitchEnvOct = 1.4f; p.pitchEnvMs = 70.0f; }
    if (name == "Reggaeton Sub")   { p.pitchEnvOct = 1.6f; p.pitchEnvMs = 55.0f; }
    if (name == "Club Rumble")     { p.pitchEnvOct = 1.2f; p.pitchEnvMs = 90.0f; }
    if (name == "Drill Slide")     { p.pitchEnvOct = 2.4f; p.pitchEnvMs = 130.0f; }  // the glide

    // Hook leads: a touch more resonance so they cut through a mix.
    if (name == "Amapiano Lead")   { p.filterQ = 1.8f; }
    if (name == "Hyperpop Squeak") { p.filterQ = 3.2f; p.satAmount = 0.30f; }
    if (name == "K-Pop Saw")       { chorus = 0.45f; }
    if (name == "Drill Bell Lead") { p.filterQ = 1.4f; }
    if (name == "Riser Sweep")     { p.filterQ = 2.2f; }
    // A downlifter FALLS: the pitch envelope starts high and decays to the
    // played note, so the drop is a positive amount over a long time (the
    // engine clamps the amount to 0..5 octaves, negatives would do nothing).
    if (name == "Downlifter")      { p.pitchEnvOct = 2.5f; p.pitchEnvMs = 900.0f; }

    p.chorusMix = chorus;

    // --- Portamento defaults ------------------------------------------------
    // Only the voices whose whole identity is the SLIDE ship with glide on;
    // everything else starts at zero and the Glide knob is there if wanted.
    float glide = 0.0f;
    if (name == "Drill Slide" || name == "Talk Bass" || name == "Glide Solo"
        || name == "Whistle Lead" || name == "Phonk Whistle" || name == "Acid Lead"
        || name == "Squelch 303" || name == "Wobble Growl")
        glide = 90.0f;

    p.glideMs = glide;

    // Publish the resolved module values for applyInstrument to mirror into
    // the knobs (race-free snapshot; see ModuleDefaults in the header).
    moduleDefaults = { d.unison, d.spread, d.sub, d.noise, d.fm,
                       d.vibCents, chorus, glide };

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
    set ("synthGlide",   moduleDefaults.glideMs);
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
    root.setAttribute ("sfzPath",     loadedSfzFile.getFullPathName());
    root.setAttribute ("sliceMode",   (int) sliceEngine.getMode());
    root.setAttribute ("gridDiv",     sliceEngine.getGridDivision());
    root.setAttribute ("sensitivity", (double) sliceEngine.getSensitivity());
    root.setAttribute ("theme",       ThemeManager::current());
    // Like the instrument: the NAME is authoritative — the theme list grows
    // and indices drift across versions.
    root.setAttribute ("themeName",   ThemeManager::active().name);
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

    // Prefer the saved theme NAME; sessions saved before the Studio themes
    // were prepended carry only an index into the OLD 7-theme list, which
    // now sits shifted by 8.
    {
        int themeIdx = -1;
        auto savedTheme = xml->getStringAttribute ("themeName");
        // Retired themes map to their nearest surviving relative instead of
        // silently landing on whatever index now sits in their old slot.
        if (savedTheme == "Neon Rider" || savedTheme == "Neo-Seoul")
            savedTheme = "Neon Ocean";
        else if (savedTheme == "Sunset")        savedTheme = "Studio Amber";
        else if (savedTheme == "Emerald")       savedTheme = "Studio Mint";
        else if (savedTheme == "Royal Velvet")  savedTheme = "Studio Indigo";
        else if (savedTheme == "Carbon")        savedTheme = "Studio Red";
        else if (savedTheme == "Space Gray")    savedTheme = "Graphite";
        if (savedTheme.isNotEmpty())
        {
            const auto& list = ThemeManager::themes();
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].name == savedTheme)
                    { themeIdx = i; break; }
        }
        if (themeIdx < 0 && xml->hasAttribute ("theme") && savedTheme.isEmpty())
        {
            // Sessions older than the themeName attribute stored a bare index
            // into the ORIGINAL seven-theme list. Resolve it through that
            // list by name (an index shift is meaningless now - the list has
            // been rebuilt twice since).
            static const char* legacy7[] = { "Neon Rider", "Neo-Seoul", "Neon Ocean",
                                             "Silver", "Graphite", "Midnight", "Space Gray" };
            const int li = xml->getIntAttribute ("theme");
            if (juce::isPositiveAndBelow (li, 7))
            {
                juce::String legacyName (legacy7[li]);
                if (legacyName == "Neon Rider" || legacyName == "Neo-Seoul")
                    legacyName = "Neon Ocean";
                else if (legacyName == "Space Gray")
                    legacyName = "Graphite";

                const auto& list = ThemeManager::themes();
                for (int i = 0; i < (int) list.size(); ++i)
                    if (list[(size_t) i].name == legacyName)
                        { themeIdx = i; break; }
            }
        }
        if (themeIdx >= 0)
            ThemeManager::setIndex (themeIdx);
    }

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

    // Restore the SFZ bank (quietly skipped if the files moved).
    const juce::File sfz (xml->getStringAttribute ("sfzPath"));
    if (sfz.existsAsFile())
    {
        juce::String ignored;
        if (samplerEngine.loadSfz (sfz, ignored))
            loadedSfzFile = sfz;
    }

    // A session saved in Sampled mode whose SFZ files are gone would leave
    // every key silent - drop that session back onto the Synth engine.
    if (isSamplerMode() && ! samplerEngine.hasBank())
        if (auto* p = apvts.getParameter ("engine"))
            p->setValueNotifyingHost (p->convertTo0to1 (1.0f));

    // Let an open editor re-sync its combos / theme / cached waveform
    // (sendChangeMessage is async and safe from any thread).
    sendChangeMessage();
}

//==============================================================================
juce::File VocalChopAudioProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Slyce").getChildFile ("Presets");
}

juce::StringArray VocalChopAudioProcessor::getUserPresetNames()
{
    juce::StringArray out;
    auto dir = userPresetFolder();
    if (! dir.isDirectory())
        return out;

    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.slyce"))
        out.add (f.getFileNameWithoutExtension());
    out.sortNatural();
    return out;
}

bool VocalChopAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;

    auto dir = userPresetFolder();
    dir.createDirectory();

    // The parameter tree only: a preset is a SOUND, not a session, so it must
    // not drag someone else's sample path or theme along with it.
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        return xml->writeTo (dir.getChildFile (clean + ".slyce"));
    return false;
}

bool VocalChopAudioProcessor::loadUserPreset (const juce::String& name)
{
    const auto f = userPresetFolder()
                       .getChildFile (juce::File::createLegalFileName (name) + ".slyce");
    if (! f.existsAsFile())
        return false;

    auto xml = juce::parseXML (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    sendChangeMessage();
    return true;
}

//==============================================================================
// Recently loaded SFZ banks: stored app-wide (not per session) so instruments
// the user hunted down once stay one click away in every project.
static juce::File recentSfzStore()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Slyce").getChildFile ("recent_sfz.xml");
}

static std::unique_ptr<juce::XmlElement> readRecentSfz()
{
    if (auto xml = juce::parseXML (recentSfzStore()))
        if (xml->hasTagName ("RecentSfz"))
            return xml;
    return nullptr;
}

juce::StringArray VocalChopAudioProcessor::getRecentSfzNames()
{
    juce::StringArray out;
    if (auto xml = readRecentSfz())
        for (auto* e : xml->getChildIterator())
            out.add (e->getStringAttribute ("name"));
    return out;
}

juce::StringArray VocalChopAudioProcessor::getRecentSfzPaths()
{
    juce::StringArray out;
    if (auto xml = readRecentSfz())
        for (auto* e : xml->getChildIterator())
            out.add (e->getStringAttribute ("path"));
    return out;
}

void VocalChopAudioProcessor::rememberSfzBank (const juce::String& name, const juce::File& f)
{
    auto xml = readRecentSfz();
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement> ("RecentSfz");

    // Newest first, de-duplicated by path, capped at 12 entries.
    for (int i = xml->getNumChildElements(); --i >= 0;)
        if (auto* e = xml->getChildElement (i))
            if (e->getStringAttribute ("path") == f.getFullPathName())
                xml->removeChildElement (e, true);

    auto* entry = new juce::XmlElement ("Bank");
    entry->setAttribute ("name", name.isNotEmpty() ? name
                                                   : f.getFileNameWithoutExtension());
    entry->setAttribute ("path", f.getFullPathName());
    xml->insertChildElement (entry, 0);

    while (xml->getNumChildElements() > 12)
        xml->removeChildElement (xml->getChildElement (xml->getNumChildElements() - 1), true);

    auto store = recentSfzStore();
    store.getParentDirectory().createDirectory();
    xml->writeTo (store);
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
