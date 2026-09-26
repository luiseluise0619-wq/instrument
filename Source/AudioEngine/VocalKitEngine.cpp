#include "VocalKitEngine.h"
#include "SampleLoader.h"
#include "SampleStateCodec.h"

#include <algorithm>
#include <cmath>

namespace
{
float sampleLinear (const juce::AudioBuffer<float>& b, int channel, double p) noexcept
{
    const int n = b.getNumSamples();
    if (n <= 0) return 0.0f;
    p = juce::jlimit (0.0, (double) (n - 1), p);
    const int a = (int) p;
    const int z = juce::jmin (n - 1, a + 1);
    const float f = (float) (p - a);
    const float x = b.getSample (juce::jmin (channel, b.getNumChannels() - 1), a);
    return x + (b.getSample (juce::jmin (channel, b.getNumChannels() - 1), z) - x) * f;
}
}

VocalKitEngine::SlotSettings VocalKitEngine::sanitise (SlotSettings s, int slot)
{
    s.midiNote = juce::jlimit (0, 127, s.midiNote <= 0 ? kRootNote + slot : s.midiNote);
    s.start = juce::jlimit (0.0f, 0.9999f, s.start);
    s.end = juce::jlimit (s.start + 0.0001f, 1.0f, s.end);
    s.loopStart = juce::jlimit (s.start, s.end, s.loopStart);
    s.loopEnd = juce::jlimit (s.loopStart, s.end, s.loopEnd);
    s.pitchSemitones = juce::jlimit (-24.0f, 24.0f, s.pitchSemitones);
    s.fineCents = juce::jlimit (-100.0f, 100.0f, s.fineCents);
    s.formantSemitones = juce::jlimit (-12.0f, 12.0f, s.formantSemitones);
    s.timeStretch = juce::jlimit (0.25f, 4.0f, s.timeStretch);
    s.attackMs = juce::jlimit (0.0f, 2000.0f, s.attackMs);
    s.decayMs = juce::jlimit (0.0f, 8000.0f, s.decayMs);
    s.sustain = juce::jlimit (0.0f, 1.0f, s.sustain);
    s.releaseMs = juce::jlimit (1.0f, 8000.0f, s.releaseMs);
    s.gainDb = juce::jlimit (-60.0f, 18.0f, s.gainDb);
    s.pan = juce::jlimit (-1.0f, 1.0f, s.pan);
    s.filterHz = juce::jlimit (40.0f, 20000.0f, s.filterHz);
    s.delaySend = juce::jlimit (0.0f, 1.0f, s.delaySend);
    s.reverbSend = juce::jlimit (0.0f, 1.0f, s.reverbSend);
    s.velocityAmount = juce::jlimit (0.0f, 1.0f, s.velocityAmount);
    s.loopCrossfadeMs = juce::jlimit (0.0f, 100.0f, s.loopCrossfadeMs);
    s.chokeGroup = juce::jlimit (0, 16, s.chokeGroup);
    return s;
}

void VocalKitEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    blockCapacity = juce::jmax (1, maxBlockSize);
    delaySendScratch.setSize (2, blockCapacity, false, false, true);
    reverbSendScratch.setSize (2, blockCapacity, false, false, true);
    delayLineL.assign ((size_t) juce::jmax (1, (int) (sr * 2.0)), 0.0f);
    delayLineR.assign (delayLineL.size(), 0.0f);
    reverbLineL.assign ((size_t) juce::jmax (1, (int) (sr * 0.071)), 0.0f);
    reverbLineR.assign ((size_t) juce::jmax (1, (int) (sr * 0.089)), 0.0f);
    delayPos = reverbPos = 0;
    for (auto& p : playheads) p.store (-1.0f, std::memory_order_relaxed);
    releaseAll();
}

