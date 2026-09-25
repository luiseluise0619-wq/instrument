#include "AirVocalEngine.h"
#include <cmath>

namespace
{
float clamp01 (float x) noexcept { return juce::jlimit (0.0f, 1.0f, x); }
}

void AirVocalEngine::prepare (double sampleRate, int)
{
    sr = juce::jmax (8000.0, sampleRate);
    // Allocated only here. The longest line is intentionally shared by every
    // note instead of constructing a reverb for every voice.
    preL.assign ((size_t) std::ceil (sr * 0.060) + 2u, 0.0f);
    preR.assign (preL.size(), 0.0f);
    haloL.assign ((size_t) std::ceil (sr * 0.137) + 2u, 0.0f);
    haloR.assign ((size_t) std::ceil (sr * 0.173) + 2u, 0.0f);
    preWrite = haloWrite = 0;
    haloLowL = haloLowR = haloDcL = haloDcR = duckEnv = 0.0f;
    stopAll();
}

void AirVocalEngine::setBank (std::shared_ptr<const Bank> next)
{
    if (next != nullptr)
        std::atomic_store (&bank, std::move (next));
    releaseAll();
}

void AirVocalEngine::setPresetShape (const PresetShape& p) noexcept
{
    shape.attackMs = juce::jlimit (5.0f, 1500.0f, p.attackMs);
    shape.releaseMs = juce::jlimit (40.0f, 5000.0f, p.releaseMs);
    shape.coreGain = clamp01 (p.coreGain);
    shape.airGain = clamp01 (p.airGain);
    shape.haloSend = clamp01 (p.haloSend);
    shape.haloDecay = juce::jlimit (0.35f, 0.94f, p.haloDecay);
    shape.haloPredelayMs = juce::jlimit (0.0f, 55.0f, p.haloPredelayMs);
    shape.brightnessHz = juce::jlimit (900.0f, 16000.0f, p.brightnessHz);
}

void AirVocalEngine::setMacros (float a, float b, float v, float bl, float m, float s) noexcept
{
    macros = { clamp01(a), clamp01(b), clamp01(v), clamp01(bl), clamp01(m), clamp01(s) };
}

AirVocalEngine::Voice* AirVocalEngine::acquireVoice() noexcept
{
    for (auto& v : voices)
        if (! v.active()) return &v;

    Voice* best = &voices.front();
    float bestScore = std::numeric_limits<float>::max();
    for (auto& v : voices)
    {
        const float score = v.env + (v.releasing ? -1.0f : 0.0f)
                          + (float) ((voiceCounter - v.age) < 64 ? 2.0 : 0.0);
        if (score < bestScore) { best = &v; bestScore = score; }
    }
    return best;
}

void AirVocalEngine::noteOn (int midiNote, float velocity, bool selfRelease) noexcept
{
    auto current = std::atomic_load (&bank);
    if (! current) return;
    bool usable = current->air.audio != nullptr;
    for (const auto& s : current->core) usable = usable || s.audio != nullptr;
    if (! usable) return;

    auto* v = acquireVoice();
    v->stop();
    v->bank = std::move (current);
    for (int i = 0; i < 3; ++i)
        if (const auto& s = v->bank->core[(size_t) i]; s.audio)
            v->corePos[(size_t) i] = s.loopStart * (double) s.audio->getNumSamples();
    v->note = juce::jlimit (0, 127, midiNote);
    v->velocity = clamp01 (velocity);
    v->age = ++voiceCounter;
    v->selfReleasing = selfRelease;
    // Stable per-voice phase/rate: voices move independently without random
    // pitch wandering from run to run.
    v->motionPhase = (float) ((v->age * 0.61803398875) - std::floor (v->age * 0.61803398875));
    v->motionRate = 0.11f + 0.025f * (float) (v->age % 7u);
    const float pan = juce::jlimit (-0.22f, 0.22f, ((midiNote % 7) - 3) * 0.045f);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    v->panL = std::cos (angle); v->panR = std::sin (angle);
    v->releaseCoeff = std::exp (-1.0f / juce::jmax (1.0f, shape.releaseMs * 0.001f * (float) sr));
}

void AirVocalEngine::noteOff (int midiNote) noexcept
{
    for (auto& v : voices)
        if (v.active() && v.note == midiNote)
            v.releasing = true;
}

void AirVocalEngine::releaseAll() noexcept
{
    for (auto& v : voices) if (v.active()) v.releasing = true;
}

void AirVocalEngine::stopAll() noexcept
{
    for (auto& v : voices) v.stop();
}

float AirVocalEngine::sampleLinear (const Source& s, int channel, double position) noexcept
{
    if (! s.audio || s.audio->getNumSamples() < 2) return 0.0f;
    const int n = s.audio->getNumSamples();
    position = juce::jlimit (0.0, (double) n - 1.001, position);
    const int i = (int) position;
    const float f = (float) (position - i);
    const int ch = juce::jlimit (0, s.audio->getNumChannels() - 1, channel);
    const float* p = s.audio->getReadPointer (ch);
    return p[i] + (p[i + 1] - p[i]) * f;
}

