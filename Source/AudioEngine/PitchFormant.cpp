#include "PitchFormant.h"
#include <algorithm>

void PitchFormant::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr       = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = juce::jlimit (1, 2, numChannels);

    stretch.presetDefault (channels, (float) sr);
    stretch.reset();

    latency = stretch.inputLatency() + stretch.outputLatency();

    inputScratch.setSize (channels, juce::jmax (1, maxBlockSize), false, false, true);

    ringCap = juce::jmax (1, latency + juce::jmax (1, maxBlockSize) + 1);
    for (int ch = 0; ch < 2; ++ch)
        dryRing[(size_t) ch].assign ((size_t) ringCap, 0.0f);
    ringWrite = 0;

    mixSmoothed.reset (sr, 0.02); // ~20 ms ramp
    mixSmoothed.setCurrentAndTargetValue (1.0f);
}

void PitchFormant::reset()
{
    stretch.reset();
    for (int ch = 0; ch < 2; ++ch)
        std::fill (dryRing[(size_t) ch].begin(), dryRing[(size_t) ch].end(), 0.0f);
    ringWrite = 0;
}

void PitchFormant::process (juce::AudioBuffer<float>& buffer,
                            float pitchSemitones, float formantSemitones, float mix)
{
    setPitch (pitchSemitones);
    setFormant (formantSemitones);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), channels);
    if (numSamples == 0 || numChannels == 0)
        return;

    mix = juce::jlimit (0.0f, 1.0f, mix);

    // TRUE bypass at neutral settings: the stretcher's ~100 ms latency and
    // CPU must not tax normal playing. Engage only when a knob leaves zero.
    const bool wantEngage = std::abs (pitchSemi) > 0.01f
                         || std::abs (formantSemi) > 0.01f;
    if (wantEngage != engaged)
    {
        engaged = wantEngage;
        fadePos = 0;               // mask the path switch with a short fade-in
        if (engaged)
            stretch.reset();       // the dry ring keeps running - see below
    }
    if (! engaged)
    {
        // Keep feeding the dry history even while bypassed. Clearing it on
        // engage stopped minute-old audio replaying, but replaced it with a
        // ~100 ms hole in the dry path (the ring read trails by `latency`,
        // and the 256-sample toggle fade cannot mask that). Feeding it here
        // means the delayed dry is always genuine recent signal.
        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                dryRing[(size_t) ch][(size_t) ringWrite] = buffer.getSample (ch, n);
            ringWrite = (ringWrite + 1) % ringCap;
        }

        applyToggleFade (buffer, numSamples, numChannels);
        return;                    // signal passes untouched: zero delay
    }

    // Hosts may deliver a block larger than prepareToPlay promised; writing
    // it into the scratch would overflow the heap. Pass it through dry - but
    // still advance the fade, or a toggle landing on such a block plays at
    // full gain and clicks.
    if (numSamples > inputScratch.getNumSamples())
    {
        applyToggleFade (buffer, numSamples, numChannels);
        return;
    }

    // Apply pitch/formant on the audio thread (cheap parameter setters).
    stretch.setTransposeSemitones (pitchSemi);
    stretch.setFormantSemitones (formantSemi, true); // compensate = preserve character

    // Keep an untouched dry copy, then stretch it into the output buffer.
    for (int ch = 0; ch < numChannels; ++ch)
        inputScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    float* inPtrs[2]  = { nullptr, nullptr };
    float* outPtrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numChannels; ++ch)
    {
        inPtrs[ch]  = inputScratch.getWritePointer (ch);
        outPtrs[ch] = buffer.getWritePointer (ch);
    }

    stretch.process (inPtrs, numSamples, outPtrs, numSamples);

    // Latency-matched, per-sample-smoothed dry/wet blend. The ring always
    // advances so toggling the mix at runtime stays coherent; the smoother
    // avoids zipper noise when the Mix knob moves.
    mixSmoothed.setTargetValue (mix);
    const bool doBlend = mixSmoothed.isSmoothing() || mix < 1.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        int readIdx = ringWrite - latency;
        if (readIdx < 0)
            readIdx += ringCap;

        const float m = doBlend ? mixSmoothed.getNextValue() : 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            dryRing[(size_t) ch][(size_t) ringWrite] = inputScratch.getSample (ch, n);

            if (doBlend)
            {
                const float delayedDry = dryRing[(size_t) ch][(size_t) readIdx];
                const float wet = buffer.getSample (ch, n);
                buffer.setSample (ch, n, delayedDry * (1.0f - m) + wet * m);
            }
        }

        ringWrite = (ringWrite + 1) % ringCap;
    }

    applyToggleFade (buffer, numSamples, numChannels);
}

void PitchFormant::applyToggleFade (juce::AudioBuffer<float>& buffer,
                                    int numSamples, int numChannels)
{
    if (fadePos >= kFadeLen)
        return;
    for (int n = 0; n < numSamples && fadePos < kFadeLen; ++n, ++fadePos)
    {
        const float g = (float) fadePos / (float) kFadeLen;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, n, buffer.getSample (ch, n) * g);
    }
}
