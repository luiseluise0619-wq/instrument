#include "SamplerEngine.h"

#include <functional>

namespace
{
    // One opcode token: name=value, where value runs until the next
    // " name=" boundary (sample paths may contain spaces).
    void parseOpcodes (const juce::String& line,
                       std::function<void (const juce::String&, const juce::String&)> cb)
    {
        int i = 0;
        const int len = line.length();
        while (i < len)
        {
            const int eq = line.indexOfChar (i, '=');
            if (eq < 0)
                break;

            // Opcode name = last word before '='.
            int nameStart = eq;
            while (nameStart > i && ! juce::CharacterFunctions::isWhitespace (line[nameStart - 1]))
                --nameStart;
            const auto name = line.substring (nameStart, eq).trim().toLowerCase();

            // Value runs to the start of the NEXT opcode (word directly
            // followed by '='), or to end of line.
            int valEnd = len;
            const int nextEq = line.indexOfChar (eq + 1, '=');
            if (nextEq >= 0)
            {
                int ws = nextEq;
                while (ws > eq + 1 && ! juce::CharacterFunctions::isWhitespace (line[ws - 1]))
                    --ws;
                valEnd = ws;
            }
            const auto value = line.substring (eq + 1, valEnd).trim();
            if (name.isNotEmpty())
                cb (name, value);
            i = valEnd;
        }
    }

    int noteNameToMidi (const juce::String& v)
    {
        // Numeric ("60") or names like c4, c#4, db3 (SFZ: C4 = 60).
        if (v.containsOnly ("0123456789-"))
            return v.getIntValue();

        const auto s = v.toLowerCase();
        static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
        if (s.isEmpty() || s[0] < 'a' || s[0] > 'g')
            return -1;
        int semi = base[s[0] - 'a'];
        int idx = 1;
        if (idx < s.length() && (s[idx] == '#' || s[idx] == 'b'))
        {
            semi += (s[idx] == '#') ? 1 : -1;
            ++idx;
        }
        const int octave = s.substring (idx).getIntValue();
        return juce::jlimit (0, 127, (octave + 1) * 12 + semi);
    }
} // namespace

bool SamplerEngine::loadSfz (const juce::File& sfzFile, juce::String& error)
{
    if (! sfzFile.existsAsFile())
    {
        error = "File not found: " + sfzFile.getFullPathName();
        return false;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();   // wav, aiff, flac, ogg

    auto newBank = std::make_shared<Bank>();
    newBank->name = sfzFile.getFileNameWithoutExtension();

    juce::File defaultPath = sfzFile.getParentDirectory();
    Region groupDefaults, current;
    bool inRegion = false;
    int64_t totalBytes = 0;
    constexpr int64_t kMaxBytes = (int64_t) 1600 * 1024 * 1024;   // 1.6 GB decoded cap

    juce::StringArray lines;
    lines.addLines (sfzFile.loadFileAsString());

    juce::String pendingSample;   // path of the region being built
    juce::String groupSample;

    auto applyOpcode = [&] (Region& r, juce::String& samplePath,
                            const juce::String& name, const juce::String& value)
    {
        if      (name == "sample")          samplePath = value;
        else if (name == "default_path")    defaultPath = sfzFile.getParentDirectory()
                                                              .getChildFile (value.replaceCharacter ('\\', '/'));
        else if (name == "lokey")           r.lokey = noteNameToMidi (value);
        else if (name == "hikey")           r.hikey = noteNameToMidi (value);
        else if (name == "key")             { r.lokey = r.hikey = r.root = noteNameToMidi (value); }
        else if (name == "pitch_keycenter") r.root  = noteNameToMidi (value);
        else if (name == "lovel")           r.lovel = value.getIntValue();
        else if (name == "hivel")           r.hivel = value.getIntValue();
        else if (name == "tune")            r.tuneCents = value.getFloatValue();
        else if (name == "volume")          r.volumeDb  = value.getFloatValue();
        else if (name == "loop_mode")       r.loop = value.trim().startsWithIgnoreCase ("loop");
        else if (name == "loop_start")      r.loopStart = value.getIntValue();
        else if (name == "loop_end")        r.loopEnd   = value.getIntValue();
        else if (name == "ampeg_release")   r.releaseSeconds = juce::jlimit (0.01f, 8.0f,
                                                                             value.getFloatValue());
    };

    auto finishRegion = [&]() -> bool
    {
        if (! inRegion)
            return true;
        inRegion = false;

        auto path = pendingSample.isNotEmpty() ? pendingSample : groupSample;
        if (path.isEmpty())
            return true;   // header-only region: ignore

        const auto audioFile = defaultPath.getChildFile (path.replaceCharacter ('\\', '/'));
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (audioFile));
        if (reader == nullptr)
        {
            error = "Can't read sample: " + audioFile.getFullPathName();
            return false;
        }

        Region r = current;
        const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);
        totalBytes += (int64_t) reader->lengthInSamples * numCh * (int64_t) sizeof (float);
        if (totalBytes > kMaxBytes)
        {
            error = "This bank decodes to more than 1.6 GB of RAM - use a "
                    "smaller instrument set.";
            return false;
        }

        r.data.setSize (numCh, (int) reader->lengthInSamples);
        reader->read (&r.data, 0, (int) reader->lengthInSamples, 0, true, numCh > 1);
        r.srcRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
        if (r.loopEnd <= r.loopStart || r.loopEnd > r.data.getNumSamples())
            r.loop = false;

        newBank->regions.push_back (std::move (r));
        return true;
    };

    for (auto rawLine : lines)
    {
        auto line = rawLine.upToFirstOccurrenceOf ("//", false, false).trim();
        if (line.isEmpty())
            continue;

        // Headers and opcodes can share a line; walk header by header.
        while (line.isNotEmpty())
        {
            if (line.startsWithIgnoreCase ("<region>"))
            {
                if (! finishRegion())
                    return false;
                inRegion = true;
                current = groupDefaults;
                pendingSample = {};
                line = line.substring (8).trim();
                continue;
            }
            if (line.startsWithIgnoreCase ("<group>"))
            {
                if (! finishRegion())
                    return false;
                groupDefaults = {};
                groupSample = {};
                line = line.substring (7).trim();
                // Opcodes after <group> configure the group defaults.
                parseOpcodes (line.upToFirstOccurrenceOf ("<", false, false),
                              [&] (const juce::String& n, const juce::String& v)
                              { applyOpcode (groupDefaults, groupSample, n, v); });
                const int next = line.indexOfChar ('<');
                line = next >= 0 ? line.substring (next) : juce::String();
                continue;
            }
            if (line.startsWith ("<"))   // <control>, <global>, <curve>...
            {
                const auto body = line.fromFirstOccurrenceOf (">", false, false);
                parseOpcodes (body.upToFirstOccurrenceOf ("<", false, false),
                              [&] (const juce::String& n, const juce::String& v)
                              {
                                  if (n == "default_path")
                                      applyOpcode (groupDefaults, groupSample, n, v);
                              });
                const int next = body.indexOfChar ('<');
                line = next >= 0 ? body.substring (next) : juce::String();
                continue;
            }

            // Plain opcode line for the current region/group.
            parseOpcodes (line, [&] (const juce::String& n, const juce::String& v)
            {
                if (inRegion) applyOpcode (current, pendingSample, n, v);
                else          applyOpcode (groupDefaults, groupSample, n, v);
            });
            break;
        }
    }
    if (! finishRegion())
        return false;

    if (newBank->regions.empty())
    {
        error = "No playable <region> entries found in this .sfz.";
        return false;
    }

    std::atomic_store (&bank, std::shared_ptr<const Bank> (newBank));
    return true;
}

