#include "VoicePool.h"

//==============================================================================
void Voice::start (double hostSampleRate,
                   std::shared_ptr<const juce::AudioBuffer<float>> src, double srcSampleRate,
                   int startSample, int lengthSamples,
                   float vel, float attackMs)
{
    if (src == nullptr || lengthSamples <= 0 || src->getNumSamples() == 0)
    {
        active = false;
        return;
    }

    source        = std::move (src);
    srcNumSamples = source->getNumSamples();
    srcL          = source->getReadPointer (0);
    srcR          = source->getNumChannels() > 1 ? source->getReadPointer (1) : srcL;

    const double hostSR = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    ratio = (srcSampleRate > 0.0 ? srcSampleRate : hostSR) / hostSR;

    start  = (double) juce::jlimit (0, srcNumSamples - 1, startSample);
    length = (double) juce::jmin (lengthSamples, srcNumSamples - (int) start);
    pos    = start;

    velocity       = juce::jlimit (0.0f, 1.0f, vel);
    attackSamples  = juce::jmax (1, (int) (attackMs * 0.001 * hostSR));
    releaseSamples = juce::jmax (1, (int) (0.005 * hostSR)); // 5 ms tail
    outPos         = 0;
    releasePos     = -1;
    active         = length > 0.0;
}

float Voice::envelope() const
{
    float env = 1.0f;

    // Attack ramp (output-time).
    if (outPos < attackSamples)
        env = (float) outPos / (float) attackSamples;

    // End-of-slice fade: remaining output samples until the slice ends.
    const double srcRemaining = (start + length) - pos;
    const double outRemaining = srcRemaining / juce::jmax (1.0e-9, ratio);
    if (outRemaining < (double) releaseSamples)
        env *= (float) juce::jmax (0.0, outRemaining / (double) releaseSamples);

    // Explicit note-off release.
    if (releasePos >= 0)
        env *= juce::jlimit (0.0f, 1.0f, 1.0f - (float) releasePos / (float) releaseSamples);

    return juce::jlimit (0.0f, 1.0f, env);
}

void Voice::render (juce::AudioBuffer<float>& out, int numSamples)
{
    if (! active || source == nullptr)
        return;

    const int outChannels = out.getNumChannels();
    float* outL = out.getWritePointer (0);
    float* outR = outChannels > 1 ? out.getWritePointer (1) : nullptr;

    const double end = start + length;

    for (int n = 0; n < numSamples; ++n)
    {
        if (pos >= end)
        {
            active = false;
            return;
        }

        // Linear interpolation for the resampled read.
        const int   i0   = (int) pos;
        const int   i1   = juce::jmin (i0 + 1, srcNumSamples - 1);
        const float frac = (float) (pos - (double) i0);

        const float sL = srcL[i0] + frac * (srcL[i1] - srcL[i0]);
        const float sR = srcR[i0] + frac * (srcR[i1] - srcR[i0]);

        const float env = envelope() * velocity;
        outL[n] += sL * env;
        if (outR != nullptr)
            outR[n] += sR * env;

        pos += ratio;
        ++outPos;

        if (releasePos >= 0)
        {
            ++releasePos;
            if (releasePos >= releaseSamples)
            {
                active = false;
                return;
            }
        }
    }
}

void Voice::release()
{
    if (active && releasePos < 0)
        releasePos = 0;
}

//==============================================================================
void VoicePool::prepare (juce::dsp::ProcessSpec spec)
{
    hostSampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    releaseAll();
}

void VoicePool::setSource (std::shared_ptr<const juce::AudioBuffer<float>> src, double sampleRate)
{
    const juce::SpinLock::ScopedLockType sl (sourceLock);
    source = std::move (src);
    sourceSampleRate = sampleRate > 0.0 ? sampleRate : hostSampleRate;
}

void VoicePool::triggerVoice (int startSample, int lengthSamples, float velocity, float attackMs)
{
    // Audio thread. Grab a snapshot of the source without blocking the loader.
    std::shared_ptr<const juce::AudioBuffer<float>> src;
    double srcSR;
    {
        const juce::SpinLock::ScopedTryLockType sl (sourceLock);
        if (! sl.isLocked() || source == nullptr)
            return;
        src   = source;            // ref-count bump keeps the buffer alive
        srcSR = sourceSampleRate;
    }

    // Prefer a free voice; otherwise steal the first one.
    for (auto& v : voices)
    {
        if (! v.isActive())
        {
            v.start (hostSampleRate, src, srcSR, startSample, lengthSamples, velocity, attackMs);
            return;
        }
    }
    voices[0].start (hostSampleRate, src, srcSR, startSample, lengthSamples, velocity, attackMs);
}

void VoicePool::releaseAll()
{
    for (auto& v : voices)
        v.release();
}

void VoicePool::renderNextBlock (juce::AudioBuffer<float>& out, int numSamples)
{
    for (auto& v : voices)
        v.render (out, numSamples);
}
