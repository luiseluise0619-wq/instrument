#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <cstdint>

/**
    RC-505-style multi-track layering looper (kNumTracks) for the plugin's
    own output, with a BPM metronome click that is mixed into the output
    only — never into a recording.

    Track 1's first take defines the master loop length; later tracks are
    quantised to a multiple of it (tap slightly early and the recorder keeps
    going to the boundary; tap slightly late and the tail past the boundary
    is dropped), so everything stays locked. All finished tracks read and
    overdub through one shared transport counter, which keeps their phases
    aligned no matter when each was recorded.

    Every UI action arrives through lock-free per-track / master command
    atomics and is applied on the audio thread. The first layer of a track
    WRITES (no giant memset needed for Clear) and overdubs ADD.

    UNDO: while a track overdubs, the pre-dub value of every visited sample
    is saved into a shadow buffer, so "undo" = copy the visited region back
    - it removes everything added since Overdub was last entered.

    Signal placement: process() is called with the plugin's pre-limiter
    output. It records that "performance" signal only (never its own
    playback, so layers don't multiply themselves), then mixes the loops
    into the buffer; the limiter after us protects the sum.
*/
class LoopStation
{
public:
    enum State { Empty = 0, Recording, Playing, Overdub, Stopped };
    static constexpr int kNumTracks = 6;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxLenSamples = (int) (sr * 30.0);          // up to 30 s master loops
        for (auto& t : tracks)
        {
            t.loop.setSize (2, maxLenSamples, false, true);
            t.shadow.setSize (2, maxLenSamples, false, true);
            t.state.store (Empty);
            t.lenSamples.store (0);
            t.layers.store (0);
            t.uiPos.store (0.0f);
            t.undoAvail.store (false);
            t.recPos = 0;
            t.startStamp = 0;
            t.targetLen = 0;
            t.dubSamples = 0;
        }
        masterLen.store (0);
        transport = 0;

