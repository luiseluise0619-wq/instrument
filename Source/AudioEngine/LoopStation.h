#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

/**
    RC-505-style layering looper for the plugin's own output.

    The first recording pass defines the loop length; every later Overdub
    pass ADDS the live performance on top, so a beat builds up layer by
    layer from one laptop. All state changes come in through lock-free
    command atomics and are applied on the audio thread; the first layer
    WRITES (no giant memset needed for Clear) and overdubs ADD.

    Signal placement: process() is called with the plugin's pre-limiter
    output. It records that "performance" signal only (never its own
    playback, so layers don't multiply themselves), then mixes the loop
    into the buffer; the limiter after us protects the sum.
*/
class LoopStation
{
public:
    enum State { Empty = 0, Recording, Playing, Overdub, Stopped };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxLenSamples = (int) (sr * 30.0);          // up to 30 s loops
        loop.setSize (2, maxLenSamples, false, true);
        state.store (Empty);
        lenSamples.store (0);
        pos = 0;
        layers.store (0);
        uiPos.store (0.0f);
    }

    // ---- Message thread (UI) --------------------------------------------
    /** Main button: Empty->Record, Record->set length + Play,
        Play<->Overdub. */
    void tapMain()  { pendingCmd.store (1); }
    /** Stop playback / restart from the top when stopped. */
    void tapStop()  { pendingCmd.store (2); }
    /** Throw the loop away. */
    void tapClear() { pendingCmd.store (3); }

    int   getState() const     { return state.load(); }
    int   getLayers() const    { return layers.load(); }
    float getPosition() const  { return uiPos.load(); }      // 0..1 in loop
    float getLengthSeconds (double sr) const
    {
        return sr > 0.0 ? (float) lenSamples.load() / (float) sr : 0.0f;
    }

    void setVolume (float v)   { volume.store (juce::jlimit (0.0f, 1.5f, v)); }
    float getVolume() const    { return volume.load(); }

    // ---- Audio thread -----------------------------------------------------
    void process (juce::AudioBuffer<float>& io, int numSamples)
    {
        applyPendingCommand();

        const int st = state.load (std::memory_order_relaxed);
        if (st == Empty || st == Stopped)
            return;

        const int numCh = juce::jmin (2, io.getNumChannels());
        float* L = loop.getWritePointer (0);
        float* R = loop.getWritePointer (1);
        const float vol = volume.load (std::memory_order_relaxed);

        for (int n = 0; n < numSamples; ++n)
        {
            const float inL = io.getSample (0, n);
            const float inR = numCh > 1 ? io.getSample (1, n) : inL;

            if (st == Recording)
            {
                // First layer WRITES, so a cleared loop needs no memset.
                L[pos] = inL;
                R[pos] = inR;

                if (++pos >= maxLenSamples)   // safety: auto-finalise at max
                {
                    lenSamples.store (maxLenSamples);
                    state.store (Playing);
                    layers.store (1);
                    pos = 0;
                    break;
                }
            }
            else   // Playing / Overdub
            {
                const int len = lenSamples.load (std::memory_order_relaxed);
                if (len <= 0)
                    break;

                if (st == Overdub)
                {
                    L[pos] += inL;
                    R[pos] += inR;
                }

                io.addSample (0, n, L[pos] * vol);
                if (numCh > 1)
                    io.addSample (1, n, R[pos] * vol);

                if (++pos >= len)
                {
                    pos = 0;
                    if (st == Overdub)
                        layers.fetch_add (1);   // one full pass = one layer
                }
            }
        }

        const int len = juce::jmax (1, st == Recording ? maxLenSamples
                                                       : lenSamples.load());
        uiPos.store ((float) pos / (float) len);
    }

private:
    void applyPendingCommand()
    {
        const int cmd = pendingCmd.exchange (0, std::memory_order_relaxed);
        if (cmd == 0)
            return;

        const int st = state.load (std::memory_order_relaxed);

        if (cmd == 1)          // main button
        {
            if (st == Empty)                      { pos = 0; state.store (Recording); }
            else if (st == Recording)
            {
                lenSamples.store (juce::jmax (1, pos));
                layers.store (1);
                pos = 0;
                state.store (Playing);
            }
            else if (st == Playing)               state.store (Overdub);
            else if (st == Overdub)               state.store (Playing);
            else if (st == Stopped)               { pos = 0; state.store (Overdub); }
        }
        else if (cmd == 2)     // stop / restart
        {
            if (st == Playing || st == Overdub || st == Recording)
            {
                if (st == Recording)   // cancel an unfinished first take
                {
                    state.store (lenSamples.load() > 0 ? Stopped : Empty);
                }
                else
                    state.store (Stopped);
            }
            else if (st == Stopped)               { pos = 0; state.store (Playing); }
        }
        else if (cmd == 3)     // clear
        {
            state.store (Empty);
            lenSamples.store (0);
            layers.store (0);
            pos = 0;
        }
    }

    juce::AudioBuffer<float> loop;
    int maxLenSamples = 1;
    int pos = 0;

    std::atomic<int>   state { Empty };
    std::atomic<int>   lenSamples { 0 };
    std::atomic<int>   layers { 0 };
    std::atomic<int>   pendingCmd { 0 };
    std::atomic<float> volume { 0.9f };
    std::atomic<float> uiPos { 0.0f };
};
