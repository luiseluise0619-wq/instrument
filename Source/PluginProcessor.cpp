#include "PluginProcessor.h"
#include "AudioEngine/FactoryInstruments.h"
#include "AudioEngine/SampleStateCodec.h"
namespace {
struct ScopedAtomicPatch { std::atomic<bool>& flag; bool old;
    explicit ScopedAtomicPatch(std::atomic<bool>& f):flag(f),old(f.exchange(true)){}
    ~ScopedAtomicPatch(){flag.store(old);}
};
}
using slyce::factory::kInstruments;
using slyce::factory::kNumInstruments;
using slyce::factory::voicedDefinition;
#include "UI/Reference/ReferenceEditor.h"
#include "UI/ThemeManager.h"
#include "AudioEngine/SampleLoader.h"
#include "BinaryData.h"
#include <cmath>
#include <limits>

//==============================================================================
VocalChopAudioProcessor::VocalChopAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& semi : sliceTransposeSemis)
        semi.store (0.0f, std::memory_order_relaxed);
    for (int i = 0; i < 6; ++i)
        looperMidiCC[(size_t) i].store (20 + i, std::memory_order_relaxed);
    const int defaults[] = { 26, 27, 28, 29, 30, 31, 32, 33, 34 };
    for (int i = 0; i < 9; ++i)
        looperMidiCC[(size_t) (6 + i)].store (defaults[i], std::memory_order_relaxed);

   #if SLYCE_DEMO_GATE
    // A purchase is proven by the online check performed from UnlockPanel.
    // Once that succeeds, we keep a small machine-bound ticket so the buyer
    // does not have to activate again every time the plugin opens.
    licensed.store (loadLocalActivation());
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
    fxBypassParam  = apvts.getRawParameterValue ("fxBypass");
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
    airMacroParam   = apvts.getRawParameterValue ("airVocalAir");
    bodyMacroParam  = apvts.getRawParameterValue ("airVocalBody");
    vowelMacroParam = apvts.getRawParameterValue ("airVocalVowel");
    bloomMacroParam = apvts.getRawParameterValue ("airVocalBloom");
    motionMacroParam = apvts.getRawParameterValue ("airVocalMotion");
    airSpaceMacroParam = apvts.getRawParameterValue ("airVocalSpace");

    apvts.addParameterListener ("pitch", this);
    apvts.addParameterListener ("formant", this);
    for (const auto& id : ownableFx()) apvts.addParameterListener(id,this);

    // Synth mode boots with a designed patch (Supersaw Lead) whenever the
    // user flips the engine over.
    applyEnginePatch (defaultInstrumentIndex());
    loadAirVocalPresetBank (0);
    startTimerHz (20);

    // First-run experience = the name promise: decode the embedded demo
    // vocal and slice it, so the very first key press CHOPS. No parameter
    // writes here (hosts dislike notifications mid-construction) — the
    // engine parameter's default is already Chop.
    {
        double sr = currentSampleRate;
        if (auto demo = SampleLoader::decode (BinaryData::premium_glass_tide_a_1_wav,
                                              BinaryData::premium_glass_tide_a_1_wavSize, sr))
        {
            loadedSampleName = "Glass Tide - Sung Hooks";
            currentDemo=0;
            sampleBuffer     = demo;
            loadedSampleRate = sr;
            reassignSampleToEngines();
            rescanSlices();
            analyzeSampleKey();
        }
    }
}

int VocalChopAudioProcessor::getLooperMidiCC (int action) const
{
    return juce::isPositiveAndBelow (action, (int) looperMidiCC.size())
        ? looperMidiCC[(size_t) action].load (std::memory_order_relaxed) : -1;
}

void VocalChopAudioProcessor::setLooperMidiCC (int action, int cc)
{
    if (juce::isPositiveAndBelow (action, (int) looperMidiCC.size()))
        looperMidiCC[(size_t) action].store (juce::jlimit (0, 127, cc), std::memory_order_relaxed);
}

VocalChopAudioProcessor::~VocalChopAudioProcessor()
{
    stopTimer();
    apvts.removeParameterListener ("pitch", this);
    apvts.removeParameterListener ("formant", this);
    for (const auto& id : ownableFx()) apvts.removeParameterListener(id,this);
}

juce::File VocalChopAudioProcessor::localLicenseFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Slyce")
        .getChildFile ("license-ticket.xml");
}

juce::String VocalChopAudioProcessor::localLicenseSignature (const juce::String& email,
                                                             const juce::String& keyHash,
                                                             const juce::String& machine)
{
    // This is a local persistence guard, not the source of licensing truth.
    // The source of truth remains /api/license/activate. The guard prevents a
    // plain-text "licensed=true" edit and binds the ticket to this machine.
    static constexpr const char* pepper = "Slyce.LocalLicenseTicket.v1.2026";
    const auto payload = vcs::Licensing::normEmail (email) + "|" + keyHash + "|" + machine + "|" + pepper;
    const juce::SHA256 sha (payload.toRawUTF8(), (size_t) payload.getNumBytesAsUTF8());
    return sha.toHexString().removeCharacters (" ");
}

bool VocalChopAudioProcessor::loadLocalActivation()
{
    const auto file = localLicenseFile();
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::XmlElement> xml (juce::parseXML (file));
    if (xml == nullptr || ! xml->hasTagName ("SlyceLicenseTicket"))
        return false;

    const auto email   = vcs::Licensing::normEmail (xml->getStringAttribute ("email"));
    const auto keyHash = xml->getStringAttribute ("keyHash").trim();
    const auto machine = xml->getStringAttribute ("machine").trim();
    const auto sig     = xml->getStringAttribute ("signature").trim();

    if (email.isEmpty() || keyHash.length() != 64 || machine.length() != 64 || sig.length() != 64)
        return false;
    if (! machine.equalsIgnoreCase (vcs::Licensing::machineHash()))
        return false;

    return sig.equalsIgnoreCase (localLicenseSignature (email, keyHash, machine));
}

bool VocalChopAudioProcessor::saveLocalActivation (const juce::String& emailIn,
                                                   const juce::String& keyIn)
{
    const auto email = vcs::Licensing::normEmail (emailIn);
    const auto key = vcs::Licensing::stripInvisible (keyIn);
    if (email.isEmpty() || key.isEmpty())
        return false;

    const juce::SHA256 keySha (key.toRawUTF8(), (size_t) key.getNumBytesAsUTF8());
    const auto keyHash = keySha.toHexString().removeCharacters (" ");
    const auto machine = vcs::Licensing::machineHash();

    juce::XmlElement xml ("SlyceLicenseTicket");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("email", email);
    xml.setAttribute ("keyHash", keyHash);
    xml.setAttribute ("machine", machine);
    xml.setAttribute ("signature", localLicenseSignature (email, keyHash, machine));
    xml.setAttribute ("activatedAt", juce::Time::getCurrentTime().toISO8601 (true));

    auto file = localLicenseFile();
    if (! file.getParentDirectory().createDirectory())
        return false;

    return xml.writeTo (file);
}

bool VocalChopAudioProcessor::finalizeActivation (const juce::String& email,
                                                  const juce::String& key)
{
    // The key has just been verified by the server on a worker thread. Save a
    // machine-bound local ticket so the unlock survives DAW/plugin restarts.
    if (! saveLocalActivation (email, key))
        return false;

    licensed.store (true);
    sendChangeMessage();
    return true;
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
        // Defaults to Low Pass, not Off. With Off as the default, Cutoff and
        // Reso sat there turning freely and doing absolutely nothing until you
        // found a fourth control and changed it - which reads, entirely
        // reasonably, as "the filter is broken".
        juce::StringArray { "Off", "Low Pass", "High Pass", "Band Pass" }, 1));

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
        "outputGain", "Output", Range (-18.0f, 6.0f, 0.1f), 0.0f));

    // Synth engine mode: play oscillators instead of sample slices.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "engine", "Engine", juce::StringArray { "Chop", "Synth", "Sampled", "Melody", "Vocal Kit", "Air Vocal" }, 0));
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

    // APPENDED for host automation compatibility. These six parameters map
    // one-for-one to the dedicated AIR VOCAL instrument macros.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "airVocalAir", "AIR", Range (0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "airVocalBody", "BODY", Range (0.0f, 1.0f, 0.001f), 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "airVocalVowel", "VOWEL", Range (0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "airVocalBloom", "BLOOM", Range (0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "airVocalMotion", "MOTION", Range (0.0f, 1.0f, 0.001f), 0.18f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "airVocalSpace", "SPACE", Range (0.0f, 1.0f, 0.001f), 0.40f));

    // APPENDED: a host-automatable, project-persistent true bypass.  Never
    // emulate bypass by zeroing effect parameters: doing so destroys
    // automation and prevents the previous sound being restored after an
    // editor is closed and reopened.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "fxBypass", "Creative FX Bypass", false));

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
    vocalKitEngine.prepare (sampleRate, samplesPerBlock);
    airVocalEngine.prepare (sampleRate, samplesPerBlock);
    pitchFormant.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    granularEngine.prepare (spec);
    fxChain.prepare (spec);
    looper.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate, samplesPerBlock);

    widthSmoothed.reset (sampleRate, 0.02);
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr ? widthParam->load() : 1.0f);

    outputGainSmoothed.reset (sampleRate, 0.020);
    outputGainSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputGainParam != nullptr ? outputGainParam->load() : 0.0f));

    noteToVoice.fill (-1);
    padKeyToVoice.fill (-1);
    physicalHeld.fill(false); sustainDeferred.fill(false); sustainDown=false;

    // Transport/device changes must not resurrect an arp pattern or leave the
    // pump mid-duck (a stale phase makes the first bar after a restart lopsided).
    arpHeld.fill (false);
    arpVel.fill (0.0f);
    arpNote = -1;
    arpStepSamples = 0;
    arpGateSamples = 0;
    arpIndex = 0; arpDir = 1; arpOctave = 0;
    pumpPhase = 0.0;
    mappedSampleInputNote = -1;
    mappedSamplePitchSemitones = 0.0f;

    const bool pitchActive = std::abs(pitchParam->load()) > 0.01f
                          || std::abs(formantParam->load()) > 0.01f;
    const int latency = limiter.getLatencySamples() + (pitchActive ? pitchFormant.getLatencySamples() : 0);
    desiredHostLatency.store(latency);
    setLatencySamples(latency);

    reassignSampleToEngines();
}

void VocalChopAudioProcessor::timerCallback()
{
    const int wanted = desiredHostLatency.load(std::memory_order_relaxed);
    if (wanted != getLatencySamples()) setLatencySamples(wanted);
    looper.collectRetired();
}

