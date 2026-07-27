#include "VoicePool.h"

#include <algorithm>

//==============================================================================
void Voice::start (double hostSampleRate,
                   std::shared_ptr<const juce::AudioBuffer<float>> src, double srcSampleRate,
                   int startSample, int lengthSamples,
                   float vel,
                   float attackMs, float decayMs, float sustain0to1, float releaseMs,
                   bool reverse, bool oneShot)
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

    sliceStart = (double) juce::jlimit (0, srcNumSamples - 1, startSample);
    length     = (double) juce::jmin (lengthSamples, srcNumSamples - (int) sliceStart);

    reversePlay = reverse;
    oneShotMode = oneShot;

    // Reverse reads from the slice end backwards; forward reads from the start.
    // Keep pos strictly inside the slice for bounds safety.
    if (reversePlay)
        pos = (sliceStart + length) - 1.0;
    else
        pos = sliceStart;

    velocity = juce::jlimit (0.0f, 1.0f, vel);

    attackSamples  = juce::jmax (1, (int) (attackMs  * 0.001 * hostSR));
    decaySamples   = juce::jmax (1, (int) (decayMs   * 0.001 * hostSR));
    releaseSamples = juce::jmax (1, (int) (releaseMs * 0.001 * hostSR));
    endFadeSamples = juce::jmax (1, (int) (0.005 * hostSR)); // 5 ms click-free tail
    sustainLevel   = juce::jlimit (0.0f, 1.0f, sustain0to1);

    stage       = Stage::attack;
    stagePos    = 0;
    releaseFrom = 1.0f;

    active = length > 0.0;
}

float Voice::envelope() const
{
    float env = 1.0f;

    switch (stage)
    {
        case Stage::attack:
            env = (float) stagePos / (float) attackSamples;
            break;

        case Stage::decay:
            // Ramp from 1.0 down to the sustain level over the decay stage.
            env = 1.0f + ((float) stagePos / (float) decaySamples) * (sustainLevel - 1.0f);
            break;

        case Stage::sustain:
            env = sustainLevel;
            break;

        case Stage::release:
            // Ramp from the level captured at note-off down to zero.
            env = releaseFrom * (1.0f - (float) stagePos / (float) releaseSamples);
            break;

        case Stage::finished:
        default:
            env = 0.0f;
            break;
    }

    // End-of-slice fade: remaining output samples until the slice ends. Applies
    // in both directions so playback never clicks at the boundary.
    const double srcRemaining = reversePlay ? (pos - sliceStart)
                                            : ((sliceStart + length) - pos);
    const double outRemaining = srcRemaining / juce::jmax (1.0e-9, ratio);
    if (outRemaining < (double) endFadeSamples)
        env *= (float) juce::jmax (0.0, outRemaining / (double) endFadeSamples);

    return juce::jlimit (0.0f, 1.0f, env);
}

void Voice::render (juce::AudioBuffer<float>& out, int numSamples)
{
    if (! active || source == nullptr)
        return;

    const int outChannels = out.getNumChannels();
    float* outL = out.getWritePointer (0);
    float* outR = outChannels > 1 ? out.getWritePointer (1) : nullptr;

    const double sliceEnd = sliceStart + length;

    for (int n = 0; n < numSamples; ++n)
    {
        // Bounds / slice-end check (direction aware).
        if (reversePlay ? (pos < sliceStart) : (pos >= sliceEnd))
        {
            active = false;
            return;
        }

        // Linear interpolation for the resampled read.
        const int   i0   = juce::jlimit (0, srcNumSamples - 1, (int) pos);
        const int   i1   = juce::jmin (i0 + 1, srcNumSamples - 1);
        const float frac = (float) (pos - (double) i0);

        const float sL = srcL[i0] + frac * (srcL[i1] - srcL[i0]);
        const float sR = srcR[i0] + frac * (srcR[i1] - srcR[i0]);

        const float env = envelope() * velocity;
        outL[n] += sL * env;
        if (outR != nullptr)
            outR[n] += sR * env;

        // Advance the read head (reverse steps by -ratio).
        pos += reversePlay ? -ratio : ratio;

        // Advance the envelope state machine (output-sample time).
        ++stagePos;
        switch (stage)
        {
            case Stage::attack:
                if (stagePos >= attackSamples)
                {
                    stage    = Stage::decay;
                    stagePos = 0;
                }
                break;

            case Stage::decay:
                if (stagePos >= decaySamples)
                {
                    stage    = Stage::sustain;
                    stagePos = 0;
                }
                break;

            case Stage::sustain:
                // Held until the slice ends or release() moves us on.
                break;

            case Stage::release:
                if (stagePos >= releaseSamples)
                {
                    active = false;
                    return;
                }
                break;

            case Stage::finished:
            default:
                active = false;
                return;
        }
    }
}

void Voice::release()
{
    // One-shot voices ignore note-off entirely.
    if (! active || oneShotMode)
        return;

    if (stage != Stage::release && stage != Stage::finished)
    {
        // Capture the current level so the release ramps from where we are.
        // release and finished are excluded by the `if` above, so the switch
        // genuinely cannot see them; `default` covers idle.
        switch (stage)
        {
            case Stage::release: case Stage::finished:
            case Stage::attack:  releaseFrom = (float) stagePos / (float) attackSamples; break;
            case Stage::decay:   releaseFrom = 1.0f + ((float) stagePos / (float) decaySamples) * (sustainLevel - 1.0f); break;
            case Stage::sustain: releaseFrom = sustainLevel; break;
            default:             releaseFrom = 1.0f; break;
        }
        releaseFrom = juce::jlimit (0.0f, 1.0f, releaseFrom);

        stage    = Stage::release;
        stagePos = 0;
    }
}

