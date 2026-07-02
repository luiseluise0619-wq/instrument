#include "SynthEngine.h"
#include <cmath>

namespace
{
    // PolyBLEP residual: smooths the discontinuity at a wrap point so saw and
    // square don't alias. t = current phase (0..1), dt = phase increment.
    inline float polyBlep (float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    inline double midiToHz (int note)
    {
        return 440.0 * std::pow (2.0, (note - 69) / 12.0);
    }
}

//==============================================================================
float SynthEngine::Voice::envelope() const
{
    switch (stage)
    {
        case Stage::attack:  return (float) stagePos / (float) attackSamples;
        case Stage::decay:   return 1.0f + ((float) stagePos / (float) decaySamples)
                                          * (sustainLevel - 1.0f);
        case Stage::sustain: return sustainLevel;
        case Stage::release: return releaseFrom
                                    * (1.0f - (float) stagePos / (float) releaseSamples);
        case Stage::idle:
        default:             return 0.0f;
    }
}

//==============================================================================
void SynthEngine::prepare (juce::dsp::ProcessSpec spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices)
    {
        v.stage = Voice::Stage::idle;
        v.note  = -1;
        v.phase1 = v.phase2 = 0.0;
        v.autoOffCounter = -1;
    }
}

void SynthEngine::setEnvelope (float a, float d, float s, float r)
{
    attackMs   = juce::jmax (0.0f, a);
    decayMs    = juce::jmax (0.0f, d);
    sustainLvl = juce::jlimit (0.0f, 1.0f, s);
    releaseMs  = juce::jmax (0.0f, r);
}

SynthEngine::Voice* SynthEngine::findFreeVoice()
{
    for (auto& v : voices)
        if (! v.isActive())
            return &v;
    return &voices[0];   // steal deterministically
}

void SynthEngine::startVoice (Voice& v, int midiNote, float velocity, int autoOffSamples)
{
    const double hz     = midiToHz (juce::jlimit (0, 127, midiNote + octave * 12));
    const double detune = std::pow (2.0, detuneCents / 1200.0);

    v.note     = midiNote;
    v.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    v.inc1     = hz / sampleRate;
    v.inc2     = (hz * detune) / sampleRate;
    // Keep phases running (free-running oscillators blend retriggers nicely).

    v.attackSamples  = juce::jmax (1, (int) (attackMs  * 0.001 * sampleRate));
    v.decaySamples   = juce::jmax (1, (int) (decayMs   * 0.001 * sampleRate));
    v.releaseSamples = juce::jmax (1, (int) (releaseMs * 0.001 * sampleRate));
    v.sustainLevel   = sustainLvl;

    v.stage       = Voice::Stage::attack;
    v.stagePos    = 0;
    v.releaseFrom = 1.0f;
    v.autoOffCounter = autoOffSamples;
}

void SynthEngine::noteOn (int midiNote, float velocity)
{
    startVoice (*findFreeVoice(), midiNote, velocity, -1);
}

void SynthEngine::tapNote (int midiNote, float velocity)
{
    // Hold ~300 ms then auto-release, so a mouse click plays a note.
    startVoice (*findFreeVoice(), midiNote, velocity, (int) (0.3 * sampleRate));
}

void SynthEngine::noteOff (int midiNote)
{
    for (auto& v : voices)
    {
        if (v.isActive() && v.note == midiNote
            && v.stage != Voice::Stage::release)
        {
            v.releaseFrom = juce::jlimit (0.0f, 1.0f, v.envelope());
            v.stage       = Voice::Stage::release;
            v.stagePos    = 0;
        }
    }
}

void SynthEngine::releaseAll()
{
    for (auto& v : voices)
    {
        if (v.isActive() && v.stage != Voice::Stage::release)
        {
            v.releaseFrom = juce::jlimit (0.0f, 1.0f, v.envelope());
            v.stage       = Voice::Stage::release;
            v.stagePos    = 0;
        }
    }
}

//==============================================================================
float SynthEngine::renderOsc (double phase, double inc) const
{
    const float t  = (float) phase;
    const float dt = (float) inc;

    switch (wave)
    {
        case Saw:
            return (2.0f * t - 1.0f) - polyBlep (t, dt);

        case Square:
        {
            float sq = t < 0.5f ? 1.0f : -1.0f;
            sq += polyBlep (t, dt);
            sq -= polyBlep (std::fmod (t + 0.5f, 1.0f), dt);
            return sq;
        }

        case Sine:
            return std::sin (juce::MathConstants<float>::twoPi * t);

        case Triangle:
        default:
            return 4.0f * std::abs (t - 0.5f) - 1.0f;
    }
}

void SynthEngine::render (juce::AudioBuffer<float>& out, int numSamples)
{
    const int numChannels = out.getNumChannels();
    if (numChannels == 0)
        return;

    float* L = out.getWritePointer (0);
    float* R = numChannels > 1 ? out.getWritePointer (1) : nullptr;

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;

        for (int n = 0; n < numSamples; ++n)
        {
            const float env = v.envelope() * v.velocity * 0.35f; // headroom

            // Osc 1 slightly left, detuned osc 2 slightly right for width.
            const float o1 = renderOsc (v.phase1, v.inc1);
            const float o2 = renderOsc (v.phase2, v.inc2);

            L[n] += (o1 * 0.65f + o2 * 0.35f) * env;
            if (R != nullptr)
                R[n] += (o1 * 0.35f + o2 * 0.65f) * env;

            v.phase1 += v.inc1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            v.phase2 += v.inc2; if (v.phase2 >= 1.0) v.phase2 -= 1.0;

            // Envelope state machine.
            ++v.stagePos;
            switch (v.stage)
            {
                case Voice::Stage::attack:
                    if (v.stagePos >= v.attackSamples) { v.stage = Voice::Stage::decay; v.stagePos = 0; }
                    break;
                case Voice::Stage::decay:
                    if (v.stagePos >= v.decaySamples) { v.stage = Voice::Stage::sustain; v.stagePos = 0; }
                    break;
                case Voice::Stage::sustain:
                    break;
                case Voice::Stage::release:
                    if (v.stagePos >= v.releaseSamples) { v.stage = Voice::Stage::idle; v.note = -1; }
                    break;
                case Voice::Stage::idle:
                default:
                    break;
            }
            if (! v.isActive())
                break;

            // Auto note-off for UI taps.
            if (v.autoOffCounter > 0 && --v.autoOffCounter == 0
                && v.stage != Voice::Stage::release)
            {
                v.releaseFrom = juce::jlimit (0.0f, 1.0f, v.envelope());
                v.stage       = Voice::Stage::release;
                v.stagePos    = 0;
            }
        }
    }
}