void VocalKitEngine::publish (std::shared_ptr<const Bank> next)
{
    const juce::ScopedLock lock (retireLock);
    auto old = std::atomic_exchange (&bank, std::move (next));
    if (old) retiredBanks.push_back (std::move (old));
    retiredBanks.erase (std::remove_if (retiredBanks.begin(), retiredBanks.end(),
        [] (const auto& b) { return b.use_count() == 1; }), retiredBanks.end());
}

std::shared_ptr<VocalKitEngine::Bank> VocalKitEngine::mutableCopy() const
{
    auto current = std::atomic_load (&bank);
    return current ? std::make_shared<Bank> (*current) : std::make_shared<Bank>();
}

bool VocalKitEngine::setSlotFromBuffer (int index,
                                        std::shared_ptr<juce::AudioBuffer<float>> buffer,
                                        double sourceRate, const juce::String& name,
                                        const juce::String& sourcePath)
{
    if (! juce::isPositiveAndBelow (index, kNumSlots) || ! buffer
        || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0)
        return false;
    auto next = mutableCopy();
    auto& slot = next->slots[(size_t) index];
    slot.sample = std::move (buffer);
    slot.sourceRate = sourceRate > 0.0 ? sourceRate : 44100.0;
    slot.settings.name = name.isNotEmpty() ? name : "Slot " + juce::String (index + 1);
    slot.settings.sourcePath = sourcePath;
    slot.settings.midiNote = kRootNote + index;
    slot.settings.missing = false;
    slot.settings = sanitise (slot.settings, index);
    publish (next);
    return true;
}

bool VocalKitEngine::loadSlotFromFile (int slot, const juce::File& file, juce::String& error)
{
    double rate = 44100.0;
    auto decoded = SampleLoader::decode (file, rate);
    if (! decoded)
    {
        error = "Unsupported or unreadable audio file: " + file.getFullPathName();
        return false;
    }
    return setSlotFromBuffer (slot, std::move (decoded), rate,
                              file.getFileNameWithoutExtension(), file.getFullPathName());
}

bool VocalKitEngine::relinkMissingSlot (int slot, const juce::File& file, juce::String& error)
{
    const auto old = getSlotSettings (slot);
    if (! loadSlotFromFile (slot, file, error)) return false;
    auto settings = old;
    settings.sourcePath = file.getFullPathName();
    settings.name = file.getFileNameWithoutExtension();
    settings.missing = false;
    setSlotSettings (slot, settings);
    return true;
}

void VocalKitEngine::clearSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return;
    auto next = mutableCopy();
    next->slots[(size_t) slot] = {};
    next->slots[(size_t) slot].settings.midiNote = kRootNote + slot;
    publish (next);
}

void VocalKitEngine::clear()
{
    auto next = std::make_shared<Bank>();
    for (int i = 0; i < kNumSlots; ++i) next->slots[(size_t) i].settings.midiNote = kRootNote + i;
    publish (next);
    releaseAll();
}

VocalKitEngine::SlotSettings VocalKitEngine::getSlotSettings (int slot) const
{
    auto current = std::atomic_load (&bank);
    if (! current || ! juce::isPositiveAndBelow (slot, kNumSlots)) return {};
    return current->slots[(size_t) slot].settings;
}

void VocalKitEngine::setSlotSettings (int slot, const SlotSettings& value)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return;
    auto next = mutableCopy();
    next->slots[(size_t) slot].settings = sanitise (value, slot);
    publish (next);
}

std::shared_ptr<const juce::AudioBuffer<float>> VocalKitEngine::getSlotSample (int slot) const
{
    auto current = std::atomic_load (&bank);
    if (! current || ! juce::isPositiveAndBelow (slot, kNumSlots)) return {};
    return current->slots[(size_t) slot].sample;
}

bool VocalKitEngine::hasSlot (int slot) const { return getSlotSample (slot) != nullptr; }

