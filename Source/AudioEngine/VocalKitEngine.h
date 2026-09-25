#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
    Legacy fixed-slot vocal kit player.

    Kept for backwards-compatible session restore. New SLYCE imports use the
    chromatic mapped-sample engine instead. MIDI notes C3..D4 select fifteen
    stored slots, with monophonic phrase replacement. All file decoding and bank mutation happens on the message
    thread.  The audio thread only reads an immutable Bank through an atomic
    shared_ptr and uses fixed-size voices/scratch buffers.
*/
class VocalKitEngine
{
public:
    static constexpr int kNumSlots = 15;
    static constexpr int kRootNote = 48; // C3 in SLYCE's existing keybed

    struct SlotSettings
    {
        juce::String name;
        juce::String sourcePath;
        int midiNote = kRootNote;
        float start = 0.0f, end = 1.0f;          // normalised source range
        float loopStart = 0.0f, loopEnd = 1.0f;  // normalised source range
        float pitchSemitones = 0.0f;
        float fineCents = 0.0f;
        float formantSemitones = 0.0f;           // lightweight resonant colour shift
        float timeStretch = 1.0f;                // playback-time ratio (see limitation report)
        float attackMs = 3.0f, decayMs = 0.0f, sustain = 1.0f, releaseMs = 80.0f;
        float gainDb = 0.0f, pan = 0.0f;
        float filterHz = 20000.0f;
        float delaySend = 0.0f, reverbSend = 0.0f;
        float velocityAmount = 0.75f;
        float loopCrossfadeMs = 5.0f;
        int chokeGroup = 0;
        bool reverse = false;
        bool oneShot = true;
        bool loop = false;
        bool missing = false;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void releaseAll() noexcept;
    void stopAll() noexcept;
    void noteOn (int midiNote, float velocity, bool tap = false) noexcept;
    void noteOff (int midiNote) noexcept;
    void render (juce::AudioBuffer<float>& out, int numSamples) noexcept;

    bool loadSlotFromFile (int slot, const juce::File&, juce::String& error);
    bool setSlotFromBuffer (int slot, std::shared_ptr<juce::AudioBuffer<float>>,
                            double sourceRate, const juce::String& name,
                            const juce::String& sourcePath = {});
    void clearSlot (int slot);
    void clear();
    bool relinkMissingSlot (int slot, const juce::File&, juce::String& error);

    SlotSettings getSlotSettings (int slot) const;
    void setSlotSettings (int slot, const SlotSettings&);
    std::shared_ptr<const juce::AudioBuffer<float>> getSlotSample (int slot) const;
    bool hasSlot (int slot) const;
    int getLastTriggeredSlot() const noexcept { return lastTriggeredSlot.load(); }
    float getSlotPlayhead (int slot) const noexcept
    {
        return juce::isPositiveAndBelow (slot, kNumSlots) ? playheads[(size_t) slot].load() : -1.0f;
    }
    juce::StringArray getMissingFiles() const;

    void writeState (juce::XmlElement& parent) const;
    void restoreState (const juce::XmlElement& parent);

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioBuffer<float>> sample;
        double sourceRate = 44100.0;
        SlotSettings settings;
    };
    struct Bank { std::array<Slot, kNumSlots> slots; };

    struct Voice
    {
        std::shared_ptr<const Bank> hold;
        const Slot* slot = nullptr;
        int note = -1;
        double position = 0.0, step = 1.0;
        float env = 0.0f, attackStep = 1.0f, decayStep = 1.0f;
        float sustain = 1.0f, releaseCoeff = 0.999f;
        int envelopeStage = 0; // attack, decay, sustain, release
        float gain = 1.0f, panL = 1.0f, panR = 1.0f;
        float filterStateL = 0.0f, filterStateR = 0.0f, filterCoeff = 1.0f;
        bool releasing = false;
        bool active() const noexcept { return slot != nullptr; }
        void stop() noexcept { *this = {}; }
    };

    static constexpr int kMaxVoices = 24;
    std::array<Voice, kMaxVoices> voices {};
    std::shared_ptr<const Bank> bank { std::make_shared<Bank>() };
    std::vector<std::shared_ptr<const Bank>> retiredBanks;
    juce::CriticalSection retireLock;
    double sr = 44100.0;
    int blockCapacity = 1;
    std::atomic<int> lastTriggeredSlot { -1 };
    std::array<std::atomic<float>, kNumSlots> playheads {};

    juce::AudioBuffer<float> delaySendScratch, reverbSendScratch;
    std::vector<float> delayLineL, delayLineR, reverbLineL, reverbLineR;
    int delayPos = 0, reverbPos = 0;

    void publish (std::shared_ptr<const Bank>);
    std::shared_ptr<Bank> mutableCopy() const;
    Voice* freeVoice() noexcept;
    void renderSends (juce::AudioBuffer<float>&, int) noexcept;
    static SlotSettings sanitise (SlotSettings, int slot);
};
