#include "SynthEngine.h"
#include <cmath>

namespace
{
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
        // Full wrap, not a single subtract: pitch envelopes can push inc
        // past 1.0 (drum drops on very high keys), and a partial wrap lets
        // the phase ratchet upward until the oscillator formulas explode.
        phase += inc;
        if (phase >= 1.0) phase -= std::floor (phase);
    }
}

//==============================================================================
float SynthEngine::Voice::envelope() const
{
    // Curved stages: linear ramps sound cheap; these bloom (ease-out attack)
    // and die away with an exponential feel (squared falloff) — all without
    // any pow() on the audio thread.
    switch (stage)
    {
        case Stage::attack:
        {
            const float t = (float) stagePos / (float) attackSamples;
            const float u = 1.0f - t;
            return 1.0f - u * u;                       // ease-out rise
        }
        case Stage::decay:
        {
            const float t = (float) stagePos / (float) decaySamples;
            const float u = 1.0f - t;
            return sustainLevel + (1.0f - sustainLevel) * u * u;
        }
        case Stage::sustain:
            return sustainLevel;
        case Stage::release:
        {
            const float t = (float) stagePos / (float) releaseSamples;
            const float u = 1.0f - t;
            return releaseFrom * u * u;                // natural die-away
        }
        case Stage::idle:
        default:
            return 0.0f;
    }
}

//==============================================================================
void SynthEngine::prepare (juce::dsp::ProcessSpec spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    scratch.setSize (2, juce::jmax (16, (int) spec.maximumBlockSize), false, false, true);

    for (auto& line : chorusLine)
        line.assign ((size_t) kChorusSize, 0.0f);
    chorusWrite = 0;
    chorusLfo   = 0.0;

    // Force a formant-coefficient rebuild: they are normalised to the
    // sample rate, and a device change would otherwise shift every vowel.
    formantVowelSet = -2;

    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices)
    {
        v.stage = Voice::Stage::idle;
        v.note  = -1;
        v.autoOffCounter = -1;
        v.ic1L = v.ic2L = v.ic1R = v.ic2R = 0.0f;
        v.driftCents = 0.0f;
        v.glideRatio = 1.0f;
        v.glideCoeff = 0.0f;
    }
    lastStartedNote = -1;   // the first note after a reset must not slide in
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

    // All voices busy: steal the least audible one — prefer voices already
    // in release, then the lowest envelope. Stealing voices[0] blindly cut
    // off held notes.
    Voice* best     = &voices[0];
    float  bestCost = 1.0e9f;

    for (auto& v : voices)
    {
        const float cost = v.envelope()
                         + (v.stage == Voice::Stage::release ? 0.0f : 10.0f);
        if (cost < bestCost)
        {
            bestCost = cost;
            best     = &v;
        }
    }
    return best;
}