juce::StringArray VocalKitEngine::getMissingFiles() const
{
    juce::StringArray result;
    auto current = std::atomic_load (&bank);
    if (! current) return result;
    for (const auto& slot : current->slots)
        if (slot.settings.missing && slot.settings.sourcePath.isNotEmpty())
            result.addIfNotAlreadyThere (slot.settings.sourcePath);
    return result;
}

VocalKitEngine::Voice* VocalKitEngine::freeVoice() noexcept
{
    for (auto& voice : voices) if (! voice.active()) return &voice;
    return &*std::min_element (voices.begin(), voices.end(),
        [] (const Voice& a, const Voice& b) { return a.env < b.env; });
}

void VocalKitEngine::noteOn (int note, float velocity, bool tap) noexcept
{
    auto current = std::atomic_load (&bank);
    if (! current) return;
    int index = -1;
    for (int i = 0; i < kNumSlots; ++i)
        if (current->slots[(size_t) i].settings.midiNote == note) { index = i; break; }
    if (index < 0) return;
    const auto& slot = current->slots[(size_t) index];
    if (! slot.sample || slot.settings.missing) return;

    const auto& s = slot.settings;

    // Vocal chop slots are independent playable voices.  Do not choke the
    // entire kit when another key arrives: holding two pads/keys must allow
    // both chops to sound together, just like a normal mapped instrument.
    // Note-off still releases only the matching MIDI note below.

    auto* voice = freeVoice();
    *voice = {};
    voice->hold = std::move (current);
    voice->slot = &voice->hold->slots[(size_t) index];
    voice->note = note;
    const int length = slot.sample->getNumSamples();
    const double first = s.start * (double) juce::jmax (0, length - 1);
    const double last = s.end * (double) juce::jmax (0, length - 1);
    voice->position = s.reverse ? last : first;
    const double pitch = (double) s.pitchSemitones + (double) s.fineCents / 100.0;
    const double rate = slot.sourceRate / sr;
    // timeStretch is a duration ratio: 2.0 takes twice as long. This compact
    // realtime path is varispeed; the editor describes it as SPEED until a
    // per-voice spectral stretcher is introduced.
    voice->step = rate * std::pow (2.0, pitch / 12.0) / (double) s.timeStretch;
    const float vel = juce::jlimit (0.0f, 1.0f, velocity);
    voice->gain = juce::Decibels::decibelsToGain (s.gainDb)
                * ((1.0f - s.velocityAmount) + s.velocityAmount * vel);
    const float angle = (s.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    voice->panL = std::cos (angle); voice->panR = std::sin (angle);
    voice->env = s.attackMs <= 0.01f ? 1.0f : 0.0f;
    voice->attackStep = s.attackMs <= 0.01f ? 1.0f : 1000.0f / (s.attackMs * (float) sr);
    voice->sustain = s.sustain;
    voice->decayStep = s.decayMs <= 0.01f ? 1.0f
                                          : (1.0f - s.sustain) * 1000.0f / (s.decayMs * (float) sr);
    voice->envelopeStage = s.attackMs <= 0.01f ? (s.decayMs <= 0.01f ? 2 : 1) : 0;
    voice->releaseCoeff = std::exp (-1.0f / juce::jmax (1.0f, s.releaseMs * 0.001f * (float) sr));
    const float velocityBrightness = 0.45f + 0.55f * vel;
    const float formantColour = std::pow (2.0f, s.formantSemitones / 24.0f);
    const float cutoff = juce::jlimit (40.0f, 20000.0f, s.filterHz * velocityBrightness * formantColour);
    voice->filterCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr);
    if (tap && ! s.oneShot) voice->releasing = true;
    playheads[(size_t) index].store ((float) (voice->position / juce::jmax (1, length - 1)),
                                    std::memory_order_relaxed);
    lastTriggeredSlot.store (index, std::memory_order_release);
}

void VocalKitEngine::noteOff (int note) noexcept
{
    for (auto& voice : voices)
        if (voice.active() && voice.note == note && ! voice.slot->settings.oneShot)
            voice.releasing = true;
}

