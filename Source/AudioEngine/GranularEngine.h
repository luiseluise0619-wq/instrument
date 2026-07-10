// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '그래뉼러(granular) 텍스처' 생성기예요. 소리를 아주 짧은 조각(grain)으로
// 잘게 쪼갠 뒤, 최근 소리 기록(history)에서 여러 알갱이를 겹쳐 뿌려 '안개/구름' 같은
// 몽환적인 질감을 만듭니다. mix가 0이면 완전 통과(원음 그대로)라, 꺼진 채로 체인에
// 남겨둬도 안전합니다. FXChain 계열의 특수 효과로 이해하면 됩니다.

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

/**
    Granular texture generator.

    Captures the incoming signal into a history ring buffer and sprays
    overlapping Hann-windowed grains read from recent history with per-grain
    pan and position jitter. Output is blended with the dry signal by an
    internal mix control; at mix == 0 the engine is a pure pass-through, so it
    is safe to leave in the chain when granular mode is off.
*/
// [클래스] GranularEngine — 입력을 기록하고, 그 기록에서 알갱이들을 겹쳐 재생.
class GranularEngine
{
public:
    // 준비(기록 버퍼 확보)와 초기화.
    void prepare (juce::dsp::ProcessSpec spec);
    void reset();

    // 알갱이 크기(ms)/믹스/밀도 설정. 밀도가 높을수록 알갱이가 촘촘히 겹침.
    void setGrainSize (float milliseconds);
    void setMix (float m)      { mix = juce::jlimit (0.0f, 1.0f, m); }
    void setDensity (float d)  { density = juce::jlimit (0.05f, 1.0f, d); }

    // [역할] process — ★오디오 콜백★. 입력을 기록하고 알갱이를 뿌려 원음과 섞음.
    void process (juce::AudioBuffer<float>& buffer);

private:
    // [내부 구조체] Grain — 알갱이 하나의 상태.
    struct Grain
    {
        // 기록 타임라인에서의 절대 읽기 위치.
        long long readPos = 0;   // absolute position in the history timeline
        // 알갱이 길이와 현재 나이(진행도).
        int   length = 0;
        int   age    = 0;
        // 좌/우 팬 게인(기본은 중앙, 0.7071=1/√2).
        float panL   = 0.7071f;
        float panR   = 0.7071f;
        // 현재 재생 중인지.
        bool  active = false;
    };

    // 새 알갱이 생성과, 기록 버퍼에서 특정 절대위치 샘플 읽기.
    void spawnGrain();
    float historyRead (int channel, long long absolutePos) const;

    juce::dsp::ProcessSpec spec {};
    // 최대 채널/알갱이 수 상수.
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxGrains   = 64;

    // 최근 소리를 담는 기록 링버퍼(채널별)와 절대 쓰기 카운터/길이.
    std::vector<float> history[kMaxChannels];
    long long writeAbs = 0;
    int historyLen = 0;

    // 알갱이 풀(고정 배열)과 설정값들.
    std::array<Grain, kMaxGrains> grains;
    int   grainLenSamples = 2048;
    float density = 0.5f;
    float mix     = 0.0f;
    // 다음 알갱이를 뿌릴 때까지 남은 카운트.
    int   hopCounter = 0;
    // 위치/팬 지터(무작위)용 난수기.
    juce::Random rng;
};
