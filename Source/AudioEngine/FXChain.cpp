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
    auto stereoSpec = spec;
    stereoSpec.numChannels = 2;
    reverb.prepare (stereoSpec);

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

    wetBus.setSize (2, juce::jmax (1, (int) spec.maximumBlockSize));

    preSamples = juce::jmax (1, (int) (0.012 * sampleRate));   // 12 ms pre-delay
    for (auto& l : preLine)
        l.assign ((size_t) preSamples, 0.0f);
    prePos = 0;

    // One-pole high-pass ~150 Hz on the wet return keeps the low end dry.
    hpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                               * 150.0f / (float) sampleRate);
    hpState[0] = hpState[1] = lpState[0] = lpState[1] = 0.0f;
    lpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                 * juce::jmin (8500.0f, (float) sampleRate * 0.40f) / (float) sampleRate);
    duckAttack = std::exp (-1.0f / (0.004f * (float) sampleRate));
    duckRelease = std::exp (-1.0f / (0.160f * (float) sampleRate));
    duckEnvelope = 0.0f;
    idleFlushed = false;
}

void ReverbFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0 || preSamples <= 0) return;
    const int capacity = wetBus.getNumSamples();
    if (numSamples > capacity)
    {
        for (int offset = 0; offset < numSamples; offset += capacity)
        {
            const int count = juce::jmin (capacity, numSamples - offset);
            float* p[2] { buffer.getWritePointer (0, offset),
                          buffer.getWritePointer (numChannels - 1, offset) };
            juce::AudioBuffer<float> part (p, numChannels, count);
            process (part, amount);
        }
        return;
    }
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedAmount.setTargetValue (target);
    if (target <= 0.0f && ! smoothedAmount.isSmoothing())
    {
        if (! idleFlushed)
        {
            reverb.reset();
            for (auto& line : preLine) std::fill (line.begin(), line.end(), 0.0f);
            hpState[0] = hpState[1] = lpState[0] = lpState[1] = 0.0f;
            duckEnvelope = 0.0f;
            idleFlushed = true;
        }
        return;
    }
    idleFlushed = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* input = buffer.getReadPointer (juce::jmin (ch, numChannels - 1));
        float* wet = wetBus.getWritePointer (ch);
        int position = prePos;
        for (int n = 0; n < numSamples; ++n)
        {
            wet[n] = preLine[ch][(size_t) position];
            preLine[ch][(size_t) position] = input[n];
            position = (position + 1) % preSamples;
        }
        if (ch == 1) prePos = position;
    }
    juce::dsp::AudioBlock<float> block (wetBus.getArrayOfWritePointers(), 2, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);

    for (int n = 0; n < numSamples; ++n)
    {
        // One shared per-sample ramp, not skip(blockSize) followed by a fixed
        // block-end gain. The old code still stepped at every host buffer.
        const float amt = smoothedAmount.getNextValue();
        const float dryGain = std::cos (amt * juce::MathConstants<float>::halfPi * 0.55f);
        const float wetGain = std::sin (amt * juce::MathConstants<float>::halfPi) * 0.62f;
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, n)));
        const float coefficient = peak > duckEnvelope ? duckAttack : duckRelease;
        duckEnvelope = peak + coefficient * (duckEnvelope - peak);
        const float duck = juce::jmax (0.70f, 1.0f / (1.0f + 1.5f * duckEnvelope));
        for (int ch = 0; ch < 2; ++ch)
        {
            const float wet = wetBus.getSample (ch, n);
            hpState[ch] += hpCoeff * (wet - hpState[ch]);
            lpState[ch] += lpCoeff * ((wet - hpState[ch]) - lpState[ch]);
            if (ch < numChannels)
                buffer.setSample (ch, n, buffer.getSample (ch, n) * dryGain
                                         + lpState[ch] * wetGain * duck);
        }
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
    smoothedQ.reset (sampleRate, 0.020);
    smoothedQ.setCurrentAndTargetValue (0.707f);

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
    smoothedQ.setTargetValue (q);

    const int numCh      = juce::jmin (buffer.getNumChannels(), numChannels);
    const int numSamples = buffer.getNumSamples();

    // Recompute coefficients per sample only while the cutoff is smoothing;
    // otherwise recompute once when cutoff/reso/type change, then hold.
    for (int n = 0; n < numSamples; ++n)
    {
        const float cut = smoothedCutoff.getNextValue();
        const float qNow = smoothedQ.getNextValue();

        // Exact float comparison on purpose: this is a cache check, not a
        // measurement. smoothedCutoff returns bit-identical values once it has
        // settled, and that is precisely when the coefficients must NOT be
        // recomputed. An epsilon here would rebuild the filter forever.
       #if defined (__GNUC__) || defined (__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wfloat-equal"
       #endif
        const bool dirty = (cut != lastCutoff) || (qNow != lastQ) || (type != lastType);
       #if defined (__GNUC__) || defined (__clang__)
        #pragma GCC diagnostic pop
       #endif

        if (dirty)
        {
            updateCoefficients (cut, qNow, type);
            lastCutoff = cut;
            lastQ      = qNow;
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
    smoothedFeedback.reset (sampleRate, 0.020);
    smoothedFeedback.setCurrentAndTargetValue (feedback);
    dampCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                   * juce::jmin (3000.0f, (float) sampleRate * 0.4f) / (float) sampleRate);
    feedbackHpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 70.0f / (float) sampleRate);
    timeFadeLength = juce::jmax (1, (int) (0.020 * sampleRate));
    timeFadePosition = timeFadeLength;
    currentDelay = previousDelay = delaySamples;
    duckAttack = std::exp (-1.0f / (0.004f * (float) sampleRate));
    duckRelease = std::exp (-1.0f / (0.140f * (float) sampleRate));
    idleFlushed = false;
}