void VocalKitEngine::releaseAll() noexcept
{
    for (auto& voice : voices) if (voice.active()) { voice.releasing = true; voice.envelopeStage = 3; }
}

void VocalKitEngine::stopAll() noexcept
{
    for (auto& voice : voices) voice.stop();
    for (auto& p : playheads) p.store (-1.0f, std::memory_order_relaxed);
}

void VocalKitEngine::render (juce::AudioBuffer<float>& out, int numSamples) noexcept
{
    if (numSamples <= 0 || out.getNumChannels() <= 0) return;
    const int count = juce::jmin (numSamples, blockCapacity);
    delaySendScratch.clear(); reverbSendScratch.clear();

    for (auto& voice : voices)
    {
        if (! voice.active()) continue;
        const auto& slot = *voice.slot;
        const auto& s = slot.settings;
        const auto& sample = *slot.sample;
        const int slotIndex = (int) (voice.slot - voice.hold->slots.data());
        const double total = (double) sample.getNumSamples();
        const double first = s.start * (total - 1.0), last = s.end * (total - 1.0);
        const double loopFirst = s.loopStart * (total - 1.0), loopLast = s.loopEnd * (total - 1.0);
        const double direction = s.reverse ? -1.0 : 1.0;
        const double crossfade = juce::jmin ((loopLast - loopFirst) * 0.45,
                                             s.loopCrossfadeMs * 0.001 * slot.sourceRate);

        for (int n = 0; n < count; ++n)
        {
            const double boundary = s.loop ? (s.reverse ? loopFirst : loopLast)
                                           : (s.reverse ? first : last);
            const bool past = s.reverse ? voice.position <= boundary : voice.position >= boundary;
            if (past)
            {
                if (s.loop && loopLast > loopFirst + 2.0)
                    voice.position = s.reverse ? loopLast : loopFirst;
                else { voice.stop(); break; }
            }
            if (voice.releasing || voice.envelopeStage == 3)
            {
                voice.envelopeStage = 3;
                voice.env *= voice.releaseCoeff;
            }
            else if (voice.envelopeStage == 0)
            {
                voice.env = juce::jmin (1.0f, voice.env + voice.attackStep);
                if (voice.env >= 1.0f) voice.envelopeStage = voice.decayStep >= 1.0f ? 2 : 1;
            }
            else if (voice.envelopeStage == 1)
            {
                voice.env = juce::jmax (voice.sustain, voice.env - voice.decayStep);
                if (voice.env <= voice.sustain) voice.envelopeStage = 2;
            }
            if (voice.env < 0.00005f) { voice.stop(); break; }

            float l = sampleLinear (sample, 0, voice.position);
            float r = sampleLinear (sample, sample.getNumChannels() > 1 ? 1 : 0, voice.position);
            if (s.loop && crossfade > 1.0)
            {
                const double distance = s.reverse ? voice.position - loopFirst
                                                  : loopLast - voice.position;
                if (distance >= 0.0 && distance < crossfade)
                {
                    const float t = (float) (1.0 - distance / crossfade);
                    const double wrapPos = s.reverse ? loopLast - (crossfade - distance)
                                                     : loopFirst + (crossfade - distance);
                    l = l * (1.0f - t) + sampleLinear (sample, 0, wrapPos) * t;
                    r = r * (1.0f - t) + sampleLinear (sample, sample.getNumChannels() > 1 ? 1 : 0, wrapPos) * t;
                }
            }
            voice.filterStateL += voice.filterCoeff * (l - voice.filterStateL);
            voice.filterStateR += voice.filterCoeff * (r - voice.filterStateR);
            l = voice.filterStateL * voice.gain * voice.env * voice.panL;
            r = voice.filterStateR * voice.gain * voice.env * voice.panR;
            out.addSample (0, n, l);
            if (out.getNumChannels() > 1) out.addSample (1, n, r);
            delaySendScratch.addSample (0, n, l * s.delaySend);
            delaySendScratch.addSample (1, n, r * s.delaySend);
            reverbSendScratch.addSample (0, n, l * s.reverbSend);
            reverbSendScratch.addSample (1, n, r * s.reverbSend);
            voice.position += direction * voice.step;
        }
        if (juce::isPositiveAndBelow (slotIndex, kNumSlots))
            playheads[(size_t) slotIndex].store (voice.active()
                ? (float) (voice.position / juce::jmax (1.0, total - 1.0)) : -1.0f,
                std::memory_order_relaxed);
    }
    renderSends (out, count);
}