void SynthEngine::startVoice (Voice& v, int midiNote, float velocity, int autoOffSamples,
                              int pitchNoteOverride)
{
    // Drum-kit mode plays a FIXED pitch per piece while the voice stays keyed
    // to the pressed note (so note-off still matches).
    const int pitchNote   = pitchNoteOverride >= 0 ? pitchNoteOverride : midiNote;
    // Kit pieces bake their own octave into the override pitch - adding the
    // engine octave again dropped every drum a further 12 semitones.
    const double baseNote = juce::jlimit (0, 127, pitchNote)
                          + (pitchNoteOverride >= 0 ? 0 : octave * 12);
    const double hz       = midiToHz (baseNote);

    v.note     = midiNote;
    v.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    v.waveSnap = wave;   // per-voice: later engine-wave swaps can't mutate it

    // --- Unison bank ---------------------------------------------------------
    v.unison = juce::jlimit (1, kMaxUnison, patchSettings.unison.load());
    const float spread = juce::jlimit (0.0f, 1.0f, patchSettings.stereoSpread.load());

    for (int u = 0; u < v.unison; ++u)
    {
        const float pos = v.unison > 1
                            ? -1.0f + 2.0f * (float) u / (float) (v.unison - 1)
                            : 0.0f;

        const double cents = (double) detuneCents * pos;
        v.incs[u] = hz * std::pow (2.0, cents / 1200.0) / sampleRate;

        const float pan = 0.5f + 0.5f * pos * spread;
        v.panL[u] = std::cos (pan * juce::MathConstants<float>::halfPi);
        v.panR[u] = std::sin (pan * juce::MathConstants<float>::halfPi);
    }
    v.unisonNorm = 1.0f / std::sqrt ((float) v.unison);

    // Free-running-oscillator feel: random start phases decorrelate the
    // unison bank and FM pair, so every retrigger blooms slightly
    // differently instead of machine-gunning an identical waveform. The sub
    // keeps phase 0 so bass attacks stay consistent and punchy.
    for (int u = 0; u < v.unison; ++u)
        v.phases[u] = (double) noiseRng.nextFloat();
    v.fmCarPhase = (double) noiseRng.nextFloat();
    v.fmModPhase = (double) noiseRng.nextFloat();
    v.subPhase   = 0.0;

    // --- Sub / noise / FM / vibrato / drift ----------------------------------
    v.subLevel   = juce::jlimit (0.0f, 1.0f, patchSettings.subLevel.load());
    v.noiseLevel = juce::jlimit (0.0f, 1.0f, patchSettings.noiseLevel.load());
    v.fmAmount   = juce::jlimit (0.0f, 1.0f, patchSettings.fmAmount.load());

    v.subInc   = (hz * 0.5) / sampleRate;
    v.fmCarInc = hz / sampleRate;
    v.fmModInc = (hz * juce::jmax (0.1f, patchSettings.fmRatio.load())) / sampleRate;

    v.vibDepthCents = patchSettings.vibDepthCents.load();
    v.vibInc        = patchSettings.vibRateHz.load() / sampleRate;

    v.driftDepth = juce::jlimit (0.0f, 15.0f, patchSettings.driftCents.load());

    // Portamento: slide in from wherever the last note was. Poly voices each
    // glide from the last note started, which is what a mono lead or an 808
    // slide wants and is harmless when the knob is at zero.
    {
        const float gms = patchSettings.glideMs.load();
        if (gms > 1.0f && pitchNoteOverride < 0
            && lastStartedNote >= 0 && lastStartedNote != (int) baseNote)
        {
            const double semis = juce::jlimit (-36.0, 36.0,
                                               (double) lastStartedNote - baseNote);
            v.glideRatio = (float) std::pow (2.0, semis / 12.0);
            v.glideCoeff = std::exp (-1.0f / (gms * 0.001f * (float) sampleRate));
        }
        else
        {
            v.glideRatio = 1.0f;
            v.glideCoeff = 0.0f;
        }
        if (pitchNoteOverride < 0)
            lastStartedNote = (int) baseNote;
    }

    // --- Attack transient ----------------------------------------------------
    // A resonant band-pass ringing on a single noise impulse: the pick, the
    // hammer, the mallet. It is over in a few milliseconds and it is most of
    // what tells the ear this is an instrument and not an oscillator.
    {
        v.atkAmt = juce::jlimit (0.0f, 1.0f, patchSettings.attackNoise.load());
        if (v.atkAmt > 0.001f)
        {
            const float ms = juce::jmax (2.0f, patchSettings.attackMs.load());
            v.atkCoeff = std::exp (-1.0f / (ms * 0.001f * (float) sampleRate));
            v.atkLevel = 1.0f;

            // Band-pass centred a few multiples above the note, so the burst
            // tracks pitch the way a real instrument's does - a struck bass
            // string thuds, a struck treble string ticks.
            const float centre = juce::jlimit (200.0f, 0.42f * (float) sampleRate,
                                               (float) hz * juce::jmax (1.0f, patchSettings.attackTone.load()));
            const float w  = juce::MathConstants<float>::twoPi * centre / (float) sampleRate;
            const float Q  = 1.6f;
            const float al = std::sin (w) / (2.0f * Q);
            const float c  = std::cos (w), a0 = 1.0f + al;
            v.atkB1 = 0.0f;                 // band-pass: b0 = al/a0, b1 = 0, b2 = -b0
            v.atkB2 = al / a0;
            v.atkA1 = (-2.0f * c) / a0;
            v.atkA2 = (1.0f - al) / a0;
            v.atkZ1 = v.atkZ2 = 0.0f;
        }
        else v.atkLevel = 0.0f;
    }

    // --- Inharmonic partial ---------------------------------------------------
    // Real strings and bars are stiff, so their overtones sit SHARP of the
    // harmonic series. Perfectly harmonic partials are why synthesised pianos
    // and bells sound glassy; a stretched partial fixes it for almost nothing.
    {
        const float inh = juce::jlimit (0.0f, 1.0f, patchSettings.inharmonic.load());
        v.inhLevel = inh * 0.34f;
        // 2nd partial, stretched sharp by up to ~35 cents at full amount
        v.inhInc = (hz * 2.0 * std::pow (2.0, inh * 0.35 / 12.0)) / sampleRate;
        v.inhPhase = (double) noiseRng.nextFloat();
    }

    // Percussion pitch envelope: start high, fall exponentially to base.
    v.penvOct = juce::jlimit (0.0f, 5.0f, patchSettings.pitchEnvOct.load());
    v.penv    = v.penvOct > 0.0f ? 1.0f : 0.0f;
    {
        const float pms = juce::jmax (5.0f, patchSettings.pitchEnvMs.load());
        v.penvCoeff = std::exp (-1.0f / (pms * 0.001f * (float) sampleRate));
    }

    // --- Resonant filter (velocity opens/closes the base cutoff) -------------
    const float velOct = patchSettings.velToFilterOct.load();
    const float velFactor = std::pow (2.0f, velOct * (v.velocity - 1.0f));
    v.fltBaseHz = juce::jlimit (40.0f,
                                juce::jmin (20000.0f, 0.45f * (float) sampleRate),
                                patchSettings.filterCutoff.load() * velFactor);
    v.fltEnvOct = juce::jlimit (0.0f, 6.0f, patchSettings.filterEnvOct.load());
    v.fltK      = 1.0f / juce::jlimit (0.5f, 8.0f, patchSettings.filterQ.load());
    v.fltActive = v.fltBaseHz < 19000.0f || v.fltEnvOct > 0.0f
                    || patchSettings.filterQ.load() > 0.8f;

    v.fenv = 1.0f;
    const float envMs = juce::jmax (5.0f, patchSettings.filterEnvMs.load());
    v.fenvCoeff = std::exp (-1.0f / (envMs * 0.001f * (float) sampleRate));
    v.fltUpdateCounter = 0;
    v.ic1L = v.ic2L = v.ic1R = v.ic2R = 0.0f;

    // --- Curved ADSR ----------------------------------------------------------
    v.attackSamples  = juce::jmax (1, (int) (attackMs  * 0.001 * sampleRate));
    v.decaySamples   = juce::jmax (1, (int) (decayMs   * 0.001 * sampleRate));
    v.releaseSamples = juce::jmax (1, (int) (releaseMs * 0.001 * sampleRate));
    v.sustainLevel   = sustainLvl;

    v.stage       = Voice::Stage::attack;
    v.stagePos    = 0;
    v.releaseFrom = 1.0f;
    v.autoOffCounter = autoOffSamples;
}

