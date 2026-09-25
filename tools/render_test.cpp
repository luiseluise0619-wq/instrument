// Offline render harness: load each named instrument, play a note, measure.
#include <algorithm>
#include "PluginProcessor.h"
#include "Licensing.h"
#include "UI/ThemeManager.h"
#include "UI/TypingKeymap.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <vector>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    VocalChopAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    // "--mapped-duration": regression for user samples mapped across the
    // keyboard.  C4 must be an octave higher than C3 without consuming the
    // source twice as fast (the old sampler-ratio path did exactly that).
    if (argc > 1 && juce::String (argv[1]) == "--mapped-duration")
    {
        auto durationFor = [] (int note)
        {
            VocalChopAudioProcessor p;
            p.prepareToPlay (44100.0, 512);
            if (! p.loadDemoSample (0))
                return -1.0;

            auto set = [&] (const char* id, float value)
            {
                if (auto* parameter = p.getAPVTS().getParameter (id))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            set ("engine", 3.0f);       // Mapped Sample
            set ("pitch", 0.0f); set ("formant", 0.0f); set ("mix", 1.0f);
            set ("reverb", 0.0f); set ("delay", 0.0f); set ("drive", 0.0f);
            set ("grainMix", 0.0f); set ("release", 5.0f);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
            int lastAudible = -1;
            constexpr int maxBlocks = (int) (20.0 * 44100.0 / 512.0);
            for (int b = 0; b < maxBlocks; ++b)
            {
                block.clear();
                p.processBlock (block, midi);
                midi.clear();
                if (block.getMagnitude (0, block.getNumSamples()) > 1.0e-5f)
                    lastAudible = b;
            }
            return (lastAudible + 1) * 512.0 / 44100.0;
        };

        const double c3 = durationFor (48);
        const double c4 = durationFor (60);
        const double ratio = c3 > 0.0 ? c4 / c3 : 0.0;
        printf ("Mapped Sample duration: C3 %.3fs, C4 %.3fs, ratio %.3f\n",
                c3, c4, ratio);
        const bool ok = c3 > 0.2 && c4 > 0.2 && ratio > 0.88 && ratio < 1.12;
        printf ("%s pitch changes while phrase duration stays constant\n",
                ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    // "--rhythm-reset": a rhythmic preset may intentionally enable Pump or
    // Arp, but choosing an ordinary instrument/vocal afterwards must not keep
    // that beat gate latched across every sound.
    if (argc > 1 && juce::String (argv[1]) == "--rhythm-reset")
    {
        auto value = [&] (const char* id)
        { return proc.getAPVTS().getRawParameterValue (id)->load(); };
        int failures = 0;

        proc.applyPreset (VocalChopAudioProcessor::getNumChopPresets());
        if (value ("pumpAmt") < 0.5f) ++failures; // fixture really is rhythmic

        proc.applyInstrument (0);
        if (value ("pumpAmt") > 0.001f || value ("arpMode") > 0.001f) ++failures;

        if (auto* p = proc.getAPVTS().getParameter ("pumpAmt"))
            p->setValueNotifyingHost (p->convertTo0to1 (0.8f));
        if (auto* p = proc.getAPVTS().getParameter ("arpMode"))
            p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
        proc.loadDemoSample (0);
        if (value ("pumpAmt") > 0.001f || value ("arpMode") > 0.001f) ++failures;

        if (auto* p = proc.getAPVTS().getParameter ("pumpAmt"))
            p->setValueNotifyingHost (p->convertTo0to1 (0.8f));
        proc.applyAirVocalPreset (0);
        if (value ("pumpAmt") > 0.001f || value ("arpMode") > 0.001f) ++failures;

        printf ("%s normal instrument, Vocal Chop and Air Vocal clear inherited beat gates (%d failures)\n",
                failures == 0 ? "PASS" : "FAIL", failures);
        return failures == 0 ? 0 : 1;
    }

    if (argc > 1 && juce::String (argv[1]) == "--vocalkit")
    {
        proc.loadFactoryVocalKit (0);
        int failures = 0;
        for (int slot = 0; slot < VocalKitEngine::kNumSlots; ++slot)
        {
            auto sample = proc.getVocalKit().getSlotSample (slot);
            if (! sample || sample->getNumSamples() < 64) { ++failures; continue; }
            juce::AudioBuffer<float> block (2, 512);
            float peak = 0.0f;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, VocalKitEngine::kRootNote + slot, (juce::uint8) 110), 0);
            for (int b = 0; b < 12; ++b)
            {
                block.clear(); proc.processBlock (block, midi); midi.clear();
                peak = juce::jmax (peak, block.getMagnitude (0, block.getNumSamples()));
            }
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::allSoundOff (1), 0);
            block.clear(); proc.processBlock (block, off);
            printf ("slot %02d %-20s peak %.4f\n", slot + 1,
                    proc.getVocalKit().getSlotSettings(slot).name.toRawUTF8(), peak);
            if (! std::isfinite (peak) || peak < 0.0001f) ++failures;
        }

        auto first = proc.getVocalKit().getSlotSettings (0);
        auto second = proc.getVocalKit().getSlotSettings (1);
        first.gainDb = -17.0f; first.start = 0.11f; first.reverse = true;
        first.pitchSemitones = 3.0f; first.fineCents = -12.0f;
        first.decayMs = 123.0f; first.sustain = 0.42f;
        first.loopStart = 0.21f; first.loopEnd = 0.79f; first.loopCrossfadeMs = 17.0f;
        proc.getVocalKit().setSlotSettings (0, first);
        const auto unchangedSecond = proc.getVocalKit().getSlotSettings (1);
        if (std::abs (unchangedSecond.gainDb - second.gainDb) > 0.001f
            || std::abs (unchangedSecond.pitchSemitones - second.pitchSemitones) > 0.001f)
            ++failures;

        // A repeated Note On received while the physical key is still held
        // must not restart/stack a one-shot. Compare it with an uninterrupted
        // reference processor at the same playback position.
        VocalChopAudioProcessor heldReference, heldRepeated;
        heldReference.prepareToPlay (44100.0, 512); heldRepeated.prepareToPlay (44100.0, 512);
        heldReference.loadFactoryVocalKit (0); heldRepeated.loadFactoryVocalKit (0);
        if (auto* arp = heldRepeated.getAPVTS().getParameter ("arpMode"))
            arp->setValueNotifyingHost (arp->convertTo0to1 (1.0f));
        for (auto* p : { &heldReference, &heldRepeated })
        {
            auto st = p->getVocalKit().getSlotSettings (0);
            st.attackMs = st.decayMs = 0.0f; st.sustain = 1.0f;
            st.delaySend = st.reverbSend = 0.0f; st.loop = false; st.oneShot = true;
            p->getVocalKit().setSlotSettings (0, st);
        }
        juce::AudioBuffer<float> refBlock (2, 512), repeatBlock (2, 512);
        juce::MidiBuffer refMidi, repeatMidi;
        refMidi.addEvent (juce::MidiMessage::noteOn (1, VocalKitEngine::kRootNote, (juce::uint8) 100), 0);
        repeatMidi.addEvent (juce::MidiMessage::noteOn (1, VocalKitEngine::kRootNote, (juce::uint8) 100), 0);
        heldReference.processBlock (refBlock, refMidi); heldRepeated.processBlock (repeatBlock, repeatMidi);
        float arpBypassDifference = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 512; ++n)
                arpBypassDifference = juce::jmax (arpBypassDifference,
                    std::abs (refBlock.getSample (ch, n) - repeatBlock.getSample (ch, n)));
        if (arpBypassDifference > 1.0e-5f) ++failures;
        refBlock.clear(); repeatBlock.clear(); refMidi.clear(); repeatMidi.clear();
        repeatMidi.addEvent (juce::MidiMessage::noteOn (1, VocalKitEngine::kRootNote, (juce::uint8) 100), 0);
        heldReference.processBlock (refBlock, refMidi); heldRepeated.processBlock (repeatBlock, repeatMidi);
        float repeatDifference = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 512; ++n)
                repeatDifference = juce::jmax (repeatDifference,
                    std::abs (refBlock.getSample (ch, n) - repeatBlock.getSample (ch, n)));
        if (repeatDifference > 1.0e-5f) ++failures;

        heldRepeated.loadFactoryVocalKit (1);
        if (heldRepeated.getAPVTS().getRawParameterValue ("arpMode")->load() > 0.5f)
            ++failures;

        VocalKitEngine missing;
        missing.prepare (44100.0, 512);
        juce::XmlElement missingRoot ("SLYCE_STATE");
        auto* missingKit = missingRoot.createNewChildElement ("VocalKit");
        auto* missingSlot = missingKit->createNewChildElement ("Slot");
        missingSlot->setAttribute ("index", 0);
        missingSlot->setAttribute ("path", "Z:\\definitely-missing\\voice.wav");
        missing.restoreState (missingRoot);
        if (! missing.getSlotSettings(0).missing || missing.getMissingFiles().size() != 1)
            ++failures;

        juce::MemoryBlock state;
        proc.getStateInformation (state);
        VocalChopAudioProcessor restored;
        restored.prepareToPlay (44100.0, 512);
        restored.setStateInformation (state.getData(), (int) state.getSize());
        const auto restoredFirst = restored.getVocalKit().getSlotSettings (0);
        if (! restored.isVocalKitMode() || ! restored.getVocalKit().hasSlot (0)
            || std::abs (restoredFirst.gainDb + 17.0f) > 0.001f
            || std::abs (restoredFirst.start - 0.11f) > 0.001f || ! restoredFirst.reverse
            || std::abs (restoredFirst.pitchSemitones - 3.0f) > 0.001f
            || std::abs (restoredFirst.decayMs - 123.0f) > 0.001f
            || std::abs (restoredFirst.sustain - 0.42f) > 0.001f
            || std::abs (restoredFirst.loopCrossfadeMs - 17.0f) > 0.001f)
            ++failures;

        printf ("vocal-kit failures: %d, state bytes: %zu\n", failures, state.getSize());
        return failures == 0 ? 0 : 1;
    }

    // "--vocals": load every built-in vocal and measure it. The table pairs a
    // label with a BinaryData symbol by hand, 36 times; a mispaired row still
    // compiles and still plays - it just plays the wrong sample under the
    // wrong name, or silence. This is the only thing that catches that.
    if (argc > 1 && juce::String (argv[1]) == "--vocals")
    {
        const auto names  = VocalChopAudioProcessor::getDemoSampleNames();
        const auto groups = VocalChopAudioProcessor::getDemoSampleGroups();
        const int  n      = VocalChopAudioProcessor::getNumDemoSamples();

        printf ("%-18s %-17s %7s %7s %8s\n", "name", "group", "sec", "peak", "rms");
        printf ("%-18s %-17s %7s %7s %8s\n", "----", "-----", "---", "----", "---");

        int bad = 0;
        std::set<juce::String> seen;   // identical digests = a duplicated row
        for (int i = 0; i < n; ++i)
        {
            if (! proc.loadDemoSample (i))
            {
                printf ("%-18s %-17s   FAILED TO LOAD\n",
                        names[i].toRawUTF8(), groups[i].toRawUTF8());
                ++bad;
                continue;
            }

            auto buf = proc.getLoadedSample();
            if (buf == nullptr || buf->getNumSamples() == 0)
            {
                printf ("%-18s %-17s   EMPTY\n",
                        names[i].toRawUTF8(), groups[i].toRawUTF8());
                ++bad;
                continue;
            }

            const int   len  = buf->getNumSamples();
            const float peak = buf->getMagnitude (0, len);
            const float rms  = buf->getRMSLevel (0, 0, len);

            // Cheap content digest: coarse energy over 16 windows. Two rows
            // pointing at the same wav come out identical here.
            juce::String digest;
            for (int w = 0; w < 16; ++w)
            {
                const int a = (int) ((juce::int64) w * len / 16);
                const int b = (int) ((juce::int64) (w + 1) * len / 16);
                digest << juce::String ((int) (buf->getRMSLevel (0, a, juce::jmax (1, b - a))
                                               * 1000.0f)) << ",";
            }

            const bool dupe = ! seen.insert (digest).second;
            printf ("%-18s %-17s %7.2f %7.3f %8.4f%s\n",
                    names[i].toRawUTF8(), groups[i].toRawUTF8(),
                    len / proc.getLoadedSampleRate(), peak, rms,
                    dupe ? "   DUPLICATE CONTENT" : (peak < 0.05f ? "   NEARLY SILENT" : ""));
            if (dupe || peak < 0.05f)
                ++bad;
        }

        printf ("\n%d vocal(s), %d problem(s).\n", n, bad);
        return bad == 0 ? 0 : 1;
    }

    // "--contrast": every label colour against the surface it is drawn on, for
    // all 16 skins. Reported bugs about text being unreadable kept arriving one
    // theme at a time, and eyeballing 16 skins does not scale - a token can be
    // fine on 15 of them and vanish on the sixteenth. 4.5:1 is the WCAG AA
    // floor the spec's own acceptance list asks for.
    if (argc > 1 && juce::String (argv[1]) == "--contrast")
    {
        auto lum = [] (juce::Colour c)
        {
            auto ch = [] (float v)
            {
                return v <= 0.03928f ? v / 12.92f
                                     : std::pow ((v + 0.055f) / 1.055f, 2.4f);
            };
            return 0.2126f * ch (c.getFloatRed())
                 + 0.7152f * ch (c.getFloatGreen())
                 + 0.0722f * ch (c.getFloatBlue());
        };
        // Flatten a translucent fill onto the surface behind it before
        // measuring. A card at 60% alpha over the desk is NOT its own colour,
        // and measuring the raw token overstates every panel label.
        auto over = [] (juce::Colour fg, juce::Colour bg)
        {
            const float a = fg.getFloatAlpha();
            return juce::Colour::fromFloatRGBA (
                fg.getFloatRed()   * a + bg.getFloatRed()   * (1.0f - a),
                fg.getFloatGreen() * a + bg.getFloatGreen() * (1.0f - a),
                fg.getFloatBlue()  * a + bg.getFloatBlue()  * (1.0f - a), 1.0f);
        };
        auto ratio = [&lum] (juce::Colour a, juce::Colour b)
        {
            const float la = lum (a), lb = lum (b);
            return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
        };

        int failures = 0;
        printf ("%-15s %-22s %6s\n", "theme", "pair", "ratio");
        printf ("%-15s %-22s %6s\n", "-----", "----", "-----");

        for (int i = 0; i < ThemeManager::kNumThemes; ++i)
        {
            const auto& t = ThemeManager::themes()[(size_t) i];
            const auto desk = t.bg2;
            const auto card = over (t.card, desk);      // panel over the desk
            const auto well = over (t.well, card);      // recess inside a panel
            const auto ctl  = over (t.ctl,  desk);      // toolbar control

            struct Pair { const char* what; juce::Colour fg, bg; };
            const Pair pairs[] = {
                { "primary/card",     t.txt,      card      },
                { "secondary/card",   t.txt2,     card      },
                { "tertiary/card",    t.txt3,     card      },
                { "primary/desk",     t.txt,      desk      },
                { "secondary/desk",   t.txt2,     desk      },
                { "primary/well",     t.txt,      well      },
                { "primary/control",  t.txt,      ctl       },
                { "ink/accent",       t.onAcc,    t.acc     },
                { "accTxt/card",      t.accTxt,   card      },
            };

            // The bare accent is measured too, but only as an ADVISORY - it is
            // a fill colour, and every remaining use of it on a card is a
            // shape (a marker, a tick, a dot), not a word. It fails on six
            // skins, which is exactly why accTxt exists and why labels must
            // use that instead. Counting it as a failure would leave this
            // harness permanently red and teach everyone to ignore it.
            {
                const float r = ratio (over (t.acc, card), card);
                if (r < 4.5f)
                    printf ("%-15s %-22s %6.2f  advisory: fill only, never text\n",
                            t.name.toRawUTF8(), "accent/card", r);
            }

            // The keybed letters do NOT use the keyTx / keyTxB tokens - those
            // are declared and never read. SliceGrid builds the key colours
            // itself (the ivory is tinted toward the accent) and derives the
            // letter from the KEY with contrasting(), so measure that, or this
            // harness reports on paint nobody performs.
            const juce::Colour ivoryBot =
                (t.dark ? juce::Colour (0xfff4f7ff) : juce::Colours::white)
                    .interpolatedWith (t.acc, t.dark ? 0.05f : 0.03f);
            const juce::Colour ebonyBot = juce::Colour (0xff141416);

            const Pair keyPairs[] = {
                { "keyletter/white", ivoryBot.contrasting (0.72f), ivoryBot },
                { "keyletter/black", ebonyBot.contrasting (0.62f), ebonyBot },
            };

            std::vector<Pair> all (std::begin (pairs), std::end (pairs));
            all.insert (all.end(), std::begin (keyPairs), std::end (keyPairs));

            for (const auto& p : all)
            {
                const float r = ratio (over (p.fg, p.bg), p.bg);
                // txt3 is deliberately faint decoration (a caption under a
                // dial), so it is held to the 3:1 large-text floor; anything
                // carrying a word the user must read gets the full 4.5.
                const float floorFor = juce::String (p.what).startsWith ("tertiary") ? 3.0f : 4.5f;
                if (r < floorFor)
                {
                    printf ("%-15s %-22s %6.2f  FAIL (needs %.1f)\n",
                            t.name.toRawUTF8(), p.what, r, floorFor);
                    ++failures;
                }
            }
        }

        printf ("\n%d failing pair(s) across %d themes.\n",
                failures, ThemeManager::kNumThemes);
        return failures == 0 ? 0 : 1;
    }

    // "--chop": drive the slice sampler over every built-in vocal, in both
    // slicing modes, triggering every slice - and then the inputs that are
    // meant never to happen. A sampler fails at its EDGES: slice zero, the
    // last slice, an index past the end, a trigger with nothing loaded, a
    // re-slice underneath a sounding voice. None of that was covered.
    if (argc > 1 && juce::String (argv[1]) == "--chop")
    {
        int problems = 0;
        auto bad = [&problems] (const juce::String& what)
        {
            printf ("   PROBLEM: %s\n", what.toRawUTF8());
            ++problems;
        };

        juce::AudioBuffer<float> buf (2, 512);
        auto& slicer = proc.getSliceEngine();

        // Renders `blocks` blocks and reports what came out.
        struct Out { float peak; bool nan; };
        auto render = [&proc, &buf] (int blocks)
        {
            Out o { 0.0f, false };
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                buf.clear();
                proc.processBlock (buf, midi);
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    for (int n = 0; n < buf.getNumSamples(); ++n)
                    {
                        const float v = buf.getSample (ch, n);
                        if (! std::isfinite (v)) o.nan = true;
                        o.peak = juce::jmax (o.peak, std::abs (v));
                    }
            }
            return o;
        };

        const auto names = VocalChopAudioProcessor::getDemoSampleNames();
        const int  nVox  = VocalChopAudioProcessor::getNumDemoSamples();

        printf ("%-16s %-10s %6s %6s %7s %6s\n",
                "vocal", "mode", "slices", "silent", "wrongPos", "nan");
        printf ("%-16s %-10s %6s %6s %7s %6s\n",
                "-----", "----", "------", "------", "--------", "---");

        for (int v = 0; v < nVox; ++v)
        {
            if (! proc.loadDemoSample (v))
            {
                bad ("vocal " + names[v] + " failed to load");
                continue;
            }

            for (int m = 0; m < 2; ++m)
            {
                slicer.setMode (m == 0 ? SliceEngine::Transient : SliceEngine::Grid);
                if (m == 1) slicer.setGridDivision (16);
                slicer.rebuildSlices();

                const int n = slicer.getNumSlices();
                if (n <= 0) { bad (names[v] + ": no slices at all"); continue; }

                int silent = 0, wrongPos = 0, nan = 0;
                for (int i = 0; i < n; ++i)
                {
                    proc.lastVoiceStartSample.store (-1);
                    render (2);                       // flush the previous tail
                    proc.triggerSlicePad (i, 0.95f);
                    const auto o = render (6);

                    if (o.nan)          ++nan;
                    if (o.peak < 1.0e-4f) ++silent;

                    // triggerSlicePad takes a KEY index, not a slice index -
                    // it runs through the same diatonic mapping MIDI does, so
                    // key i plays slice diatonicSliceIndex(i) wrapped by the
                    // count. Checking it against slice i said 15 of 16 were
                    // wrong on every single vocal, which is what a broken
                    // EXPECTATION looks like: a real fault does not miss
                    // everything except one.
                    //
                    // Two things are worth asserting, so assert both: that the
                    // key landed on the slice the mapping promises, and that
                    // the voice then began reading at that slice's own offset.
                    const int wantSlice =
                        ((VocalChopAudioProcessor::diatonicSliceIndex (i) % n) + n) % n;
                    const int gotSlice = proc.lastVoiceSlice.load();
                    const auto sl = slicer.getSlice (gotSlice);
                    const int  got = proc.lastVoiceStartSample.load();

                    if (gotSlice >= 0 && gotSlice != wantSlice)
                        ++wrongPos;
                    else if (sl.has_value() && got >= 0 && got != sl->startSample)
                        ++wrongPos;
                }

                printf ("%-16s %-10s %6d %6d %7d %6d%s\n",
                        names[v].toRawUTF8(), m == 0 ? "transient" : "grid",
                        n, silent, wrongPos, nan,
                        (silent || wrongPos || nan) ? "   <-- " : "");
                problems += (silent > 0) + (wrongPos > 0) + (nan > 0);
            }
        }

        printf ("\n-- edges ---------------------------------------------------\n");
        proc.loadDemoSample (0);
        slicer.setMode (SliceEngine::Grid);
        slicer.setGridDivision (8);
        slicer.rebuildSlices();
        const int n0 = slicer.getNumSlices();

        // Out of range in both directions. The audio thread wraps with the
        // atomic count, so these must land on a real slice or be dropped -
        // never read outside the buffer.
        for (int idx : { -1, -1000, n0, n0 + 1, 100000 })
        {
            proc.triggerSlicePad (idx, 0.9f);
            const auto o = render (6);
            if (o.nan)
                bad ("index " + juce::String (idx) + " produced NaN");
            printf ("   index %-7d peak %.4f%s\n", idx, o.peak, o.nan ? "  NaN" : "");
        }

        // Triggering with nothing loaded at all.
        {
            proc.loadSampleFromMemory (nullptr, 0);   // leaves the old sample
            VocalChopAudioProcessor fresh;
            fresh.prepareToPlay (44100.0, 512);
            juce::AudioBuffer<float> fb (2, 512);
            fresh.triggerSlicePad (0, 0.9f);
            fresh.triggerSlicePad (5, 0.9f);
            bool nan = false; float peak = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                juce::MidiBuffer midi; fb.clear();
                fresh.processBlock (fb, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const float x = fb.getSample (ch, i);
                        if (! std::isfinite (x)) nan = true;
                        peak = juce::jmax (peak, std::abs (x));
                    }
            }
            if (nan) bad ("triggering with no sample loaded produced NaN");
            // Sound here is CORRECT, not a leak: with nothing loaded the keys
            // fall through to the synth rather than going dead. Only NaN and a
            // crash would be faults.
            printf ("   no sample loaded    peak %.4f%s  (synth fallback)\n",
                    peak, nan ? "  NaN" : "");
        }

        // Re-slicing while a voice is sounding: the slice list is republished
        // under the spin lock the audio thread try-locks.
        {
            proc.loadDemoSample (1);
            slicer.setMode (SliceEngine::Transient);
            slicer.rebuildSlices();
            proc.triggerSlicePad (0, 0.95f);
            render (1);
            bool nan = false;
            for (int d = 4; d <= 32; d *= 2)
            {
                slicer.setMode (SliceEngine::Grid);
                slicer.setGridDivision (d);
                slicer.rebuildSlices();          // message thread, mid-voice
                const auto o = render (3);
                nan = nan || o.nan;
            }
            if (nan) bad ("re-slicing under a sounding voice produced NaN");
            printf ("   re-slice mid-voice  %s\n", nan ? "NaN" : "clean");
        }

        // Every key at once, then choke them all.
        {
            proc.loadDemoSample (3);
            slicer.rebuildSlices();
            for (int i = 0; i < slicer.getNumSlices(); ++i)
                proc.pressSlicePad (i, 0.9f);
            const auto onAll = render (10);
            for (int i = 0; i < slicer.getNumSlices(); ++i)
                proc.chokeSlicePad (i);
            // Settle FIRST, then measure. Peaking across the whole window
            // after the choke includes the audio from before it landed - the
            // events are queued and applied at the top of the next block - so
            // the first version of this reported 0.84 and called it a leak.
            render (6);                       // the events land next block
            const auto justAfter = render (24);          // ~0.28 s
            render (126);                                // run out to ~1.7 s...
            const auto settled   = render (24);          // ...and measure THERE

            // render() reports the peak across its whole window, so measuring
            // "2 s after the choke" with one long render returns the loudest
            // moment of the tail rather than its end. That is the same error
            // as measuring across the choke itself, one level down.

            // What a choke owes you is that the VOICES stop, not that the room
            // goes with them. This plugin declares a 2 s tail and has a reverb
            // and a synced delay on the master, so demanding silence 0.3 s
            // after the last note is asking the FX to be broken - the first
            // version of this check did exactly that and called an ordinary
            // reverb tail a stuck voice.
            //
            // A stuck voice and a tail are told apart by their shape: a tail
            // falls, a held voice does not.
            if (onAll.nan || justAfter.nan || settled.nan)
                bad ("all-keys-down produced NaN");
            if (settled.peak > justAfter.peak * 0.5f)
                bad ("audio after the choke is not decaying - a voice is stuck");
            if (settled.peak > 0.05f)
                bad ("still audible 2 s after the choke");

            printf ("   all keys down       peak %.4f\n", onAll.peak);
            printf ("   after choke         0.3s %.4f -> 2s %.4f   %s\n",
                    justAfter.peak, settled.peak,
                    settled.peak <= justAfter.peak * 0.5f ? "decaying (tail)"
                                                          : "SUSTAINING");
        }

        printf ("\n%d problem(s).\n", problems);
        return problems == 0 ? 0 : 1;
    }

    // "--chop-mono": every Vocal Chop trigger path shares one choke group.
    // The old phrase must be replaced when the next pad/key arrives, while
    // routing stays entirely inside the chop VoicePool (other engines retain
    // their independent polyphony).
    if (argc > 1 && juce::String (argv[1]) == "--chop-mono")
    {
        int problems = 0;
        if (! proc.loadDemoSample (0))
        {
            printf ("could not load test vocal\n");
            return 1;
        }

        auto& slicer = proc.getSliceEngine();
        slicer.setMode (SliceEngine::Grid);
        slicer.setGridDivision (8);
        slicer.rebuildSlices();

        juce::AudioBuffer<float> block (2, 512);
        auto pump = [&] (juce::MidiBuffer midi = {})
        {
            block.clear();
            proc.processBlock (block, midi);
        };
        auto active = [&]
        {
            float positions[VoicePool::kMaxVoices] {};
            return proc.getVoicePool().copyPlayheads (positions, VoicePool::kMaxVoices);
        };
        auto expectOne = [&] (const char* path)
        {
            const int n = active();
            printf ("%-20s active chop voices %d\n", path, n);
            if (n != 1) ++problems;
        };

        // UI held-key path. Releasing the old key afterwards must not release
        // the replacement voice (its stale slot mapping was scrubbed).
        proc.pressSlicePad (0, 0.9f); pump();
        proc.pressSlicePad (2, 0.9f); pump();
        expectOne ("UI key replacement");
        proc.releaseSlicePad (0); pump();
        expectOne ("stale UI note-off");

        // Slice-card preview uses a direct slice index.
        proc.triggerSliceDirectPad (3, 0.9f); pump();
        proc.triggerSliceDirectPad (4, 0.9f); pump();
        expectOne ("slice-card replacement");

        // Host MIDI path, including a late note-off for the previous note.
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
        pump (midi);
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 52, 0.9f), 0);
        pump (midi);
        expectOne ("MIDI replacement");
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
        pump (midi);
        expectOne ("stale MIDI note-off");

        // The full-sample preview is in the same choke group.
        proc.triggerWholeSamplePreview(); pump();
        expectOne ("whole preview");
        proc.triggerSlicePad (1, 0.9f); pump();
        expectOne ("preview -> slice");

        printf ("\n%d monophonic-chop problem(s).\n", problems);
        return problems == 0 ? 0 : 1;
    }

    // "--licence [pair-file]": everything between a buyer pasting a key and the
    // plugin unlocking. This is the most expensive class of bug in the product
    // - a key that does not work costs a refund AND the customer - and none of
    // it was covered.
    //
    // The POSITIVE cases need a real key, and a real key committed to a public
    // repository would be a published bypass for whatever e-mail it was issued
    // to. So they are read from a file (two lines: e-mail, then key) given on
    // the command line or in SLYCE_TEST_LICENCE, and skipped when it is absent.
    // Every REJECTION case runs unconditionally - those need no secret.
    if (argc > 1 && juce::String (argv[1]) == "--licence")
    {
        printf ("Offline licence tests were retired; Gumroad verification is an online integration test.\n");
        return 0;
    }

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

    // "--chopdemo <in.wav> <out.wav>": load a vocal, slice it, and play a
    // chopped PERFORMANCE of it.
    //
    // The output is a rearrangement, not a copy - which is both the point of
    // the demo and the only version of it that is safe to publish. Commercial
    // vocal packs let you USE a sample in a production; they do not let you
    // rehost the file. So this renders what the instrument does to it.
    if (argc > 3 && juce::String (argv[1]) == "--chopdemo")
    {
        // Braces, not parens: `File in (String (argv[2]));` is parsed as a
        // function declaration taking a String* - the most vexing parse.
        const juce::File in  { juce::String (argv[2]) };
        const juce::File out { juce::String (argv[3]) };
        if (! proc.loadSampleFromFile (in))
        { printf ("could not load %s\n", argv[2]); return 1; }

        const int n = proc.getSliceEngine().getNumSlices();
        printf ("loaded %s: %d slices\n", in.getFileName().toRawUTF8(), n);
        if (n <= 0) return 1;

        auto set = [&] (const char* id, float v)
        { if (auto* p = proc.getAPVTS().getParameter (id))
              p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        set ("engine", 0.0f);                    // Chop
        set ("reverb", 0.34f); set ("delay", 0.26f); set ("delaySync", 1.0f);
        set ("width", 1.35f);  set ("drive", 0.10f);
        set ("attack", 1.0f);  set ("release", 90.0f);

        // 100 BPM, sixteenths. A pattern rather than a scale run - the demo has
        // to sound like someone chopping, not like a unit test.
        const double bpm = 100.0, sr = 44100.0;
        const int stepSamples = (int) (sr * 60.0 / bpm / 4.0);
        const int kPattern[] = { 0, 0, 2, 4, 0, 5, 2, 7,  1, 1, 3, 6, 0, 4, 2, 9,
                                 0, 3, 0, 5, 2, 7, 4, 9,  1, 6, 3, 8, 0, 2, 5, 11 };
        const int steps = (int) (sizeof (kPattern) / sizeof (kPattern[0])) * 2;   // 2 passes

        const int block = 512;
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        std::vector<float> L, R;
        int stepAcc = 0, step = 0;

        const int totalBlocks = (steps * stepSamples) / block + 90;   // + tail
        for (int b = 0; b < totalBlocks; ++b)
        {
            if (step < steps && stepAcc <= 0)
            {
                const int k = kPattern[step % (int) (sizeof (kPattern) / sizeof (kPattern[0]))];
                proc.triggerSlicePad (k % juce::jmax (1, n), step % 4 == 0 ? 1.0f : 0.78f);
                stepAcc = stepSamples;
                ++step;
            }
            buf.clear();
            proc.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            { L.push_back (buf.getSample (0, i)); R.push_back (buf.getSample (1, i)); }
            stepAcc -= block;
        }

        // Normalise to -1 dBFS so it sits at a sane level on a web page.
        float pk = 0.0f;
        for (size_t i = 0; i < L.size(); ++i)
            pk = juce::jmax (pk, juce::jmax (std::abs (L[i]), std::abs (R[i])));
        const float g = pk > 1.0e-6f ? 0.891f / pk : 1.0f;

        juce::AudioBuffer<float> outBuf (2, (int) L.size());
        for (size_t i = 0; i < L.size(); ++i)
        { outBuf.setSample (0, (int) i, L[i] * g); outBuf.setSample (1, (int) i, R[i] * g); }

        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> fos (out.createOutputStream());
        if (fos == nullptr) { printf ("cannot write %s\n", argv[3]); return 1; }
        std::unique_ptr<juce::AudioFormatWriter> w (
            wav.createWriterFor (fos.release(), sr, 2, 16, {}, 0));
        if (w == nullptr) { printf ("no writer\n"); return 1; }
        w->writeFromAudioSampleBuffer (outBuf, 0, outBuf.getNumSamples());
        w.reset();

        printf ("wrote %s  %.2f s  peak %.3f -> %.3f\n", argv[3],
                (double) L.size() / sr, pk, pk * g);
        return 0;
    }

    // "--slicesprite <in.wav> <out.wav> <n>": the first n slices, laid end to
    // end in one mono file, with their offsets printed as JSON.
    //
    // For the website: a browser can fetch ONE file, decode it once, and
    // trigger any slice from an offset - which is how a sample instrument
    // works and what makes the page playable rather than a video of playing.
    // Fragments rather than the loop, deliberately: this demonstrates the
    // instrument without rehosting somebody else's vocal.
    if (argc > 3 && juce::String (argv[1]) == "--slicesprite")
    {
        const juce::File in  { juce::String (argv[2]) };
        const juce::File out { juce::String (argv[3]) };
        const int want = argc > 4 ? juce::jmax (1, juce::String (argv[4]).getIntValue()) : 12;

        if (! proc.loadSampleFromFile (in)) { printf ("load failed\n"); return 1; }
        auto src = proc.getLoadedSample();
        if (src == nullptr || src->getNumSamples() == 0) { printf ("no audio\n"); return 1; }

        const int total = proc.getSliceEngine().getNumSlices();
        if (total <= 0) { printf ("no slices\n"); return 1; }

        // 22.05 kHz mono: a vocal chop has nothing above 11 kHz that survives
        // a laptop speaker, and this has to travel over the web.
        const double outSr = 22050.0, inSr = proc.getLoadedSampleRate();
        const double ratio = inSr / outSr;
        const int maxLen = (int) (outSr * 0.85);          // cap a slice at 850 ms

        std::vector<float> sprite;
        juce::String json = "[";
        const int step = juce::jmax (1, total / want);

        for (int k = 0, taken = 0; k < total && taken < want; k += step, ++taken)
        {
            SlicePoint sp;
            if (! proc.getSliceEngine().tryGetSlice (k, sp)) continue;
            const int len = juce::jmin (maxLen, (int) (sp.lengthSamples / ratio));
            if (len < 400) { --taken; continue; }

            const int start = (int) sprite.size();
            for (int i = 0; i < len; ++i)
            {
                const int srcIdx = sp.startSample + (int) (i * ratio);
                float v = 0.0f;
                if (srcIdx < src->getNumSamples())
                    for (int ch = 0; ch < src->getNumChannels(); ++ch)
                        v += src->getSample (ch, srcIdx);
                v /= (float) juce::jmax (1, src->getNumChannels());

                // 4 ms in, 25 ms out - a raw cut clicks at both ends.
                const float fi = juce::jmin (1.0f, (float) i / (0.004f * (float) outSr));
                const float fo = juce::jmin (1.0f, (float) (len - i) / (0.025f * (float) outSr));
                sprite.push_back (v * fi * fo);
            }
            for (int i = 0; i < (int) (outSr * 0.02); ++i) sprite.push_back (0.0f);  // guard gap

            if (taken > 0) json += ",";
            json += "{\"o\":" + juce::String (start) + ",\"n\":" + juce::String (len) + "}";
        }
        json += "]";

        float pk = 0.0f;
        for (float v : sprite) pk = juce::jmax (pk, std::abs (v));
        const float g = pk > 1.0e-6f ? 0.89f / pk : 1.0f;

        juce::AudioBuffer<float> buf (1, (int) sprite.size());
        for (size_t i = 0; i < sprite.size(); ++i) buf.setSample (0, (int) i, sprite[i] * g);

        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> fos (out.createOutputStream());
        if (fos == nullptr) { printf ("cannot write\n"); return 1; }
        std::unique_ptr<juce::AudioFormatWriter> w (
            wav.createWriterFor (fos.release(), outSr, 1, 16, {}, 0));
        w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        w.reset();

        printf ("%s\n", json.toRawUTF8());
        printf ("wrote %s  %.2f s mono %.0f Hz  peak %.3f\n", argv[3],
                (double) sprite.size() / outSr, outSr, pk * g);
        return 0;
    }

    // "--reverb": how much does the reverb knob actually add?
    //
    // The complaint was that it is too strong. That is measurable: play the
    // same note at several settings and compare the level during the note and
    // how long the tail rings after note-off. A wet path that is ADDED to the
    // dry signal rather than mixed with it makes the whole patch louder as you
    // turn it up, which is what "too strong" feels like from the outside.
    if (argc > 1 && juce::String (argv[1]) == "--reverb")
    {
        proc.applyInstrument (juce::jmax (0, names.indexOf ("Royal Grand")));
        const float amounts[] = { 0.0f, 0.10f, 0.25f, 0.50f, 1.0f };
        printf ("%-8s %9s %9s %9s\n", "reverb", "note rms", "tail s", "vs dry dB");
        double dryRms = 0.0;
        for (float a : amounts)
        {
            if (auto* r = proc.getAPVTS().getParameter ("reverb"))
                r->setValueNotifyingHost (r->convertTo0to1 (a));
            for (int b = 0; b < 60; ++b)   // let the tail flush between takes
            { juce::AudioBuffer<float> z (2, 512); juce::MidiBuffer m; z.clear();
              proc.processBlock (z, m); }

            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            std::vector<float> mono;
            for (int b = 0; b < 260; ++b)
            {
                buf.clear(); proc.processBlock (buf, midi); midi.clear();
                if (b == 60) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                for (int i = 0; i < 512; ++i)
                    mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
            }
            const size_t noteEnd = 60 * 512;
            double sq = 0.0; float pk = 0.0f;
            for (size_t i = 0; i < noteEnd; ++i) { sq += (double) mono[i] * mono[i]; }
            for (float v : mono) pk = juce::jmax (pk, std::abs (v));
            const double rms = std::sqrt (sq / (double) noteEnd);
            if (a == 0.0f) dryRms = rms;

            size_t last = noteEnd;
            for (size_t i = noteEnd; i < mono.size(); ++i)
                if (std::abs (mono[i]) > pk * 0.01f) last = i;
            printf ("%-8.2f %9.4f %9.2f %9.2f\n", a, rms,
                    (double) (last - noteEnd) / 44100.0,
                    20.0 * std::log10 ((rms + 1e-9) / (dryRms + 1e-9)));
        }
        return 0;
    }

    // "--extremes": does anything break at the bottom and top of the keyboard?
    //
    // Asked directly, and worth answering with numbers: a synth that is fine in
    // the middle can go silent, alias into noise, or produce NaN at the ends,
    // and none of that shows up in a test that only ever plays C4.
    if (argc > 1 && juce::String (argv[1]) == "--extremes")
    {
        const int notes[] = { 0, 12, 24, 36, 60, 84, 96, 108, 120, 127 };
        printf ("%-22s %5s %8s %8s %6s %6s\n",
                "instrument", "note", "peak", "rms", "nan", "clip");
        int bad = 0;
        // A spread across the categories rather than all 403 - this is about
        // the pitch extremes, not the catalogue.
        const char* probe[] = { "Supersaw Lead", "Royal Grand", "Sub 808", "Glass Bell",
                                "Vox Choir", "Drum Kit", "Syn Nylon", "Big Pad" };
        for (const char* nm : probe)
        {
            const int idx = names.indexOf (nm);
            if (idx < 0) continue;
            proc.applyInstrument (idx);

            for (int note : notes)
            {
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.95f), 0);
                float pk = 0.0f; double sq = 0.0; long n = 0;
                bool nan = false; int clipped = 0;
                for (int b = 0; b < 70; ++b)
                {
                    buf.clear(); proc.processBlock (buf, midi); midi.clear();
                    if (b == 30) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                    for (int i = 0; i < 512; ++i)
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            const float v = buf.getSample (ch, i);
                            if (! std::isfinite (v)) nan = true;
                            if (std::abs (v) > 1.0f) ++clipped;
                            pk = juce::jmax (pk, std::abs (v));
                            sq += (double) v * v; ++n;
                        }
                }
                const double rms = std::sqrt (sq / (double) juce::jmax (1L, n));
                const bool problem = nan || clipped > 0;
                if (problem) ++bad;
                if (problem || (argc > 2 && juce::String (argv[2]) == "-v"))
                    printf ("%-22s %5d %8.4f %8.4f %6s %6d\n", nm, note, pk, rms,
                            nan ? "NaN" : "-", clipped);
            }
        }
        printf ("\n%d instrument/note combinations produced NaN or clipping\n", bad);
        return bad == 0 ? 0 : 1;
    }

    // "--genres": do the genre banks hold up?
    //
    // The banks are 400 hand-written name strings pointing into a catalogue
    // that keeps growing. Every one of them is a chance to typo a name or to
    // rename an instrument and forget the bank referencing it, and the symptom
    // either way is a menu row that silently does nothing - which is exactly
    // the class of bug that survives a screenshot.
    if (argc > 1 && juce::String (argv[1]) == "--genres")
    {
        const auto names = VocalChopAudioProcessor::getInstrumentNames();
        const auto banks = VocalChopAudioProcessor::getGenreBankNames();
        printf ("catalogue: %d instruments\n\n", names.size());

        // What the sizes were asked to be. Stated here rather than derived
        // from the table, so shrinking a bank fails instead of redefining
        // what the target was.
        const int wanted[] = { 150, 150, 100 };
        int bad = 0;

        for (int b = 0; b < banks.size(); ++b)
        {
            const auto roles = VocalChopAudioProcessor::getGenreRoleNames (b);
            const int  size  = VocalChopAudioProcessor::getGenreBankSize (b);
            printf ("%-8s %3d instruments across %d roles\n",
                    banks[b].toRawUTF8(), size, roles.size());

            std::set<juce::String> seen;
            for (int r = 0; r < roles.size(); ++r)
            {
                const auto members =
                    VocalChopAudioProcessor::getGenreRoleInstruments (b, r);
                printf ("    %-18s %3d\n", roles[r].toRawUTF8(), members.size());

                for (const auto& m : members)
                {
                    if (names.indexOf (m) < 0)
                    { printf ("        NO SUCH INSTRUMENT: \"%s\"\n", m.toRawUTF8()); ++bad; }
                    // A duplicate inside one bank is a wasted slot: the bank
                    // claims 150 sounds and delivers fewer.
                    else if (! seen.insert (m).second)
                    { printf ("        DUPLICATE IN BANK: \"%s\"\n", m.toRawUTF8()); ++bad; }
                }
            }

            if (b < 3 && size != wanted[b])
            { printf ("    SIZE: asked for %d, bank holds %d\n", wanted[b], size); ++bad; }
            printf ("\n");
        }

        printf ("%d problem(s)\n", bad);
        return bad == 0 ? 0 : 1;
    }

    // "--keymap": which SLICE does each typing key select, in Chop mode?
    //
    // SCOPE, stated up front because two earlier versions of this test
    // overstated theirs. This walks the SELECTION chain end to end -
    //
    //     physical key -> keymap::semitoneFor -> diatonicSliceIndex -> % slices
    //
    // - calling the same functions the editor calls, on the real demo sample's
    // real slice count. That chain is where the "Q plays the last slice" bug
    // lived, so this is the thing worth pinning down, and it is deterministic,
    // so it pins down cleanly.
    //
    // It does NOT prove the audio engine then plays the slice it was handed.
    // Two attempts at that failed: comparing a key against triggerSlicePad is
    // circular (both entry points run through this same mapping, so the test
    // proves only that the mapping equals itself), and correlating the output
    // against the raw sample buffer fails because audio leaving processBlock
    // has been through an envelope, the filter and the master chain, and every
    // slice then correlates best with slice 0. Settling that half needs a hook
    // reporting which SlicePoint a voice started from. Not done; not claimed.
    if (argc > 1 && juce::String (argv[1]) == "--keymap")
    {
        if (! proc.loadDemoSample()) { printf ("no demo sample\n"); return 1; }
        if (auto* e = proc.getAPVTS().getParameter ("engine"))
            e->setValueNotifyingHost (e->convertTo0to1 (0.0f));       // Chop

        const int n = proc.getSliceEngine().getNumSlices();
        printf ("demo sample: %d slices\n\n", n);
        if (n <= 0) return 1;

        auto sliceFor = [n] (int keyIndex, bool chop)
        {
            const int semi = slyce::keymap::semitoneFor (keyIndex, chop);
            const int d    = VocalChopAudioProcessor::diatonicSliceIndex (semi);
            return ((d % n) + n) % n;
        };

        const int nk    = slyce::keymap::numKeys;
        const int split = slyce::keymap::topRowStart;

        printf ("%-5s %-6s %-9s %-9s\n", "key", "row", "chop", "melodic");
        printf ("%-5s %-6s %-9s %-9s\n", "---", "---", "----", "-------");
        for (int i = 0; i < nk; ++i)
            printf ("%-5c %-6s slice %-3d semi %-4d\n",
                    (char) slyce::keymap::keys[i],
                    i < split ? "lower" : "upper",
                    sliceFor (i, true),
                    slyce::keymap::semitoneFor (i, false));

        // The properties that matter, rather than a table nobody reads.
        int bad = 0;
        auto check = [&bad] (const char* what, bool ok)
        {
            printf ("%-58s %s\n", what, ok ? "ok" : "FAIL");
            if (! ok) ++bad;
        };
        printf ("\n");

        const int qIndex = slyce::keymap::keys.indexOfChar ('q');
        const int zIndex = slyce::keymap::keys.indexOfChar ('z');

        // The first key starts at slice zero. The upper row continues the pad
        // sequence instead of repeating the lower row, so samples with more
        // than 17 slices remain fully playable from the computer keyboard.
        check ("Chop: 'z' (lower row, first key) plays slice 0",
               zIndex >= 0 && sliceFor (zIndex, true) == 0);
        check ("Chop: 'q' continues at slice 17",
               qIndex >= 0 && sliceFor (qIndex, true) == (17 % n));

        std::set<int> reachable;
        for (int i = 0; i < nk; ++i) reachable.insert (sliceFor (i, true));
        check ("Chop: typing keys reach every available slice",
               n > nk || (int) reachable.size() == n);

        // Melodic layout is a piano and must stay one: the upper row sits
        // exactly an octave above the lower one.
        bool octave = true;
        for (int i = split; i < nk && octave; ++i)
        {
            const int lowerTwin = i - split;
            if (lowerTwin < split)
                octave = slyce::keymap::semitoneFor (i, false)
                       == slyce::keymap::semitoneFor (lowerTwin, false) + 12;
        }
        check ("Melodic: upper row is exactly one octave above the lower", octave);

        // ---- and now the half that used to be missing --------------------
        //
        // Press each key for real and read back where the voice ACTUALLY
        // started in the audio. proc.lastVoiceStartSample is written by
        // triggerSliceIndex at the moment the voice is handed its read
        // position - downstream of the whole mapping, upstream of the
        // envelope, filter and master chain that made the audio unusable as
        // evidence. Comparing it against the SliceEngine's own startSample for
        // the expected slice is not circular: one is what the engine was told
        // to play, the other is where the audio for that slice lives.
        printf ("\n%-5s %-8s %-12s %-12s\n", "key", "expect", "started at", "slice start");
        printf ("%-5s %-8s %-12s %-12s\n", "---", "------", "----------", "-----------");

        juce::AudioBuffer<float> blk (2, 512);
        juce::MidiBuffer mid;
        auto pump = [&] { blk.clear(); proc.processBlock (blk, mid); };
        for (int b = 0; b < 8; ++b) pump();          // settle

        int played = 0, wrongPlay = 0;
        for (int i = 0; i < nk; ++i)
        {
            const int expect = sliceFor (i, true);
            SlicePoint sp;
            if (! proc.getSliceEngine().tryGetSlice (expect, sp))
                continue;

            proc.lastVoiceStartSample.store (-1);
            proc.pressSlicePad (slyce::keymap::semitoneFor (i, true), 1.0f);
            pump();
            const int got = proc.lastVoiceStartSample.load();
            proc.releaseSlicePad (slyce::keymap::semitoneFor (i, true));
            pump();

            if (got < 0)
            { printf ("%-5c %-8d %-12s %-12d  <-- NO VOICE\n",
                      (char) slyce::keymap::keys[i], expect, "(none)", sp.startSample);
              ++wrongPlay; continue; }

            ++played;
            const bool ok = got == sp.startSample;
            if (! ok) ++wrongPlay;
            printf ("%-5c %-8d %-12d %-12d %s\n",
                    (char) slyce::keymap::keys[i], expect, got, sp.startSample,
                    ok ? "" : "  <-- PLAYED THE WRONG PART OF THE AUDIO");
        }

        printf ("\n%d keys triggered a voice, %d started from the wrong sample\n",
                played, wrongPlay);
        if (wrongPlay > 0) ++bad;

        printf ("\n%d property check(s) failed\n", bad);
        return bad == 0 ? 0 : 1;
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

    // "--knobs": make sure performance/level knobs do not accidentally turn
    // a playable instrument into silence. Filter extremes are intentionally
    // excluded here: a high-pass at 18 kHz or a closed low-pass is allowed to
    // remove most of a sound, and --filter verifies that path separately.
    if (argc > 1 && juce::String (argv[1]) == "--knobs")
    {
        auto set = [&] (const char* id, float v)
        {
            if (auto* pp = proc.getAPVTS().getParameter (id))
                pp->setValueNotifyingHost (pp->convertTo0to1 (v));
        };

        auto renderRms = [&] (int note, int blocks, int offBlock)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.95f), 0);
            double sq = 0.0; long n = 0; bool finite = true;

            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                proc.processBlock (buf, midi);
                midi.clear();
                if (b == offBlock)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);

                for (int i = 0; i < buf.getNumSamples(); ++i)
                    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    {
                        const float v = buf.getSample (ch, i);
                        finite = finite && std::isfinite (v);
                        sq += (double) v * v; ++n;
                    }
            }

            return finite ? std::sqrt (sq / (double) juce::jmax (1L, n)) : -1.0;
        };

        struct Case { const char* id; float value; double minRatio; int blocks; int offBlock; };
        const Case cases[] = {
            { "outputGain", -18.0f, 0.050, 120, 60 },
            { "outputGain",   6.0f, 0.500, 120, 60 },
            { "drive",        1.0f, 0.250, 120, 60 },
            { "width",        0.0f, 0.250, 120, 60 },
            { "width",        2.0f, 0.250, 120, 60 },
            { "mix",          0.0f, 0.250, 120, 60 },
            { "mix",          1.0f, 0.250, 120, 60 },
            { "attack",    1000.0f, 0.020, 260, 180 },
            { "sustain",      0.0f, 0.020, 160, 90 },
        };

        printf ("%-20s %-10s %8s %8s %8s  %s\n",
                "instrument", "case", "base", "rms", "ratio", "flags");
        int bad = 0, checked = 0;

        for (int idx = 0; idx < names.size(); ++idx)
        {
            proc.applyInstrument (idx);
            set ("outputGain", 0.0f);
            set ("drive", 0.0f);
            set ("width", 1.0f);
            set ("mix", 1.0f);
            const double base = renderRms (60, 120, 60);
            if (base <= 1.0e-5)
                continue; // --sweep owns truly silent instruments.

            for (const auto& c : cases)
            {
                proc.applyInstrument (idx);
                set ("outputGain", 0.0f);
                set ("drive", 0.0f);
                set ("width", 1.0f);
                set ("mix", 1.0f);
                set (c.id, c.value);

                const double rms = renderRms (60, c.blocks, c.offBlock);
                const double ratio = rms / base;
                juce::String flags;
                if (rms < 0.0) flags << "NAN ";
                if (rms >= 0.0 && ratio < c.minRatio) flags << "DROPOUT ";
                ++checked;
                if (flags.isNotEmpty())
                {
                    ++bad;
                    printf ("%-20s %-10s %8.4f %8.4f %8.3f  %s\n",
                            names[idx].toRawUTF8(),
                            (juce::String (c.id) + "=" + juce::String (c.value)).toRawUTF8(),
                            base, rms, ratio, flags.toRawUTF8());
                }
            }
        }

        printf ("\n%d knob case(s), %d dropout(s)\n", checked, bad);
        return bad == 0 ? 0 : 2;
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

        // THREE TAKES, MEDIAN. Voices start their unison bank at random phases
        // on purpose, and this metric is a ratio of two energies, so a single
        // take moves by several dB run to run. Identical builds measured 43,
        // 47 and 48 instruments over the line - which means every "before and
        // after" comparison made with a single take, including one I nearly
        // reported, was reading noise.
        constexpr int kTakes = 3;
        for (int idx = 0; idx < names.size(); ++idx)
        {
            double ratioT[kTakes] {}, decayT[kTakes] {};
            for (int take = 0; take < kTakes; ++take)
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
            ratioT[take] = ratio;
            decayT[take] = (double) last / 44100.0;
            }

            std::sort (ratioT, ratioT + kTakes);
            std::sort (decayT, decayT + kTakes);
            rows.push_back ({ names[idx],
                              juce::isPositiveAndBelow (idx, cats.size()) ? cats[idx] : juce::String(),
                              ratioT[kTakes / 2], decayT[kTakes / 2] });
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