void VocalKitEngine::renderSends (juce::AudioBuffer<float>& out, int count) noexcept
{
    if (delayLineL.empty() || reverbLineL.empty()) return;
    const int delaySamples = juce::jlimit (1, (int) delayLineL.size() - 1, (int) (sr * 0.375));
    for (int n = 0; n < count; ++n)
    {
        int read = delayPos - delaySamples; if (read < 0) read += (int) delayLineL.size();
        const float dl = delayLineL[(size_t) read], dr = delayLineR[(size_t) read];
        delayLineL[(size_t) delayPos] = delaySendScratch.getSample (0, n) + dr * 0.38f;
        delayLineR[(size_t) delayPos] = delaySendScratch.getSample (1, n) + dl * 0.38f;
        if (++delayPos >= (int) delayLineL.size()) delayPos = 0;

        const int riL = reverbPos % (int) reverbLineL.size();
        const int riR = reverbPos % (int) reverbLineR.size();
        const float rvL = reverbLineL[(size_t) riL], rvR = reverbLineR[(size_t) riR];
        reverbLineL[(size_t) riL] = reverbSendScratch.getSample (0, n) + rvR * 0.73f;
        reverbLineR[(size_t) riR] = reverbSendScratch.getSample (1, n) + rvL * 0.71f;
        ++reverbPos; if (reverbPos > 0x3fffffff) reverbPos = 0;
        out.addSample (0, n, dl * 0.55f + rvL * 0.45f);
        if (out.getNumChannels() > 1) out.addSample (1, n, dr * 0.55f + rvR * 0.45f);
    }
}

void VocalKitEngine::writeState (juce::XmlElement& parent) const
{
    auto current = std::atomic_load (&bank);
    if (! current) return;
    auto* kit = parent.createNewChildElement ("VocalKit");
    kit->setAttribute ("version", 1);
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& slot = current->slots[(size_t) i];
        const auto& s = slot.settings;
        if (! slot.sample && s.sourcePath.isEmpty()) continue;
        auto* x = kit->createNewChildElement ("Slot");
        x->setAttribute ("index", i); x->setAttribute ("name", s.name);
        x->setAttribute ("path", s.sourcePath); x->setAttribute ("note", s.midiNote);
        x->setAttribute ("start", s.start); x->setAttribute ("end", s.end);
        x->setAttribute ("loopStart", s.loopStart); x->setAttribute ("loopEnd", s.loopEnd);
        x->setAttribute ("pitch", s.pitchSemitones); x->setAttribute ("fine", s.fineCents);
        x->setAttribute ("formant", s.formantSemitones); x->setAttribute ("stretch", s.timeStretch);
        x->setAttribute ("attack", s.attackMs); x->setAttribute ("decay", s.decayMs);
        x->setAttribute ("sustain", s.sustain); x->setAttribute ("release", s.releaseMs);
        x->setAttribute ("gain", s.gainDb); x->setAttribute ("pan", s.pan);
        x->setAttribute ("filter", s.filterHz); x->setAttribute ("delay", s.delaySend);
        x->setAttribute ("reverb", s.reverbSend); x->setAttribute ("velocity", s.velocityAmount);
        x->setAttribute ("crossfade", s.loopCrossfadeMs); x->setAttribute ("choke", s.chokeGroup);
        x->setAttribute ("reverse", s.reverse); x->setAttribute ("oneShot", s.oneShot);
        x->setAttribute ("loop", s.loop);
        // Factory/edited material has no stable external path. Embed only
        // those buffers; user files remain lightweight path references and
        // can be explicitly relinked if moved.
        if (slot.sample && s.sourcePath.isEmpty())
        {
            auto bytes = slyce::SampleStateCodec::encode (*slot.sample, slot.sourceRate);
            if (! bytes.empty())
                x->createNewChildElement ("PCM")->addTextElement (
                    juce::MemoryBlock (bytes.data(), bytes.size()).toBase64Encoding());
        }
    }
}