void VocalChopAudioProcessor::releaseResources()
{
    voicePool.releaseAll();
    vocalKitEngine.releaseAll();
    airVocalEngine.releaseAll();
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
        else melodyEngine.clearBank();
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

    const int n = sliceEngine.getNumSlices();
    for (int i = n; i < (int) sliceTransposeSemis.size(); ++i)
        sliceTransposeSemis[(size_t) i].store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
void VocalChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    hostBpm.store(0.0); hostPpq.store(-1.0); hostPlaying.store(false);
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

    looper.updateHostTempo(hostBpm.load(), hostPpq.load(), hostPlaying.load());
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
    airVocalEngine.setMacros (airMacroParam->load(), bodyMacroParam->load(),
                              vowelMacroParam->load(), bloomMacroParam->load(),
                              motionMacroParam->load(), airSpaceMacroParam->load());

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

    // MIDI timestamps are sample positions INSIDE this callback, not hints.
    // Render up to an event, apply it, and only then render what follows.
    desiredHostLatency.store(limiter.getLatencySamples()
        + ((std::abs(pitchParam->load()) > 0.01f || std::abs(formantParam->load()) > 0.01f
            || (isMelodyMode() && std::abs(mappedSamplePitchSemitones) > 0.01f))
              ? pitchFormant.getLatencySamples() : 0), std::memory_order_relaxed);
    drainPadQueue();
    if (wholeSamplePreviewPending.exchange(false, std::memory_order_acq_rel))
    {
        // Whole-sample preview belongs to the same Vocal Chop choke group as
        // the slice pads: starting either one replaces the previous phrase.
        const int voice = voicePool.triggerMonophonicVoice (
            0, std::numeric_limits<int>::max(), 0.9f, 0.0f, true);
        clearVoiceMapping (voice);
    }
    int cursor = 0;
    auto renderUntil = [&] (int end) {
        while(cursor < end) {
            advanceArp(cursor);
            int count = juce::jmin(end-cursor, juce::jmax(1, (int)spec.maximumBlockSize));
            if((isSynthMode() || isSamplerMode() || isMelodyMode())
                && arpModeParam != nullptr && arpModeParam->load() > 0.5f) {
                if(arpStepSamples > 0)count=juce::jmin(count, arpStepSamples);
                if(arpNote >= 0 && arpGateSamples > 0)count=juce::jmin(count, arpGateSamples);
            }
            renderSegment(buffer,cursor,count);
            if(arpStepSamples > 0)arpStepSamples-=count;
            if(arpNote >= 0)arpGateSamples-=count;
            cursor+=count;
        }
    };
    for(const auto meta : midi) {
        const int at=juce::jlimit(cursor,numSamples,meta.samplePosition);
        renderUntil(at);
        if(meta.numBytes > 0 && meta.numBytes <= 3)handleMidiMessage(meta.getMessage());
    }
    renderUntil(numSamples);
    midi.clear();

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

    // 10) Demo gate: without a license the output mutes for 2 s every 60 s
    //     (short fades at the window edges so there is no click).
    //
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

void VocalChopAudioProcessor::renderSegment(juce::AudioBuffer<float>& whole, int offset, int numSamples)
{
    if(numSamples <= 0 || whole.getNumChannels() == 0)return;
    float* channels[2] { whole.getWritePointer(0, offset),
        whole.getWritePointer(juce::jmin(1, whole.getNumChannels()-1), offset) };
    juce::AudioBuffer<float> buffer(channels, juce::jmin(2, whole.getNumChannels()), numSamples);
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
    if (! isVocalKitMode())
        vocalKitEngine.releaseAll();
    vocalKitEngine.render (buffer, numSamples);
    if (! isAirVocalMode())
        airVocalEngine.releaseAll();
    airVocalEngine.render (buffer, numSamples);

    // 3) Pitch / formant transformation. In Mapped Sample mode the reader
    // stays at C3/original speed and this pitch-only stage follows the key.
    // Previously C4 consumed the source at 2x speed and cut a vocal in half.
    const float mappedPitch = isMelodyMode() ? mappedSamplePitchSemitones : 0.0f;
    pitchFormant.process (buffer,
                          juce::jlimit (-36.0f, 36.0f,
                                        pitchParam->load() + mappedPitch),
                          formantParam->load(),
                          mixParam->load());

    // A single imported sample is pitch-shifted after it is mapped to the
    // keyboard.  The stretcher naturally loses a little energy as the note
    // moves upward, which made higher keys sound noticeably quieter than the
    // root key.  Apply a small, bounded musical compensation only to the
    // mapped-sample path; factory multisamples and the global pitch control
    // keep their original gain staging.
    if (isMelodyMode() && std::abs (mappedPitch) > 0.01f)
    {
        const float compensationDb = juce::jlimit (-3.0f, 3.0f,
                                                   mappedPitch * 0.18f);
        buffer.applyGain (juce::Decibels::decibelsToGain (compensationDb));
    }

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
    outputGainSmoothed.setTargetValue (gain);
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = outputGainSmoothed.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, n, buffer.getSample (ch, n) * g);
    }

    // 8) Loop station: records the performance, then adds the stacked loop
    //    (the limiter after us protects the sum).
    looper.process (buffer, numSamples);

    // 9) Brick-wall limiter.
    limiter.process (buffer);

    // Final output guard for extreme Creative FX combinations. The limiter
    // normally keeps this below ceiling, but a stale/non-finite sample from a
    // third-party host or an FX feedback edge must never reach the DAC.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const float x = buffer.getSample (ch, n);
            buffer.setSample (ch, n, std::isfinite (x) ? juce::jlimit (-0.98f, 0.98f, x) : 0.0f);
        }

}

void VocalChopAudioProcessor::handleMidiMessage(const juce::MidiMessage& msg)
{
        const int note = (msg.isNoteOn() || msg.isNoteOff()) ? msg.getNoteNumber() : -1;

        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            // Hardware and OS typing bridges can emit repeated note-ons while
            // a key is physically held. Treat those as key-repeat, not as a
            // request to restart a one-shot or to stack another gate voice.
            if (juce::isPositiveAndBelow (note, 128) && physicalHeld[(size_t) note])
                return;
            if(juce::isPositiveAndBelow(note,128)) { physicalHeld[(size_t)note]=true; sustainDeferred[(size_t)note]=false; }
            // With the arp on, held notes feed the PATTERN instead of
            // sounding directly; advanceArp() plays them on the grid.
            if ((isSynthMode() || isSamplerMode() || isMelodyMode() || isAirVocalMode())
                && arpModeParam != nullptr && (int) arpModeParam->load() > 0)
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
            if(juce::isPositiveAndBelow(note,128))physicalHeld[(size_t)note]=false;
            if(sustainDown && juce::isPositiveAndBelow(note,128))sustainDeferred[(size_t)note]=true;
            else routeNoteOff (note);
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
            if (msg.isAllSoundOff()) vocalKitEngine.stopAll();
            else                     vocalKitEngine.releaseAll();
            if (msg.isAllSoundOff()) airVocalEngine.stopAll();
            else                     airVocalEngine.releaseAll();
            noteToVoice.fill (-1);
            padKeyToVoice.fill (-1);
            mappedSampleInputNote = -1;
            mappedSamplePitchSemitones = 0.0f;
            physicalHeld.fill(false); sustainDeferred.fill(false); sustainDown=false;
            arpHeld.fill (false);
            arpVel.fill (0.0f);
            arpNote = -1;
        }
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int value = msg.getControllerValue();
            if(cc == 64 || cc == 121) {
                const bool down = cc == 64 && value >= 64;
                if(sustainDown && !down)for(int n=0;n<128;++n) {
                    if(sustainDeferred[(size_t)n] && !physicalHeld[(size_t)n])routeNoteOff(n);
                    sustainDeferred[(size_t)n]=false;
                }
                sustainDown=down; return;
            }
            if (value < 64)
                return; // footswitch/button release

            int track = -1;
            for (int i = 0; i < 6; ++i)
                if (cc == getLooperMidiCC (i)) { track = i; break; }
            if (track >= 0)
            {
                midiLooperTrack.store (track, std::memory_order_relaxed);
                looper.tapMain (track);
            }
            // Legacy per-track footswitch ranges remain available alongside
            // the configurable actions: stop 40-45, re-record 50-55, undo
            // 80-85. This keeps existing controller templates working.
            else if (cc >= 40 && cc <= 45)
                looper.tapStopTrack (cc - 40);
            else if (cc >= 50 && cc <= 55)
                looper.tapReRecord (cc - 50);
            else if (cc >= 80 && cc <= 85)
                looper.tapUndo (cc - 80);
            else if (cc == getLooperMidiCC (6))
            {
                looper.tapStopAll();
            }
            else if (cc == getLooperMidiCC (7))
            {
                looper.tapPlayAll();
            }
            else if (cc == getLooperMidiCC (8))
            {
                const int next = (midiLooperTrack.load (std::memory_order_relaxed)
                                  + LoopStation::kNumTracks - 1) % LoopStation::kNumTracks;
                midiLooperTrack.store (next, std::memory_order_relaxed);
            }
            else if (cc == getLooperMidiCC (9))
            {
                const int next = (midiLooperTrack.load (std::memory_order_relaxed) + 1)
                               % LoopStation::kNumTracks;
                midiLooperTrack.store (next, std::memory_order_relaxed);
            }
            else if (cc == getLooperMidiCC (10))
            {
                looper.tapMain (midiLooperTrack.load (std::memory_order_relaxed));
            }
            else if (cc == getLooperMidiCC (11))
            {
                looper.tapStopTrack (midiLooperTrack.load (std::memory_order_relaxed));
            }
            else if (cc == getLooperMidiCC (12))
            {
                looper.tapReRecord (midiLooperTrack.load (std::memory_order_relaxed));
            }
            else if (cc == getLooperMidiCC (13))
            {
                looper.tapUndo (midiLooperTrack.load (std::memory_order_relaxed));
            }
            else if (cc == getLooperMidiCC (14))
            {
                looper.tapClear (midiLooperTrack.load (std::memory_order_relaxed));
            }
        }
}

