#include "FXChain.h"
#include <algorithm>
#include <cmath>

//==============================================================================
void DistortionFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    smoothedDrive.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedDrive.setCurrentAndTargetValue (0.0f);
}

void DistortionFX::process (juce::AudioBuffer<float>& buffer, float drive)
{
    const float target = juce::jlimit (0.0f, 1.0f, drive);
    smoothedDrive.setTargetValue (target);

    // Safe idle skip: only bail if the effect is fully off AND not mid-ramp,
    // otherwise we'd chop the tail of a fade-out.
    if (target <= 0.0f && ! smoothedDrive.isSmoothing())
    {
        smoothedDrive.setCurrentAndTargetValue (0.0f);
        return;
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Snapshot the smoother start state so every channel gets the same ramp.
    const auto startDrive = smoothedDrive;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto smoother = startDrive; // per-channel copy of the ramp
        float* d = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dNow    = smoother.getNextValue();
            const float preGain = 1.0f + dNow * 24.0f;   // up to ~+28 dB
            const float makeup  = 1.0f / std::sqrt (preGain);

            const float clean = d[n];
            const float dirty = std::tanh (clean * preGain) * makeup;
            d[n] = clean * (1.0f - dNow) + dirty * dNow;
        }

        // Keep the shared smoother advanced to the block-end state after the
        // last channel so its position stays consistent across blocks.
        if (ch == numChannels - 1)
            smoothedDrive = smoother;
    }

    // If the buffer had no channels, still advance the smoother by the block.
    if (numChannels == 0)
        smoothedDrive.skip (numSamples);
}

//==============================================================================
void ReverbFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    reverb.reset();
    reverb.prepare (spec);

    smoothedAmount.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedAmount.setCurrentAndTargetValue (0.0f);

    // Wet-only inside the reverb; we do the mixing ourselves.
    juce::dsp::Reverb::Parameters p;
    // Smaller and darker than it was (0.68 / 0.35). At the old setting even a
    // quarter turn put a 0.9 s tail on every note, which is a hall, not an
    // instrument's own space.
    p.roomSize = 0.56f;
    p.damping  = 0.48f;
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    p.width    = 1.0f;
    reverb.setParameters (p);

    wetBus.setSize (2, (int) spec.maximumBlockSize);

    preSamples = juce::jmax (1, (int) (0.012 * sampleRate));   // 12 ms pre-delay
    for (auto& l : preLine)
        l.assign ((size_t) preSamples, 0.0f);
    prePos = 0;

    // One-pole high-pass ~150 Hz on the wet return keeps the low end dry.
    hpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                               * 150.0f / (float) sampleRate);
    hpState[0] = hpState[1] = 0.0f;
}

void ReverbFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedAmount.setTargetValue (target);

    // Safe idle skip: only bail if fully off and not still ramping down.
    if (target <= 0.0f && ! smoothedAmount.isSmoothing())
    {
        smoothedAmount.setCurrentAndTargetValue (0.0f);
        if (! idleFlushed)
        {
            // Flush ONCE on entering idle: a frozen tail + pre-delay line
            // would otherwise replay minutes-old audio when the knob returns.
            reverb.reset();
            for (auto& l : preLine)
                std::fill (l.begin(), l.end(), 0.0f);
            hpState[0] = hpState[1] = 0.0f;
            idleFlushed = true;
        }
        return;
    }
    idleFlushed = false;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    if (numSamples > wetBus.getNumSamples())
        return;   // host exceeded the prepared block; skip defensively

    // Feed the wet bus through the pre-delay ring.
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* in  = buffer.getReadPointer (juce::jmin (ch, numChannels - 1));
        float*       wet = wetBus.getWritePointer (ch);
        auto&        line = preLine[ch];
        int          pos  = prePos;

        for (int n = 0; n < numSamples; ++n)
        {
            wet[n] = line[(size_t) pos];
            line[(size_t) pos] = in[n];
            pos = (pos + 1) % preSamples;
        }
        if (ch == 1)
            prePos = pos;
    }

    // 100%-wet reverb on the bus.
    juce::dsp::AudioBlock<float> block (wetBus.getArrayOfWritePointers(), 2,
                                        (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);

    // A MIX, not a send.
    //
    // This used to be `out[n] += wet * amount * 0.85`, with the dry path left
    // alone. Adding a wet signal on top of an untouched dry one means turning
    // the knob up makes the whole patch LOUDER - measured at +6.2 dB against
    // dry at full - so "more reverb" and "more volume" were the same gesture
    // and the reverb could never sit behind anything. That is what "why is the
    // reverb so strong" is describing.
    //
    // Equal-power crossfade instead, with the dry deliberately not going all
    // the way out: this is an instrument's own space, not a send bus, and a
    // fully-wet chop is not a sound anyone asked for. The wet ceiling of 0.62
    // is set so full wet measures within about a dB of dry.
    smoothedAmount.skip (numSamples);
    const float amt     = smoothedAmount.getCurrentValue();
    const float halfPi  = juce::MathConstants<float>::halfPi;
    const float wetGain = std::sin (amt * halfPi) * 0.62f;
    const float dryGain = std::cos (amt * halfPi * 0.55f);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        const float* wet = wetBus.getReadPointer (ch);
        float state = hpState[ch];

        for (int n = 0; n < numSamples; ++n)
        {
            state += hpCoeff * (wet[n] - state);
            out[n] = out[n] * dryGain + (wet[n] - state) * wetGain;
        }
        hpState[ch] = state;
    }
}

