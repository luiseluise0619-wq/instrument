// Offline render harness: load each named instrument, play a note, measure.
#include <algorithm>
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

    // "--attack <name>": is there a transient at all? Prints the first 40 ms in
    // 2 ms slices with the brightness of each, so the strike can be seen
    // rather than argued about.
    if (argc > 2 && juce::String (argv[1]) == "--attack")
    {
        const auto nm = juce::String (argv[2]);
        const int idx = names.indexOf (nm);
        if (idx < 0) { printf ("no instrument '%s'\n", nm.toRawUTF8()); return 1; }
        proc.applyInstrument (idx);

        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        std::vector<float> mono;
        for (int b = 0; b < 400; ++b)
        {
            buf.clear(); proc.processBlock (buf, midi); midi.clear();
            for (int i = 0; i < 64; ++i)
                mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
        }
        const int hop = (int) (44100 * 0.002);
        printf ("%s — first 40 ms, 2 ms slices\n", nm.toRawUTF8());
        printf ("%6s %8s %8s\n", "ms", "peak", "zcHz");
        for (int k = 0; k < 20; ++k)
        {
            float pk = 0; int zc = 0;
            for (int i = k * hop; i < (k + 1) * hop && i < (int) mono.size(); ++i)
            {
                pk = juce::jmax (pk, std::abs (mono[(size_t) i]));
                if (i > 0 && (mono[(size_t) i - 1] <= 0) != (mono[(size_t) i] <= 0)) ++zc;
            }
            printf ("%6.0f %8.4f %8.0f %s\n", k * 2.0, pk, zc * 0.5 * 44100.0 / hop,
                    juce::String::repeatedString ("#", (int) (pk * 90)).toRawUTF8());
        }
        float sus = 0;
        for (size_t i = mono.size() / 2; i < mono.size() / 2 + 2000 && i < mono.size(); ++i)
            sus = juce::jmax (sus, std::abs (mono[i]));
        float atk = 0;
        for (int i = 0; i < hop * 4 && i < (int) mono.size(); ++i)
            atk = juce::jmax (atk, std::abs (mono[(size_t) i]));
        printf ("\nattack peak %.4f  vs sustain %.4f  ->  %+.1f dB strike\n",
                atk, sus, 20.0 * std::log10 ((atk + 1e-6f) / (sus + 1e-6f)));
        return 0;
    }

    // "--decay <name>": how the tone changes WHILE it dies. A real plucked or
    // struck instrument loses its highs faster than its level, because each
    // reflection down the string is filtered. An amplitude envelope on a fixed
    // waveform cannot do that - it fades without changing colour. So the ratio
    // of brightness-loss to level-loss is a structural test, not a taste one.
    if (argc > 2 && juce::String (argv[1]) == "--decay")
    {
        const auto nm = juce::String (argv[2]);
        const int idx = names.indexOf (nm);
        if (idx < 0) { printf ("no instrument '%s'\n", nm.toRawUTF8()); return 1; }
        proc.applyInstrument (idx);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 52, 1.0f), 0);   // low E
        std::vector<float> mono;
        for (int b = 0; b < 200; ++b)
        {
            buf.clear(); proc.processBlock (buf, midi); midi.clear();
            for (int i = 0; i < 512; ++i)
                mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
        }

        const int win = (int) (44100 * 0.05);   // 50 ms
        printf ("%s\n%6s %9s %9s\n", nm.toRawUTF8(), "ms", "level dB", "bright Hz");
        double firstZc = 0, firstRms = 0;
        for (int k = 0; k * win + win < (int) mono.size() && k < 20; ++k)
        {
            double sq = 0; int zc = 0;
            for (int i = k * win; i < (k + 1) * win; ++i)
            {
                sq += mono[(size_t) i] * mono[(size_t) i];
                if (i > 0 && (mono[(size_t) i - 1] <= 0) != (mono[(size_t) i] <= 0)) ++zc;
            }
            const double rms = std::sqrt (sq / win);
            const double zcHz = zc * 0.5 * 44100.0 / win;
            if (k == 2) { firstZc = zcHz; firstRms = rms; }
            printf ("%6d %9.1f %9.0f\n", k * 50, 20.0 * std::log10 (rms + 1e-9), zcHz);
        }
        // Compare the tail against the body of the note.
        double lastZc = 0, lastRms = 0; int lastK = 0;
        for (int k = 3; k * win + win < (int) mono.size() && k < 20; ++k)
        {
            double sq = 0; int zc = 0;
            for (int i = k * win; i < (k + 1) * win; ++i)
            {
                sq += mono[(size_t) i] * mono[(size_t) i];
                if (i > 0 && (mono[(size_t) i - 1] <= 0) != (mono[(size_t) i] <= 0)) ++zc;
            }
            const double rms = std::sqrt (sq / win);
            if (rms > 1e-5) { lastRms = rms; lastZc = zc * 0.5 * 44100.0 / win; lastK = k; }
        }
        if (firstRms > 0 && lastRms > 0)
        {
            const double dLevel = 20.0 * std::log10 (lastRms / firstRms);
            const double dBright = lastZc / juce::jmax (1.0, firstZc);
            printf ("\nover %d ms: level %+.1f dB, brightness x%.2f\n",
                    (lastK - 2) * 50, dLevel, dBright);
            printf ("%s\n", dBright < 0.75
                    ? "-> highs die faster than level: filtered reflections, i.e. a string"
                    : "-> colour holds while level falls: an envelope on a fixed waveform");
        }
        return 0;
    }

    // "--filter": does the filter actually filter?
    //
    // Sweeps the cutoff across the range for each type and reports the
    // brightness of what comes out, as a zero-crossing rate. A working low
    // pass makes that number fall as the cutoff closes; a working high pass
    // makes it rise. If the column is flat, the control is decorative.
    if (argc > 1 && juce::String (argv[1]) == "--filter")
    {
        const char* typeName[4] = { "Off", "Low pass", "High pass", "Band pass" };
        proc.applyInstrument (juce::jmax (0, names.indexOf ("Supersaw Lead")));

        auto set = [&] (const char* id, float v)
        {
            if (auto* pp = proc.getAPVTS().getParameter (id))
                pp->setValueNotifyingHost (pp->convertTo0to1 (v));
        };
        // A filter envelope would move the cutoff underneath the measurement.
        set ("filterReso", 0.7f);

        printf ("%-10s", "cutoff Hz");
        for (int t = 0; t < 4; ++t) printf (" %11s", typeName[t]);
        printf ("   (zero-crossing rate, Hz)\n");

        const float cuts[] = { 200.0f, 500.0f, 1200.0f, 3000.0f, 8000.0f, 18000.0f };
        // Two passes: the synth engine, then CHOP - the filter living in the
        // synth voice path and never touching sliced sample playback is
        // exactly the kind of gap a synth-only test would never notice.
        for (int pass = 0; pass < 2; ++pass)
        {
          const bool chop = (pass == 1);
          if (chop)
          {
              if (! proc.loadDemoSample()) { printf ("\n(no demo sample; skipping Chop)\n"); break; }
              set ("engine", 0.0f);
              printf ("\n-- CHOP engine (sliced sample playback) --\n%-10s", "cutoff Hz");
              for (int t = 0; t < 4; ++t) printf (" %11s", typeName[t]);
              printf ("\n");
          }
          else
              set ("engine", 1.0f);

          for (float cut : cuts)
          {
            printf ("%-10.0f", cut);
            for (int t = 0; t < 4; ++t)
            {
                set ("filterType", (float) t);
                set ("filterCutoff", cut);

                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                int zc = 0; long n = 0; float prev = 0.0f; double sumSq = 0.0;
                for (int b = 0; b < 60; ++b)
                {
                    buf.clear(); proc.processBlock (buf, midi); midi.clear();
                    if (b < 10) continue;             // let the envelope settle
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
                        if ((prev <= 0) != (v <= 0)) ++zc;
                        prev = v; ++n; sumSq += (double) v * v;
                    }
                }
                printf (" %11.0f", n > 0 ? zc * 0.5 * 44100.0 / (double) n : 0.0);
            }
            printf ("\n");
          }
        }
        return 0;
    }

    // "--harsh": find the voices that FATIGUE the ear.
    //
    // Not the loud ones and not the bright ones - the ones with too much
    // energy in the 2-6 kHz band, where human hearing peaks and where a
    // synthesised bell or pluck turns into an ice pick. Measured as the RMS of
    // that band against the RMS of everything below it, so it is a question of
    // BALANCE rather than of level: a sound can be quiet and still shrill.
    //
    // The band is isolated with two one-pole filters rather than an FFT. The
    // slopes are gentle, which for a broad question like "is this thing
    // top-heavy" is the right amount of precision.
    if (argc > 1 && juce::String (argv[1]) == "--harsh")
    {
        struct Row { juce::String name, cat; double ratio, decayS; };
        std::vector<Row> rows;
        const auto cats = VocalChopAudioProcessor::getInstrumentCategories();

        for (int idx = 0; idx < names.size(); ++idx)
        {
            proc.applyInstrument (idx);
            juce::AudioBuffer<float> buf (2, 64);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 72, 1.0f), 0);   // C5
            std::vector<float> mono;
            for (int b = 0; b < 900; ++b)
            {
                buf.clear(); proc.processBlock (buf, midi); midi.clear();
                if (b == 300) midi.addEvent (juce::MidiMessage::noteOff (1, 72), 0);
                for (int i = 0; i < 64; ++i)
                    mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
            }

            // one-pole coefficients for 2 kHz and 6 kHz at 44.1 kHz
            auto coeff = [] (double hz)
            { return (float) std::exp (-2.0 * juce::MathConstants<double>::pi * hz / 44100.0); };
            const float a2 = coeff (2000.0), a6 = coeff (6000.0);
            float lp2 = 0.0f, lp6 = 0.0f;
            double eBand = 0.0, eLow = 0.0;
            for (float v : mono)
            {
                lp2 += (1.0f - a2) * (v - lp2);      // everything below 2 k
                lp6 += (1.0f - a6) * (v - lp6);      // everything below 6 k
                const float band = lp6 - lp2;        // the 2-6 kHz slice
                eBand += (double) band * band;
                eLow  += (double) lp2 * lp2;
            }
            const double ratio = 10.0 * std::log10 ((eBand + 1e-12) / (eLow + 1e-12));

            // how long it rings after release, in seconds
            float pk = 0.0f;
            for (float v : mono) pk = juce::jmax (pk, std::abs (v));
            size_t last = 0;
            for (size_t i = 0; i < mono.size(); ++i)
                if (std::abs (mono[i]) > pk * 0.02f) last = i;
            rows.push_back ({ names[idx],
                              juce::isPositiveAndBelow (idx, cats.size()) ? cats[idx] : juce::String(),
                              ratio, (double) last / 44100.0 });
        }

        std::sort (rows.begin(), rows.end(),
                   [] (const Row& a, const Row& b) { return a.ratio > b.ratio; });

        printf ("%-22s %-8s %9s %8s  %s\n", "instrument", "cat", "2-6k dB", "ring s", "");
        int bad = 0;
        for (const auto& r : rows)
        {
            const bool harsh = r.ratio > -6.0;
            if (harsh) ++bad;
            if (harsh || (argc > 2 && juce::String (argv[2]) == "-v"))
                printf ("%-22s %-8s %9.1f %8.2f  %s\n", r.name.toRawUTF8(),
                        r.cat.toRawUTF8(), r.ratio, r.decayS, harsh ? "HARSH" : "");
        }
        printf ("\n%d instruments - %d above the -6 dB fatigue line\n",
                names.size(), bad);
        return 0;
    }

    // "--noise": find voices whose STRIKE is broadband hiss rather than a
    // pitched knock. A real hammer, pick or mallet rings the body at a definite
    // frequency, so the attack's zero-crossing rate sits within a few multiples
    // of the note. White noise sits near half the sample rate no matter what
    // note you play, and that difference is audible as "why is there noise on
    // this". Written because a broken biquad in the transient generator shipped
    // a hiss on every struck voice and nobody could name which control caused
    // it - a per-instrument number would have found it the first day.
    if (argc > 1 && juce::String (argv[1]) == "--noise")
    {
        printf ("%-22s %9s %9s %7s  %s\n",
                "instrument", "atkZcHz", "bodyZcHz", "ratio", "flag");
        int bad = 0;
        for (int idx = 0; idx < names.size(); ++idx)
        {
            proc.applyInstrument (idx);

            // Three takes, median reported. A voice's unison bank starts at
            // random phases - that is what stops repeated notes machine-
            // gunning - so a single take's zero-crossing count wanders, and a
            // borderline instrument crossed the threshold about one run in
            // ten. A check that flags something every tenth run teaches you to
            // ignore it, which is worse than not having it.
            double aTakes[3] {}, bTakes[3] {};
            for (int take = 0; take < 3; ++take)
            {
                juce::AudioBuffer<float> buf (2, 64);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                std::vector<float> mono;
                for (int bl = 0; bl < 400; ++bl)
                {
                    buf.clear(); proc.processBlock (buf, midi); midi.clear();
                    for (int i = 0; i < 64; ++i)
                        mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
                }
                auto zcOf = [&mono] (int from, int to)
                {
                    int zc = 0, n = 0;
                    for (int i = juce::jmax (1, from); i < to && i < (int) mono.size(); ++i, ++n)
                        if ((mono[(size_t) i - 1] <= 0) != (mono[(size_t) i] <= 0)) ++zc;
                    return n > 0 ? zc * 0.5 * 44100.0 / n : 0.0;
                };
                // First 15 ms is the strike; 150-400 ms is the body it decays into.
                aTakes[take] = zcOf (0, (int) (44100 * 0.015));
                bTakes[take] = zcOf ((int) (44100 * 0.15), (int) (44100 * 0.40));
            }
            auto median3 = [] (double* v)
            {
                std::sort (v, v + 3);
                return v[1];
            };
            const double a = median3 (aTakes);
            const double b = median3 (bTakes);

            // Hats, shakers and cymbals ARE broadband noise - that is the
            // instrument, not a defect. Flagging them taught me nothing except
            // that the check needed to know the difference.
            const auto& nm = names[idx];
            const bool meantToBeNoise =
                   nm.startsWith ("Hat ") || nm.contains ("Shaker")
                || nm.contains ("Crash") || nm.contains ("Ride")
                || nm.contains ("Tambo") || nm.contains ("Snap")
                || nm.contains ("Clap")  || nm.contains ("Noise")
                || nm.contains ("Riser") || nm.contains ("Sweep")
                || nm.contains ("Wind")  || nm.contains ("Vinyl");

            juce::String flag;
            if (meantToBeNoise)                {}
            else if (a > 9000.0)               flag = "HISS";
            else if (b > 1.0 && a / b > 12.0)  flag = "BRIGHT-BURST";
            if (flag.isNotEmpty()) ++bad;
            if (flag.isNotEmpty() || (argc > 2 && juce::String (argv[2]) == "-v"))
                printf ("%-22s %9.0f %9.0f %7.1f  %s\n",
                        names[idx].toRawUTF8(), a, b,
                        b > 1.0 ? a / b : 0.0, flag.toRawUTF8());
        }
        printf ("\n%d instruments — %d flagged\n", names.size(), bad);
        return bad == 0 ? 0 : 1;
    }

    // "--sweep": play every instrument and flag anything pathological. Adding a
    // transient, a string, a body and a morph across 300+ voices at once is
    // exactly the kind of change that improves ten sounds and quietly ruins
    // five, and no amount of listening to favourites will find the five.
    if (argc > 1 && juce::String (argv[1]) == "--sweep")
    {
        printf ("%-20s %8s %8s %8s %8s  %s\n",
                "instrument", "peak", "rms", "tail", "zcHz", "flags");
        std::vector<double> rmsAll;
        int bad = 0;

        // Three takes, median reported. Voices start their unison bank at random
        // phases on purpose - it is what stops repeated notes machine-gunning -
        // so a single take's PEAK wobbles by up to ~6 dB on identical code.
        // Comparing two single-take runs made noise look like regressions.
        constexpr int kTakes = 3;
        for (int idx = 0; idx < names.size(); ++idx)
        {
            double pk[kTakes] {}, rm[kTakes] {}, tl[kTakes] {}, zh[kTakes] {};
            bool nan = false;

            for (int take = 0; take < kTakes; ++take)
            {
                proc.applyInstrument (idx);
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.95f), 0);

                double sq = 0; float peak = 0; int zc = 0; float prev = 0; long n = 0;
                double tailSq = 0; long tailN = 0;

                for (int b = 0; b < 200; ++b)          // ~2.3 s
                {
                    buf.clear();
                    proc.processBlock (buf, midi);
                    midi.clear();
                    if (b == 60) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
                        if (! std::isfinite (v)) nan = true;
                        peak = juce::jmax (peak, std::abs (v));
                        sq += (double) v * v;
                        if ((prev <= 0) != (v <= 0)) ++zc;
                        prev = v; ++n;
                        if (b >= 170) { tailSq += (double) v * v; ++tailN; }
                    }
                }
                pk[take] = peak;
                rm[take] = std::sqrt (sq / (double) juce::jmax (1L, n));
                tl[take] = std::sqrt (tailSq / (double) juce::jmax (1L, tailN));
                zh[take] = zc * 0.5 * 44100.0 / (double) n;
            }

            auto median3 = [] (double* v) {
                double a2 = v[0], b2 = v[1], c2 = v[2];
                return juce::jmax (juce::jmin (a2, b2), juce::jmin (juce::jmax (a2, b2), c2));
            };
            const double peak = median3 (pk);
            const double rms  = median3 (rm);
            const double tail = median3 (tl);
            const double zcHz = median3 (zh);
            rmsAll.push_back (rms);

            juce::String flags;
            if (nan)                 flags << "NAN ";
            if (peak >= 0.999)       flags << "CLIP ";
            if (rms < 1.0e-4)        flags << "SILENT ";
            // Still loud a second after the key came up: a runaway resonator
            // or a feedback path that never settles.
            if (tail > rms * 0.85 && tail > 0.02) flags << "NO-DECAY ";
            if (flags.isNotEmpty()) ++bad;

            if (flags.isNotEmpty() || (argc > 2 && juce::String (argv[2]) == "-v"))
                printf ("%-20s %8.4f %8.4f %8.4f %8.0f  %s\n",
                        names[idx].toRawUTF8(), peak, rms, tail, zcHz,
                        flags.toRawUTF8());
        }

        std::sort (rmsAll.begin(), rmsAll.end());
        const double med = rmsAll[rmsAll.size() / 2];
        const double p05 = rmsAll[rmsAll.size() / 20];
        const double p95 = rmsAll[rmsAll.size() * 19 / 20];
        printf ("\n%d instruments — %d flagged\n", names.size(), bad);
        printf ("rms  median %.4f   5th %.4f   95th %.4f   spread %.1f dB\n",
                med, p05, p95, 20.0 * std::log10 (p95 / juce::jmax (1.0e-6, p05)));
        return bad == 0 ? 0 : 2;
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
