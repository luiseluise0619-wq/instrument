#include "PluginProcessor.h"
#include <cstdio>
#include <vector>

namespace
{
constexpr double sr = 44100.0;
constexpr int block = 512;

struct Event { double sec; juce::MidiMessage msg; };

void addNote (std::vector<Event>& e, double at, double length, int note, float velocity)
{
    e.push_back ({ at, juce::MidiMessage::noteOn (1, note, velocity) });
    e.push_back ({ at + length, juce::MidiMessage::noteOff (1, note) });
}

void addChord (std::vector<Event>& e, double at, double length,
               std::initializer_list<int> notes, float velocity)
{
    for (int n : notes) addNote (e, at, length, n, velocity);
}

std::vector<Event> score()
{
    std::vector<Event> e;
    // Root and the requested +/-3, +/-7 checks at two velocities.
    double t = 0.25;
    for (int n : { 53, 57, 60, 63, 67, 72 }) { addNote (e, t, .62, n, n < 60 ? .42f : .86f); t += .78; }
    // Short repeated melody: catches held-note/retrigger and release faults.
    for (int n : { 60, 63, 67, 63, 65, 68, 72, 67 }) { addNote (e, t, .26, n, .72f); t += .34; }
    t += .25;
    addChord (e, t, 1.65, { 60, 64, 67, 71 }, .68f); t += 1.95; // Cmaj7
    addChord (e, t, 1.65, { 57, 60, 64, 71 }, .58f); t += 1.95; // Am9
    addChord (e, t, 1.65, { 50, 53, 57, 60, 64 }, .76f); t += 1.95; // Dm9
    // Sustain transition Cmaj7 -> Am9.
    e.push_back ({ t, juce::MidiMessage::controllerEvent (1, 64, 127) });
    addChord (e, t + .05, 1.0, { 60, 64, 67, 71 }, .55f);
    addChord (e, t + 1.15, 1.0, { 57, 60, 64, 71 }, .63f);
    e.push_back ({ t + 2.35, juce::MidiMessage::controllerEvent (1, 64, 0) });
    // More than 24 voices verifies deterministic voice stealing and finite output.
    t += 2.8;
    for (int n = 36; n < 66; ++n) addNote (e, t + (n - 36) * .008, .55, n, .35f);
    e.push_back ({ t + 1.0, juce::MidiMessage::allSoundOff (1) });
    return e;
}

void setParam (VocalChopAudioProcessor& p, const char* id, float value)
{
    if (auto* x = p.getAPVTS().getParameter (id))
        x->setValueNotifyingHost (x->convertTo0to1 (value));
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    file.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    if (! stream) return false;
    juce::WavAudioFormat fmt;
    auto writer = std::unique_ptr<juce::AudioFormatWriter> (
        fmt.createWriterFor (stream.release(), sr, (unsigned) audio.getNumChannels(), 24, {}, 0));
    return writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : "air-vocal-demo");
    if (! outDir.createDirectory()) return 2;
    const auto events = score();
    const double seconds = 20.0;
    const int total = (int) (seconds * sr);
    const auto names = VocalChopAudioProcessor::getAirVocalPresetNames();

    // One MIDI example containing exactly the notes rendered below.
    juce::MidiMessageSequence sequence;
    for (const auto& e : events)
    {
        auto m = e.msg; m.setTimeStamp (e.sec * 960.0 * 120.0 / 60.0);
        sequence.addEvent (m);
    }
    sequence.updateMatchedPairs();
    juce::MidiFile midi; midi.setTicksPerQuarterNote (960); midi.addTrack (sequence);
    auto midiStream = std::unique_ptr<juce::FileOutputStream> (
        outDir.getChildFile ("AIR_VOCAL_performance.mid").createOutputStream());
    if (! midiStream || ! midi.writeTo (*midiStream)) return 3;

    for (int preset = 0; preset < names.size(); ++preset)
    {
        VocalChopAudioProcessor p;
        p.prepareToPlay (sr, block);
        p.applyAirVocalPreset (preset);
        // Keep the demo's first half relatively dry so CORE quality is audible;
        // HALO still follows the preset through its dedicated SPACE macro.
        setParam (p, "delay", 0.0f);
        setParam (p, "outputGain", -5.0f);

        juce::AudioBuffer<float> audio (2, total); audio.clear();
        double sumSq = 0.0, monoSq = 0.0; float peak = 0.0f; bool finite = true;
        size_t event = 0;
        for (int pos = 0; pos < total; pos += block)
        {
            const int count = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> b (2, count); b.clear();
            juce::MidiBuffer mb;
            while (event < events.size() && events[event].sec * sr < pos + count)
            {
                const int at = juce::jlimit (0, count - 1, (int) (events[event].sec * sr) - pos);
                mb.addEvent (events[event].msg, at); ++event;
            }
            p.processBlock (b, mb);
            for (int ch = 0; ch < 2; ++ch) audio.copyFrom (ch, pos, b, ch, 0, count);
            for (int n = 0; n < count; ++n)
            {
                const float l = b.getSample (0, n), r = b.getSample (1, n);
                finite = finite && std::isfinite (l) && std::isfinite (r);
                peak = juce::jmax (peak, std::abs (l), std::abs (r));
                sumSq += l*l + r*r; const double m = .5 * (l+r); monoSq += m*m;
            }
        }
        const auto safe = names[preset].removeCharacters (" []").replaceCharacter ('/', '-');
        const auto file = outDir.getChildFile (juce::String (preset + 1).paddedLeft ('0', 2) + "_" + safe + ".wav");
        const bool wrote = writeWav (file, audio);
        if (! finite || peak < 0.0001f || ! wrote)
        {
            std::printf ("FAILED preset %d finite=%d peak=%g wrote=%d path=%s\n",
                         preset + 1, finite ? 1 : 0, peak, wrote ? 1 : 0,
                         file.getFullPathName().toRawUTF8());
            return 10 + preset;
        }
        const double rms = std::sqrt (sumSq / (double) (total * 2));
        const double monoRms = std::sqrt (monoSq / (double) total);
        std::printf ("%d %-24s peak %.4f rms %.5f mono %.5f voices %d\n",
                     preset + 1, names[preset].toRawUTF8(), peak, rms, monoRms,
                     p.getAirVocal().getActiveVoiceCount());
    }
    return 0;
}
