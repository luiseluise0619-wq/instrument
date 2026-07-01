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
*/
class SliceEngine
{
public:
    enum Mode { Transient, Grid, Manual };

    void setSample (std::shared_ptr<juce::AudioBuffer<float>> buffer, double sr);

    void setMode (Mode m)              { mode = m; }
    Mode getMode() const               { return mode; }
    void setSensitivity (float s)      { sensitivity = juce::jlimit (0.01f, 0.99f, s); }
    void setGridDivision (int div)     { gridDiv = juce::jlimit (2, 64, div); }
    int  getGridDivision() const       { return gridDiv; }

    void rebuildSlices();
    void sliceByManual (const std::vector<int>& points);

    int getNumSlices() const                       { return (int) slices.size(); }
    std::optional<SlicePoint> getSlice (int i) const;
    const std::vector<SlicePoint>& getSlices() const { return slices; }
    const std::vector<int>& getTransientPoints() const { return transients; }

private:
    void sliceByTransient();
    void sliceByGrid();
    void detectTransients();

    std::shared_ptr<juce::AudioBuffer<float>> sample;
    std::vector<SlicePoint> slices;
    std::vector<int>        transients;

    Mode   mode        = Transient;
    float  sensitivity = 0.3f;
    int    gridDiv     = 16;
    double sampleRate  = 44100.0;
};
