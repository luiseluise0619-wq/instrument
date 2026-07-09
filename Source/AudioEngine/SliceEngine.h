/*  [초보자 안내]
    샘플(음성 파일)을 '조각(슬라이스)'으로 자르는 칼이에요. 세 가지 방법:
    ① Transient — 소리가 '탁' 하고 시작되는 지점을 자동 감지해서 자르기
    ② Grid — 그냥 균등하게 N등분  ③ Manual — 사용자가 직접 찍은 위치.
    자르는 계산은 느긋한 메시지 스레드에서 하고, 결과 목록만 발행합니다.
    오디오 스레드는 tryGetSlice()(try-lock)로 읽어요 — 마침 재계산 중이면
    기다리지 않고 그 한 번의 트리거를 포기합니다. '늦는 것보다 건너뛰는 게
    낫다'가 실시간 오디오의 철칙이거든요.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <optional>
#include <vector>

struct SlicePoint
{
    int startSample  = 0;
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
class SliceEngine
{
public:
    enum Mode { Transient, Grid, Manual };

    void setSample (std::shared_ptr<juce::AudioBuffer<float>> buffer, double sr);

    void setMode (Mode m)              { mode = m; }
    Mode getMode() const               { return mode; }
    void  setSensitivity (float s)     { sensitivity = juce::jlimit (0.01f, 0.99f, s); }
    float getSensitivity() const       { return sensitivity; }
    void  setGridDivision (int div)    { gridDiv = juce::jlimit (2, 64, div); }
    int   getGridDivision() const      { return gridDiv; }

    void rebuildSlices();
    void sliceByManual (const std::vector<int>& points);

    // Message-thread reads (UI / waveform).
    int getNumSlices() const                       { return (int) slices.size(); }
    std::optional<SlicePoint> getSlice (int i) const;
    const std::vector<SlicePoint>& getSlices() const { return slices; }
    const std::vector<int>& getTransientPoints() const { return transients; }

    // Audio-thread read: returns false (and leaves out untouched) if the slice
    // list is being rebuilt or the index is out of range.
    bool tryGetSlice (int index, SlicePoint& out) const;

private:
    std::vector<SlicePoint> buildTransient();
    std::vector<SlicePoint> buildGrid() const;
    void publish (std::vector<SlicePoint> newSlices);

    std::shared_ptr<juce::AudioBuffer<float>> sample;
    std::vector<SlicePoint> slices;
    std::vector<int>        transients;

    Mode   mode        = Transient;
    float  sensitivity = 0.3f;
    int    gridDiv     = 16;
    double sampleRate  = 44100.0;

    mutable juce::SpinLock slicesLock;
};
