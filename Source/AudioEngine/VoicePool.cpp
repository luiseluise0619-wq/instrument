#include "VoicePool.h"

//==============================================================================
void Voice::start (double sr,
                   const juce::AudioBuffer<float>* src,
                   int startSample, int lengthSamples,
                   float vel, float attackMs)
{
    if (src == nullptr || lengthSamples <= 0)
    {
        active = false;
        return;
    }

    sampleRate     = sr > 0.0 ? sr : 44100.0;
    source         = src;
    start          = juce::jlimit (0, juce::jmax (0, src->getNumSamples() - 1), startSample);
    length         = juce::jmin (lengthSamples, src->getNumSamples() - start);
    pos            = 0;
    velocity       = juce::jlimit (0.0f, 1.0f, vel);
    attackSamples  = juce::jmax (1, (int) (attackMs * 0.001f * (float) sampleRate));
    releaseSamples = juce::jmax (1, (int) (0.005f * (float) sampleRate)); // 5 ms tail
    releasePos     = -1;
    active         = length > 0;
}

float Voice::envelopeAt (int position) const
{
    float env = 1.0f;

    // Attack ramp.
    if (position < attackSamples)
        env = (float) position / (float) attackSamples;

    // End-of-slice fade so the tail never clicks.
    const int fadeStart = length - releaseSamples;
    if (fadeStart > 0 && position >= fadeStart)
        env *= (float) (length - position) / (float) releaseSamples;

    // Explicit note-off release.
    if (releasePos >= 0)
    {
        const float r = 1.0f - (float) releasePos / (float) releaseSamples;
        env *= juce::jlimit (0.0f, 1.0f, r);
    }

    return juce::jlimit (0.0f, 1.0f, env);
}

void Voice::render (juce::AudioBuffer<float>& out, int numSamples)
{
    if (! active || source == nullptr)
        return;

    const int   srcChannels = source->getNumChannels();
    const float* srcL = source->getReadPointer (0);
    const float* srcR = srcChannels > 1 ? source->getReadPointer (1) : srcL;

    const int outChannels = out.getNumChannels();
    float* outL = out.getWritePointer (0);
    float* outR = outChannels > 1 ? out.getWritePointer (1) : nullptr;

    for (int n = 0; n < numSamples; ++n)
    {
        if (pos >= length)
        {
            active = false;
            return;
        }

        const float env = envelopeAt (pos) * velocity;
        const int   idx = start + pos;

        outL[n] += srcL[idx] * env;
        if (outR != nullptr)
            outR[n] += srcR[idx] * env;

        ++pos;

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
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    releaseAll();
}

void VoicePool::setSource (std::shared_ptr<juce::AudioBuffer<float>> src)
{
    releaseAll();
    source = std::move (src);
}

void VoicePool::triggerVoice (int startSample, int lengthSamples,
                              float velocity, float attackMs, double sr)
{
    if (source == nullptr)
        return;

    // Prefer a free voice.
    for (auto& v : voices)
    {
        if (! v.isActive())
        {
            v.start (sr, source.get(), startSample, lengthSamples, velocity, attackMs);
            return;
        }
    }

    // Otherwise steal the first voice (simple, deterministic policy).
    voices[0].start (sr, source.get(), startSample, lengthSamples, velocity, attackMs);
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
