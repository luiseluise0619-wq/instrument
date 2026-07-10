// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 RC-505 스타일 '루프 스테이션(다중 트랙 루퍼)'이에요. 연주한 소리를 트랙에
// 녹음하고, 그 위에 겹쳐 녹음(오버덥)하며 층층이 쌓아 반복 재생합니다.
// [신호 위치] 리미터 '바로 앞'의 출력에 붙습니다. 마이크/연주 신호만 녹음하고(자기 재생음은
//   녹음 안 함 → 층이 스스로 불어나는 사고 방지), 루프들을 출력에 섞습니다. 합은 뒤 리미터가 보호.
// [스레드 핵심] 모든 UI 버튼은 '락 없는 atomic 명령'으로 오디오 스레드에 전달되고, 실제 처리는
//   오디오 스레드가 합니다(process). 메시지 스레드는 명령만 큐에 넣고, 오디오 스레드가 꺼내 실행.
// [UNDO 원리] 오버덥 중 덮어쓰기 전 원본 샘플을 그림자(shadow) 버퍼에 백업 → undo=그 구간을 되돌림.

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
// [클래스] LoopStation — 여러 트랙을 하나의 공유 '트랜스포트(재생 위치 카운터)'로 묶어 위상을 맞춤.
class LoopStation
{
public:
    // 트랙 상태들: 빈/녹음/재생/오버덥/정지/대기(Armed).
    enum State { Empty = 0, Recording, Playing, Overdub, Stopped, Armed };
    // 트랙 개수.
    static constexpr int kNumTracks = 6;

    // [함수] prepare — 준비(메시지 스레드). 트랙 버퍼(최대 30초)와 상태·메트로놈을 초기화.
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        // 트랙 하나가 담을 수 있는 최대 길이(30초).
        maxLenSamples = (int) (sr * 30.0);          // up to 30 s master loops
        for (auto& t : tracks)
        {
            // 루프 버퍼와 undo용 그림자 버퍼를 미리 확보(재생 중 할당 금지).
            t.loop.setSize (2, maxLenSamples, false, true);
            t.shadow.setSize (2, maxLenSamples, false, true);
            // 각 트랙 상태값들을 기본으로 초기화(atomic store).
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
        }
        // 마스터 길이/트랜스포트 초기화.
        masterLen.store (0);
        transport = 0;

        // 메트로놈(클릭) 관련 초기화. clickDecay=약 12ms 감쇠 계수.
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
    // [UI→오디오] 아래 tap 함수들은 명령 번호만 큐에 넣음(pushCmd). 실제 동작은 오디오 스레드가 처리.
    void tapMain (int track)  { pushCmd (track, 1); }
    /** Throw one track away. */
    void tapClear (int track) { pushCmd (track, 2); }
    /** Remove everything added since this track last entered Overdub. */
    void tapUndo (int track)  { pushCmd (track, 3); }
    /** Wipe the track and start recording again in one tap. */
    void tapReRecord (int track) { pushCmd (track, 4); }

    // Metronome: an audible click at a set BPM, never recorded into loops.
    // [메트로놈] 켤 때 위상 리셋을 요청(다음 오디오 블록에서 적용).
    void setMetronomeOn (bool on)
    {
        if (on && ! metroOn.load()) metroResetReq.store (true);
        metroOn.store (on);
    }
    bool isMetronomeOn() const      { return metroOn.load(); }
    // BPM 설정/조회(40~240 클램프).
    void setMetroBpm (float bpm)    { metroBpm.store (juce::jlimit (40.0f, 240.0f, bpm)); }
    float getMetroBpm() const       { return metroBpm.load(); }

    // 트랙별 음소거/역재생/팬 설정·조회. 모두 atomic이라 UI↔오디오 안전.
    void setMuted (int track, bool m)   { trk (track).muted.store (m); }
    bool isMuted (int track) const      { return trk (track).muted.load(); }
    void setReversed (int track, bool r) { trk (track).reversed.store (r); }
    bool isReversed (int track) const    { return trk (track).reversed.load(); }
    void setPan (int track, float p)     { trk (track).pan.store (juce::jlimit (-1.0f, 1.0f, p)); }
    float getPan (int track) const       { return trk (track).pan.load(); }
    /** After UNDO the same button REDOES (the dub is parked, not gone). */
    // undo 후 같은 버튼이 redo가 됨(덥은 버려지지 않고 그림자에 '주차').
    bool  isRedo (int track) const       { return trk (track).redoState.load(); }
    // 트랙 볼륨 설정/조회(0~1.5).
    void setTrackVolume (int track, float v)
    {
        trk (track).volume.store (juce::jlimit (0.0f, 1.5f, v));
    }
    float getTrackVolume (int track) const { return trk (track).volume.load(); }