void VocalChopAudioProcessor::routeNoteOn (int note, float velocity, bool selfReleasing)
{
    // Only the CURRENT sample engine may speak. Switching from a chop/mapped
    // vocal to any other playable area used to leave the old phrase ringing
    // underneath until its natural end. Hard-stop the inactive sample paths
    // at the next note; the active engine keeps its intended polyphony.
    if (! isChopMode())
        voicePool.stopAll();
    if (! isMelodyMode())
    {
        melodyEngine.chokeAll();
        mappedSampleInputNote = -1;
    }
    if (! isVocalKitMode())
        vocalKitEngine.stopAll();
    if (! isSynthMode())
        synthEngine.chokeAll();
    if (! isSamplerMode())
        samplerEngine.chokeAll();
    if (! isAirVocalMode())
        airVocalEngine.stopAll();

    // Sampled/Melody modes without a loaded bank fall through to the synth
    // branch (isSynthMode() is true there too): keys must NEVER be silent.
    if (isAirVocalMode())
    {
        airVocalEngine.noteOn (note, velocity, selfReleasing);
    }
    else if (isVocalKitMode())
    {
        vocalKitEngine.noteOn (note, velocity, selfReleasing);
        const int slot = vocalKitEngine.getLastTriggeredSlot();
        if (slot >= 0) selectedSlice.store (slot, std::memory_order_relaxed);
    }
    else if (isMelodyMode() && melodyEngine.hasBank())
    {
        // A mapped vocal is one phrase played at different pitches, not a
        // piano multisample. The next key replaces the previous phrase. Keep
        // playback at the root rate; PitchFormant changes pitch afterwards
        // without changing the phrase duration.
        melodyEngine.chokeAll();
        mappedSampleInputNote = note;
        mappedSamplePitchSemitones = (float) (note - kRootNote);
        if (selfReleasing) melodyEngine.tapNote (kRootNote, velocity);
        else               melodyEngine.noteOn  (kRootNote, velocity);
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
    if (note == mappedSampleInputNote)
    {
        melodyEngine.noteOff (kRootNote);
        mappedSampleInputNote = -1;
    }
    else
        melodyEngine.noteOff (note);
    vocalKitEngine.noteOff (note);
    airVocalEngine.noteOff (note);

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

void VocalChopAudioProcessor::advanceArp (int blockOffset)
{
    const bool arpCapableEngine = isSynthMode() || isSamplerMode() || isMelodyMode() || isAirVocalMode();
    const int mode = arpCapableEngine && arpModeParam != nullptr ? (int) arpModeParam->load() : 0;

    if (mode <= 0)
    {
        // Turning the arp off must not leave its last note hanging.
        if (arpNote >= 0) { routeNoteOff (arpNote); arpNote = -1; }
        arpStepSamples = 0;
        arpLastStep = std::numeric_limits<int64_t>::min();
        if (! arpCapableEngine)
        {
            arpHeld.fill (false);
            arpVel.fill (0.0f);
        }
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
        arpLastStep = std::numeric_limits<int64_t>::min();
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

    // Note-off at the exact sample deadline.
    if (arpNote >= 0)
    {
        // The renderer has already advanced the exact number of samples.
        if (arpGateSamples <= 0)
        {
            routeNoteOff (arpNote);
            arpNote = -1;
        }
    }

    // Step boundary. With a rolling transport the grid comes from the host's
    // musical position, so the pattern sits on the DAW's beats no matter when
    // the chord was pressed; otherwise fall back to a free-running counter.
    const double ppq = hostPpq.load() + (hostPpq.load() >= 0.0 ?
        blockOffset * currentBpm() / (60.0 * currentSampleRate) : 0.0);
    if (ppq >= 0.0 && hostPlaying.load())
    {
        arpStepSamples = juce::jmax(1, (int)std::ceil(((std::floor(ppq/rateMult[rate])+1.0) * rateMult[rate] - ppq) * 60.0 * currentSampleRate / currentBpm() - 1.0e-7));
        const auto step = (int64_t) std::floor (ppq / rateMult[rate]);
        if (step == arpLastStep)
            return;
        arpLastStep = step;
    }
    else
    {
        arpLastStep = std::numeric_limits<int64_t>::min();

        // Exact countdown is decremented after rendering, not before.
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
        const float semi = getSliceTranspose (sliceIndex);
        // Vocal chops are a single choke group. Every entry point (host MIDI,
        // typing keyboard, on-screen key, slice card and arpeggiator) arrives
        // here, so replacing the dedicated mono slot fixes all of them while
        // Synth, Sampled, Melody and Drum Kit keep their existing polyphony.
        const int voice = voicePool.triggerMonophonicVoice (slice.startSample,
                                                            slice.lengthSamples, velocity,
                                                            semi);
        // Ground truth for --keymap: the position in the audio this voice
        // actually began reading from. Relaxed - nothing synchronises on it.
        lastVoiceSlice.store (sliceIndex, std::memory_order_relaxed);
        lastVoiceStartSample.store (slice.startSample, std::memory_order_relaxed);
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

void VocalChopAudioProcessor::triggerSliceDirectPad (int sliceIndex, float velocity)
{
    queuePadEvent (sliceIndex, velocity, padDirectTap);
}

void VocalChopAudioProcessor::pressSlicePad (int sliceIndex, float velocity)
{
    queuePadEvent (sliceIndex, velocity, padOn);
}

void VocalChopAudioProcessor::releaseSlicePad (int sliceIndex)
{
    queuePadEvent (sliceIndex, 0.0f, padOff);
}

void VocalChopAudioProcessor::chokeSlicePad (int sliceIndex)
{
    queuePadEvent (sliceIndex, 0.0f, padChoke);
}

float VocalChopAudioProcessor::getSliceTranspose (int slice) const
{
    if (slice < 0 || slice >= (int) sliceTransposeSemis.size())
        return 0.0f;
    return sliceTransposeSemis[(size_t) slice].load (std::memory_order_relaxed);
}

void VocalChopAudioProcessor::setSliceTranspose (int slice, float semitones)
{
    const int n = sliceEngine.getNumSlices();
    if (n <= 0 || slice < 0)
        return;

    slice = juce::jlimit (0, juce::jmin (n - 1, (int) sliceTransposeSemis.size() - 1), slice);
    sliceTransposeSemis[(size_t) slice].store (juce::jlimit (-24.0f, 24.0f, semitones),
                                               std::memory_order_relaxed);
}

void VocalChopAudioProcessor::nudgeSelectedSliceTranspose (float semitoneDelta)
{
    int s = selectedSlice;
    if (s < 0)
        s = 0;
    setSliceTranspose (s, getSliceTranspose (s) + semitoneDelta);
}

namespace
{
    juce::AudioBuffer<float> renderSliceForExport (const juce::AudioBuffer<float>& src,
                                                   SlicePoint slice,
                                                   float semitones)
    {
        const int channels = juce::jmax (1, src.getNumChannels());
        const double ratio = std::pow (2.0, (double) juce::jlimit (-24.0f, 24.0f, semitones) / 12.0);
        const int outLen = juce::jmax (1, (int) std::ceil ((double) slice.lengthSamples / juce::jmax (1.0e-6, ratio)));
        juce::AudioBuffer<float> out (channels, outLen);
        out.clear();

        const int srcEnd = juce::jmin (src.getNumSamples(), slice.startSample + slice.lengthSamples);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* in = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
            float* dst = out.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i)
            {
                const double pos = (double) slice.startSample + (double) i * ratio;
                if (pos >= (double) srcEnd)
                    break;
                const int i0 = juce::jlimit (0, src.getNumSamples() - 1, (int) pos);
                const int i1 = juce::jmin (i0 + 1, src.getNumSamples() - 1);
                const float frac = (float) (pos - (double) i0);
                float v = in[i0] + frac * (in[i1] - in[i0]);
                const int fade = juce::jmin (128, outLen / 4);
                if (fade > 1)
                {
                    if (i < fade) v *= (float) i / (float) fade;
                    if (outLen - i < fade) v *= (float) (outLen - i) / (float) fade;
                }
                dst[i] = v;
            }
        }
        return out;
    }

    bool writeWavFile (const juce::File& file,
                       const juce::AudioBuffer<float>& buffer,
                       double sampleRate,
                       juce::String& error)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
        {
            error = "Could not create " + file.getFullPathName();
            return false;
        }
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sampleRate, (unsigned int) buffer.getNumChannels(), 24, {}, 0));
        if (writer == nullptr)
        {
            error = "Could not create WAV writer for " + file.getFullPathName();
            return false;
        }
        stream.release();
        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            error = "Failed writing " + file.getFullPathName();
            return false;
        }
        return true;
    }
}

bool VocalChopAudioProcessor::exportSlicesToFolder (const juce::File& folder,
                                                    juce::String& error) const
{
    auto src = sampleBuffer;
    if (src == nullptr || src->getNumSamples() <= 0)
    {
        error = "No sample loaded.";
        return false;
    }
    if (! folder.createDirectory())
    {
        error = "Could not create export folder.";
        return false;
    }

    const int n = sliceEngine.getNumSlices();
    if (n <= 0)
    {
        error = "No slices to export.";
        return false;
    }

    for (int i = 0; i < n; ++i)
    {
        auto sl = sliceEngine.getSlice (i);
        if (! sl.has_value())
            continue;
        auto rendered = renderSliceForExport (*src, *sl, getSliceTranspose (i));
        const auto semi = getSliceTranspose (i);
        const juce::String index = (i + 1 < 10 ? "0" : "") + juce::String (i + 1);
        const juce::String pitch = semi == 0.0f ? "0" : juce::String (semi, 1).replaceCharacter ('.', 'p');
        auto out = folder.getChildFile ("slice_" + index + "_st" + pitch + ".wav");
        if (! writeWavFile (out, rendered, loadedSampleRate, error))
            return false;
    }
    return true;
}

bool VocalChopAudioProcessor::exportSliceToFile (int sliceIndex,
                                                 const juce::File& file,
                                                 juce::String& error) const
{
    auto src = sampleBuffer;
    if (src == nullptr || src->getNumSamples() <= 0)
    {
        error = "No sample loaded.";
        return false;
    }

    const auto slice = sliceEngine.getSlice (sliceIndex);
    if (! slice.has_value())
    {
        error = "That slice is no longer available.";
        return false;
    }

    const auto rendered = renderSliceForExport (*src, *slice, getSliceTranspose (sliceIndex));
    return writeWavFile (file, rendered, loadedSampleRate, error);
}

bool VocalChopAudioProcessor::exportSlicesMergedToFile (const juce::File& file,
                                                        juce::String& error) const
{
    auto src = sampleBuffer;
    if (src == nullptr || src->getNumSamples() <= 0)
    {
        error = "No sample loaded.";
        return false;
    }
    const int n = sliceEngine.getNumSlices();
    if (n <= 0)
    {
        error = "No slices to export.";
        return false;
    }

    std::vector<juce::AudioBuffer<float>> rendered;
    int total = 0;
    const int gap = (int) (loadedSampleRate * 0.035);
    for (int i = 0; i < n; ++i)
    {
        auto sl = sliceEngine.getSlice (i);
        if (! sl.has_value())
            continue;
        rendered.push_back (renderSliceForExport (*src, *sl, getSliceTranspose (i)));
        total += rendered.back().getNumSamples() + gap;
    }

    if (rendered.empty())
    {
        error = "No valid slices to export.";
        return false;
    }

    juce::AudioBuffer<float> merged (src->getNumChannels(), juce::jmax (1, total));
    merged.clear();
    int pos = 0;
    for (auto& r : rendered)
    {
        for (int ch = 0; ch < merged.getNumChannels(); ++ch)
            merged.copyFrom (ch, pos, r, juce::jmin (ch, r.getNumChannels() - 1), 0, r.getNumSamples());
        pos += r.getNumSamples() + gap;
    }
    return writeWavFile (file, merged, loadedSampleRate, error);
}

