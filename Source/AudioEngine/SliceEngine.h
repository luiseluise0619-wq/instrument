#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
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

    // Safe from ANY thread: the count is mirrored into an atomic at publish
    // time (the audio thread wraps key indices with it every note-on).
    int getNumSlices() const                       { return numSlicesAtomic.load (std::memory_order_relaxed); }
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
    std::atomic<int>        numSlicesAtomic { 0 };

    Mode   mode        = Transient;
    float  sensitivity = 0.3f;
    int    gridDiv     = 16;
    double sampleRate  = 44100.0;

    mutable juce::SpinLock slicesLock;
};