const SamplerEngine::Region* SamplerEngine::findRegion (const Bank& b, int note, int vel127) const
{
    const Region* best = nullptr;
    for (const auto& r : b.regions)
        if (note >= r.lokey && note <= r.hikey && vel127 >= r.lovel && vel127 <= r.hivel)
        {
            best = &r;
            break;
        }
    if (best == nullptr)   // fall back to nearest key range, any velocity
    {
        int bestDist = 1 << 20;
        for (const auto& r : b.regions)
        {
            const int d = note < r.lokey ? r.lokey - note
                        : note > r.hikey ? note - r.hikey : 0;
            if (d < bestDist)
            {
                bestDist = d;
                best = &r;
            }
        }
    }
    return best;
}

SamplerEngine::Voice* SamplerEngine::findFreeVoice()
{
    for (auto& v : voices)
        if (! v.active())
            return &v;

    // Steal the quietest.
    Voice* steal = &voices[0];
    for (auto& v : voices)
        if (v.env * v.gain < steal->env * steal->gain)
            steal = &v;
    return steal;
}

void SamplerEngine::noteOn (int midiNote, float velocity)
{
    auto b = std::atomic_load (&bank);
    if (b == nullptr)
        return;

    const int vel127 = juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f));
    const auto* r = findRegion (*b, midiNote, vel127);
    if (r == nullptr)
        return;

    auto& v = *findFreeVoice();
    v.hold      = b;
    v.region    = r;
    v.pos       = 0.0;
    v.note      = midiNote;
    v.releasing = false;
    v.env       = 1.0f;
    v.fadeIn    = 64;
    v.relCoeff  = std::exp (-1.0f / (float) (juce::jmax (0.01f, r->releaseSeconds) * sr));
    v.gain      = velocity * juce::Decibels::decibelsToGain (r->volumeDb);
    v.ratio     = std::pow (2.0, (midiNote - r->root + r->tuneCents / 100.0) / 12.0)
                  * (r->srcRate / sr);
}

void SamplerEngine::noteOff (int midiNote)
{
    for (auto& v : voices)
        if (v.active() && v.note == midiNote)
            v.releasing = true;
}

void SamplerEngine::releaseAll()
{
    for (auto& v : voices)
        v.releasing = true;
}

void SamplerEngine::render (juce::AudioBuffer<float>& out, int numSamples)
{
    const int numCh = juce::jmin (2, out.getNumChannels());
    if (numCh == 0)
        return;

    for (auto& v : voices)
    {
        if (! v.active())
            continue;

        const auto& d = v.region->data;
        const int srcCh  = d.getNumChannels();
        const int srcLen = d.getNumSamples();

        for (int n = 0; n < numSamples; ++n)
        {
            int i0 = (int) v.pos;
            if (v.region->loop && i0 >= v.region->loopEnd - 1)
            {
                v.pos -= (double) (v.region->loopEnd - v.region->loopStart);
                i0 = (int) v.pos;
            }
            if (i0 >= srcLen - 1 || v.env < 1.0e-4f)
            {
                v.region = nullptr;
                v.hold.reset();
                break;
            }

            const float frac = (float) (v.pos - (double) i0);
            float g = v.gain * v.env;
            if (v.fadeIn > 0)
            {
                g *= 1.0f - (float) v.fadeIn / 64.0f;
                --v.fadeIn;
            }
            if (v.releasing)
                v.env *= v.relCoeff;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* src = d.getReadPointer (juce::jmin (ch, srcCh - 1));
                const float s = src[i0] + frac * (src[i0 + 1] - src[i0]);
                out.addSample (ch, n, s * g);
            }
            v.pos += v.ratio;
        }
    }
}
