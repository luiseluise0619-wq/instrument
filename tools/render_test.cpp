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

    // "--state": save the session, scramble every parameter, restore, and
    // diff. A parameter that silently fails to round-trip means a reopened
    // project comes back wrong, which is the worst class of bug to ship.
    if (argc > 1 && juce::String (argv[1]) == "--state")
    {
        auto& apvts = proc.getAPVTS();
        auto& params = proc.getParameters();

        proc.applyPreset (8);                       // a sound preset, not Init
        juce::Random rng (12345);
        for (auto* p : params)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rng.nextFloat());

        // Compare the LOGICAL value, not the raw normalised float: JUCE's bool
        // parameters keep whatever float you hand them but report true/false
        // from it, so 0.694 legitimately comes back as 1.0 meaning the same
        // thing. Text is the value the user and the host actually see.
        juce::StringArray before;
        for (auto* p : params) before.add (p->getCurrentValueAsText());
        const int instBefore = proc.getCurrentInstrument();

        juce::MemoryBlock blob;
        proc.getStateInformation (blob);

        for (auto* p : params)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rng.nextFloat());
        proc.applyInstrument (0);

        proc.setStateInformation (blob.getData(), (int) blob.getSize());

        int bad = 0;
        for (int i = 0; i < params.size(); ++i)
        {
            const auto now = params[i]->getCurrentValueAsText();
            if (now != before[i])
            {
                printf ("  MISMATCH %-16s '%s' -> '%s'\n",
                        params[i]->getName (16).toRawUTF8(),
                        before[i].toRawUTF8(), now.toRawUTF8());
                ++bad;
            }
        }
        const int instAfter = proc.getCurrentInstrument();
        if (instAfter != instBefore)
        { printf ("  MISMATCH instrument %d -> %d\n", instBefore, instAfter); ++bad; }

        printf ("state round-trip: %d parameters, %d mismatches -> %s\n",
                params.size(), bad, bad == 0 ? "PASS" : "FAIL");
        juce::ignoreUnused (apvts);
        return bad == 0 ? 0 : 1;
    }

    // "--bench": how much of a CPU core one instance eats. Renders a fixed
    // amount of audio as fast as it can and reports the ratio.
    if (argc > 1 && juce::String (argv[1]) == "--bench")
    {
        struct Case { const char* name; const char* inst; int voices; bool fx; };
        const Case cases[] = {
            { "idle (no notes)",       "Supersaw Lead", 0,  false },
            { "1 note",                "Supersaw Lead", 1,  false },
            { "8 notes",               "Supersaw Lead", 8,  false },
            { "16 notes (max poly)",   "Supersaw Lead", 16, false },
            { "16 notes + all FX",     "Supersaw Lead", 16, true  },
            { "16 notes + arp + pump", "Crystal Pluck", 16, true  },
        };

        const double sr = 48000.0;
        const int    block = 256;
        const int    blocks = (int) (sr * 20.0 / block);   // 20 seconds of audio

        printf ("%-24s %8s %10s %9s\n", "case", "x realtime", "1 core %", "sec/20s");
        for (const auto& c : cases)
        {
            VocalChopAudioProcessor p2;
            p2.prepareToPlay (sr, block);
            p2.applyInstrument (juce::jmax (0, VocalChopAudioProcessor::getInstrumentNames().indexOf (c.inst)));

            auto set = [&] (const char* id, float v) {
                if (auto* pp = p2.getAPVTS().getParameter (id))
                    pp->setValueNotifyingHost (pp->convertTo0to1 (v));
            };
            if (c.fx) { set ("drive", 0.6f); set ("reverb", 0.6f); set ("delay", 0.5f);
                        set ("grainMix", 0.5f); set ("pitch", 5.0f); set ("formant", 3.0f);
                        set ("filterType", 1.0f); set ("filterCutoff", 3000.0f); }
            if (juce::String (c.name).contains ("arp"))
                { set ("arpMode", 1.0f); set ("arpRate", 3.0f); set ("pumpAmt", 0.8f); }

            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            for (int i = 0; i < c.voices; ++i)
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + i * 3, 0.9f), 0);

            // let it settle before timing
            for (int b = 0; b < 40; ++b) { buf.clear(); juce::MidiBuffer m = b ? juce::MidiBuffer() : midi; p2.processBlock (buf, m); }

            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int b = 0; b < blocks; ++b) { buf.clear(); juce::MidiBuffer m; p2.processBlock (buf, m); }
            const double secs = juce::Time::highResolutionTicksToSeconds (
                                    juce::Time::getHighResolutionTicks() - t0);
            const double rt = 20.0 / secs;
            printf ("%-24s %8.1fx %9.2f%% %9.3f\n", c.name, rt, 100.0 / rt, secs);
        }
        printf ("\n(48 kHz, 256-sample blocks, single instance, Release build)\n");
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