void SynthEngine::noteOn (int midiNote, float velocity, int pitchNoteOverride)
{
    startVoice (*findFreeVoice(), midiNote, velocity, -1, pitchNoteOverride);
}

void SynthEngine::tapNote (int midiNote, float velocity, int pitchNoteOverride)
{
    // Long enough for chord previews to ring musically before releasing.
    startVoice (*findFreeVoice(), midiNote, velocity, (int) (1.0 * sampleRate),
                pitchNoteOverride);
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
float SynthEngine::renderOsc (double phase, double inc, int waveType) const
{
    const float t  = (float) phase;
    const float dt = (float) inc;

    switch (waveType)
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

void SynthEngine::updateFormantBank (int vowel)
{
    // Classic vowel formant table (F1/F2/F3 in Hz + per-band gains); values
    // are the widely used averages for a sung "ah eh ee oh oo".
    static const float freq[5][3] = {
        { 650.0f, 1080.0f, 2650.0f },   // A
        { 400.0f, 1700.0f, 2600.0f },   // E
        { 290.0f, 1870.0f, 2800.0f },   // I
        { 400.0f,  800.0f, 2600.0f },   // O
        { 350.0f,  600.0f, 2700.0f },   // U
    };
    static const float gain[5][3] = {
        { 1.00f, 0.60f, 0.35f },
        { 1.00f, 0.70f, 0.40f },
        { 1.00f, 0.65f, 0.40f },
        { 1.00f, 0.70f, 0.30f },
        { 1.00f, 0.60f, 0.25f },
    };

    const int v = juce::jlimit (0, 4, vowel);
    for (int b = 0; b < 3; ++b)
    {
        auto& fb = formantBands[b];
        const float f = juce::jmin (freq[v][b], (float) sampleRate * 0.45f);
        const float Q = 9.0f;
        const float w = juce::MathConstants<float>::twoPi * f / (float) sampleRate;
        const float alpha = std::sin (w) / (2.0f * Q);
        const float a0 = 1.0f + alpha;

        // RBJ constant-skirt bandpass.
        fb.b0 =  alpha / a0;
        fb.b1 =  0.0f;
        fb.b2 = -alpha / a0;
        fb.a1 = -2.0f * std::cos (w) / a0;
        fb.a2 = (1.0f - alpha) / a0;
        fb.gain = gain[v][b];
        fb.x1L = fb.x2L = fb.y1L = fb.y2L = 0.0f;
        fb.x1R = fb.x2R = fb.y1R = fb.y2R = 0.0f;
    }
    formantVowelSet = v;
}

void SynthEngine::processBus (int numSamples)
{
    float* L = scratch.getWritePointer (0);
    float* R = scratch.getWritePointer (1);

    // --- Vocal formant bank (before saturation: shape first, glue after) ----
    // Clamp BEFORE the set-comparison: an out-of-range vowel would otherwise
    // rebuild (and zero) the bank every block - a constant click machine.
    const int   vowelRaw = patchSettings.formantVowel.load (std::memory_order_relaxed);
    const int   vowel = vowelRaw < 0 ? -1 : juce::jlimit (0, 4, vowelRaw);
    const float famt  = juce::jlimit (0.0f, 1.0f,
                                      patchSettings.formantAmount.load (std::memory_order_relaxed));
    if (vowel >= 0 && famt > 0.001f)
    {
        if (vowel != formantVowelSet)
            updateFormantBank (vowel);

        const float makeup = 2.6f;   // the narrow bands eat a lot of level
        for (int n = 0; n < numSamples; ++n)
        {
            float wetL = 0.0f, wetR = 0.0f;
            for (auto& fb : formantBands)
            {
                const float xL = L[n];
                const float yL = fb.b0 * xL + fb.b1 * fb.x1L + fb.b2 * fb.x2L
                                 - fb.a1 * fb.y1L - fb.a2 * fb.y2L;
                fb.x2L = fb.x1L; fb.x1L = xL;
                fb.y2L = fb.y1L; fb.y1L = yL;
                wetL += yL * fb.gain;

                const float xR = R[n];
                const float yR = fb.b0 * xR + fb.b1 * fb.x1R + fb.b2 * fb.x2R
                                 - fb.a1 * fb.y1R - fb.a2 * fb.y2R;
                fb.x2R = fb.x1R; fb.x1R = xR;
                fb.y2R = fb.y1R; fb.y1R = yR;
                wetR += yR * fb.gain;
            }
            L[n] = L[n] * (1.0f - famt) + wetL * makeup * famt;
            R[n] = R[n] * (1.0f - famt) + wetR * makeup * famt;
        }
    }

    // --- Gentle saturation glue ----------------------------------------------
    const float sat = juce::jlimit (0.0f, 1.0f, patchSettings.satAmount.load());
    if (sat > 0.0f)
    {
        const float driveIn  = 1.0f + sat * 1.5f;
        const float driveOut = 1.0f / std::tanh (driveIn);
        for (int n = 0; n < numSamples; ++n)
        {
            L[n] = std::tanh (L[n] * driveIn) * driveOut;
            R[n] = std::tanh (R[n] * driveIn) * driveOut;
        }
    }

    // --- Stereo chorus ---------------------------------------------------------
    const float mix = juce::jlimit (0.0f, 1.0f, patchSettings.chorusMix.load());
    const double lfoInc = 0.35 / sampleRate;                 // 0.35 Hz sweep
    const float baseDelay = 0.015f * (float) sampleRate;     // ~15 ms centre
    const float depth     = 0.004f * (float) sampleRate;     // ~4 ms swing

    for (int n = 0; n < numSamples; ++n)
    {
        chorusLine[0][(size_t) chorusWrite] = L[n];
        chorusLine[1][(size_t) chorusWrite] = R[n];

        if (mix > 0.0f)
        {
            const float lfo = std::sin (juce::MathConstants<float>::twoPi
                                        * (float) chorusLfo);

            // Opposite-phase modulation left vs right widens the image.
            auto readTap = [this] (int ch, float delaySamples) -> float
            {
                float pos = (float) chorusWrite - delaySamples;
                while (pos < 0.0f) pos += (float) kChorusSize;
                const int   i0 = (int) pos;
                const int   i1 = (i0 + 1) % kChorusSize;
                const float fr = pos - (float) i0;
                const auto& ln = chorusLine[(size_t) ch];
                return ln[(size_t) i0] + fr * (ln[(size_t) i1] - ln[(size_t) i0]);
            };

            const float wetL = readTap (0, baseDelay + depth * lfo);
            const float wetR = readTap (1, baseDelay - depth * lfo);

            L[n] = L[n] * (1.0f - 0.5f * mix) + wetL * mix * 0.7f;
            R[n] = R[n] * (1.0f - 0.5f * mix) + wetR * mix * 0.7f;
        }

        chorusWrite = (chorusWrite + 1) % kChorusSize;
        chorusLfo += lfoInc;
        if (chorusLfo >= 1.0) chorusLfo -= 1.0;
    }
}

void SynthEngine::render (juce::AudioBuffer<float>& out, int numSamples)
{
    const int numChannels = out.getNumChannels();
    if (numChannels == 0 || numSamples <= 0)
        return;

    if (scratch.getNumSamples() < numSamples)
        return;   // host exceeded the prepared block size; skip defensively

    scratch.clear (0, 0, numSamples);
    scratch.clear (1, 0, numSamples);
    float* L = scratch.getWritePointer (0);
    float* R = scratch.getWritePointer (1);

    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    bool anyVoice = false;

    // Global filter LFO: one shared phase so all voices breathe together.
    const double lfoInc   = (double) juce::jlimit (0.0f, 12.0f,
                                patchSettings.lfoRateHz.load()) / sampleRate;
    const float  lfoDepth = juce::jlimit (0.0f, 2.0f,
                                patchSettings.lfoDepthOct.load());

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;
        anyVoice = true;

        for (int n = 0; n < numSamples; ++n)
        {
            // --- Vibrato + analog drift -------------------------------------
            float cents = 0.0f;
            if (v.vibDepthCents > 0.0f)
            {
                cents += v.vibDepthCents * std::sin (twoPi * (float) v.vibPhase);
                advance (v.vibPhase, v.vibInc);
            }
            if (v.driftDepth > 0.0f)
            {
                // Slow random walk, softly pulled back toward centre.
                v.driftCents += (noiseRng.nextFloat() - 0.5f) * 0.004f;
                v.driftCents *= 0.99999f;
                v.driftCents  = juce::jlimit (-v.driftDepth, v.driftDepth, v.driftCents);
                cents += v.driftCents;
            }
            float pitchRatio = 1.0f + cents * 0.000578f;
            if (v.penv > 0.00001f)
            {
                // Exponential drop from +penvOct octaves down to the note.
                pitchRatio *= std::exp2 (v.penvOct * v.penv);
                v.penv *= v.penvCoeff;
            }
            if (v.glideCoeff > 0.0f)
            {
                pitchRatio *= v.glideRatio;
                v.glideRatio = 1.0f + (v.glideRatio - 1.0f) * v.glideCoeff;
                if (std::abs (v.glideRatio - 1.0f) < 1.0e-4f)
                    v.glideCoeff = 0.0f;      // arrived: stop paying for it
            }

            // --- Unison oscillator bank -------------------------------------
            float oscL = 0.0f, oscR = 0.0f;
            for (int u = 0; u < v.unison; ++u)
            {
                const double inc = v.incs[u] * pitchRatio;
                const float  s   = renderOsc (v.phases[u], inc, v.waveSnap);
                oscL += s * v.panL[u];
                oscR += s * v.panR[u];
                advance (v.phases[u], inc);
            }
            oscL *= v.unisonNorm;
            oscR *= v.unisonNorm;

            // --- FM/PM pair ----------------------------------------------------
            if (v.fmAmount > 0.0f)
            {
                const float mod = std::sin (twoPi * (float) v.fmModPhase);
                const float car = std::sin (twoPi * (float) v.fmCarPhase
                                            + v.fmAmount * 4.0f * mod);
                advance (v.fmCarPhase, v.fmCarInc * pitchRatio);
                advance (v.fmModPhase, v.fmModInc * pitchRatio);

                oscL = oscL * (1.0f - v.fmAmount) + car * v.fmAmount;
                oscR = oscR * (1.0f - v.fmAmount) + car * v.fmAmount;
            }

            // --- Sub + noise -----------------------------------------------------
            if (v.subLevel > 0.0f)
            {
                const float sub = std::sin (twoPi * (float) v.subPhase) * v.subLevel;
                advance (v.subPhase, v.subInc * (double) pitchRatio);
                oscL += sub;
                oscR += sub;
            }
            if (v.noiseLevel > 0.0f)
            {
                const float nz = (noiseRng.nextFloat() * 2.0f - 1.0f) * v.noiseLevel;
                oscL += nz;
                oscR += nz;
            }

            // --- Stretched partial ------------------------------------------
            if (v.inhLevel > 0.0f)
            {
                const float ih = std::sin (twoPi * (float) v.inhPhase) * v.inhLevel;
                advance (v.inhPhase, v.inhInc * (double) pitchRatio);
                oscL += ih;
                oscR += ih;
            }

            // --- Attack transient -------------------------------------------
            // Deliberately added AFTER the oscillators and BEFORE the filter,
            // so the filter shapes it the way a real body would.
            if (v.atkLevel > 0.0001f)
            {
                const float x = (noiseRng.nextFloat() * 2.0f - 1.0f) * v.atkLevel;
                const float y = v.atkB2 * x - v.atkB2 * v.atkZ2
                              - v.atkA1 * v.atkZ1 - v.atkA2 * v.atkZ2;
                v.atkZ2 = v.atkZ1;
                v.atkZ1 = y;
                const float burst = y * v.atkAmt * 2.6f;
                oscL += burst;
                oscR += burst;
                v.atkLevel *= v.atkCoeff;
            }

            // --- Resonant TPT SVF lowpass with decay envelope --------------------
            if (v.fltActive)
            {
                if (--v.fltUpdateCounter <= 0)
                {
                    v.fltUpdateCounter = 32;
                    // Filter LFO ("Motion"): slow sine wobble in octaves.
                    float lfoOct = 0.0f;
                    if (lfoDepth > 0.0f)
                        lfoOct = lfoDepth * std::sin (twoPi * (float) std::fmod (
                                     lfoPhaseBase + lfoInc * (double) n, 1.0));
                    // Clamp relative to Nyquist, not just an absolute 18 kHz:
                    // past fs/2 the tan() warp goes negative and the SVF poles
                    // leave the unit circle (inf/NaN at 32 kHz hosts).
                    const float hz = juce::jmin (0.45f * (float) sampleRate,
                                                 juce::jmin (18000.0f,
                        v.fltBaseHz * std::pow (2.0f, v.fltEnvOct * v.fenv + lfoOct)));
                    const float g  = std::tan (juce::MathConstants<float>::pi
                                               * hz / (float) sampleRate);
                    v.svfA1 = 1.0f / (1.0f + g * (g + v.fltK));
                    v.svfA2 = g * v.svfA1;
                    v.svfA3 = g * v.svfA2;
                }
                v.fenv *= v.fenvCoeff;

                {   // left
                    const float v3 = oscL - v.ic2L;
                    const float v1 = v.svfA1 * v.ic1L + v.svfA2 * v3;
                    const float v2 = v.ic2L + v.svfA2 * v.ic1L + v.svfA3 * v3;
                    v.ic1L = 2.0f * v1 - v.ic1L;
                    v.ic2L = 2.0f * v2 - v.ic2L;
                    oscL = v2;
                }
                {   // right
                    const float v3 = oscR - v.ic2R;
                    const float v1 = v.svfA1 * v.ic1R + v.svfA2 * v3;
                    const float v2 = v.ic2R + v.svfA2 * v.ic1R + v.svfA3 * v3;
                    v.ic1R = 2.0f * v1 - v.ic1R;
                    v.ic2R = 2.0f * v2 - v.ic2R;
                    oscR = v2;
                }
            }

            // --- Amp envelope -----------------------------------------------------
            const float env = v.envelope() * v.velocity * 0.32f;
            L[n] += oscL * env;
            R[n] += oscR * env;

            // --- Envelope state machine -------------------------------------------
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

            if (v.autoOffCounter > 0 && --v.autoOffCounter == 0
                && v.stage != Voice::Stage::release)
            {
                v.releaseFrom = juce::jlimit (0.0f, 1.0f, v.envelope());
                v.stage       = Voice::Stage::release;
                v.stagePos    = 0;
            }
        }
    }

    // Advance the shared filter-LFO phase once per block.
    lfoPhaseBase = std::fmod (lfoPhaseBase + lfoInc * (double) numSamples, 1.0);

    // Bus polish runs even without voices so the chorus tail rings out.
    const bool chorusActive = patchSettings.chorusMix.load() > 0.0f;
    if (anyVoice || chorusActive)
        processBus (numSamples);
    else
        return;   // nothing to add

    // Mix the synth bus into the host buffer.
    float* outL = out.getWritePointer (0);
    if (numChannels > 1)
    {
        float* outR = out.getWritePointer (1);
        for (int n = 0; n < numSamples; ++n)
        {
            outL[n] += L[n];
            outR[n] += R[n];
        }
    }
    else
    {
        for (int n = 0; n < numSamples; ++n)
            outL[n] += (L[n] + R[n]) * 0.5f;
    }
}