//==============================================================================
void FilterFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    numChannels = juce::jlimit (1, 2, (int) spec.numChannels);

    for (int ch = 0; ch < 2; ++ch)
    {
        lp[ch].setSampleRate (sampleRate);
        hp[ch].setSampleRate (sampleRate);
    }

    reset();

    smoothedCutoff.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedCutoff.setCurrentAndTargetValue (1000.0f);

    // Force a recompute on first process().
    lastCutoff = -1.0f;
    lastQ      = -1.0f;
    lastType   = -1;
}

void FilterFX::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        lp[ch].reset();
        hp[ch].reset();
    }
}

void FilterFX::updateCoefficients (float cutoffHz, float q, int type) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        switch (type)
        {
            case 1: // LowPass
                lp[ch].lowPass (cutoffHz, q);
                break;
            case 2: // HighPass
                lp[ch].highPass (cutoffHz, q);
                break;
            case 3: // BandPass: HighPass -> LowPass cascade at the same centre
                hp[ch].highPass (cutoffHz, q);
                lp[ch].lowPass  (cutoffHz, q);
                break;
            default:
                break;
        }
    }
}

void FilterFX::process (juce::AudioBuffer<float>& buffer, float cutoffHz, float resonance, int type)
{
    if (type == 0) // Off/bypass
    {
        if (lastType > 0)
        {
            reset();          // drop stale states so re-enabling can't thump
            lastType = 0;
        }
        return;
    }

    if (type != lastType)
    {
        reset();              // topology switch: stale z-states would step
        typeFade = 0.0f;      // ...and crossfade the dry->filtered handover
    }

    const float nyqLimit = (float) (sampleRate * 0.49);
    const float targetCut = juce::jlimit (20.0f, nyqLimit, cutoffHz);

    // resonance [0.1, 8] -> Q. Q is proportional to resonance; clamp keeps the
    // filter stable and prevents runaway self-oscillation.
    const float q = juce::jlimit (0.1f, 8.0f, resonance);

    smoothedCutoff.setTargetValue (targetCut);

    const int numCh      = juce::jmin (buffer.getNumChannels(), numChannels);
    const int numSamples = buffer.getNumSamples();

    // Recompute coefficients per sample only while the cutoff is smoothing;
    // otherwise recompute once when cutoff/reso/type change, then hold.
    for (int n = 0; n < numSamples; ++n)
    {
        const float cut = smoothedCutoff.getNextValue();

        // Exact float comparison on purpose: this is a cache check, not a
        // measurement. smoothedCutoff returns bit-identical values once it has
        // settled, and that is precisely when the coefficients must NOT be
        // recomputed. An epsilon here would rebuild the filter forever.
       #if defined (__GNUC__) || defined (__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wfloat-equal"
       #endif
        const bool dirty = (cut != lastCutoff) || (q != lastQ) || (type != lastType);
       #if defined (__GNUC__) || defined (__clang__)
        #pragma GCC diagnostic pop
       #endif

        if (dirty)
        {
            updateCoefficients (cut, q, type);
            lastCutoff = cut;
            lastQ      = q;
            lastType   = type;
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = buffer.getSample (ch, n);
            float x = dry;

            if (type == 3) // band-pass cascade
                x = lp[ch].process (hp[ch].process (x));
            else
                x = lp[ch].process (x);

            if (typeFade < 1.0f)
                x = dry + (x - dry) * typeFade;

            buffer.setSample (ch, n, x);
        }

        if (typeFade < 1.0f)   // ~5 ms dry->filtered crossfade
            typeFade = juce::jmin (1.0f, typeFade + 1.0f / (0.005f * (float) sampleRate));
    }

    // If no channels were processed, keep the cutoff smoother advanced.
    if (numCh == 0)
        smoothedCutoff.skip (numSamples);
}