void VocalKitEngine::restoreState (const juce::XmlElement& parent)
{
    auto next = std::make_shared<Bank>();
    for (int i = 0; i < kNumSlots; ++i) next->slots[(size_t) i].settings.midiNote = kRootNote + i;
    auto* kit = parent.getChildByName ("VocalKit");
    if (! kit) { publish (next); return; }
    for (auto* x = kit->getFirstChildElement(); x; x = x->getNextElement())
    {
        if (! x->hasTagName ("Slot")) continue;
        const int i = x->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (i, kNumSlots)) continue;
        auto& slot = next->slots[(size_t) i];
        auto& s = slot.settings;
        s.name=x->getStringAttribute("name"); s.sourcePath=x->getStringAttribute("path");
        s.midiNote=x->getIntAttribute("note",kRootNote+i);
        s.start=(float)x->getDoubleAttribute("start",0); s.end=(float)x->getDoubleAttribute("end",1);
        s.loopStart=(float)x->getDoubleAttribute("loopStart",0); s.loopEnd=(float)x->getDoubleAttribute("loopEnd",1);
        s.pitchSemitones=(float)x->getDoubleAttribute("pitch",0); s.fineCents=(float)x->getDoubleAttribute("fine",0);
        s.formantSemitones=(float)x->getDoubleAttribute("formant",0); s.timeStretch=(float)x->getDoubleAttribute("stretch",1);
        s.attackMs=(float)x->getDoubleAttribute("attack",3); s.decayMs=(float)x->getDoubleAttribute("decay",0);
        s.sustain=(float)x->getDoubleAttribute("sustain",1); s.releaseMs=(float)x->getDoubleAttribute("release",80);
        s.gainDb=(float)x->getDoubleAttribute("gain",0); s.pan=(float)x->getDoubleAttribute("pan",0);
        s.filterHz=(float)x->getDoubleAttribute("filter",20000); s.delaySend=(float)x->getDoubleAttribute("delay",0);
        s.reverbSend=(float)x->getDoubleAttribute("reverb",0); s.velocityAmount=(float)x->getDoubleAttribute("velocity",.75);
        s.loopCrossfadeMs=(float)x->getDoubleAttribute("crossfade",5); s.chokeGroup=x->getIntAttribute("choke",0);
        s.reverse=x->getBoolAttribute("reverse",false); s.oneShot=x->getBoolAttribute("oneShot",true);
        s.loop=x->getBoolAttribute("loop",false); s=sanitise(s,i);
        if (auto* pcm = x->getChildByName ("PCM"))
        {
            juce::MemoryBlock bytes;
            if (bytes.fromBase64Encoding (pcm->getAllSubText()))
                slot.sample = slyce::SampleStateCodec::decode (bytes.getData(), bytes.getSize(), slot.sourceRate);
            s.missing = slot.sample == nullptr;
        }
        juce::File file (s.sourcePath);
        if (! slot.sample && file.existsAsFile())
        {
            double rate=44100.0; slot.sample=SampleLoader::decode(file,rate); slot.sourceRate=rate;
            s.missing = slot.sample == nullptr;
        }
        else if (! slot.sample) s.missing = s.sourcePath.isNotEmpty();
    }
    publish (next);
    releaseAll();
}
