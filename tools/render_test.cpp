// Offline render harness: load each named instrument, play a note, measure.
#include "PluginProcessor.h"
#include <cstdio>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    VocalChopAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    const auto names = VocalChopAudioProcessor::getInstrumentNames();
    juce::StringArray want;
    for (int i = 1; i < argc; ++i) want.add (juce::String (argv[i]));

    printf ("%-18s %8s %8s %8s\n", "instrument", "peak", "rms", "centroidHz");
    for (const auto& n : want)
    {
        const int idx = names.indexOf (n);
        if (idx < 0) { printf ("%-18s MISSING\n", n.toRawUTF8()); continue; }
        proc.applyInstrument (idx);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

        double sumSq = 0, sumAbs = 0; float peak = 0;
        std::vector<float> mono;
        for (int b = 0; b < 86; ++b)   // ~1 s
        {
            buf.clear();
            proc.processBlock (buf, midi);
            midi.clear();
            if (b == 40) { juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 60), 0); midi = off; }
            for (int i = 0; i < 512; ++i)
            {
                const float v = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
                mono.push_back (v);
                peak = juce::jmax (peak, std::abs (v));
                sumSq += v * v; sumAbs += std::abs (v);
            }
        }
        // Crude spectral centroid via zero-crossing rate -> rough brightness.
        int zc = 0;
        for (size_t i = 1; i < mono.size(); ++i)
            if ((mono[i-1] <= 0) != (mono[i] <= 0)) ++zc;
        const double zcHz = zc * 0.5 * 44100.0 / (double) mono.size();
        printf ("%-18s %8.4f %8.4f %8.0f\n", n.toRawUTF8(), peak,
                std::sqrt (sumSq / (double) mono.size()), zcHz);
    }
    return 0;
}