float AirVocalEngine::sampleLooped (const Source& s, int channel, double position) noexcept
{
    if (! s.audio || s.audio->getNumSamples() < 8) return 0.0f;
    const double total = (double) s.audio->getNumSamples();
    const double a = juce::jlimit (1.0, total - 4.0, (double) s.loopStart * total);
    const double b = juce::jlimit (a + 2.0, total - 2.0, (double) s.loopEnd * total);
    const double xf = juce::jmin ((b - a) * 0.45, s.crossfadeMs * 0.001 * s.sampleRate);
    position = wrapPosition (s, position);
    float x = sampleLinear (s, channel, position);
    if (xf > 2.0 && position > b - xf)
    {
        const float t = (float) ((position - (b - xf)) / xf);
        const double other = a + (position - (b - xf));
        // Equal-power crossfade keeps a sustained chord from pulsing at every
        // loop boundary.
        x = x * std::cos (t * juce::MathConstants<float>::halfPi)
          + sampleLinear (s, channel, other) * std::sin (t * juce::MathConstants<float>::halfPi);
    }
    return x;
}

double AirVocalEngine::wrapPosition (const Source& s, double position) noexcept
{
    if (! s.audio || s.audio->getNumSamples() < 8) return 0.0;
    const double total = (double) s.audio->getNumSamples();
    const double a = juce::jlimit (1.0, total - 4.0, (double) s.loopStart * total);
    const double b = juce::jlimit (a + 2.0, total - 2.0, (double) s.loopEnd * total);
    const double length = b - a;
    while (position >= b) position -= length;
    while (position < a) position += length;
    return position;
}

void AirVocalEngine::render (juce::AudioBuffer<float>& out, int numSamples) noexcept
{
    if (numSamples <= 0 || out.getNumChannels() <= 0) return;
    const float vowel = macros[Vowel] * 2.0f;
    const int lo = juce::jlimit (0, 1, (int) vowel);
    const int hi = lo + 1;
    const float vowelT = vowel - (float) lo;
    const float airAmount = shape.airGain * (0.18f + 1.12f * macros[Air]);
    const float bodyGain = shape.coreGain * (0.70f + 0.55f * macros[Body]);
    const float bloom = macros[Bloom];
    const float attackMs = shape.attackMs * (0.38f + 2.0f * bloom);
    const float attackStep = 1.0f / juce::jmax (1.0f, attackMs * 0.001f * (float) sr);
    const float motionDepth = macros[Motion];
    const float haloSend = shape.haloSend * (0.18f + 1.18f * macros[Space]);

    for (int n = 0; n < numSamples; ++n)
    {
        float dryL = 0.0f, dryR = 0.0f, sendL = 0.0f, sendR = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active()) continue;
            if (v.releasing) v.env *= v.releaseCoeff;
            else             v.env = juce::jmin (1.0f, v.env + attackStep);
            if (v.selfReleasing && v.env >= 0.999f) v.releasing = true;
            // A slow attack can legitimately begin below the silence floor.
            // Only retire a voice after note-off/release; otherwise BLOOM
            // presets with ~1 s attacks die on their very first sample.
            if (v.releasing && v.env < 0.00004f) { v.stop(); continue; }

            const float phase = v.motionPhase + v.motionRate * (float) n / (float) sr;
            const float lfo = std::sin (phase * juce::MathConstants<float>::twoPi);
            const float cents = lfo * (1.5f + 4.0f * motionDepth); // <= 5.5 cents
            const float velocityGain = 0.42f + 0.58f * v.velocity;
            const float velocityBright = 0.68f + 0.42f * v.velocity;
            const float cutoff = juce::jlimit (700.0f, 18000.0f,
                shape.brightnessHz * velocityBright * (0.72f + 0.45f * macros[Body]));
            const float coeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr);

            auto renderCore = [&] (int sourceIndex, int channel) noexcept
            {
                const auto& src = v.bank->core[(size_t) sourceIndex];
                if (! src.audio) return 0.0f;
                return sampleLooped (src, channel, v.corePos[(size_t) sourceIndex]);
            };
            float cL0 = renderCore (lo, 0), cR0 = renderCore (lo, 1);
            float cL1 = renderCore (hi, 0), cR1 = renderCore (hi, 1);
            float coreL = cL0 * std::cos (vowelT * juce::MathConstants<float>::halfPi)
                        + cL1 * std::sin (vowelT * juce::MathConstants<float>::halfPi);
            float coreR = cR0 * std::cos (vowelT * juce::MathConstants<float>::halfPi)
                        + cR1 * std::sin (vowelT * juce::MathConstants<float>::halfPi);
            v.lpL[(size_t)lo] += coeff * (coreL - v.lpL[(size_t)lo]);
            v.lpR[(size_t)lo] += coeff * (coreR - v.lpR[(size_t)lo]);
            coreL = v.lpL[(size_t)lo]; coreR = v.lpR[(size_t)lo];

            float airL = 0.0f, airR = 0.0f;
            const auto& air = v.bank->air;
            if (air.audio && v.airPos < air.audio->getNumSamples() - 2)
            {
                airL = sampleLinear (air, 0, v.airPos);
                airR = sampleLinear (air, 1, v.airPos);
                // AIR is deliberately darker and naturally runs out instead
                // of becoming a permanently looping white-noise layer.
                const float airCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 7200.0f / (float) sr);
                v.airLpL += airCoeff * (airL - v.airLpL);
                v.airLpR += airCoeff * (airR - v.airLpR);
                airL = v.airLpL; airR = v.airLpR;
            }

            const float g = v.env * velocityGain;
            const float l = (coreL * bodyGain + airL * airAmount) * g * v.panL;
            const float r = (coreR * bodyGain + airR * airAmount) * g * v.panR;
            dryL += l; dryR += r;
            sendL += l * haloSend; sendR += r * haloSend;

            for (int i = 0; i < 3; ++i)
            {
                const auto& src = v.bank->core[(size_t) i];
                if (! src.audio) continue;
                const double step = src.sampleRate / sr
                    * std::pow (2.0, ((double) v.note - src.rootNote + cents / 100.0) / 12.0);
                v.corePos[(size_t) i] = wrapPosition (src, v.corePos[(size_t) i] + step);
            }
            if (air.audio)
                v.airPos += air.sampleRate / sr * std::pow (2.0, ((double) v.note - air.rootNote) / 12.0);
        }

        if (out.getNumChannels() == 1)
            out.addSample (0, n, (dryL + dryR) * 0.70710678f);
        else
        {
            out.addSample (0, n, dryL);
            out.addSample (1, n, dryR);
        }
        renderHalo (out, n, sendL, sendR, 0.5f * (std::abs (dryL) + std::abs (dryR)));
    }

    for (auto& v : voices)
        if (v.active())
        {
            v.motionPhase += v.motionRate * (float) numSamples / (float) sr;
            v.motionPhase -= std::floor (v.motionPhase);
        }
}