    // 화면 표시용 읽기: 상태/층수/재생위치/undo 가능 여부.
    int   getTrackState (int track) const  { return trk (track).state.load(); }
    int   getTrackLayers (int track) const { return trk (track).layers.load(); }
    float getTrackPosition (int track) const { return trk (track).uiPos.load(); }
    bool  canUndo (int track) const        { return trk (track).undoAvail.load(); }

    /** Master transport: stop everything / restart everything from the top /
        wipe all four tracks. */
    // 마스터 명령도 atomic 한 칸으로 전달(정지/처음부터 재생/전체 삭제).
    void tapStopAll()  { pendingMaster.store (1); }
    void tapPlayAll()  { pendingMaster.store (2); }
    void tapClearAll() { pendingMaster.store (3); }

    // [조회] 내용이 있는 트랙이 하나라도 있는지.
    bool anyContent() const
    {
        for (auto& t : tracks)
            if (t.lenSamples.load() > 0 || t.state.load() == Recording)
                return true;
        return false;
    }
    // [조회] 실행 중(녹음/재생/오버덥)인 트랙이 하나라도 있는지.
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
    // [함수] process — ★오디오 콜백★. 명령 적용 + 녹음 + 오버덥 + 루프 믹스 + 메트로놈.
    void process (juce::AudioBuffer<float>& io, int numSamples)
    {
        // 먼저 UI가 넣어둔 명령들을 소화(마스터 → 트랙별).
        applyMasterCommand();
        for (int i = 0; i < kNumTracks; ++i)
            drainTrackCommands (i);

        const int numCh = juce::jmin (2, io.getNumChannels());
        bool running = false;

        // Metronome bookkeeping (audio thread only).
        // 메트로놈 상태 계산(오디오 스레드 전용). samplesPerBeat=한 박자당 샘플 수.
        const bool  click = metroOn.load (std::memory_order_relaxed);
        const double samplesPerBeat = srHz * 60.0
                                      / (double) metroBpm.load (std::memory_order_relaxed);
        // 리셋 요청이 있었으면 카운트다운/박자 인덱스 초기화(exchange로 읽고 동시에 false로).
        if (metroResetReq.exchange (false, std::memory_order_relaxed))
        {
            metroCountdown = 0.0;
            metroBeatIdx = 0;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            // 이번 샘플의 입력(연주 신호). 모노면 좌우 동일.
            const float inL = io.getSample (0, n);
            const float inR = numCh > 1 ? io.getSample (1, n) : inL;
            float outL = 0.0f, outR = 0.0f;
            bool advance = false;

            // The click is summed into the OUTPUT only — the recorders below
            // capture the raw input, so it never ends up inside a loop.
            // 클릭은 출력에만 더함. 녹음은 원본 입력(inL/inR)만 담으므로 클릭이 루프에 안 섞임.
            if (click)
            {
                // 카운트다운이 0에 닿으면 새 클릭 트리거(첫 박은 강조음 1568Hz).
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
            // 클릭이 꺼져도 이미 울리던 꼬리는 마저 냄(사인을 중간에 끊으면 '툭' 소리).
            if (clickEnv > 1.0e-4f)
            {
                const float c = std::sin (clickPhase) * clickEnv * 0.35f;
                clickPhase += (float) (juce::MathConstants<double>::twoPi * clickFreq / srHz);
                clickEnv   *= clickDecay;
                outL += c;
                outR += c;
            }

            // 모든 트랙을 순회하며 녹음/재생 처리.
            for (auto& t : tracks)
            {
                const int st = t.state.load (std::memory_order_relaxed);
                // 빈/정지 트랙은 건너뜀.
                if (st == Empty || st == Stopped)
                    continue;

                if (st == Armed)
                {
                    // Armed tracks are silent and don't drive the transport.
                    // 대기(Armed) 트랙: 소리 없이 녹음 시작 시점을 기다림.
                    if (t.armCountdown > 0)              // metronome count-in
                    {
                        // 카운트인이 끝나면 녹음 시작.
                        if (--t.armCountdown <= 0)
                            t.state.store (Recording);
                    }
                    else                                  // wait for the loop top
                    {
                        // 마스터 루프의 '맨 앞' 순간에 정확히 녹음 시작(그리드 정렬).
                        const int master = masterLen.load (std::memory_order_relaxed);
                        if (master <= 0)
                            t.state.store (Recording);
                        else if ((int) (((transport - masterStamp) % master + master) % master) == 0)
                            t.state.store (Recording);
                    }
                    continue;
                }

                // 여기부터는 실제로 트랜스포트를 굴리는 트랙.
                advance = true;
                float* L  = t.loop.getWritePointer (0);
                float* R  = t.loop.getWritePointer (1);

                if (st == Recording)
                {
                    // 첫 층 녹음: 입력을 그대로 '쓰기'(WRITE). recPos 전진.
                    L[t.recPos] = inL;
                    R[t.recPos] = inR;
                    ++t.recPos;

                    // Auto-finalise at a pending quantise boundary or the cap.
                    // 예약된 양자화 경계나 최대 길이에 닿으면 자동으로 마감.
                    if ((t.targetLen > 0 && t.recPos >= t.targetLen)
                        || t.recPos >= maxLenSamples)
                        finalizeTake (t);
                }
                else   // Playing / Overdub
                {
                    const int len = t.lenSamples.load (std::memory_order_relaxed);
                    if (len <= 0)
                        continue;

                    // 공유 트랜스포트로 현재 재생 인덱스 계산(트랙 시작 스탬프 기준, 원형).
                    int idx = (int) (((transport - t.startStamp) % len + len) % len);
                    // 역재생이면 인덱스를 뒤집음.
                    if (t.reversed.load (std::memory_order_relaxed))
                        idx = len - 1 - idx;   // read AND overdub run backwards

                    if (st == Overdub)
                    {
                        // Save each sample's PRE-SESSION value exactly once —
                        // rewriting the shadow on later passes would make UNDO
                        // restore "original + pass 1" on half the loop only.
                        // [UNDO 백업] 이번 덥 세션에서 방문한 각 샘플의 '덥 이전 값'을 딱 한 번만 그림자에 저장.
                        if (t.dubSamples < len)
                        {
                            t.shadow.setSample (0, idx, L[idx]);
                            t.shadow.setSample (1, idx, R[idx]);
                            ++t.dubSamples;
                        }
                        // 오버덥은 '더하기'(ADD).
                        L[idx] += inL;
                        R[idx] += inR;
                        // One full pass = one layer (wrap point flips when
                        // the track runs backwards).
                        // 한 바퀴 돌면 층(layer) +1(역재생이면 되감김 지점이 반대).
                        if (idx == (t.reversed.load (std::memory_order_relaxed) ? 0 : len - 1))
                            t.layers.fetch_add (1);
                    }

                    // 음소거가 아니면 볼륨/팬을 적용해 출력에 더함.
                    if (! t.muted.load (std::memory_order_relaxed))
                    {
                        const float vol = t.volume.load (std::memory_order_relaxed);
                        const float pn  = t.pan.load (std::memory_order_relaxed);
                        // 팬: 오른쪽으로 치우치면 좌 게인 줄이고, 왼쪽이면 우 게인 줄임.
                        outL += L[idx] * vol * (pn > 0.0f ? 1.0f - pn : 1.0f);
                        outR += R[idx] * vol * (pn < 0.0f ? 1.0f + pn : 1.0f);
                    }
                }
            }

            // 만든 루프 합을 입출력 버퍼에 더함(io는 입력이자 출력).
            io.addSample (0, n, outL);
            if (numCh > 1)
                io.addSample (1, n, outR);

            // 굴러가는 트랙이 있었으면 트랜스포트 전진.
            if (advance)
            {
                ++transport;
                running = true;
            }
        }

        // 블록 끝에서 화면 표시용 재생 위치(uiPos)를 갱신.
        if (running)
            for (auto& t : tracks)
            {
                const int st = t.state.load (std::memory_order_relaxed);
                if (st == Recording)
                {
                    // 녹음 중이면 recPos/예상 길이 비율.
                    const int span = t.targetLen > 0 ? t.targetLen : maxLenSamples;
                    t.uiPos.store ((float) t.recPos / (float) juce::jmax (1, span));
                }
                else if (st == Playing || st == Overdub)
                {
                    // 재생/오버덥이면 현재 인덱스/길이 비율.
                    const int len = t.lenSamples.load (std::memory_order_relaxed);
                    if (len > 0)
                        t.uiPos.store ((float) (((transport - t.startStamp) % len + len) % len)
                                       / (float) len);
                }
            }
    }

private:
    // [내부 구조체] Track — 트랙 하나의 모든 상태. atomic=UI/오디오 공유, 나머지=오디오 전용.
    struct Track
    {
        // 루프 오디오와 undo용 그림자 버퍼.
        juce::AudioBuffer<float> loop, shadow;
        // 상태/길이/층수/볼륨/화면위치/음소거/undo가능 (모두 atomic).
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
        // [명령 링버퍼] UI가 쓰고 오디오가 읽음. 한 칸만 있으면 빠른 더블탭의 두 번째가 씹힘 → 8칸.
        std::atomic<int>   cmdQueue[8] {};
        std::atomic<int>   cmdWrite { 0 };
        int     cmdRead = 0;       // audio thread only

        // undo/redo 주차 상태, 역재생, 팬(-1 왼 ~ +1 오른).
        std::atomic<bool>  redoState { false };   // dub currently parked in shadow
        std::atomic<bool>  reversed { false };
        std::atomic<float> pan { 0.0f };          // -1 L .. +1 R

        // 오디오 스레드 전용 상태들.
        int     recPos = 0;        // write head while Recording (audio thread)
        int64_t startStamp = 0;    // transport value that maps to sample 0
        int     targetLen = 0;     // quantised finalise point (0 = free take)
        int     dubSamples = 0;    // samples visited in the current dub session
        int     undoEnd = 0;       // loop index where the last dub session ended
        int     armCountdown = 0;  // count-in samples left (0 = sync to loop top)
    };

    // [도우미] trk — 트랙 번호를 안전 범위로 클램프해 트랙 참조 반환(const/비const 두 버전).
    Track&       trk (int i)       { return tracks[juce::jlimit (0, kNumTracks - 1, i)]; }
    const Track& trk (int i) const { return tracks[juce::jlimit (0, kNumTracks - 1, i)]; }

    // [도우미] pushCmd — ★메시지 스레드★. 명령을 링에 넣음. release로 써서 오디오가 acquire로 안전하게 봄.
    void pushCmd (int track, int cmd)
    {
        auto& t = trk (track);
        const int w = t.cmdWrite.load (std::memory_order_relaxed);
        t.cmdQueue[w & 7].store (cmd, std::memory_order_relaxed);
        t.cmdWrite.store (w + 1, std::memory_order_release);
    }

    // [도우미] idxOf — 현재 재생 인덱스 계산(오디오 스레드). 역재생 반영.
    int idxOf (const Track& t) const   // current play index (audio thread)
    {
        const int len = t.lenSamples.load (std::memory_order_relaxed);
        if (len <= 0)
            return 0;
        const int fwd = (int) (((transport - t.startStamp) % len + len) % len);
        return t.reversed.load (std::memory_order_relaxed) ? len - 1 - fwd : fwd;
    }

    // [도우미] finalizeTake — 녹음을 마감해 길이를 확정하고 재생 상태로 전환.
    void finalizeTake (Track& t)
    {
        // 길이 = 목표가 있으면 recPos와 목표 중 작은 값, 없으면 recPos(최소 1).
        int len = juce::jmax (1, t.targetLen > 0 ? juce::jmin (t.recPos, t.targetLen)
                                                 : t.recPos);
        const int master = masterLen.load (std::memory_order_relaxed);
        // 아직 마스터 길이가 없으면 이 테이크가 마스터가 됨(그리드 기준점 설정).
        if (master <= 0)
        {
            masterLen.store (len);
            masterStamp = transport - (int64_t) t.recPos;   // grid top = take start
        }

        t.lenSamples.store (len);
        t.layers.store (1);
        // Phase-continue the take: for an exact-boundary or free take this is
        // "sample 0 = now" (recPos == len, same thing mod len); for a LATE
        // tap that got truncated it keeps playback where the performer
        // actually is instead of jumping back to the loop top.
        // 위상 연속: 시작 스탬프를 '테이크 시작 순간'으로 잡아, 늦게 눌러 잘려도 튀지 않고 이어짐.
        t.startStamp = transport - (int64_t) t.recPos;
        t.targetLen = 0;
        t.state.store (Playing);
    }

    // [도우미] drainTrackCommands — 오디오 스레드가 링에서 명령을 모두 꺼내 실행.
    void drainTrackCommands (int i)
    {
        Track& t = tracks[(size_t) i];
        // acquire로 읽어 pushCmd의 release와 짝을 이룸(명령이 확실히 보임).
        while (t.cmdRead != t.cmdWrite.load (std::memory_order_acquire))
        {
            const int cmd = t.cmdQueue[t.cmdRead & 7].load (std::memory_order_relaxed);
            ++t.cmdRead;
            applyTrackCommand (t, cmd);
        }
    }

    // [도우미] applyTrackCommand — 명령 하나를 현재 상태에 맞게 처리(루퍼의 핵심 상태 기계).
    void applyTrackCommand (Track& t, int cmd)
    {
        const int st = t.state.load (std::memory_order_relaxed);

        if (cmd == 1)          // main button
        {
            if (st == Empty)
            {
                // 빈 트랙에서 메인 버튼: 상황에 따라 대기/카운트인/즉시 녹음.
                t.recPos = 0;
                t.targetLen = 0;
                const int master = masterLen.load (std::memory_order_relaxed);

                if (master > 0 && anyRunning())
                {
                    // Other loops are rolling: wait for the loop top so the
                    // take starts dead on the grid.
                    // 다른 루프가 돌고 있으면 루프 맨 앞까지 대기(그리드에 딱 맞춰 시작).
                    t.armCountdown = 0;
                    t.state.store (Armed);
                }
                else if (metroOn.load (std::memory_order_relaxed))
                {
                    // First take with the click on: one bar of count-in.
                    // 첫 테이크 + 메트로놈 켜짐: 한 마디(4박) 카운트인 후 시작.
                    t.armCountdown = (int) std::llround (
                        4.0 * srHz * 60.0 / (double) metroBpm.load (std::memory_order_relaxed));
                    metroResetReq.store (true);
                    t.state.store (Armed);
                }
                else
                {
                    // 아무 길이도 없으면 클릭 그리드를 이 테이크와 함께 시작.
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
                const int master = masterLen.load (std::memory_order_relaxed);
                if (master > 0)
                {
                    // Quantise: round to the nearest multiple of the master
                    // length. Late tap -> truncate; early tap -> keep rolling
                    // to the boundary.
                    // 양자화: 마스터 길이의 가장 가까운 배수로 반올림. 늦으면 자르고, 이르면 경계까지 더 감.
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
                    // 마스터가 없으면 지금 즉시 마감(이 테이크가 기준이 됨).
                    finalizeTake (t);
            }
            else if (st == Playing)
            {
                // 재생 중 메인 버튼: 오버덥 시작(그림자 백업 준비, undo 가능 표시).
                t.dubSamples = 0;
                t.redoState.store (false);   // a new dub claims the shadow
                t.undoAvail.store (true);
                t.state.store (Overdub);
            }
            else if (st == Overdub)
            {
                // 오버덥 중 버튼: 세션 종료 지점 기록 후 재생으로.
                t.undoEnd = idxOf (t);   // where this dub session stopped
                t.state.store (Playing);
            }
            else if (st == Stopped)
            {
                // Resume IN PHASE: startStamp is untouched, so the track
                // comes back locked to the shared transport instead of
                // restarting at its own top out of sync.
                // 정지에서 재생 복귀: 시작 스탬프를 안 건드려 공유 트랜스포트에 위상 맞춰 복귀.
                t.state.store (Playing);
            }
        }
        else if (cmd == 2)     // clear this track
        {
            // 이 트랙을 완전히 비움.
            t.state.store (Empty);
            t.lenSamples.store (0);
            t.layers.store (0);
            t.undoAvail.store (false);
            t.recPos = 0;
            t.targetLen = 0;
            t.dubSamples = 0;
            t.redoState.store (false);
            // 남은 루프가 하나도 없으면 마스터 길이도 리셋(다음 테이크가 재정의).
            if (! anyLength())
            {
                masterLen.store (0);   // last loop gone: next take re-defines it
                masterStamp = 0;
            }
        }
        else if (cmd == 4)     // re-record: wipe this track and roll again
        {
            // 재녹음: 트랙을 비우고 곧바로 다시 녹음.
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
        else if (cmd == 3)     // undo <-> redo the latest dub session
        {
            // undo/redo: 최근 덥 세션을 되돌리거나 다시 넣음.
            if (t.undoAvail.load() && (st == Overdub || st == Playing))
            {
                const int len = t.lenSamples.load (std::memory_order_relaxed);
                if (len > 0 && t.dubSamples > 0)
                {
                    // 오버덥 중이면 지금 지점을 세션 끝으로 고정.
                    if (st == Overdub)
                        t.undoEnd = idxOf (t);   // freeze the session end here

                    const int count = juce::jmin (t.dubSamples, len);
                    const int idx   = t.undoEnd;
                    // Forward dubs walked (idx-count, idx]; reversed dubs
                    // walked [idx+1, idx+count] — both contiguous mod len.
                    // 덥이 방문한 구간의 시작점 계산(정방향/역방향 각각, 원형).
                    int start = t.reversed.load (std::memory_order_relaxed)
                                    ? (idx + 1) % len
                                    : idx - count;
                    while (start < 0) start += len;
                    // 원형 경계 전까지의 앞부분 길이.
                    const int first = juce::jmin (count, len - start);

                    // SWAP loop <-> shadow so the same button REDOES: the
                    // removed dub is parked in the shadow, not thrown away.
                    // [핵심] loop와 shadow를 '교환'. 덥을 버리지 않고 그림자에 보관 → 같은 버튼이 redo가 됨.
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        float* a = t.loop.getWritePointer (ch);
                        float* b = t.shadow.getWritePointer (ch);
                        for (int k = 0; k < first; ++k)
                            std::swap (a[start + k], b[start + k]);
                        for (int k = 0; k < count - first; ++k)   // wrapped part
                            std::swap (a[k], b[k]);
                    }

                    // redo 상태 토글 및 층수 조정.
                    const bool nowRemoved = ! t.redoState.load();
                    t.redoState.store (nowRemoved);
                    if (t.dubSamples >= len)
                    {
                        if (nowRemoved && t.layers.load() > 1) t.layers.fetch_sub (1);
                        else if (! nowRemoved)                 t.layers.fetch_add (1);
                    }
                }
                // 오버덥 중이었으면 재생으로 전환.
                if (st == Overdub)
                    t.state.store (Playing);
            }
        }
    }

    // [도우미] applyMasterCommand — 마스터 명령(정지/전체재생/전체삭제) 처리.
    void applyMasterCommand()
    {
        // 대기 중인 마스터 명령을 읽고 동시에 0으로 비움.
        const int cmd = pendingMaster.exchange (0, std::memory_order_relaxed);
        if (cmd == 0)
            return;

        if (cmd == 1)          // stop all
        {
            // 전체 정지: 녹음 중이면 내용 여부에 따라 정지/비움, 대기는 비움, 재생/오버덥은 정지.
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
            // 전체를 처음부터 다시 동기 재생(트랜스포트/스탬프 리셋).
            transport = 0;
            masterStamp = 0;
            metroResetReq.store (true);
            for (auto& t : tracks)
                if (t.lenSamples.load() > 0)
                    playFromTop (t);
        }
        else if (cmd == 3)     // clear all
        {
            // 전체 삭제.
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

    // [도우미] playFromTop — 트랙을 지금 순간을 0으로 삼아 재생(모든 트랙 동시 정렬).
    void playFromTop (Track& t)
    {
        t.startStamp = transport;   // sample 0 = now, same instant for all
        // The rewritten stamp shifts every loop index, so the recorded undo
        // region no longer lines up — restoring it would corrupt the loop.
        // 스탬프를 바꾸면 인덱스가 다 밀려 이전 undo 구간이 안 맞음 → undo 비활성.
        t.undoAvail.store (false);
        t.dubSamples = 0;
        t.state.store (Playing);
    }

    // [도우미] anyLength — 길이가 있는(내용 있는) 트랙이 하나라도 있는지.
    bool anyLength() const
    {
        for (auto& t : tracks)
            if (t.lenSamples.load() > 0)
                return true;
        return false;
    }

    // 트랙 배열과 전역 상태들.
    Track tracks[kNumTracks];
    int maxLenSamples = 1;
    // 공유 트랜스포트(모든 트랙이 이 카운터로 위상 정렬)와 마스터 그리드 기준 스탬프.
    int64_t transport = 0;
    int64_t masterStamp = 0;   // transport value at the master grid's loop top

    // 마스터 길이와 대기 중 마스터 명령(atomic).
    std::atomic<int> masterLen { 0 };
    std::atomic<int> pendingMaster { 0 };

    // Metronome (atomics = UI writes; the rest is audio-thread state).
    // 메트로놈: atomic은 UI가 쓰는 값, 나머지는 오디오 스레드 전용 상태.
    std::atomic<bool>  metroOn { false };
    std::atomic<bool>  metroResetReq { false };
    std::atomic<float> metroBpm { 120.0f };
    double srHz = 44100.0;
    double metroCountdown = 0.0;
    int    metroBeatIdx = 0;
    float  clickEnv = 0.0f, clickPhase = 0.0f, clickFreq = 1046.5f, clickDecay = 0.99f;
};