        srHz = sr;
        clickDecay = (float) std::exp (-1.0 / (0.012 * sr));   // ~12 ms tick
        metroCountdown = 0.0;
        metroBeatIdx = 0;
        clickEnv = 0.0f;
        clickPhase = 0.0f;
    }

    // ---- Message thread (UI) --------------------------------------------
    /** Track main button: Empty->Record, Record->set length + Play,
        Play<->Overdub, Stopped->Play (all synced from the top). */
    void tapMain (int track)  { trk (track).pendingCmd.store (1); }
    /** Throw one track away. */
    void tapClear (int track) { trk (track).pendingCmd.store (2); }
    /** Remove everything added since this track last entered Overdub. */
    void tapUndo (int track)  { trk (track).pendingCmd.store (3); }
    /** Wipe the track and start recording again in one tap. */
    void tapReRecord (int track) { trk (track).pendingCmd.store (4); }

    // Metronome: an audible click at a set BPM, never recorded into loops.
    void setMetronomeOn (bool on)
    {
        if (on && ! metroOn.load()) metroResetReq.store (true);
        metroOn.store (on);
    }
    bool isMetronomeOn() const      { return metroOn.load(); }
    void setMetroBpm (float bpm)    { metroBpm.store (juce::jlimit (40.0f, 240.0f, bpm)); }
    float getMetroBpm() const       { return metroBpm.load(); }

    void setMuted (int track, bool m)   { trk (track).muted.store (m); }
    bool isMuted (int track) const      { return trk (track).muted.load(); }
    void setTrackVolume (int track, float v)
    {
        trk (track).volume.store (juce::jlimit (0.0f, 1.5f, v));
    }
    float getTrackVolume (int track) const { return trk (track).volume.load(); }

    int   getTrackState (int track) const  { return trk (track).state.load(); }
    int   getTrackLayers (int track) const { return trk (track).layers.load(); }
    float getTrackPosition (int track) const { return trk (track).uiPos.load(); }
    bool  canUndo (int track) const        { return trk (track).undoAvail.load(); }

    /** Master transport: stop everything / restart everything from the top /
        wipe all four tracks. */
    void tapStopAll()  { pendingMaster.store (1); }
    void tapPlayAll()  { pendingMaster.store (2); }
    void tapClearAll() { pendingMaster.store (3); }

    bool anyContent() const
    {
        for (auto& t : tracks)
            if (t.lenSamples.load() > 0 || t.state.load() == Recording)
                return true;
        return false;
    }
    bool anyRunning() const
    {
        for (auto& t : tracks)
        {
            const int s = t.state.load();
            if (s == Recording || s == Playing || s == Overdub)
                return true;
        }
        return false;
    }

    // ---- Audio thread -----------------------------------------------------
    void process (juce::AudioBuffer<float>& io, int numSamples)
    {
        applyMasterCommand();
        for (int i = 0; i < kNumTracks; ++i)
            applyTrackCommand (i);

        const int numCh = juce::jmin (2, io.getNumChannels());
        bool running = false;

        // Metronome bookkeeping (audio thread only).
        const bool  click = metroOn.load (std::memory_order_relaxed);
        const double samplesPerBeat = srHz * 60.0
                                      / (double) metroBpm.load (std::memory_order_relaxed);
        if (metroResetReq.exchange (false, std::memory_order_relaxed))
        {
            metroCountdown = 0.0;
            metroBeatIdx = 0;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            const float inL = io.getSample (0, n);
            const float inR = numCh > 1 ? io.getSample (1, n) : inL;
            float outL = 0.0f, outR = 0.0f;
            bool advance = false;

            // The click is summed into the OUTPUT only — the recorders below
            // capture the raw input, so it never ends up inside a loop.
            if (click)
            {
                if (metroCountdown <= 0.0)
                {
                    clickEnv   = 1.0f;
                    clickPhase = 0.0f;
                    clickFreq  = (metroBeatIdx % 4 == 0) ? 1568.0f : 1046.5f;  // accent on 1
                    ++metroBeatIdx;
                    metroCountdown += samplesPerBeat;
                }
                metroCountdown -= 1.0;

                const float c = std::sin (clickPhase) * clickEnv * 0.35f;
                clickPhase += (float) (juce::MathConstants<double>::twoPi * clickFreq / srHz);
                clickEnv   *= clickDecay;
                outL += c;
                outR += c;
            }

            for (auto& t : tracks)
            {
                const int st = t.state.load (std::memory_order_relaxed);
                if (st == Empty || st == Stopped)
                    continue;

                advance = true;
                float* L  = t.loop.getWritePointer (0);
                float* R  = t.loop.getWritePointer (1);

                if (st == Recording)
                {
                    L[t.recPos] = inL;
                    R[t.recPos] = inR;
                    ++t.recPos;

                    // Auto-finalise at a pending quantise boundary or the cap.
                    if ((t.targetLen > 0 && t.recPos >= t.targetLen)
                        || t.recPos >= maxLenSamples)
                        finalizeTake (t);
                }
                else   // Playing / Overdub
                {
                    const int len = t.lenSamples.load (std::memory_order_relaxed);
                    if (len <= 0)
                        continue;

                    const int idx = (int) (((transport - t.startStamp) % len + len) % len);

                    if (st == Overdub)
                    {
                        // Save the pre-dub sample so UNDO can put it back.
                        t.shadow.setSample (0, idx, L[idx]);
                        t.shadow.setSample (1, idx, R[idx]);
                        L[idx] += inL;
                        R[idx] += inR;
                        if (t.dubSamples < len)
                            ++t.dubSamples;
                        if (idx == len - 1)
                            t.layers.fetch_add (1);   // one full pass = one layer
                    }

                    if (! t.muted.load (std::memory_order_relaxed))
                    {
                        const float vol = t.volume.load (std::memory_order_relaxed);
                        outL += L[idx] * vol;
                        outR += R[idx] * vol;
                    }
                }
            }

            io.addSample (0, n, outL);
            if (numCh > 1)
                io.addSample (1, n, outR);

            if (advance)
            {
                ++transport;
                running = true;
            }
        }

        if (running)
            for (auto& t : tracks)
            {
                const int st = t.state.load (std::memory_order_relaxed);
                if (st == Recording)
                {
                    const int span = t.targetLen > 0 ? t.targetLen : maxLenSamples;
                    t.uiPos.store ((float) t.recPos / (float) juce::jmax (1, span));
                }
                else if (st == Playing || st == Overdub)
                {
                    const int len = t.lenSamples.load (std::memory_order_relaxed);
                    if (len > 0)
                        t.uiPos.store ((float) (((transport - t.startStamp) % len + len) % len)
                                       / (float) len);
                }
            }
    }