//==============================================================================
void DelayFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxSamples = (int) (2.0 * sampleRate) + 4;
    for (auto& l : line)
        l.assign ((size_t) maxSamples, 0.0f);
    updateDelay();
    reset();

    smoothedWet.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedWet.setCurrentAndTargetValue (0.0f);
}

void DelayFX::reset()
{
    for (auto& l : line)
        std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0;
    dampState[0] = dampState[1] = 0.0f;
}

void DelayFX::updateDelay()
{
    delaySamples = juce::jlimit (1, maxSamples - 1, (int) (delaySeconds * sampleRate));
}

void DelayFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedWet.setTargetValue (target);

    // Safe idle skip: only bail if fully off and not still ramping down; a
    // fade-out still needs to feed the delay line and mix its tail.
    if (target <= 0.0f && ! smoothedWet.isSmoothing())
    {
        smoothedWet.setCurrentAndTargetValue (0.0f);
        if (! idleFlushed)
        {
            reset();   // 2 s of stale echoes must not replay on the next raise
            idleFlushed = true;
        }
        return;
    }
    idleFlushed = false;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    // Ping-pong only makes sense with a genuine stereo pair.
    const bool doPingpong = pingpong && numChannels == 2;

    for (int n = 0; n < numSamples; ++n)
    {
        const float wet     = smoothedWet.getNextValue(); // per-sample wet mix
        const int   readPos = (writePos - delaySamples + maxSamples) % maxSamples;

        if (doPingpong)
        {
            auto& lL = line[0];
            auto& lR = line[1];

            const float inL = buffer.getSample (0, n);
            const float inR = buffer.getSample (1, n);

            const float delayedL = lL[(size_t) readPos];
            const float delayedR = lR[(size_t) readPos];

            // Damped feedback (one-pole lowpass) so repeats get warmer and
            // darker like an analog echo instead of building up harshness.
            dampState[0] += kDampCoeff * (delayedR - dampState[0]);
            dampState[1] += kDampCoeff * (delayedL - dampState[1]);

            // Cross-feed the feedback: the left tap feeds the right line and
            // vice-versa, so echoes bounce L<->R.
            lL[(size_t) writePos] = inL + dampState[0] * feedback;
            lR[(size_t) writePos] = inR + dampState[1] * feedback;

            buffer.setSample (0, n, inL + delayedL * wet);
            buffer.setSample (1, n, inR + delayedR * wet);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& l = line[(size_t) ch];
                const float in      = buffer.getSample (ch, n);
                const float delayed = l[(size_t) readPos];

                // Damped feedback: each repeat passes a gentle lowpass.
                dampState[(size_t) ch] += kDampCoeff * (delayed - dampState[(size_t) ch]);

                l[(size_t) writePos] = in + dampState[(size_t) ch] * feedback;
                buffer.setSample (ch, n, in + delayed * wet);
            }
        }

        writePos = (writePos + 1) % maxSamples;
    }

    // If fewer than expected channels were present, keep the smoother advanced.
    if (numChannels == 0)
        smoothedWet.skip (numSamples);
}

//==============================================================================
void FXChain::prepare (juce::dsp::ProcessSpec s)
{
    spec = s;
    distortion.prepare (spec);
    filter.prepare (spec);
    reverb.prepare (spec);
    delay.prepare (spec);
}
