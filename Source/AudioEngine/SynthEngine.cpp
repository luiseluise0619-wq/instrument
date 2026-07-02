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

    inline double midiToHz (double note)
    {
        return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
    }

    inline void advance (double& phase, double inc)
    {
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
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
        v.autoOffCounter = -1;
        v.lp1L = v.lp2L = v.lp1R = v.lp2R = 0.0f;
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
    const double baseNote = juce::jlimit (0, 127, midiNote) + octave * 12;
    const double hz       = midiToHz (baseNote);

    v.note     = midiNote;
    v.velocity = juce::jlimit (0.0f, 1.0f, velocity);

    // --- Unison bank: symmetric detune spread + stereo panning -------------
    v.unison = juce::jlimit (1, kMaxUnison, patchSettings.unison.load());
    const float spread = juce::jlimit (0.0f, 1.0f, patchSettings.stereoSpread.load());

    for (int u = 0; u < v.unison; ++u)
    {
        // Position in -1..+1 across the stack (0 for a single osc).
        const float pos = v.unison > 1
                            ? -1.0f + 2.0f * (float) u / (float) (v.unison - 1)
                            : 0.0f;

        const double cents = (double) detuneCents * pos;
        v.incs[u] = hz * std::pow (2.0, cents / 1200.0) / sampleRate;
        // Keep phases free-running for smooth retriggers.

        // Equal-power pan across the stack, scaled by stereoSpread.
        const float pan = 0.5f + 0.5f * pos * spread;             // 0..1
        v.panL[u] = std::cos (pan * juce::MathConstants<float>::halfPi);
        v.panR[u] = std::sin (pan * juce::MathConstants<float>::halfPi);
    }
    v.unisonNorm = 1.0f / std::sqrt ((float) v.unison);

    // --- Sub / noise / FM / vibrato snapshots -------------------------------
    v.subLevel   = juce::jlimit (0.0f, 1.0f, patchSettings.subLevel.load());
    v.noiseLevel = juce::jlimit (0.0f, 1.0f, patchSettings.noiseLevel.load());
    v.fmAmount   = juce::jlimit (0.0f, 1.0f, patchSettings.fmAmount.load());

    v.subInc    = (hz * 0.5) / sampleRate;
    v.fmCarInc  = hz / sampleRate;
    v.fmModInc  = (hz * juce::jmax (0.1f, patchSettings.fmRatio.load())) / sampleRate;

    v.vibDepthCents = patchSettings.vibDepthCents.load();
    v.vibInc        = patchSettings.vibRateHz.load() / sampleRate;

    // --- Per-voice filter ----------------------------------------------------
    v.fltBaseHz = juce::jlimit (40.0f, 20000.0f, patchSettings.filterCutoff.load());
    v.fltEnvOct = juce::jlimit (0.0f, 6.0f, patchSettings.filterEnvOct.load());
    v.fenv      = 1.0f;
    const float envMs = juce::jmax (5.0f, patchSettings.filterEnvMs.load());
    v.fenvCoeff = std::exp (-1.0f / (envMs * 0.001f * (float) sampleRate));
    v.fltUpdateCounter = 0;
    v.lp1L = v.lp2L = v.lp1R = v.lp2R = 0.0f;

    // --- Amp ADSR ------------------------------------------------------------
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

    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;

        for (int n = 0; n < numSamples; ++n)
        {
            // --- Vibrato (cheap cents -> ratio approximation) ---------------
            float vibRatio = 1.0f;
            if (v.vibDepthCents > 0.0f)
            {
                const float cents = v.vibDepthCents
                                    * std::sin (twoPi * (float) v.vibPhase);
                vibRatio = 1.0f + cents * 0.000578f;   // ~2^(c/1200) for small c
                advance (v.vibPhase, v.vibInc);
            }

            // --- Unison oscillator bank -------------------------------------
            float oscL = 0.0f, oscR = 0.0f;
            for (int u = 0; u < v.unison; ++u)
            {
                const double inc = v.incs[u] * vibRatio;
                const float  s   = renderOsc (v.phases[u], inc);
                oscL += s * v.panL[u];
                oscR += s * v.panR[u];
                advance (v.phases[u], inc);
            }
            oscL *= v.unisonNorm;
            oscR *= v.unisonNorm;

            // --- FM/PM pair (bells / EPs), blended with the osc bank --------
            if (v.fmAmount > 0.0f)
            {
                const float mod = std::sin (twoPi * (float) v.fmModPhase);
                const float car = std::sin (twoPi * (float) v.fmCarPhase
                                            + v.fmAmount * 4.0f * mod);
                advance (v.fmCarPhase, v.fmCarInc * vibRatio);
                advance (v.fmModPhase, v.fmModInc * vibRatio);

                const float blend = v.fmAmount;
                oscL = oscL * (1.0f - blend) + car * blend;
                oscR = oscR * (1.0f - blend) + car * blend;
            }

            // --- Sub oscillator + noise (centre) ----------------------------
            if (v.subLevel > 0.0f)
            {
                const float sub = std::sin (twoPi * (float) v.subPhase) * v.subLevel;
                advance (v.subPhase, v.subInc);
                oscL += sub;
                oscR += sub;
            }
            if (v.noiseLevel > 0.0f)
            {
                const float nz = (noiseRng.nextFloat() * 2.0f - 1.0f) * v.noiseLevel;
                oscL += nz;
                oscR += nz;
            }

            // --- Per-voice lowpass with its own decay envelope --------------
            if (v.fltBaseHz < 19000.0f || v.fltEnvOct > 0.0f)
            {
                if (--v.fltUpdateCounter <= 0)
                {
                    v.fltUpdateCounter = 32;
                    const float hz = juce::jmin (20000.0f,
                        v.fltBaseHz * std::pow (2.0f, v.fltEnvOct * v.fenv));
                    v.fltG = 1.0f - std::exp (-twoPi * hz / (float) sampleRate);
                }
                v.fenv *= v.fenvCoeff;

                v.lp1L += v.fltG * (oscL - v.lp1L);
                v.lp2L += v.fltG * (v.lp1L - v.lp2L);
                oscL = v.lp2L;
                v.lp1R += v.fltG * (oscR - v.lp1R);
                v.lp2R += v.fltG * (v.lp1R - v.lp2R);
                oscR = v.lp2R;
            }

            // --- Amp envelope & mix ------------------------------------------
            const float env = v.envelope() * v.velocity * 0.32f;
            if (R != nullptr)
            {
                L[n] += oscL * env;
                R[n] += oscR * env;
            }
            else
            {
                L[n] += (oscL + oscR) * 0.5f * env;   // mono host
            }

            // --- Envelope state machine --------------------------------------
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