private:
    struct Track
    {
        juce::AudioBuffer<float> loop, shadow;
        std::atomic<int>   state { Empty };
        std::atomic<int>   lenSamples { 0 };
        std::atomic<int>   layers { 0 };
        std::atomic<int>   pendingCmd { 0 };
        std::atomic<float> volume { 0.9f };
        std::atomic<float> uiPos { 0.0f };
        std::atomic<bool>  muted { false };
        std::atomic<bool>  undoAvail { false };
        int     recPos = 0;        // write head while Recording (audio thread)
        int64_t startStamp = 0;    // transport value that maps to sample 0
        int     targetLen = 0;     // quantised finalise point (0 = free take)
        int     dubSamples = 0;    // samples visited in the current dub pass
    };

    Track&       trk (int i)       { return tracks[juce::jlimit (0, kNumTracks - 1, i)]; }
    const Track& trk (int i) const { return tracks[juce::jlimit (0, kNumTracks - 1, i)]; }

    void finalizeTake (Track& t)
    {
        int len = juce::jmax (1, t.targetLen > 0 ? juce::jmin (t.recPos, t.targetLen)
                                                 : t.recPos);
        const int master = masterLen.load (std::memory_order_relaxed);
        if (master <= 0)
            masterLen.store (len);

        t.lenSamples.store (len);
        t.layers.store (1);
        t.startStamp = transport;   // sample 0 plays back right now: seamless
        t.targetLen = 0;
        t.state.store (Playing);
    }

    void applyTrackCommand (int i)
    {
        Track& t = tracks[(size_t) i];
        const int cmd = t.pendingCmd.exchange (0, std::memory_order_relaxed);
        if (cmd == 0)
            return;

        const int st = t.state.load (std::memory_order_relaxed);

        if (cmd == 1)          // main button
        {
            if (st == Empty)
            {
                t.recPos = 0;
                t.targetLen = 0;
                if (! anyLength())
                    metroResetReq.store (true);   // click grid starts with the take
                t.state.store (Recording);
            }
            else if (st == Recording)
            {
                const int master = masterLen.load (std::memory_order_relaxed);
                if (master > 0)
                {
                    // Quantise: round to the nearest multiple of the master
                    // length. Late tap -> truncate; early tap -> keep rolling
                    // to the boundary.
                    const int mult = juce::jmax (1, (int) std::lround (
                                        (double) t.recPos / (double) master));
                    const int target = juce::jmin (maxLenSamples, mult * master);
                    if (t.recPos >= target)
                    {
                        t.targetLen = target;
                        finalizeTake (t);
                    }
                    else
                        t.targetLen = target;   // finish at the boundary
                }
                else
                    finalizeTake (t);
            }
            else if (st == Playing)
            {
                t.dubSamples = 0;
                t.undoAvail.store (true);
                t.state.store (Overdub);
            }
            else if (st == Overdub)  t.state.store (Playing);
            else if (st == Stopped)  playFromTop (t);
        }
        else if (cmd == 2)     // clear this track
        {
            t.state.store (Empty);
            t.lenSamples.store (0);
            t.layers.store (0);
            t.undoAvail.store (false);
            t.recPos = 0;
            t.targetLen = 0;
            t.dubSamples = 0;
            if (! anyLength())
                masterLen.store (0);   // last loop gone: next take re-defines it
        }
        else if (cmd == 4)     // re-record: wipe this track and roll again
        {
            t.lenSamples.store (0);
            t.layers.store (0);
            t.undoAvail.store (false);
            t.recPos = 0;
            t.targetLen = 0;
            t.dubSamples = 0;
            if (! anyLength())
            {
                masterLen.store (0);
                metroResetReq.store (true);
            }
            t.state.store (Recording);
        }
        else if (cmd == 3)     // undo the current / latest dub pass
        {
            if (t.undoAvail.load() && (st == Overdub || st == Playing))
            {
                const int len = t.lenSamples.load (std::memory_order_relaxed);
                if (len > 0 && t.dubSamples > 0)
                {
                    const int count = juce::jmin (t.dubSamples, len);
                    const int idx   = (int) (((transport - t.startStamp) % len + len) % len);
                    int start = idx - count;   // region we walked through
                    while (start < 0) start += len;
                    const int first = juce::jmin (count, len - start);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        t.loop.copyFrom (ch, start, t.shadow, ch, start, first);
                        if (count > first)   // wrapped region
                            t.loop.copyFrom (ch, 0, t.shadow, ch, 0, count - first);
                    }
                    if (t.dubSamples >= len && t.layers.load() > 1)
                        t.layers.fetch_sub (1);
                }
                t.dubSamples = 0;
                t.undoAvail.store (false);
                if (st == Overdub)
                    t.state.store (Playing);
            }
        }
    }

    void applyMasterCommand()
    {
        const int cmd = pendingMaster.exchange (0, std::memory_order_relaxed);
        if (cmd == 0)
            return;

        if (cmd == 1)          // stop all
        {
            for (auto& t : tracks)
            {
                const int st = t.state.load (std::memory_order_relaxed);
                if (st == Recording)
                {
                    if (t.lenSamples.load() > 0) t.state.store (Stopped);
                    else                         t.state.store (Empty);
                }
                else if (st == Playing || st == Overdub)
                    t.state.store (Stopped);
            }
        }
        else if (cmd == 2)     // play all, re-synced from the top
        {
            transport = 0;
            metroResetReq.store (true);
            for (auto& t : tracks)
                if (t.lenSamples.load() > 0)
                    playFromTop (t);
        }
        else if (cmd == 3)     // clear all
        {
            for (auto& t : tracks)
            {
                t.state.store (Empty);
                t.lenSamples.store (0);
                t.layers.store (0);
                t.undoAvail.store (false);
                t.recPos = 0;
                t.targetLen = 0;
            }
            masterLen.store (0);
            transport = 0;
        }
    }

    void playFromTop (Track& t)
    {
        t.startStamp = transport;   // sample 0 = now, same instant for all
        t.state.store (Playing);
    }

    bool anyLength() const
    {
        for (auto& t : tracks)
            if (t.lenSamples.load() > 0)
                return true;
        return false;
    }

    Track tracks[kNumTracks];
    int maxLenSamples = 1;
    int64_t transport = 0;

    std::atomic<int> masterLen { 0 };
    std::atomic<int> pendingMaster { 0 };

    // Metronome (atomics = UI writes; the rest is audio-thread state).
    std::atomic<bool>  metroOn { false };
    std::atomic<bool>  metroResetReq { false };
    std::atomic<float> metroBpm { 120.0f };
    double srHz = 44100.0;
    double metroCountdown = 0.0;
    int    metroBeatIdx = 0;
    float  clickEnv = 0.0f, clickPhase = 0.0f, clickFreq = 1046.5f, clickDecay = 0.99f;
};
