// Offline render harness: load each named instrument, play a note, measure.
#include "PluginProcessor.h"
#include <cstdio>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    VocalChopAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    const auto names = VocalChopAudioProcessor::getInstrumentNames();

    // "--glide": play C4 then C5 with portamento on and print the pitch
    // trajectory, so the slide can be verified as a NUMBER rather than by ear.
    if (argc > 1 && juce::String (argv[1]) == "--glide")
    {
        proc.applyInstrument (juce::jmax (0, names.indexOf ("Init Synth")));
        if (auto* g = proc.getAPVTS().getParameter ("synthGlide"))
            g->setValueNotifyingHost (g->convertTo0to1 (200.0f));
        // Sine, so the zero-crossing count IS the pitch (a saw's harmonics
        // add crossings and make the measurement meaningless).
        if (auto* w = proc.getAPVTS().getParameter ("synthWave"))
            w->setValueNotifyingHost (w->convertTo0to1 (2.0f));

        juce::AudioBuffer<float> buf (2, 512);
        std::vector<float> mono;
        for (int b = 0; b < 90; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)  midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            if (b == 40) { midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                           midi.addEvent (juce::MidiMessage::noteOn (1, 72, 0.9f), 1); }
            buf.clear();
            proc.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i)
                mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
        }
        const int win = 1024;
        printf ("ms      Hz   (C4=262, C5=523; glide 200 ms starts at ~465 ms)\n");
        for (size_t start = 40 * 512; start + win < mono.size(); start += win)
        {
            int zc = 0;
            for (size_t i = start + 1; i < start + win; ++i)
                if ((mono[i-1] <= 0) != (mono[i] <= 0)) ++zc;
            printf ("%5.0f %7.1f\n", start / 44.1, zc * 0.5 * 44100.0 / win);
        }
        return 0;
    }

    // "--presets": apply every factory preset and measure what comes out, so
    // a preset that silently fails to change the sound cannot ship again.
    if (argc > 1 && juce::String (argv[1]) == "--presets")
    {
        const auto pn = VocalChopAudioProcessor::getPresetNames();
        printf ("%-20s %8s %8s %8s\n", "preset", "peak", "rms", "zcHz");
        for (int p = 0; p < pn.size(); ++p)
        {
            proc.applyPreset (p);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            double sumSq = 0; float peak = 0; int zc = 0; float prev = 0;
            long n = 0;
            for (int b = 0; b < 130; ++b)
            {
                buf.clear();
                proc.processBlock (buf, midi);
                midi.clear();
                if (b == 60) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
                    peak = juce::jmax (peak, std::abs (v));
                    sumSq += v * v;
                    if ((prev <= 0) != (v <= 0)) ++zc;
                    prev = v; ++n;
                }
            }
            printf ("%-20s %8.4f %8.4f %8.0f\n", pn[p].toRawUTF8(), peak,
                    std::sqrt (sumSq / (double) n), zc * 0.5 * 44100.0 / (double) n);
        }
        return 0;
    }

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