void Voice::hardStop()
{
    // MIDI All Sound Off: even one-shot voices must die, but through a very
    // short release ramp so the emergency stop itself doesn't click.
    if (! active || stage == Stage::finished)
        return;

    releaseFrom    = envelope();
    oneShotMode    = false;        // let the release actually run
    releaseSamples = 64;
    stage          = Stage::release;
    stagePos       = 0;
}

float Voice::normalisedPosition() const
{
    if (! active || srcNumSamples <= 0)
        return -1.0f;

    return juce::jlimit (0.0f, 1.0f, (float) (pos / (double) srcNumSamples));
}

//==============================================================================
void VoicePool::prepare (juce::dsp::ProcessSpec spec)
{
    hostSampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    // std::atomic<float> is not copyable, so initialise each slot explicitly.
    for (auto& p : playheads)
        p.store (-1.0f, std::memory_order_relaxed);

    releaseAll();
}

void VoicePool::setSource (std::shared_ptr<const juce::AudioBuffer<float>> src, double sampleRate)
{
    // Message thread. Prune retired buffers nobody references any more (a
    // use_count of 1 means only this graveyard holds it — voices can never
    // re-acquire an old source, so the count can only fall).
    retiredSources.erase (std::remove_if (retiredSources.begin(), retiredSources.end(),
                                          [] (const std::shared_ptr<const juce::AudioBuffer<float>>& s)
                                          { return s.use_count() == 1; }),
                          retiredSources.end());

    std::shared_ptr<const juce::AudioBuffer<float>> old;
    {
        const juce::SpinLock::ScopedLockType sl (sourceLock);
        old = std::move (source);
        source = std::move (src);
        sourceSampleRate = sampleRate > 0.0 ? sampleRate : hostSampleRate;
    }

    // Park the outgoing buffer so its final release happens here, not inside
    // processBlock when a voice slot gets reused. Never park duplicates (the
    // same buffer can come back through prepareToPlay -> setSource): two
    // graveyard entries would keep each other's use_count above 1 forever
    // and the buffer would leak.
    if (old != nullptr && old != source
        && std::find (retiredSources.begin(), retiredSources.end(), old)
               == retiredSources.end())
        retiredSources.push_back (std::move (old));
}

void VoicePool::setEnvelope (float attackMsIn, float decayMsIn, float sustain0to1, float releaseMsIn)
{
    attackMs   = juce::jmax (0.0f, attackMsIn);
    decayMs    = juce::jmax (0.0f, decayMsIn);
    sustainLvl = juce::jlimit (0.0f, 1.0f, sustain0to1);
    releaseMs  = juce::jmax (0.0f, releaseMsIn);
}

void VoicePool::setReverse (bool shouldReverse)
{
    reversePlay = shouldReverse;
}

void VoicePool::setPlayMode (bool oneShot)
{
    oneShotMode = oneShot;
}

int VoicePool::triggerVoice (int startSample, int lengthSamples, float velocity)
{
    // Audio thread. Grab a snapshot of the source without blocking the loader.
    std::shared_ptr<const juce::AudioBuffer<float>> src;
    double srcSR;
    {
        const juce::SpinLock::ScopedTryLockType sl (sourceLock);
        if (! sl.isLocked() || source == nullptr)
            return -1;
        src   = source;            // ref-count bump keeps the buffer alive
        srcSR = sourceSampleRate;
    }

    // Prefer a free voice; otherwise steal the first one.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.isActive())
        {
            v.start (hostSampleRate, src, srcSR, startSample, lengthSamples, velocity,
                     attackMs, decayMs, sustainLvl, releaseMs, reversePlay, oneShotMode);
            return i;
        }
    }
    // All busy: steal the QUIETEST voice. Blind-restarting slot 0 snapped a
    // possibly full-scale waveform to zero - a hard click on every steal.
    int quietest = 0;
    float qLevel = 1.0e9f;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const float lvl = voices[(size_t) i].currentLevel();
        if (lvl < qLevel)
        {
            qLevel = lvl;
            quietest = i;
        }
    }
    voices[(size_t) quietest].start (hostSampleRate, src, srcSR, startSample, lengthSamples,
                                     velocity, attackMs, decayMs, sustainLvl, releaseMs,
                                     reversePlay, oneShotMode);
    return quietest;
}

void VoicePool::releaseVoice (int voiceIndex)
{
    if (voiceIndex >= 0 && voiceIndex < kMaxVoices)
        voices[(size_t) voiceIndex].release();
}

void VoicePool::releaseAll()
{
    for (auto& v : voices)
        v.release();
}

void VoicePool::stopAll()
{
    for (auto& v : voices)
        v.hardStop();
}

void VoicePool::renderNextBlock (juce::AudioBuffer<float>& out, int numSamples)
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.render (out, numSamples);

        // Publish the playhead once per block: normalised position while
        // active, -1 once the voice has become inactive.
        playheads[(size_t) i].store (v.isActive() ? v.normalisedPosition() : -1.0f,
                                     std::memory_order_relaxed);
    }
}

int VoicePool::copyPlayheads (float* dst, int maxCount) const
{
    if (dst == nullptr || maxCount <= 0)
        return 0;

    int count = 0;
    for (int i = 0; i < kMaxVoices && count < maxCount; ++i)
    {
        const float p = playheads[(size_t) i].load (std::memory_order_relaxed);
        if (p >= 0.0f)
            dst[count++] = p;
    }
    return count;
}
