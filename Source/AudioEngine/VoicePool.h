/*  [초보자 안내]
    '보이스 풀' = 동시에 소리 낼 수 있는 목소리(Voice) 여러 개를 미리 만들어
    두고 돌려쓰는 창고예요. 건반을 누르면 빈 보이스 하나를 꺼내 슬라이스를
    재생시키고, 다 쓰면 반납합니다(재생 중 new/delete 금지라서 미리 만들어 둠).
    재미있는 부분은 '무덤(graveyard) 패턴': 새 샘플을 로드해도 옛 샘플 버퍼를
    바로 지우지 않고 무덤 목록에 눕혀 둡니다. 아직 그 버퍼로 소리 내는 보이스가
    있을 수 있으니까요. 아무도 안 쓰는 게 확인되면(참조 수 1) 그때 메시지
    스레드에서 치웁니다 — 오디오 스레드에서 메모리 해제가 터지는 걸 막는 장치.
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
    A single polyphonic voice. Plays back a region [start, start+len) of a
    shared source buffer, resampled from the sample's native rate to the host
    rate (linear interpolation), shaped by a full ADSR envelope with a tiny
    end-of-slice fade so retriggering/end-of-slice never clicks.

    Supports optional reverse playback (reads the slice backwards) and two play
    modes: Gate (release() starts the release stage) and one-shot (plays the
    whole slice to its end and ignores release()).

    The voice holds a shared_ptr to the source buffer, so the buffer stays alive
    for the voice's whole lifetime even if a new sample is loaded mid-note.
*/
class Voice
{
public:
    void start (double hostSampleRate,
                std::shared_ptr<const juce::AudioBuffer<float>> src, double srcSampleRate,
                int startSample, int lengthSamples,
                float velocity,
                float attackMs, float decayMs, float sustain0to1, float releaseMs,
                bool reverse, bool oneShot);

    void render (juce::AudioBuffer<float>& out, int numSamples);
    void release();
    void hardStop();   // All Sound Off: fast-fades even one-shot voices

    bool isActive() const { return active; }

    // Normalised position of the read head in the WHOLE sample (0..1), or -1 if
    // inactive. Valid after render(). Safe to read from any thread only via the
    // VoicePool atomic mirror; this raw accessor is for the pool's own use.
    float normalisedPosition() const;

private:
    float envelope() const;

    enum class Stage { attack, decay, sustain, release, finished };

    std::shared_ptr<const juce::AudioBuffer<float>> source;
    const float* srcL = nullptr;
    const float* srcR = nullptr;
    int   srcNumSamples = 0;

    double pos = 0.0;          // fractional read position (source samples)
    double sliceStart = 0.0;   // slice start (source samples)
    double length = 0.0;       // slice length (source samples)
    double ratio = 1.0;        // srcSampleRate / hostSampleRate

    // ADSR (all in output samples).
    int   attackSamples  = 1;
    int   decaySamples   = 1;
    int   releaseSamples = 1;
    int   endFadeSamples = 1;  // tiny click-free tail at end of slice
    float sustainLevel   = 1.0f;

    Stage stage       = Stage::attack;
    int   stagePos    = 0;     // output samples elapsed in the current stage
    float releaseFrom = 1.0f;  // env level captured when release starts

    bool  reversePlay = false;
    bool  oneShotMode = false;

    float velocity = 1.0f;
    bool  active   = false;
};

/**
    Fixed-size pool of voices sharing one source buffer.

    triggerVoice() runs on the audio thread only (MIDI and the processor's
    lock-free pad queue both feed it there). setSource() runs on the message
    thread and is guarded by a spin lock; triggerVoice() takes the lock with a
    try-lock and simply drops the trigger on the rare contended block.

    The envelope/reverse/play-mode setters are called from the audio thread once
    per block before any triggers. New voices adopt the current settings.

    Playhead readout: each active voice writes its normalised position in the
    whole sample (0..1) into its atomic slot once per block at the end of
    render; the slot is -1 while inactive. copyPlayheads() lets the message
    thread read them without locking.
*/
class VoicePool
{
public:
    void prepare (juce::dsp::ProcessSpec spec);
    void setSource (std::shared_ptr<const juce::AudioBuffer<float>> src, double sampleRate);

    // Called on the audio thread once per block before triggerVoice().
    void setEnvelope (float attackMs, float decayMs, float sustain0to1, float releaseMs);
    void setReverse (bool shouldReverse);
    void setPlayMode (bool oneShot);

    /** Returns the index of the voice that was started (for note-off routing),
        or -1 if the trigger was dropped. */
    int  triggerVoice (int startSample, int lengthSamples, float velocity);
    void releaseVoice (int voiceIndex);
    void releaseAll();
    void stopAll();    // All Sound Off (hard-stops one-shot voices too)
    void renderNextBlock (juce::AudioBuffer<float>& out, int numSamples);

    // Message thread: copy active (>=0) normalised playhead positions into dst.
    // Returns the number written (<= maxCount).
    int copyPlayheads (float* dst, int maxCount) const;

    static constexpr int kMaxVoices = 16;

private:
    std::array<Voice, kMaxVoices> voices;
    std::array<std::atomic<float>, kMaxVoices> playheads;

    std::shared_ptr<const juce::AudioBuffer<float>> source;
    double sourceSampleRate = 44100.0;
    double hostSampleRate   = 44100.0;

    // Replaced sources parked here (message thread) until no voice references
    // them any more — otherwise a voice slot being reused could drop the LAST
    // reference and free a multi-megabyte buffer on the audio thread.
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> retiredSources;

    // Current envelope / mode settings adopted by newly triggered voices.
    float attackMs    = 5.0f;
    float decayMs     = 0.0f;
    float sustainLvl  = 1.0f;
    float releaseMs   = 5.0f;
    bool  reversePlay = false;
    bool  oneShotMode = false;

    juce::SpinLock sourceLock;
};
