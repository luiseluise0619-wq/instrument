/*  [초보자 안내]
    샘플(음성 파일)을 '조각(슬라이스)'으로 자르는 칼이에요. 세 가지 방법:
    ① Transient — 소리가 '탁' 하고 시작되는 지점을 자동 감지해서 자르기
    ② Grid — 그냥 균등하게 N등분  ③ Manual — 사용자가 직접 찍은 위치.
    자르는 계산은 느긋한 메시지 스레드에서 하고, 결과 목록만 발행합니다.
    오디오 스레드는 tryGetSlice()(try-lock)로 읽어요 — 마침 재계산 중이면
    기다리지 않고 그 한 번의 트리거를 포기합니다. '늦는 것보다 건너뛰는 게
    낫다'가 실시간 오디오의 철칙이거든요.
*/

// [신호 체인에서 담당] 샘플을 조각내는 '재단사'. VoicePool보다 앞 단계입니다.
//   SliceEngine이 "어디부터 어디까지가 한 조각"인지 목록을 만들어 두면,
//   건반을 눌렀을 때 그 조각 정보를 VoicePool::triggerVoice로 넘겨 재생합니다.

#pragma once

// juce_audio_basics: AudioBuffer 등 기본 오디오 자료형.
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
// std::optional: '값이 있을 수도, 없을 수도' 있는 타입(getSlice가 실패 시 빈 값 반환).
#include <optional>
#include <vector>

// [문법] struct = class와 거의 같지만 기본이 public. 여기선 '슬라이스 한 조각'의 좌표를 담는 단순 데이터.
struct SlicePoint
{
    // 이 조각이 원본에서 시작하는 위치(샘플 단위).
    int startSample  = 0;
    // 이 조각의 길이(샘플 단위).
    int lengthSamples = 0;
};

/**
    Splits a loaded sample into playable slices using one of three strategies:
      - Transient : HFC onset detection (with a minimum-gap filter).
      - Grid      : equal divisions across the whole sample.
      - Manual    : user-supplied slice points.

    Slicing (rebuildSlices / sliceByManual) runs on the message thread. The
    slice list is published under a spin lock; the audio thread reads it with
    tryGetSlice() (a try-lock that safely drops a trigger on the rare block that
    coincides with a re-slice, rather than racing the vector).
*/
// [클래스] SliceEngine — 샘플을 조각으로 자르고, 그 목록을 스레드 안전하게 공유.
class SliceEngine
{
public:
    // 세 가지 자르기 방식. Transient(트랜지언트 감지)/Grid(등분)/Manual(수동).
    enum Mode { Transient, Grid, Manual };

    // [역할] setSample — 자를 원본 샘플을 지정(메시지 스레드).
    void setSample (std::shared_ptr<juce::AudioBuffer<float>> buffer, double sr);

    // [문법] 아래처럼 헤더에 몸통까지 있는 함수는 '인라인 함수'(짧아서 헤더에 그대로 정의).
    // 자르기 방식 설정/조회.
    void setMode (Mode m)              { mode = m; }
    Mode getMode() const               { return mode; }
    // 감도(트랜지언트 감지 민감도) 설정/조회. jlimit로 0.01~0.99 범위 안전 클램프.
    void  setSensitivity (float s)     { sensitivity = juce::jlimit (0.01f, 0.99f, s); }
    float getSensitivity() const       { return sensitivity; }
    // 그리드 등분 수 설정/조회(2~64 클램프).
    void  setGridDivision (int div)    { gridDiv = juce::jlimit (2, 64, div); }
    int   getGridDivision() const      { return gridDiv; }

    // [역할] rebuildSlices — 현재 모드/설정에 맞춰 슬라이스 목록을 다시 계산(메시지 스레드).
    void rebuildSlices();
    // [역할] sliceByManual — 사용자가 찍은 위치 목록으로 슬라이스를 만듦.
    void sliceByManual (const std::vector<int>& points);

    // Message-thread reads (UI / waveform).
    // 화면/파형 표시용 읽기 함수들(메시지 스레드).
    int getNumSlices() const                       { return (int) slices.size(); }
    // 인덱스로 한 조각 얻기. 범위를 벗어나면 optional이 '빈 값'.
    std::optional<SlicePoint> getSlice (int i) const;
    const std::vector<SlicePoint>& getSlices() const { return slices; }
    const std::vector<int>& getTransientPoints() const { return transients; }

    // Audio-thread read: returns false (and leaves out untouched) if the slice
    // list is being rebuilt or the index is out of range.
    // [역할] tryGetSlice — ★오디오 스레드★가 조각 정보를 안전히 읽는 함수. try-lock 실패/범위밖이면 false.
    bool tryGetSlice (int index, SlicePoint& out) const;

private:
    // 내부 계산 도우미: 트랜지언트/그리드 방식으로 조각 목록을 만들어 반환.
    std::vector<SlicePoint> buildTransient();
    std::vector<SlicePoint> buildGrid() const;
    // [역할] publish — 새로 만든 목록을 SpinLock으로 보호하며 교체(오디오 쪽 tryGetSlice와 안전 조율).
    void publish (std::vector<SlicePoint> newSlices);

    // 자를 원본 샘플(공유 소유).
    std::shared_ptr<juce::AudioBuffer<float>> sample;
    // 현재 발행된 조각 목록.
    std::vector<SlicePoint> slices;
    // 감지된 트랜지언트 위치들(파형 위 표시에 사용).
    std::vector<int>        transients;

    // 현재 설정값들(기본값 포함).
    Mode   mode        = Transient;
    float  sensitivity = 0.3f;
    int    gridDiv     = 16;
    double sampleRate  = 44100.0;

    // [문법] mutable = const 함수 안에서도 이 락은 잠글 수 있게 허용(락 자체는 논리적 상태가 아님).
    mutable juce::SpinLock slicesLock;
};
