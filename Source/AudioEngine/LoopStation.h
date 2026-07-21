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
    enum State { Empty = 0, Recording, Playing, Overdub, Stopped, Armed };
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
            t.undoEnd = 0;
            t.cmdWrite.store (0);
            t.cmdRead = 0;
            t.redoState.store (false);
            t.reversed.store (false);
            t.pan.store (0.0f);
            t.armCountdown = 0;
            t.importLen.store (0);
            t.importPending.store (0);
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
        Play<->Overdub, Stopped->Play (resumes in phase). */
    void tapMain (int track)  { pushCmd (track, 1); }
    /** Throw one track away. */
    void tapClear (int track) { pushCmd (track, 2); }
    /** Remove everything added since this track last entered Overdub. */
    void tapUndo (int track)  { pushCmd (track, 3); }
    /** Wipe the track and start recording again in one tap. */
    void tapReRecord (int track) { pushCmd (track, 4); }

    /** Message thread: copies an audio FILE's samples into a track and sets
        it playing (a drum loop, an acapella...). The track is parked Empty
        first so the audio thread stops reading it, the data is resampled to
        the engine rate here, then command 5 publishes it atomically. */
    bool importAudio (int track, const juce::AudioBuffer<float>& src, double srcRate)
    {
        auto& t = trk (track);
        // Counted, not boolean: two rapid imports must ignore the FIRST
        // cmd 5 (its buffer is being rewritten by the second import).
        t.importPending.fetch_add (1);
        t.state.store (Empty);

        const double ratio  = srcRate > 0.0 ? srHz / srcRate : 1.0;
        const int    outLen = juce::jmin (maxLenSamples,
                                          (int) std::llround ((double) src.getNumSamples() * ratio));
        if (outLen < 32 || src.getNumChannels() < 1)
        {
            t.importPending.fetch_sub (1);
            return false;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            float*       dst = t.loop.getWritePointer (ch);
            const float* s   = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
            for (int i = 0; i < outLen; ++i)
            {
                const double x  = (double) i / ratio;
                const int    i0 = juce::jmin ((int) x, src.getNumSamples() - 1);
                const int    i1 = juce::jmin (i0 + 1, src.getNumSamples() - 1);
                const double f  = x - (double) i0;
                dst[i] = (float) (s[i0] * (1.0 - f) + s[i1] * f);
            }
        }

        t.importLen.store (outLen);
        pushCmd (track, 5);
        return true;
    }

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
    void setReversed (int track, bool r) { trk (track).reversed.store (r); }
    bool isReversed (int track) const    { return trk (track).reversed.load(); }
    void setPan (int track, float p)     { trk (track).pan.store (juce::jlimit (-1.0f, 1.0f, p)); }
    float getPan (int track) const       { return trk (track).pan.load(); }
    /** After UNDO the same button REDOES (the dub is parked, not gone). */
    bool  isRedo (int track) const       { return trk (track).redoState.load(); }
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

    /** Message thread: renders loops (volume, pan, reverse applied) into
        `out` over one cycle of the LONGEST loop — all other loops divide
        it, so the cycle closes cleanly and every stem lines up.
        onlyTrack -1 = mix every unmuted track; 0..5 = that single track
        as a stem (mute ignored — a stem is a stem). Returns false when
        there is nothing to render. */
    bool renderMixdown (juce::AudioBuffer<float>& out, int onlyTrack = -1) const
    {
        int longest = 0;
        for (auto& t : tracks)
            longest = juce::jmax (longest, t.lenSamples.load());
        if (longest <= 0)
            return false;

        if (onlyTrack >= 0 && trk (onlyTrack).lenSamples.load() <= 0)
            return false;

        out.setSize (2, longest);
        out.clear();

        for (int ti = 0; ti < kNumTracks; ++ti)
        {
            auto& t = tracks[ti];
            const int len = t.lenSamples.load();
            if (len <= 0)
                continue;
            if (onlyTrack >= 0 ? ti != onlyTrack : t.muted.load())
                continue;

            const float vol = t.volume.load();
            const float pn  = t.pan.load();
            const bool  rev = t.reversed.load();
            const float gl  = vol * (pn > 0.0f ? 1.0f - pn : 1.0f);
            const float gr  = vol * (pn < 0.0f ? 1.0f + pn : 1.0f);

            const float* L  = t.loop.getReadPointer (0);
            const float* R  = t.loop.getReadPointer (1);
            float* oL = out.getWritePointer (0);
            float* oR = out.getWritePointer (1);

            // Render from the MASTER grid top, honouring each track's own
            // phase stamp - otherwise tracks armed a cycle late (or file
            // imports) land bar-shifted versus what the performer heard.
            const int64_t phase0 = masterStamp - t.startStamp;
            for (int i = 0; i < longest; ++i)
            {
                int idx = (int) (((phase0 + i) % len + len) % len);
                if (rev) idx = len - 1 - idx;
                oL[i] += L[idx] * gl;
                oR[i] += R[idx] * gr;
            }
        }
        return true;
    }

    double getSampleRate() const { return srHz; }

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
            drainTrackCommands (i);

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
            }

            // Render the tick tail even after the metronome is switched off —
            // cutting a decaying sine mid-cycle is an audible step.
            if (clickEnv > 1.0e-4f)
            {
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

                if (st == Armed)
                {
                    // Armed tracks are silent and don't drive the transport.
                    if (t.armCountdown > 0)              // metronome count-in
                    {
                        if (--t.armCountdown <= 0)
                            t.state.store (Recording);
                    }
                    else                                  // wait for the loop top
                    {
                        const int master = masterLen.load (std::memory_order_relaxed);
                        if (master <= 0)
                            t.state.store (Recording);
                        else if ((int) (((transport - masterStamp) % master + master) % master) == 0)
                            t.state.store (Recording);
                        else if (! anyRunning())
                            t.state.store (Recording);   // grid frozen (all other
                                                         // tracks stopped): waiting
                                                         // would hang forever
                    }
                    continue;
                }

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

                    int idx = (int) (((transport - t.startStamp) % len + len) % len);
                    if (t.reversed.load (std::memory_order_relaxed))
                        idx = len - 1 - idx;   // read AND overdub run backwards

                    if (st == Overdub)
                    {
                        // Save each sample's PRE-SESSION value exactly once —
                        // rewriting the shadow on later passes would make UNDO
                        // restore "original + pass 1" on half the loop only.
                        if (t.dubSamples < len)
                        {
                            t.shadow.setSample (0, idx, L[idx]);
                            t.shadow.setSample (1, idx, R[idx]);
                            ++t.dubSamples;
                        }
                        L[idx] += inL;
                        R[idx] += inR;
                        // One full pass = one layer (wrap point flips when
                        // the track runs backwards).
                        if (idx == (t.reversed.load (std::memory_order_relaxed) ? 0 : len - 1))
                            t.layers.fetch_add (1);
                    }

                    if (! t.muted.load (std::memory_order_relaxed))
                    {
                        const float vol = t.volume.load (std::memory_order_relaxed);
                        const float pn  = t.pan.load (std::memory_order_relaxed);
                        outL += L[idx] * vol * (pn > 0.0f ? 1.0f - pn : 1.0f);
                        outR += R[idx] * vol * (pn < 0.0f ? 1.0f + pn : 1.0f);
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
        std::atomic<float> volume { 0.9f };
        std::atomic<float> uiPos { 0.0f };
        std::atomic<bool>  muted { false };
        std::atomic<bool>  undoAvail { false };

        // Command ring (message thread writes, audio thread reads): a single
        // slot would drop the second tap of a fast double-tap that lands in
        // one audio block, leaving e.g. an unintended Overdub running.
        std::atomic<int>   cmdQueue[8] {};
        std::atomic<int>   cmdWrite { 0 };
        int     cmdRead = 0;       // audio thread only

        std::atomic<bool>  redoState { false };   // dub currently parked in shadow
        std::atomic<bool>  reversed { false };
        std::atomic<float> pan { 0.0f };          // -1 L .. +1 R

        int     recPos = 0;        // write head while Recording (audio thread)
        int64_t startStamp = 0;    // transport value that maps to sample 0
        int     targetLen = 0;     // quantised finalise point (0 = free take)
        int     dubSamples = 0;    // samples visited in the current dub session
        int     undoEnd = 0;       // loop index where the last dub session ended
        int     armCountdown = 0;  // count-in samples left (0 = sync to loop top)
        std::atomic<int> importLen { 0 };       // length of a pending file import
        std::atomic<int> importPending { 0 };   // imports in flight (see cmd 5)
    };

    Track&       trk (int i)       { return tracks[juce::jlimit (0, kNumTracks - 1, i)]; }
    const Track& trk (int i) const { return tracks[juce::jlimit (0, kNumTracks - 1, i)]; }

    void pushCmd (int track, int cmd)
    {
        auto& t = trk (track);
        const int w = t.cmdWrite.load (std::memory_order_relaxed);
        t.cmdQueue[w & 7].store (cmd, std::memory_order_relaxed);
        t.cmdWrite.store (w + 1, std::memory_order_release);
    }

    int idxOf (const Track& t) const   // current play index (audio thread)
    {
        const int len = t.lenSamples.load (std::memory_order_relaxed);
        if (len <= 0)
            return 0;
        const int fwd = (int) (((transport - t.startStamp) % len + len) % len);
        return t.reversed.load (std::memory_order_relaxed) ? len - 1 - fwd : fwd;
    }

    void finalizeTake (Track& t)
    {
        int len = juce::jmax (1, t.targetLen > 0 ? juce::jmin (t.recPos, t.targetLen)
                                                 : t.recPos);
        const int master = masterLen.load (std::memory_order_relaxed);
        if (master <= 0)
        {
            // The first take DEFINES the grid: trim it to a multiple of 8 so
            // half/quarter/eighth sub-loops divide it exactly - otherwise
            // they drift against the downbeat a few samples per cycle,
            // forever. Costs at most 7 samples of tail.
            len = juce::jmax (8, len & ~7);
            masterLen.store (len);
            masterStamp = transport - (int64_t) t.recPos;   // grid top = take start
        }

        t.lenSamples.store (len);
        t.layers.store (1);
        // Phase-continue the take: for an exact-boundary or free take this is
        // "sample 0 = now" (recPos == len, same thing mod len); for a LATE
        // tap that got truncated it keeps playback where the performer
        // actually is instead of jumping back to the loop top.
        t.startStamp = transport - (int64_t) t.recPos;
        t.targetLen = 0;
        t.state.store (Playing);
    }

    void drainTrackCommands (int i)
    {
        Track& t = tracks[(size_t) i];
        while (t.cmdRead != t.cmdWrite.load (std::memory_order_acquire))
        {
            const int cmd = t.cmdQueue[t.cmdRead & 7].load (std::memory_order_relaxed);
            ++t.cmdRead;
            applyTrackCommand (t, cmd);
        }
    }

    void applyTrackCommand (Track& t, int cmd)
    {
        // While an import is rewriting this track's buffers on the message
        // thread, every other command must be dropped - a queued REC tap
        // would otherwise write input into the same floats (data race).
        if (t.importPending.load (std::memory_order_relaxed) > 0 && cmd != 5)
            return;

        const int st = t.state.load (std::memory_order_relaxed);

        if (cmd == 1)          // main button
        {
            if (st == Empty)
            {
                t.recPos = 0;
                t.targetLen = 0;
                const int master = masterLen.load (std::memory_order_relaxed);

                if (master > 0 && anyRunning())
                {
                    // Other loops are rolling: wait for the loop top so the
                    // take starts dead on the grid.
                    t.armCountdown = 0;
                    t.state.store (Armed);
                }
                else if (metroOn.load (std::memory_order_relaxed))
                {
                    // First take with the click on: one bar of count-in.
                    t.armCountdown = (int) std::llround (
                        4.0 * srHz * 60.0 / (double) metroBpm.load (std::memory_order_relaxed));
                    metroResetReq.store (true);
                    t.state.store (Armed);
                }
                else
                {
                    if (! anyLength())
                        metroResetReq.store (true);   // click grid starts with the take
                    t.state.store (Recording);
                }
            }
            else if (st == Armed)
            {
                t.state.store (Empty);   // second tap cancels the arm
            }
            else if (st == Recording)
            {
                // A tap a few ms after record-start is a double-tap bounce,
                // not a take - finalising it would define a near-zero master
                // loop and poison the whole grid. Treat it as cancel.
                if (t.recPos < (int) (srHz * 0.12))
                {
                    t.recPos = 0;
                    t.targetLen = 0;
                    t.state.store (Empty);
                    return;
                }

                const int master = masterLen.load (std::memory_order_relaxed);
                if (master > 0)
                {
                    // The tap IS the end of the performance: close the loop as
                    // close to it as the grid allows. Long takes snap to the
                    // nearest master multiple, short takes to a half/quarter/
                    // eighth sub-loop, and we never keep rolling for more than
                    // a quarter of the master - a stopped performer hearing
                    // the recorder still running reads it as lag.
                    int target = master;
                    if (t.recPos >= master)
                    {
                        const int mult = juce::jmax (1, (int) std::lround (
                                            (double) t.recPos / (double) master));
                        target = mult * master;
                        if (target - t.recPos > master / 4)
                            target = juce::jmax (master, (mult - 1) * master);
                    }
                    else
                    {
                        double bestErr = 1.0e18;
                        for (const int d : { 1, 2, 4, 8 })
                        {
                            const double len = (double) master / d;
                            const double err = std::abs (len - (double) t.recPos);
                            if (err < bestErr)
                            {
                                bestErr = err;
                                target  = juce::jmax (1, (int) std::lround (len));
                            }
                        }
                    }
                    target = juce::jmin (maxLenSamples, target);
                    if (t.recPos >= target)
                    {
                        t.targetLen = target;
                        finalizeTake (t);
                    }
                    else
                        t.targetLen = target;   // short remaining roll to the grid
                }
                else
                    finalizeTake (t);
            }
            else if (st == Playing)
            {
                t.dubSamples = 0;
                t.redoState.store (false);   // a new dub claims the shadow
                t.undoAvail.store (true);
                t.state.store (Overdub);
            }
            else if (st == Overdub)
            {
                t.undoEnd = idxOf (t);   // where this dub session stopped
                t.state.store (Playing);
            }
            else if (st == Stopped)
            {
                // Resume IN PHASE: startStamp is untouched, so the track
                // comes back locked to the shared transport instead of
                // restarting at its own top out of sync.
                t.state.store (Playing);
            }
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
            t.redoState.store (false);
            if (! anyLength())
            {
                masterLen.store (0);   // last loop gone: next take re-defines it
                masterStamp = 0;
            }
        }
        else if (cmd == 4)     // re-record: wipe this track and roll again
        {
            t.lenSamples.store (0);
            t.layers.store (0);
            t.undoAvail.store (false);
            t.recPos = 0;
            t.targetLen = 0;
            t.dubSamples = 0;
            t.redoState.store (false);
            if (! anyLength())
            {
                masterLen.store (0);
                masterStamp = 0;
                metroResetReq.store (true);
            }
            t.state.store (Recording);
        }
        else if (cmd == 5)     // an imported audio file becomes this loop
        {
            // A newer import is still rewriting the buffer: this cmd is stale.
            if (t.importPending.fetch_sub (1) > 1)
                return;

            const int len = juce::jmax (1, t.importLen.load (std::memory_order_relaxed));
            t.lenSamples.store (len);
            t.layers.store (1);
            t.undoAvail.store (false);
            t.redoState.store (false);
            t.recPos = 0;
            t.targetLen = 0;
            t.dubSamples = 0;
            const int master = masterLen.load (std::memory_order_relaxed);
            if (master <= 0)
            {
                masterLen.store (len);       // first content defines the grid
                masterStamp = transport;
            }
            t.startStamp = transport;        // its loop top = right now
            t.state.store (Playing);
        }
        else if (cmd == 3)     // undo <-> redo the latest dub session
        {
            if (t.undoAvail.load() && (st == Overdub || st == Playing))
            {
                const int len = t.lenSamples.load (std::memory_order_relaxed);
                if (len > 0 && t.dubSamples > 0)
                {
                    if (st == Overdub)
                        t.undoEnd = idxOf (t);   // freeze the session end here

                    const int count = juce::jmin (t.dubSamples, len);
                    const int idx   = t.undoEnd;
                    // Forward dubs walked (idx-count, idx]; reversed dubs
                    // walked [idx+1, idx+count] — both contiguous mod len.
                    int start = t.reversed.load (std::memory_order_relaxed)
                                    ? (idx + 1) % len
                                    : idx - count;
                    while (start < 0) start += len;
                    const int first = juce::jmin (count, len - start);

                    // SWAP loop <-> shadow so the same button REDOES: the
                    // removed dub is parked in the shadow, not thrown away.
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        float* a = t.loop.getWritePointer (ch);
                        float* b = t.shadow.getWritePointer (ch);
                        for (int k = 0; k < first; ++k)
                            std::swap (a[start + k], b[start + k]);
                        for (int k = 0; k < count - first; ++k)   // wrapped part
                            std::swap (a[k], b[k]);
                    }

                    const bool nowRemoved = ! t.redoState.load();
                    t.redoState.store (nowRemoved);
                    if (t.dubSamples >= len)
                    {
                        if (nowRemoved && t.layers.load() > 1) t.layers.fetch_sub (1);
                        else if (! nowRemoved)                 t.layers.fetch_add (1);
                    }
                }
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
                else if (st == Armed)
                {
                    t.state.store (Empty);
                }
                else if (st == Playing || st == Overdub)
                {
                    if (st == Overdub)
                        t.undoEnd = idxOf (t);   // dub session ends here
                    t.state.store (Stopped);
                }
            }
        }
        else if (cmd == 2)     // play all, re-synced from the top
        {
            transport = 0;
            masterStamp = 0;
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
        // The rewritten stamp shifts every loop index, so the recorded undo
        // region no longer lines up — restoring it would corrupt the loop.
        t.undoAvail.store (false);
        t.dubSamples = 0;
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
    int64_t masterStamp = 0;   // transport value at the master grid's loop top

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