void DelayFX::reset()
{
    for (auto& l : line)
        std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0;
    dampState[0] = dampState[1] = feedbackHp[0] = feedbackHp[1] = 0.0f;
    duckEnvelope = 0.0f;
    currentDelay = previousDelay = delaySamples;
    timeFadePosition = timeFadeLength;
}

void DelayFX::updateDelay()
{
    delaySamples = juce::jlimit (1, maxSamples - 1, (int) (delaySeconds * sampleRate));
}

void DelayFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0 || line[0].empty()) return;
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedWet.setTargetValue (target);
    smoothedFeedback.setTargetValue (feedback);
    if (target <= 0.0f && ! smoothedWet.isSmoothing())
    {
        if (! idleFlushed) { reset(); idleFlushed = true; }
        return;
    }
    idleFlushed = false;
    const bool crossFeed = pingpong && numChannels == 2;
    for (int n = 0; n < numSamples; ++n)
    {
        // Crossfade fixed taps when tempo/division changes. Smoothing the
        // read position instead would turn every tempo change into a pitch dive.
        if (timeFadePosition >= timeFadeLength && currentDelay != delaySamples)
        {
            previousDelay = currentDelay;
            currentDelay = delaySamples;
            timeFadePosition = 0;
        }
        const float t = slyce::quality::smoothstep ((float) timeFadePosition / timeFadeLength);
        if (timeFadePosition < timeFadeLength) ++timeFadePosition;
        const int a = (writePos - previousDelay + maxSamples) % maxSamples;
        const int b = (writePos - currentDelay + maxSamples) % maxSamples;
        float delayed[2];
        for (int ch = 0; ch < 2; ++ch)
            delayed[ch] = line[ch][(size_t) a] * (1.0f - t) + line[ch][(size_t) b] * t;
        const float input[2] { buffer.getSample (0, n),
                               buffer.getSample (numChannels - 1, n) };
        const float peak = juce::jmax (std::abs (input[0]), std::abs (input[1]));
        const float coefficient = peak > duckEnvelope ? duckAttack : duckRelease;
        duckEnvelope = peak + coefficient * (duckEnvelope - peak);
        const float duck = juce::jmax (0.75f, 1.0f / (1.0f + 1.2f * duckEnvelope));
        const float wet = smoothedWet.getNextValue();
        const float fb = smoothedFeedback.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
        {
            const float feed = delayed[crossFeed ? 1 - ch : ch];
            dampState[ch] += dampCoeff * (feed - dampState[ch]);
            feedbackHp[ch] += feedbackHpCoeff * (dampState[ch] - feedbackHp[ch]);
            line[ch][(size_t) writePos] = input[ch] + (dampState[ch] - feedbackHp[ch]) * fb;
            if (ch < numChannels)
                buffer.setSample (ch, n, input[ch] * (1.0f - 0.12f * wet)
                                         + delayed[ch] * (0.72f * wet * duck));
        }
        writePos = (writePos + 1) % maxSamples;
    }
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