void AirVocalEngine::renderHalo (juce::AudioBuffer<float>& out, int n,
                                 float sendL, float sendR, float dryLevel) noexcept
{
    if (preL.empty() || haloL.empty()) return;
    duckEnv += (dryLevel > duckEnv ? 0.02f : 0.0015f) * (dryLevel - duckEnv);
    const int preDelay = juce::jlimit (1, (int) preL.size() - 1,
        (int) (shape.haloPredelayMs * 0.001f * (float) sr));
    int preRead = preWrite - preDelay;
    if (preRead < 0) preRead += (int) preL.size();
    const float pdL = preL[(size_t) preRead], pdR = preR[(size_t) preRead];
    preL[(size_t) preWrite] = sendL; preR[(size_t) preWrite] = sendR;
    if (++preWrite >= (int) preL.size()) preWrite = 0;

    const int lRead = haloWrite % (int) haloL.size();
    const int rRead = haloWrite % (int) haloR.size();
    const float wetL = haloL[(size_t) lRead], wetR = haloR[(size_t) rRead];
    // Return HP (~220 Hz) and damping LP (~8 kHz), both on the wet bus only.
    haloDcL += 0.004f * (wetL - haloDcL); haloDcR += 0.004f * (wetR - haloDcR);
    const float hpL = wetL - haloDcL, hpR = wetR - haloDcR;
    const float damp = juce::jlimit (0.12f, 0.72f, 0.52f - 0.25f * macros[Space]);
    haloLowL += damp * (hpL - haloLowL); haloLowR += damp * (hpR - haloLowR);
    const float feedback = juce::jlimit (0.35f, 0.94f, shape.haloDecay + 0.12f * macros[Space]);
    haloL[(size_t) lRead] = pdL + haloLowR * feedback;
    haloR[(size_t) rRead] = pdR + haloLowL * (feedback * 0.985f);
    if (++haloWrite > 0x3fffffff) haloWrite = 0;

    const float duck = 1.0f / (1.0f + 2.8f * duckEnv);
    const float wetGain = (0.18f + 0.52f * macros[Space]) * duck;
    if (out.getNumChannels() == 1)
        out.addSample (0, n, (haloLowL + haloLowR) * 0.5f * wetGain);
    else
    {
        out.addSample (0, n, haloLowL * wetGain);
        out.addSample (1, n, haloLowR * wetGain);
    }
}

int AirVocalEngine::getActiveVoiceCount() const noexcept
{
    int n = 0; for (const auto& v : voices) if (v.active()) ++n; return n;
}

juce::String AirVocalEngine::getPresetName() const
{
    auto current = std::atomic_load (&bank);
    return current ? current->presetName : juce::String();
}