void VocalChopAudioProcessor::drainPadQueue()
{
    int start1, size1, start2, size2;
    padFifo.prepareToRead (padFifo.getNumReady(), start1, size1, start2, size2);

    const bool synth = isSynthMode();

    auto fire = [this, synth] (int idx, float vel, int type)
    {
        // Slice cards carry a slice index directly. Do not reinterpret that
        // index as a chromatic key offset (C# otherwise maps back to slice 0).
        if (type == padDirectTap)
        {
            if (! synth && sliceEngine.getNumSlices() > 0)
                triggerSliceIndex (idx, vel);
            return;
        }

        const int note = kRootNote + idx;   // same key mapping as MIDI

        if (type == padOff || type == padChoke)
        {
            if (juce::isPositiveAndBelow (note, 128))
                arpHeld[(size_t) note] = false;

            // A choke cuts the voice in ~1.5 ms instead of running its release
            // stage. On a pad with a 900 ms tail, "released" and "stopped" are
            // not the same thing to anyone listening - they clicked the next
            // note and the last one was still there under it.
            const bool choke = (type == padChoke);

            if (choke)
            {
                if (juce::isPositiveAndBelow (note, 128) && noteToVoice[(size_t) note] >= 0)
                {
                    voicePool.chokeVoice (noteToVoice[(size_t) note]);
                    noteToVoice[(size_t) note] = -1;
                }
                synthEngine.noteOff (note);
                if (note == mappedSampleInputNote)
                {
                    melodyEngine.chokeAll();
                    mappedSampleInputNote = -1;
                }
                else
                    melodyEngine.noteOff (note);
                samplerEngine.noteOff (note);
            }
            else
            {
                routeNoteOff (note);
            }

            if (juce::isPositiveAndBelow (idx, 128) && padKeyToVoice[(size_t) idx] >= 0)
            {
                if (choke) voicePool.chokeVoice   (padKeyToVoice[(size_t) idx]);
                else       voicePool.releaseVoice (padKeyToVoice[(size_t) idx]);
                padKeyToVoice[(size_t) idx] = -1;
            }
            return;
        }

        // With the arp on, a HELD pad feeds the pattern like a held key does.
        // A tap does not: the chord bar fires taps with no matching release,
        // so latching them left the chord stuck in the arp forever. Taps stay
        // one-shot hits and play straight through.
        if (type == padOn && (isSynthMode() || isSamplerMode() || isMelodyMode())
            && arpModeParam != nullptr && (int) arpModeParam->load() > 0
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
    if (fxBypassParam != nullptr && fxBypassParam->load() >= 0.5f)
        return;

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
    const juce::ScopedLock guard(sampleStateLock);
    return sampleBuffer;
}

bool VocalChopAudioProcessor::loadSampleFromFile (const juce::File& file,
                                                  bool switchEngineToChop)
{
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (file, sr);
    if (buffer == nullptr)
        return false;

    // A slice selection belongs to the audio it was made from. Every path that
    // loads a sample comes through here, so clearing it once here covers the
    // drop target, the Load button, the demo vocal and a restored session.
    const juce::ScopedLock sourceGuard(sampleStateLock);
    selectedSlice = -1;
    for (auto& semi : sliceTransposeSemis)
        semi.store (0.0f, std::memory_order_relaxed);

    sampleBuffer     = buffer;
    loadedSampleRate = sr;
    loadedSampleFile = file;
    loadedSampleName = file.getFileNameWithoutExtension();
    currentDemo      = -1;    // a file of the user's own, not one of the ten
    prevSampleBuffer.reset();   // edit-undo must not resurrect the OLD sample
    reassignSampleToEngines();
    rescanSlices();
    analyzeSampleKey();

    // Loading a sample means the user wants to chop it - switch engines so
    // the keyboard immediately plays slices (state restore passes false).
    if (switchEngineToChop)
    {
        if (auto* p = apvts.getParameter ("engine"))
            p->setValueNotifyingHost (0.0f);
        // Rhythmic performance effects belong to the previous preset. A
        // Festival/sidechain patch used to leave Pump or Arp latched onto the
        // newly loaded vocal, making every chop duck/cut on the beat.
        if (auto* p = apvts.getParameter ("arpMode"))
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
        if (auto* p = apvts.getParameter ("pumpAmt"))
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    }

    return true;
}

namespace
{
    // The built-in vocals. Ordered so the first few land on the most obviously
    // useful sources - a chant, a sung hook, sixteenth-note stabs - rather than
    // making someone step ten times to find the one that chops well. The last
    // is a human-beatbox loop: sliced, it turns the keys into mouth drums.
    //
    // Named for what you would HEAR, not for the file: "Whisper" and "Air" on
    // their own told nobody they were vocal takes.
    // `group` is what the picker puts a header over. Thirty-six entries in one
    // flat list is a scroll, not a choice - and the question someone actually
    // arrives with is "I need a rhythmic chop" or "I need a pad", not "I need
    // the fourteenth one".
    struct DemoVocal { const void* data; int size; const char* label; const char* group; bool synthetic; };

    const DemoVocal kDemoVocals[] = {
        // Human-like sung banks extracted from user-supplied songs.  Only the
        // separated vocal stem is used.  Each bank contains up to eight
        // trimmed phrases; no drums, hats or accompaniment are embedded.
        { BinaryData::premium_glass_tide_a_1_wav, BinaryData::premium_glass_tide_a_1_wavSize, "Glass Tide - Hooks", "Sung Hooks", false },
        { BinaryData::premium_glass_tide_a_2_wav, BinaryData::premium_glass_tide_a_2_wavSize, "Glass Tide - Phrases", "Sung Hooks", false },
        { BinaryData::premium_glass_tide_a_3_wav, BinaryData::premium_glass_tide_a_3_wavSize, "Glass Tide - Cuts 3", "Sung Hooks", false },
        { BinaryData::premium_glass_tide_a_4_wav, BinaryData::premium_glass_tide_a_4_wavSize, "Glass Tide - Cuts 4", "Sung Hooks", false },
        { BinaryData::premium_glass_tide_b_1_wav, BinaryData::premium_glass_tide_b_1_wavSize, "Glass Tide Alt - Hooks", "Sung Hooks", false },
        { BinaryData::premium_glass_tide_b_2_wav, BinaryData::premium_glass_tide_b_2_wavSize, "Glass Tide Alt - Phrases", "Sung Hooks", false },
        { BinaryData::premium_glass_tide_b_3_wav, BinaryData::premium_glass_tide_b_3_wavSize, "Glass Tide Alt - Cuts 3", "Sung Hooks", false },
        { BinaryData::premium_glass_tide_b_4_wav, BinaryData::premium_glass_tide_b_4_wavSize, "Glass Tide Alt - Cuts 4", "Sung Hooks", false },
        { BinaryData::premium_quiet_blue_a_1_wav, BinaryData::premium_quiet_blue_a_1_wavSize, "Quiet Blue - Air", "Air & Emotion", false },
        { BinaryData::premium_quiet_blue_a_2_wav, BinaryData::premium_quiet_blue_a_2_wavSize, "Quiet Blue - Lines", "Air & Emotion", false },
        { BinaryData::premium_quiet_blue_a_3_wav, BinaryData::premium_quiet_blue_a_3_wavSize, "Quiet Blue - Cuts 3", "Air & Emotion", false },
        { BinaryData::premium_quiet_blue_a_4_wav, BinaryData::premium_quiet_blue_a_4_wavSize, "Quiet Blue - Cuts 4", "Air & Emotion", false },
        { BinaryData::premium_quiet_blue_b_1_wav, BinaryData::premium_quiet_blue_b_1_wavSize, "Quiet Blue Alt - Air", "Air & Emotion", false },
        { BinaryData::premium_quiet_blue_b_2_wav, BinaryData::premium_quiet_blue_b_2_wavSize, "Quiet Blue Alt - Lines", "Air & Emotion", false },
        { BinaryData::premium_quiet_blue_b_3_wav, BinaryData::premium_quiet_blue_b_3_wavSize, "Quiet Blue Alt - Cuts 3", "Air & Emotion", false },
        { BinaryData::premium_quiet_blue_b_4_wav, BinaryData::premium_quiet_blue_b_4_wavSize, "Quiet Blue Alt - Cuts 4", "Air & Emotion", false },
        { BinaryData::premium_come_undo_me_1_wav, BinaryData::premium_come_undo_me_1_wavSize, "Come Undo Me - Hooks", "Intimate Phrases", false },
        { BinaryData::premium_come_undo_me_2_wav, BinaryData::premium_come_undo_me_2_wavSize, "Come Undo Me - Lines", "Intimate Phrases", false },
        { BinaryData::premium_come_undo_me_3_wav, BinaryData::premium_come_undo_me_3_wavSize, "Come Undo Me - Cuts 3", "Intimate Phrases", false },
        { BinaryData::premium_come_undo_me_4_wav, BinaryData::premium_come_undo_me_4_wavSize, "Come Undo Me - Cuts 4", "Intimate Phrases", false },
        { BinaryData::premium_dark_space_1_wav, BinaryData::premium_dark_space_1_wavSize, "Dark Space - Hooks", "Dark Phrases", false },
        { BinaryData::premium_dark_space_2_wav, BinaryData::premium_dark_space_2_wavSize, "Dark Space - Lines", "Dark Phrases", false },
        { BinaryData::premium_dark_space_3_wav, BinaryData::premium_dark_space_3_wavSize, "Dark Space - Cuts 3", "Dark Phrases", false },
        { BinaryData::premium_dark_space_4_wav, BinaryData::premium_dark_space_4_wavSize, "Dark Space - Cuts 4", "Dark Phrases", false },
    };

    constexpr int kNumDemoVocals = (int) (sizeof (kDemoVocals) / sizeof (kDemoVocals[0]));
}

juce::StringArray VocalChopAudioProcessor::getDemoSampleNames()
{
    juce::StringArray names;
    for (const auto& d : kDemoVocals)
        names.add (juce::String(d.label) + (d.synthetic ? " [SYN]" : ""));
    return names;
}

int VocalChopAudioProcessor::getNumDemoSamples()
{
    return kNumDemoVocals;
}

juce::StringArray VocalChopAudioProcessor::getDemoSampleGroups()
{
    juce::StringArray groups;
    for (const auto& d : kDemoVocals)
        groups.add (d.group);
    return groups;
}

bool VocalChopAudioProcessor::loadVocalKitSlot (int slot, const juce::File& file,
                                                juce::String& error)
{
    juce::ignoreUnused (slot);
    if (! loadSampleFromFile (file, false))
    {
        error = "Unsupported or unreadable audio file: " + file.getFullPathName();
        return false;
    }

    // New workflow: one source is mapped chromatically over the keyboard.
    // Keep the old VocalKit state reader for project compatibility, but do
    // not create a new fifteen-independent-sample kit from user imports.
    if (auto* p = apvts.getParameter ("engine"))
        p->setValueNotifyingHost (p->convertTo0to1 (3.0f));
    if (auto* p = apvts.getParameter ("arpMode"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    if (auto* p = apvts.getParameter ("pumpAmt"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    sendChangeMessage();
    return true;
}

void VocalChopAudioProcessor::auditionVocalKitSlot (int slot, float velocity)
{
    slot = juce::jlimit (0, VocalKitEngine::kNumSlots - 1, slot);
    selectVocalKitSlot (slot);
    queuePadEvent (slot, velocity, padTap);
}

void VocalChopAudioProcessor::loadFactoryVocalKit (int kitIndex)
{
    // The former Arcade-style factory kits placed a different recording on
    // each key.  Factory vocal instruments now follow the same predictable
    // model as user audio: one carefully chosen source, root C3, mapped
    // chromatically across every key.
    static constexpr int mappedSources[] = { 0, 1, 4 };
    loadDemoSample (mappedSources[juce::jlimit (0, 2, kitIndex)]);
    if (auto* p = apvts.getParameter ("engine"))
        p->setValueNotifyingHost (p->convertTo0to1 (3.0f));
    if (auto* p = apvts.getParameter ("arpMode"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    if (auto* p = apvts.getParameter ("pumpAmt"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    sendChangeMessage();
}

void VocalChopAudioProcessor::restoreEmbeddedVocalKitSources()
{
    for (int slot = 0; slot < VocalKitEngine::kNumSlots; ++slot)
    {
        auto settings = vocalKitEngine.getSlotSettings (slot);
        if (! settings.sourcePath.startsWith ("builtin:")) continue;
        const int source = settings.sourcePath.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (! juce::isPositiveAndBelow (source, kNumDemoVocals)) continue;
        const auto& d = kDemoVocals[source];
        double rate = currentSampleRate;
        auto audio = SampleLoader::decode (d.data, d.size, rate);
        if (! audio) continue;
        vocalKitEngine.setSlotFromBuffer (slot, std::move (audio), rate,
                                          settings.name.isNotEmpty() ? settings.name : d.label,
                                          settings.sourcePath);
        settings.missing = false;
        vocalKitEngine.setSlotSettings (slot, settings);
    }
}

juce::StringArray VocalChopAudioProcessor::getAirVocalPresetNames()
{
    return { "Glass Air", "Quiet Bloom", "Blue Halo",
             "Intimate Cloud", "Dark Breath", "Frozen Tide" };
}

void VocalChopAudioProcessor::loadAirVocalPresetBank (int presetIndex)
{
    struct Raw { const void* data; int size; const char* name; int root; };
    // Release banks contain only source-separated, cleaned vocal recordings.
    // No accompaniment stem or retired procedural vowel is referenced here.
    const Raw velvet { BinaryData::premium_glass_tide_a_1_wav, BinaryData::premium_glass_tide_a_1_wavSize, "Glass Tide Hooks", 60 };
    const Raw silk   { BinaryData::premium_quiet_blue_a_1_wav, BinaryData::premium_quiet_blue_a_1_wavSize, "Quiet Blue Air", 60 };
    const Raw prism  { BinaryData::premium_glass_tide_b_1_wav, BinaryData::premium_glass_tide_b_1_wavSize, "Glass Tide Alt", 60 };
    const Raw cloud  { BinaryData::premium_quiet_blue_b_1_wav, BinaryData::premium_quiet_blue_b_1_wavSize, "Quiet Blue Alt", 60 };
    const Raw noir   { BinaryData::premium_dark_space_1_wav, BinaryData::premium_dark_space_1_wavSize, "Dark Space", 55 };
    const Raw glass  { BinaryData::premium_come_undo_me_1_wav, BinaryData::premium_come_undo_me_1_wavSize, "Come Undo Me", 60 };
    const Raw aurora { BinaryData::premium_quiet_blue_a_2_wav, BinaryData::premium_quiet_blue_a_2_wavSize, "Quiet Blue Lines", 60 };
    const Raw lunar  { BinaryData::premium_dark_space_2_wav, BinaryData::premium_dark_space_2_wavSize, "Dark Space Lines", 55 };

    static constexpr int count = 6;
    const int p = ((presetIndex % count) + count) % count;
    const Raw* core[6][3] = {
        { &velvet, &prism, &noir }, { &velvet, &noir, &aurora },
        { &prism, &velvet, &noir }, { &noir, &prism, &velvet },
        { &noir, &velvet, &prism }, { &glass, &prism, &velvet }
    };
    const Raw* air[6] = { &silk, &cloud, &silk, &cloud, &silk, &silk };
    const AirVocalEngine::PresetShape shapes[6] = {
        { 70, 780, .72f, .12f, .24f, .73f, 24, 10800 },
        { 95, 960, .80f, .08f, .22f, .72f, 28, 8200 },
        { 115, 1250, .68f, .14f, .38f, .80f, 32, 12500 },
        { 520, 2300, .62f, .10f, .44f, .84f, 36, 8800 },
        { 130, 1150, .78f, .16f, .28f, .76f, 27, 6500 },
        { 760, 3200, .64f, .10f, .52f, .89f, 40, 9800 }
    };

    auto next = std::make_shared<AirVocalEngine::Bank>();
    next->presetName = getAirVocalPresetNames()[p];
    auto decode = [&] (const Raw& raw, float loopA, float loopB, float xf)
    {
        AirVocalEngine::Source source;
        source.sampleRate = currentSampleRate;
        source.audio = SampleLoader::decode (raw.data, raw.size, source.sampleRate);
        source.rootNote = raw.root; source.name = raw.name;
        source.loopStart = loopA; source.loopEnd = loopB; source.crossfadeMs = xf;
        return source;
    };
    for (int i = 0; i < 3; ++i)
        next->core[(size_t) i] = decode (*core[p][i], 0.18f + 0.015f * i,
                                        0.82f - 0.02f * i, 72.0f + 11.0f * i);
    next->air = decode (*air[p], 0.0f, 1.0f, 5.0f); // AIR is one-shot, never looped
    airVocalEngine.setBank (next);
    airVocalEngine.setPresetShape (shapes[p]);
    currentAirVocalPreset.store (p);
}

void VocalChopAudioProcessor::applyAirVocalPreset (int presetIndex)
{
    const int p = ((presetIndex % 6) + 6) % 6;
    loadAirVocalPresetBank (p);
    static const float defaults[6][6] = {
        { .22f,.58f,.28f,.28f,.12f,.34f }, { .12f,.74f,.18f,.34f,.10f,.30f },
        { .24f,.52f,.05f,.38f,.16f,.55f }, { .18f,.50f,.62f,.78f,.30f,.62f },
        { .30f,.68f,.72f,.42f,.14f,.38f }, { .20f,.46f,.88f,.86f,.42f,.76f }
    };
    static const char* ids[6] = { "airVocalAir", "airVocalBody", "airVocalVowel",
                                  "airVocalBloom", "airVocalMotion", "airVocalSpace" };
    ScopedAtomicPatch guard (applyingPatch);
    for (int i = 0; i < 6; ++i)
        if (auto* param = apvts.getParameter (ids[i]))
            param->setValueNotifyingHost (param->convertTo0to1 (defaults[p][i]));
    if (auto* param = apvts.getParameter ("engine"))
        param->setValueNotifyingHost (param->convertTo0to1 (5.0f));
    if (auto* param = apvts.getParameter ("arpMode"))
        param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
    if (auto* param = apvts.getParameter ("pumpAmt"))
        param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
    // HALO is internal. Avoid hiding a clear dry source under the generic
    // master reverb when the preset is selected.
    if (auto* param = apvts.getParameter ("reverb"))
        param->setValueNotifyingHost (param->convertTo0to1 (0.06f));
    sendChangeMessage();
}

bool VocalChopAudioProcessor::loadDemoSample (int index)
{
    const juce::ScopedLock sourceGuard(sampleStateLock);
    // Wrap, so stepping walks off either end and comes back round rather than
    // stopping dead on the first or last vocal.
    const int which = ((index % kNumDemoVocals) + kNumDemoVocals) % kNumDemoVocals;
    const auto& d = kDemoVocals[which];

    if (! loadSampleFromMemory (d.data, d.size))
        return false;

    if (auto* p = apvts.getParameter ("arpMode"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    if (auto* p = apvts.getParameter ("pumpAmt"))
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));

    loadedSampleName = juce::String(d.label) + (d.synthetic ? " [SYN]" : "");
    currentDemo      = which;
    demoCycle        = which + 1;   // a plain Demo press continues from here
    return true;
}

bool VocalChopAudioProcessor::loadDemoSample()
{
    return loadDemoSample (demoCycle);
}

bool VocalChopAudioProcessor::loadSampleFromMemory (const void* data, int sizeBytes)
{
    double sr = currentSampleRate;
    auto buffer = SampleLoader::decode (data, sizeBytes, sr);
    if (buffer == nullptr)
        return false;

    const juce::ScopedLock sourceGuard(sampleStateLock);
    selectedSlice = -1;
    for (auto& semi : sliceTransposeSemis)
        semi.store (0.0f, std::memory_order_relaxed);

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
    const juce::ScopedLock sourceGuard(sampleStateLock);
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
            out->applyGain (0.89125094f / peak);
    }

    if(op == SampleEdit::Cut || op == SampleEdit::Trim) {
        const int edge=juce::jmin(out->getNumSamples()/4, (int)(loadedSampleRate*0.003));
        for(int ch=0;ch<chans;++ch)for(int n=0;n<edge;++n) {
            const float w=(float)n/(float)juce::jmax(1,edge-1);
            out->getWritePointer(ch)[n]*=w;
            out->getWritePointer(ch)[out->getNumSamples()-1-n]*=w;
        }
        if(op == SampleEdit::Cut && s>0 && s<out->getNumSamples()) {
            const int blend=juce::jmin(edge,juce::jmin(s,out->getNumSamples()-s));
            for(int ch=0;ch<chans;++ch)for(int n=0;n<blend;++n) {
                const float w=(float)n/(float)juce::jmax(1,blend-1);
                out->getWritePointer(ch)[s-blend+n]*=(1.0f-w);
                out->getWritePointer(ch)[s+n]*=w;
            }
        }
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
    const juce::ScopedLock sourceGuard(sampleStateLock);
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
void VocalChopAudioProcessor::parameterChanged (const juce::String& id, float /*newValue*/)
{
    // processBlock reads the atomic APVTS values and updates PitchFormant on
    // the audio thread. Writing its plain floats from this callback races with
    // audio rendering when a host/message-thread automation change arrives.

    // A change that did NOT come from applyInstrument / applyPreset is the
    // user's own. Remember it, so stepping to the next instrument stops
    // wiping it out.
    if (! applyingPatch.load(std::memory_order_relaxed))
        userOwnedFx.fetch_or(fxOwnershipBit(id), std::memory_order_relaxed);
}

unsigned VocalChopAudioProcessor::fxOwnershipBit(const juce::String& id)
{
    if(id == "drive")return 1u;
    if(id == "reverb")return 2u;
    if(id == "delay")return 4u;
    if(id == "pingpong")return 8u;
    if(id == "width")return 16u;
    return 0u;
}
bool VocalChopAudioProcessor::ownsFx(const juce::String& id) const
{ return (userOwnedFx.load(std::memory_order_relaxed) & fxOwnershipBit(id)) != 0; }

/** The OUTPUT FX. These are the mix, not the instrument: someone who has
    turned the reverb down has said something about how loud the room should
    be, and that answer does not change because they auditioned the next
    lead. Everything else - the envelope, the oscillator, the filter - IS the
    instrument and is meant to be replaced wholesale. */
const juce::StringArray& VocalChopAudioProcessor::ownableFx()
{
    static const juce::StringArray ids { "drive", "reverb", "delay",
                                         "pingpong", "width" };
    return ids;
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
    // A preset is an explicit "give me all of it", so it takes the FX back.
    // Only instrument STEPPING preserves the user's mix; otherwise picking
    // "Choir Pad" would land you with the previous patch's dry reverb and no
    // way to get the preset's actual sound.
    userOwnedFx.store(0);
    const ScopedAtomicPatch patching (applyingPatch);

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
        // Low pass wide open, NOT off. Sonically identical - 20 kHz passes
        // everything - but it leaves the filter as a control the user can
        // actually reach for, instead of switching it off under them every
        // time they pick a preset.
        set ("filterType", 1.0f);
        set ("filterCutoff", 20000.0f);
        set ("grainMix", 0.0f);
        set ("reverse", 0.0f);
        set ("playMode", 0.0f);
        set ("pitch", 0.0f);
        set ("formant", 0.0f);
        set ("mix", 1.0f);
        // The editor has no other way to learn that a preset just changed the
        // engine and the instrument - parameter attachments cover the knobs,
        // nothing covers the voice name or the engine tabs. Without this the
        // sound changes and the screen still shows the old instrument, which
        // is exactly the "the name never changes" report.
        sendChangeMessage();
        return;
    }

    // Start every preset from a known baseline, then apply the character.
    set ("pitch", 0.0f);      set ("formant", 0.0f);   set ("mix", 1.0f);
    set ("width", 1.0f);      set ("grainSize", 80.0f); set ("grainMix", 0.0f);
    set ("drive", 0.0f);      set ("reverb", 0.0f);    set ("delay", 0.0f);
    set ("attack", 5.0f);     set ("decay", 0.0f);     set ("sustain", 1.0f);
    set ("release", 20.0f);   set ("filterType", 1.0f); set ("filterCutoff", 20000.0f);
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

    sendChangeMessage();
}

//==============================================================================
namespace
{
    // -----------------------------------------------------------------------
    // GENRE BANKS
    //
    // The catalogue above is filed by what a voice IS - bass, pad, lead. That
    // is the right way to store it and the wrong way to shop for it. Someone
    // opening this to build a festival track does not think "I need a BASS",
    // they think "I need the pumping one that sits under the drop", and the
    // thing they want is called "Sidechain Bass" and lives 40 rows into a
    // 47-row list next to 46 basses that are not it.
    //
    // So the banks below are a second index over the SAME instruments: no new
    // engines, no duplicated patches, just curated running orders for the
    // three things people actually build. An instrument appears in as many
    // banks as it earns - a grand piano is pop and hip-hop and, on the right
    // record, EDM.
    //
    // Entries are names, not indices, so this cannot silently drift out of
    // sync when the catalogue grows: a name that stops existing fails the
    // --genres check instead of quietly pointing at whatever moved into that
    // slot. Everything is generic - a role, never an artist or a record.
    // -----------------------------------------------------------------------
    struct GenreGroup { const char* role; const char* const* names; int count; };
    struct GenreBank  { const char* name; const GenreGroup* groups; int numGroups; };

    #define SLYCE_GROUP(role, arr) { role, arr, (int) (sizeof (arr) / sizeof (arr[0])) }

    // --- EDM: 158 ----------------------------------------------------------
    const char* const kEdmLead[] = {
        "Supersaw Lead", "Saw Stack", "Hyper Saw", "Trance Saw", "Festival Lead",
        "Mainstage", "Stadium Lead", "Neon Lead", "Rave Hoover", "Hoover",
        "Grime Hoover", "Goa Lead", "Laser Lead", "Hard Lead", "Scream Lead",
        "Rage Lead", "Wire Lead", "Dream Lead", "Silk Lead", "Haze Lead",
        "PWM Lead", "Wavetable Lead", "Acid Lead", "Emo Lead",
        "EDM Anthem Lead", "Neon Rave Lead" };
    const char* const kEdmPluck[] = {
        "Trance Pluck", "Dance Pluck", "Neon Pluck", "Crystal Pluck", "Glass Pluck",
        "Bubble Pluck", "Droplet Pluck", "Ice Pluck", "Water Pluck", "Water Drop",
        "Tropic Pluck", "Sunny Pluck", "Isla Pluck", "Dembow Pluck", "Arp Synth",
        "Italo Arp", "Trance Gate", "Gate Dream", "Modular Blip", "Tech Blip",
        "Festival Arp" };
    const char* const kEdmPad[] = {
        "Big Pad", "Sidechain Pad", "Shimmer Pad", "Aurora Pad", "Dream Pad",
        "Nebula Pad", "Ocean Pad", "Ice Pad", "Frost Pad", "Glass Pad",
        "Polar Lights", "Solar Winds", "Deep Space", "Sunset Haze", "Vapor Pad",
        "Vapor Wash", "Analog Sweep", "Analog Wash", "Cinema Strings",
        "Cinema Swell", "Grain Cloud", "Ambient FX", "Drop Atmos" };
    const char* const kEdmBass[] = {
        "Sidechain Bass", "Future Bass", "Reese Bass", "Reese Growl", "Dark Reese",
        "Neuro Bass", "Growl Sub", "Gnarl Bass", "Wobble Growl", "Hoover Bass",
        "Donk Bass", "Bounce Bass", "Deep House", "Slap House", "Club Rumble",
        "Psy Stomp", "Metal Bass", "Rubber Bass", "Hyper Sub", "Liquid Sub",
        "Garage Sub", "Sub Drone", "Moog Bass", "Analog Warm", "Rave Reese", "Sub Pulse" };
    const char* const kEdmChord[] = {
        "Future Chords", "Future Chord", "Chord Synth", "Chord Keys", "French Chord",
        "2-Step Chord", "Soul Chords", "Rave Stab", "Bigroom Stab", "Techno Stab",
        "Gabber Stab", "Vox Stab", "Piano Chord", "Retro Pop Poly", "Warm Poly",
        "80s Poly", "Future Rave Stab", "Sidechain Chord" };
    const char* const kEdmVocal[] = {
        "Vocal Chop Hit", "Chop Vox", "Vox Pluck", "Vox Choir", "Vox Ahh",
        "Vox Ahh Wide", "Angel Choir", "Diva Vox", "Reverse Vocal", "Vocal Harmony",
        "Vocal Loop", "Whisper Air", "Vox Siren Air", "Robot Vox" };
    const char* const kEdmFx[] = {
        "Riser Sweep", "Noise Riser", "Noise Sweep", "Downlifter", "Reverse Cymbal",
        "Sub Drop", "Impact Hit", "Cinema Braam", "Rave Hit", "Air Horn",
        "Sci-Fi Sweep", "Laser Zap", "Tape Rewind", "Tonal Wind", "Horror Drone",
        "Ambient Drone" };
    const char* const kEdmDrums[] = {
        "Festival Kick", "Kick Punch", "Kick 808", "Clap", "Finger Snap",
        "Hat Closed", "Hat Open", "Hat Tight", "Crash Splash", "Ride Ping",
        "Perc 909", "Big Drums", "Drum Fill", "Drum Kit" };

    const GenreGroup kEdmGroups[] = {
        SLYCE_GROUP ("Supersaw & Lead",   kEdmLead),
        SLYCE_GROUP ("Pluck & Arp",       kEdmPluck),
        SLYCE_GROUP ("Pad & Atmosphere",  kEdmPad),
        SLYCE_GROUP ("Bass",              kEdmBass),
        SLYCE_GROUP ("Chord & Stab",      kEdmChord),
        SLYCE_GROUP ("Vocal",             kEdmVocal),
        SLYCE_GROUP ("FX & Riser",        kEdmFx),
        SLYCE_GROUP ("Drums",             kEdmDrums) };

    // --- HIP-HOP: 150 ------------------------------------------------------
    const char* const kHipBass[] = {
        "Sub 808", "Memphis 808", "Rage 808", "Drill 808", "Chart 808",
        "Growl 808", "Trap Knock", "FM Knock", "Drift Phonk", "Drill Slide",
        "Dusty Bass", "Rust Bass", "Talk Bass", "Lately Bass", "Pluck Bass",
        "Jersey Boom", "UK Bass", "Seoul Bass", "Metal Bass", "Liquid Sub",
        "Garage Sub", "Hyper Sub", "Whisper Bass", "Boogie Bass", "Syn Bass Gtr",
        "Moog Bass" };
    const char* const kHipDrums[] = {
        "Trap Snare", "Snare 808", "Snare Tight", "Hat Roll", "Hat Trap Open",
        "Hat Sizzle", "Hat Pedal", "Hat Closed", "Hat Open", "Kick 808",
        "Kick Punch", "Clap", "Rim Snap", "Rim Perc", "Perc Click",
        "Cowbell 808", "Phonk Cowbell", "Woodblock", "Clave", "Shaker",
        "Tambourine", "Tom Low", "Tom High", "Drum Machine", "Drum Fill",
        "Drum Kit" };
    const char* const kHipKeys[] = {
        "RnB Rhodes", "Tine EP", "Suitcase EP", "Soft EP", "EP Keys",
        "Wurli EP", "Dusty Keys", "Night Keys", "Noir Keys", "Ghost Keys",
        "Lo-Fi Keys", "Vinyl Keys", "Moody Keys", "Soul Keys", "Soul Sample",
        "Gospel Organ", "Full Organ", "Retro Organ", "Piano Stab", "Felt Piano" };
    const char* const kHipBell[] = {
        "Rage Bell", "Drill Bell Lead", "Cloud Bell", "Night Bell", "Deep Bell",
        "Glass Bell", "Ice Bell", "Crystal Bell", "Fairy Bell", "Syn Glock",
        "Syn Celesta", "Syn Vibes", "Syn Handbell", "Toy Bell", "Trap Flute",
        "Drill Flute", "Phonk Whistle", "Hook Marimba" };
    const char* const kHipGuitar[] = {
        "Syn Nylon", "Nylon Soft", "Syn Acoustic", "Syn Clean Gtr", "Syn Jazz Gtr",
        "Syn Mute Gtr", "Muted Chug", "Crunch Syn", "Drive Lead Gtr", "Syn Spanish",
        "Syn Slide", "Syn 12-String", "Chorus Gtr", "Guitar Loop" };
    const char* const kHipDark[] = {
        "Dark Synth", "Dark Rage", "Dark Pad", "Drill Dark Pad", "Tension Bed",
        "Sub Drone", "Nebula Drone", "Horror Drone", "Deep Space", "Submerged",
        "Hollow Glass", "Dusk Pad", "Velvet Pad", "Soul Pad", "Warm Strings",
        "Ensemble Str", "Cold Strings", "Tape Strings", "Cinema Strings",
        "Analog Strings" };
    const char* const kHipVocal[] = {
        "Chop Vox", "Vocal Adlib", "Vocal Loop", "Vocal Harmony", "Formant Vocal",
        "Talkbox Vox", "Robot Vox", "Vox Hum", "Vox Hum Ooh", "Vox Ooh",
        "Deep Choir", "Vox Chant Low", "Vox Doo Choir", "Whisper Air",
        "Beatbox Kick", "Beatbox Snare" };
    const char* const kHipFx[] = {
        "Vinyl Crackle", "Ambient FX", "Brass Stab", "Funk Brass", "Anthem Brass",
        "Air Horn", "Sub Drop", "Impact Hit", "Reverse Cymbal", "Tape Rewind" };

    const GenreGroup kHipGroups[] = {
        SLYCE_GROUP ("808 & Bass",        kHipBass),
        SLYCE_GROUP ("Drums & Perc",      kHipDrums),
        SLYCE_GROUP ("Keys & Piano",      kHipKeys),
        SLYCE_GROUP ("Bell & Melody",     kHipBell),
        SLYCE_GROUP ("Guitar",            kHipGuitar),
        SLYCE_GROUP ("Dark Synth & Pad",  kHipDark),
        SLYCE_GROUP ("Vocal",             kHipVocal),
        SLYCE_GROUP ("FX & Texture",      kHipFx) };

    // --- POP: 100 ----------------------------------------------------------
    const char* const kPopPiano[] = {
        "Royal Grand", "Concert Bright", "Emotional Piano", "Piano Chord",
        "Ballad Keys", "Felt Piano", "Syn Grand", "Syn Bright", "Syn Pop Key",
        "Syn Soft Key", "House Keys", "Glass CP80", "DX Piano", "Soft Keys",
        "Dream Keys", "K-RnB Keys", "Amapiano Keys", "Smooth EP" };
    const char* const kPopGuitar[] = {
        "Disco Guitar", "Funk Gtr Syn", "Pop Mute Gtr", "Syn Clean Gtr",
        "Syn Acoustic", "Syn Steel", "Syn Nylon", "Tropic Gtr", "Chime Syn",
        "Chorus Gtr", "Latin Guitar Pl", "Guitar Loop" };
    const char* const kPopSynth[] = {
        "K-Pop Saw", "Idol Pluck", "Silk Lead", "Velvet Lead", "Neon Lead",
        "Midnight Lead", "Neon 84 Lead", "Retro Lead", "Italo Lead", "Analog Poly",
        "Warm Poly", "80s Poly", "Night Drive", "Glass Keys", "Glass Sync",
        "Wavetable Lead", "Chord Synth", "Arp Synth" };
    const char* const kPopBass[] = {
        "Syn Bass Gtr", "Slap Funk", "Octave Disco", "Outrun Bass", "Neon Bass",
        "Analog Warm", "Moog Bass", "Rubber Bass", "Sub 808", "Seoul Bass",
        "Deep House", "Bounce Bass", "Sidechain Bass", "Syn Jazz Bass" };
    const char* const kPopVocal[] = {
        "Vocal Chop Hit", "Chop Vox", "Vox Pluck", "Vocal Harmony", "Vocal Adlib",
        "Vocal Loop", "Vox Lead", "Vox Ahh", "Vox Ooh", "Angel Choir",
        "Boys Choir", "Choir Air", "Diva Vox", "Whisper Air", "Formant Vocal",
        "Reverse Vocal" };
    const char* const kPopStrings[] = {
        "Warm Strings", "Ensemble Str", "Tape Strings", "Cinema Strings",
        "Analog Strings", "PWM Strings", "Big Pad", "Shimmer Pad", "Soul Pad",
        "Velvet Pad", "Juno Warmth", "Sunset Haze" };
    const char* const kPopDrums[] = {
        "Drum Machine", "Perc 909", "Clap", "Finger Snap", "Hat Tight",
        "Kick Punch", "Snare Tight", "Tambo Jingle", "Riser Sweep", "Impact Hit" };

    const GenreGroup kPopGroups[] = {
        SLYCE_GROUP ("Piano & Keys",      kPopPiano),
        SLYCE_GROUP ("Guitar",            kPopGuitar),
        SLYCE_GROUP ("Synth & Lead",      kPopSynth),
        SLYCE_GROUP ("Bass",              kPopBass),
        SLYCE_GROUP ("Vocal",             kPopVocal),
        SLYCE_GROUP ("Strings & Pad",     kPopStrings),
        SLYCE_GROUP ("Drums & FX",        kPopDrums) };

    #undef SLYCE_GROUP

    const GenreBank kGenreBanks[] = {
        { "EDM",     kEdmGroups, (int) (sizeof (kEdmGroups) / sizeof (kEdmGroups[0])) },
        { "HIP-HOP", kHipGroups, (int) (sizeof (kHipGroups) / sizeof (kHipGroups[0])) },
        { "POP",     kPopGroups, (int) (sizeof (kPopGroups) / sizeof (kPopGroups[0])) } };

    constexpr int kNumGenreBanks = (int) (sizeof (kGenreBanks) / sizeof (kGenreBanks[0]));
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

juce::StringArray VocalChopAudioProcessor::getGenreBankNames()
{
    juce::StringArray a;
    for (const auto& b : kGenreBanks)
        a.add (b.name);
    return a;
}

juce::StringArray VocalChopAudioProcessor::getGenreRoleNames (int bank)
{
    juce::StringArray a;
    if (juce::isPositiveAndBelow (bank, kNumGenreBanks))
        for (int g = 0; g < kGenreBanks[bank].numGroups; ++g)
            a.add (kGenreBanks[bank].groups[g].role);
    return a;
}

juce::StringArray VocalChopAudioProcessor::getGenreRoleInstruments (int bank, int role)
{
    juce::StringArray a;
    if (! juce::isPositiveAndBelow (bank, kNumGenreBanks))
        return a;
    const auto& b = kGenreBanks[bank];
    if (! juce::isPositiveAndBelow (role, b.numGroups))
        return a;
    for (int i = 0; i < b.groups[role].count; ++i)
        a.add (b.groups[role].names[i]);
    return a;
}

int VocalChopAudioProcessor::getGenreBankSize (int bank)
{
    if (! juce::isPositiveAndBelow (bank, kNumGenreBanks))
        return 0;
    int n = 0;
    for (int g = 0; g < kGenreBanks[bank].numGroups; ++g)
        n += kGenreBanks[bank].groups[g].count;
    return n;
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
        const auto d = voicedDefinition(idx);
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
        kp.attackNoise = p.attackNoise.load();
        kp.attackTone = p.attackTone.load();
        kp.attackMs = p.attackMs.load();
        kp.inharmonic = p.inharmonic.load();
        kp.stringMix = p.stringMix.load();
        kp.stringDamp = p.stringDamp.load();
        kp.stringDecay = p.stringDecay.load();
        kp.morphAmt = p.morphAmt.load();
        kp.morphMs = p.morphMs.load();
        kp.morphTo = p.morphTo.load();
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

    p.attackNoise.store (kp.attackNoise);
    p.attackTone.store (kp.attackTone);
    p.attackMs.store (kp.attackMs);
    p.inharmonic.store (kp.inharmonic);
    p.stringMix.store (kp.stringMix);
    p.stringDamp.store (kp.stringDamp);
    p.stringDecay.store (kp.stringDecay);
    p.morphAmt.store (kp.morphAmt);
    p.morphMs.store (kp.morphMs);
    p.morphTo.store (kp.morphTo);
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
    const auto d = voicedDefinition(i);

    auto& p = synthEngine.patch();
    slyce::factory::configureVoice (p, d);
    p.outputTrimDb = slyce::factory::levelTrimsDb[i];
    // Mirror RESOLVED values, not raw table values. Otherwise the next audio
    // block silently undoes FM caps, breath and choir vibrato.
    moduleDefaults = { p.unison.load(), p.stereoSpread.load(), p.subLevel.load(),
                       p.noiseLevel.load(), p.fmAmount.load(), p.vibDepthCents.load(),
                       p.chorusMix.load(), p.glideMs.load() };

    currentInstrument = i;
}

void VocalChopAudioProcessor::resetScopeRing() noexcept
{
    for (auto& sample : scopeRing)
        sample.store (0.0f, std::memory_order_relaxed);
    scopeWritePos.store (0, std::memory_order_release);
}

void VocalChopAudioProcessor::applyInstrument (int instrumentIndex)
{
    const juce::ScopedLock patchLock (getCallbackLock());
    // A browser change must replace the sound immediately. Without choking
    // the previous synth/sampler voices, a held MIDI note could keep the old
    // patch ringing and make VST3 users think the new instrument did not load.
    synthEngine.chokeAll();
    samplerEngine.chokeAll();
    applyEnginePatch (instrumentIndex);
    resetScopeRing();
    const auto d = voicedDefinition(currentInstrument);

    const ScopedAtomicPatch patching (applyingPatch);

    // Skips anything the user has taken over. Auditioning instruments used to
    // reset the whole output FX rack on every step: turn the reverb down
    // because it is too wet, press >, and it is back up - so the one thing a
    // tester complained about could not be fixed from inside the plugin.
    auto set = [this] (const juce::String& id, float value)
    {
        if (ownsFx(id))
            return;
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Common baseline, then the instrument's knob values.
    set ("engine",       1.0f);          // Synth mode
    // Browser selection means "play this instrument normally". Rhythmic
    // gating is recalled only by an explicit SOUND/ARP preset; it must not
    // leak from the previous preset into every lead, bass and pad.
    set ("pumpAmt",      0.0f);
    // Low pass wide open, NOT off. 20 kHz passes everything so this sounds
    // identical, but leaving it OFF is what made the filter look broken:
    // picking any instrument switched the filter off underneath you, so the
    // cutoff and resonance knobs turned and nothing happened. The default and
    // the preset path were both fixed for this and this path was missed - it
    // is the one that runs every single time someone changes sound.
    set ("filterType",   1.0f);   set ("filterCutoff", 20000.0f);
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

    // ARP is a real browser family, not just an oscillator name. Selecting one
    // therefore recalls a complete playable arpeggiator state. The sequencer
    // itself uses the existing fixed 128-note arrays in advanceArp(); these
    // data-only patches do not allocate or add DSP on the audio thread.
    const juce::String category (d.category), name (d.name);
    if (category == "ARP")
    {
        // Generic ARP names used to collapse to the same Up/1/16 patch. Give
        // them a stable musical variation from their factory slot; explicit
        // names still win so presets such as Random/Gate remain intentional.
        const int arpSlot = juce::jmax (0, instrumentIndex);
        int mode = 1 + (arpSlot % 4); // Up, Down, Up-Down, Random
        int rate = 2 + ((arpSlot * 3) % 4); // 1/3 .. 1/32 grid
        int octaves = 1 + ((arpSlot / 2) % 3);
        float gate = 0.44f + 0.08f * (float) ((arpSlot * 5) % 5);
        if (name.containsIgnoreCase ("Cascade")) mode = 3;
        else if (name.containsIgnoreCase ("Random")) mode = 4;
        else if (name.containsIgnoreCase ("Down")) mode = 2;
        else if (name.containsIgnoreCase ("Up")) mode = 1;
        if (name.containsIgnoreCase ("Gate") || name.containsIgnoreCase ("Techno")) rate = 5;
        else if (name.containsIgnoreCase ("Dream") || name.containsIgnoreCase ("Airy")) rate = 3;
        else if (name.containsIgnoreCase ("Fast")) rate = 4;
        if (name.containsIgnoreCase ("Octave") || name.containsIgnoreCase ("Cascade")) octaves = 2;
        if (name.containsIgnoreCase ("Gate")) gate = 0.42f;
        set ("arpMode", (float) mode);
        set ("arpRate", (float) rate);
        // arpOct is zero-based extra octaves; advanceArp adds one.
        set ("arpOct",  (float) (octaves - 1));
        set ("arpGate", juce::jlimit (0.30f, 0.86f, gate));
    }
    else
    {
        set ("arpMode", 0.0f);
    }
}

//==============================================================================
void VocalChopAudioProcessor::appendSoundState(juce::XmlElement& root)
{
    std::shared_ptr<juce::AudioBuffer<float>> source;
    double rate=44100.0;
    {
        const juce::ScopedLock guard(sampleStateLock);
        source=sampleBuffer; rate=loadedSampleRate;
        root.setAttribute("samplePath",loadedSampleFile.getFullPathName());
        root.setAttribute("sampleName",loadedSampleName);
        root.setAttribute("demoIndex",currentDemo);
        root.setAttribute("sfzPath",loadedSfzFile.getFullPathName());
        root.setAttribute("sliceMode",(int)sliceEngine.getMode());
        root.setAttribute("gridDiv",sliceEngine.getGridDivision());
        root.setAttribute("sensitivity",(double)sliceEngine.getSensitivity());
        root.setAttribute("selectedSlice",selectedSlice.load());
        auto* points=root.createNewChildElement("Slices");
        for(int i=0;i<sliceEngine.getNumSlices();++i)if(auto sl=sliceEngine.getSlice(i)) {
            auto* item=points->createNewChildElement("Slice");
            item->setAttribute("start",sl->startSample);
            item->setAttribute("transpose",(double)getSliceTranspose(i));
        }
    }
    root.setAttribute("instrument",currentInstrument.load());
    root.setAttribute("instrumentName",getInstrumentNames()[currentInstrument.load()]);
    root.setAttribute("airVocalPreset", currentAirVocalPreset.load());
    vocalKitEngine.writeState (root);
    if(source) {
        auto bytes=slyce::SampleStateCodec::encode(*source,rate);
        if(!bytes.empty())root.createNewChildElement("SamplePCM")->addTextElement(
            juce::MemoryBlock(bytes.data(),bytes.size()).toBase64Encoding());
    }
    if(auto params=apvts.copyState().createXml())root.addChildElement(params.release());
}

void VocalChopAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Saving is always lossless, including the periodically-muted demo.
    // The license changes audio muting, never whether a user's work survives.
    juce::XmlElement root("VocalChopState");
    root.setAttribute("version",2);
    appendSoundState(root);
    root.setAttribute("theme",ThemeManager::current());
    root.setAttribute("themeName",ThemeManager::active().name);
    root.setAttribute("themeSet",2);
    root.setAttribute("referenceThemeScheme", referenceThemeScheme.load());
    for (int i = 0; i < 15; ++i)
        root.setAttribute ("looperMidiCC" + juce::String (i), getLooperMidiCC (i));
    auto loopBytes=looper.serialize();
    if(!loopBytes.empty())root.createNewChildElement("LooperState")->addTextElement(
        juce::MemoryBlock(loopBytes.data(),loopBytes.size()).toBase64Encoding());

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

    referenceThemeScheme.store (juce::jlimit (0, 8,
        xml->getIntAttribute ("referenceThemeScheme", 0)));
    for (int i = 0; i < 15; ++i)
        if (xml->hasAttribute ("looperMidiCC" + juce::String (i)))
            setLooperMidiCC (i, xml->getIntAttribute ("looperMidiCC" + juce::String (i),
                                                       getLooperMidiCC (i)));

    const juce::ScopedLock callbackGuard(getCallbackLock());

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    // Kit files are decoded here, off the audio callback. Missing files stay
    // represented by their slot and path so the editor can offer relink.
    vocalKitEngine.restoreState (*xml);
    restoreEmbeddedVocalKitSources();
    loadAirVocalPresetBank (xml->getIntAttribute ("airVocalPreset", 0));

    // Prefer the saved theme NAME; sessions saved before the Studio themes
    // were prepended carry only an index into the OLD 7-theme list, which
    // now sits shifted by 8.
    {
        int themeIdx = -1;
        auto savedTheme = xml->getStringAttribute ("themeName");
        // Retired themes map to their nearest surviving relative instead of
        // silently landing on whatever index now sits in their old slot.
        // Generation 1 is anything saved before the palette became derived.
        const bool legacyNames = xml->getIntAttribute ("themeSet", 1) < 2;

        if (savedTheme == "Neon Rider" || savedTheme == "Neo-Seoul")
            savedTheme = "Neon Ocean";
        else if (! legacyNames)                 { /* names are current */ }
        else if (savedTheme == "Emerald")       savedTheme = "Mint";
        else if (savedTheme == "Royal Velvet")  savedTheme = "Indigo";
        else if (savedTheme == "Carbon")        savedTheme = "Ember";
        else if (savedTheme == "Space Gray")    savedTheme = "Graphite";
        // The "Studio *" set was replaced when the palette became derived
        // rather than hand-written. Each maps to the survivor closest in HUE,
        // so a reopened session looks like the one that was saved even though
        // no theme of that name exists any more.
        else if (savedTheme == "Studio Ocean")  savedTheme = "Cobalt";
        else if (savedTheme == "Studio Ice")    savedTheme = "Aqua";
        else if (savedTheme == "Studio Mint")   savedTheme = "Mint";
        else if (savedTheme == "Studio Amber")  savedTheme = "Sunset";
        else if (savedTheme == "Studio Rose")   savedTheme = "Signal";
        else if (savedTheme == "Studio Gold")   savedTheme = "Acid";
        else if (savedTheme == "Studio Mono")   savedTheme = "Graphite";
        else if (savedTheme == "Studio Red")    savedTheme = "Ember";
        else if (savedTheme == "Studio Indigo") savedTheme = "Indigo";
        else if (savedTheme == "Midnight")      savedTheme = "Aqua";
        else if (savedTheme == "Silver")        savedTheme = "Snow";
        else if (savedTheme == "Snow")          savedTheme = "Paper";
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
        // This applies only the selected engine architecture; the saved
        // parameter tree above remains authoritative for every knob value.
        applyEnginePatch (idx);
    }

    sliceEngine.setMode ((SliceEngine::Mode) juce::jlimit(0,2,xml->getIntAttribute("sliceMode",(int)SliceEngine::Transient)));
    sliceEngine.setGridDivision (xml->getIntAttribute ("gridDiv", 16));
    sliceEngine.setSensitivity ((float) xml->getDoubleAttribute ("sensitivity", 0.3));

    bool restoredSample=false;
    if(auto* encoded=xml->getChildByName("SamplePCM")) {
        juce::MemoryBlock bytes;
        if(bytes.fromBase64Encoding(encoded->getAllSubText())) {
            double rate=44100.0;
            if(auto decoded=slyce::SampleStateCodec::decode(bytes.getData(),bytes.getSize(),rate)) {
                const juce::ScopedLock sourceGuard(sampleStateLock);
                sampleBuffer=decoded; loadedSampleRate=rate;
                loadedSampleFile=juce::File(xml->getStringAttribute("samplePath"));
                loadedSampleName=xml->getStringAttribute("sampleName");
                currentDemo=juce::jlimit(-1,getNumDemoSamples()-1,xml->getIntAttribute("demoIndex",-1)); prevSampleBuffer.reset();
                reassignSampleToEngines(); rescanSlices(); analyzeSampleKey(); restoredSample=true;
            }
        }
    }
    const juce::File sample(xml->getStringAttribute("samplePath"));
    if(!restoredSample && sample.existsAsFile())restoredSample=loadSampleFromFile(sample,false);
    if(!restoredSample && xml->getIntAttribute("demoIndex",-1)>=0)
        restoredSample=loadDemoSample(xml->getIntAttribute("demoIndex"));
    if(!restoredSample) {
        const juce::ScopedLock sourceGuard(sampleStateLock);
        sampleBuffer.reset(); loadedSampleFile=juce::File(); loadedSampleName.clear(); currentDemo=-1;
        reassignSampleToEngines(); sliceEngine.rebuildSlices();
    }
    if(auto* points=xml->getChildByName("Slices")) {
        std::vector<int> starts;
        for(auto* item=points->getFirstChildElement();item!=nullptr;item=item->getNextElement())
            if(item->hasTagName("Slice"))starts.push_back(item->getIntAttribute("start"));
        if(restoredSample && sliceEngine.getMode()==SliceEngine::Manual)sliceEngine.sliceByManual(starts);
        int i=0;
        for(auto* item=points->getFirstChildElement();item!=nullptr;item=item->getNextElement())
            if(item->hasTagName("Slice"))setSliceTranspose(i++,(float)item->getDoubleAttribute("transpose"));
    }
    selectedSlice.store(juce::jlimit(-1,sliceEngine.getNumSlices()-1,xml->getIntAttribute("selectedSlice",-1)));
    // Loading embedded demos intentionally selects Chop; the saved controls win.
    if(auto* params=xml->getChildByName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*params));
    if(!xml->getBoolAttribute("soundOnly",false)) {
        if(auto* encoded=xml->getChildByName("LooperState")) {
            juce::MemoryBlock bytes;
            if(bytes.fromBase64Encoding(encoded->getAllSubText()))
                looper.restore(bytes.getData(),bytes.getSize());
        } else looper.tapClearAll(); // legacy session, never inherit another session's loops
    }

    // A missing SFZ must not leave a previous session's bank active.
    samplerEngine.clearBank(); loadedSfzFile=juce::File();
    // Restore the SFZ bank (external multi-sample assets remain file references).
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

    juce::XmlElement root("VocalChopPreset");
    root.setAttribute("version",2); root.setAttribute("soundOnly",true);
    appendSoundState(root);
    return root.writeTo(dir.getChildFile(clean + ".slyce"));
}

bool VocalChopAudioProcessor::loadUserPreset (const juce::String& name)
{
    const auto f = userPresetFolder()
                       .getChildFile (juce::File::createLegalFileName (name) + ".slyce");
    if (! f.existsAsFile())
        return false;

    auto xml = juce::parseXML (f);
    if(xml == nullptr)return false;
    if(xml->hasTagName(apvts.state.getType())) { // legacy parameter-only preset
        apvts.replaceState(juce::ValueTree::fromXml(*xml)); sendChangeMessage(); return true;
    }
    if(!xml->hasTagName("VocalChopPreset"))return false;
    xml->setTagName("VocalChopState"); xml->setAttribute("soundOnly",true);
    juce::MemoryBlock bytes; copyXmlToBinary(*xml,bytes);
    setStateInformation(bytes.getData(),(int)bytes.getSize()); return true;
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
    return new SlyceReferenceEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalChopAudioProcessor();
}
