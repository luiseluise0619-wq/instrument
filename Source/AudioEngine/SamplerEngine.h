// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '멀티샘플 플레이어'예요. 신스처럼 소리를 계산해 만드는 게 아니라,
// 실제 녹음(피아노·기타 등)을 건반 구간·세기별로 잔뜩 녹음해 둔 .sfz 악기를 읽어
// 재생합니다. 진짜 악기 같은 리얼함(Keyscape류)은 합성이 아니라 이렇게 '진짜 녹음'에서
// 나옵니다. 파일 파싱·디코딩은 느긋한 메시지 스레드에서, 재생은 오디오 스레드에서.

#pragma once

// juce_audio_formats: wav/flac/ogg 등 오디오 파일을 읽는 기능.
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
    Multisample player (SFZ subset) — the "Sampled" engine mode.

    This is how Keyscape-class realism actually happens: real recordings,
    one per key range and velocity layer, not synthesis. Point it at a .sfz
    file (Salamander Grand Piano, free guitars...) and every region's WAV /
    FLAC / OGG is decoded into RAM up front; playback is a lightweight
    linear-interpolating resampler per voice.

    Supported opcodes (covers Salamander + most free instrument banks):
      sample, default_path, lokey, hikey, key, pitch_keycenter,
      lovel, hivel, loop_mode (loop_continuous), loop_start, loop_end,
      ampeg_release, tune, volume

    Thread model: loadSfz() builds a complete Bank on the message thread and
    swaps it in with an atomic shared_ptr; voices hold a shared_ptr to the
    bank they started on, so a re-load never frees buffers under a playing
    voice.
*/
// [클래스] SamplerEngine — .sfz 악기를 로드해 건반별로 알맞은 녹음을 재생.
class SamplerEngine
{
public:
    // [함수] prepare — 샘플레이트 저장 + 보이스 초기화(헤더 인라인). maxBlock은 안 씀(주석 처리된 인자명).
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        // 모든 보이스를 기본값으로 리셋({} = 빈 초기화).
        for (auto& v : voices)
            v = {};
    }

    /** Message thread. Returns false and fills `error` on failure. */
    // [역할] loadSfz — 메시지 스레드에서 .sfz를 파싱·디코딩해 새 Bank를 만들고 원자적으로 교체.
    bool loadSfz (const juce::File& sfzFile, juce::String& error);

    // [메모리·스레드] atomic_load로 현재 bank 포인터를 안전하게 읽어 존재 여부/이름 확인.
    bool hasBank() const { return std::atomic_load (&bank) != nullptr; }
    juce::String getBankName() const
    {
        if (auto b = std::atomic_load (&bank)) return b->name;
        return {};
    }

    // Audio thread ----------------------------------------------------------
    // ★오디오 스레드★: 건반 누름/뗌/전체 해제/렌더.
    void noteOn (int midiNote, float velocity);
    void noteOff (int midiNote);
    void releaseAll();
    void render (juce::AudioBuffer<float>& out, int numSamples);

private:
    // [내부 구조체] Region — .sfz의 <region> 하나(특정 건반·세기 구간의 녹음과 재생 규칙).
    struct Region
    {
        // 디코딩된 오디오 데이터(RAM에 통째로).
        juce::AudioBuffer<float> data;
        double srcRate  = 44100.0;
        // 이 리전이 담당하는 건반 범위(lokey~hikey)와 기준음(root).
        int    lokey = 0, hikey = 127, root = 60;
        // 담당 세기 범위.
        int    lovel = 0, hivel = 127;
        // 미세 튜닝(cents)과 음량(dB).
        float  tuneCents = 0.0f;
        float  volumeDb  = 0.0f;
        // 루프 여부와 루프 구간.
        bool   loop = false;
        int    loopStart = 0, loopEnd = 0;
        // 릴리즈 시간(초).
        float  releaseSeconds = 0.35f;
    };

    // [내부 구조체] Bank — 악기 하나 = 이름 + 리전 목록.
    struct Bank
    {
        juce::String name;
        std::vector<Region> regions;
    };

    // [내부 구조체] Voice — 재생 중인 음 하나.
    struct Voice
    {
        // [무덤 패턴] 이 보이스가 시작한 bank를 shared_ptr로 붙듦 → 재로드해도 재생 중 버퍼가 안 사라짐.
        std::shared_ptr<const Bank> hold;   // keeps the bank alive
        // 재생 중인 리전과 현재 읽기 위치/리샘플 비율.
        const Region* region = nullptr;
        double pos = 0.0, ratio = 1.0;
        // 음량과 담당 노트.
        float  gain = 0.0f;
        int    note = -1;
        // 릴리즈 진행 여부와 엔벨로프/릴리즈 감쇠계수.
        bool   releasing = false;
        float  env = 1.0f, relCoeff = 0.0f;
        // 시작 클릭 방지용 짧은 페이드인 카운터.
        int    fadeIn = 0;

        // region이 있으면 활성.
        bool active() const { return region != nullptr; }
    };

    // 동시 최대 발음 수.
    static constexpr int kMaxVoices = 24;

    // 노트·세기에 맞는 리전 찾기, 빈 보이스 찾기.
    const Region* findRegion (const Bank&, int note, int vel127) const;
    Voice* findFreeVoice();

    // [스레드] 현재 뱅크(원자적으로 교체됨)와 보이스 풀, 샘플레이트.
    std::shared_ptr<const Bank> bank;   // swapped atomically
    std::array<Voice, kMaxVoices> voices;
    double sr = 44100.0;
};
