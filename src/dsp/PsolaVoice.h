#pragma once

#include "Scale.h"
#include "VoiceAnalyzer.h"

#include <cstdint>
#include <random>
#include <vector>

namespace voxbrew
{

struct VoiceSettings
{
    KeyContext key;
    float formant = 1.0f;       // 1 = natural; >1 brighter/smaller, <1 darker/bigger
    float glideMs = 30.0f;      // portamento towards a new target note
    float looseCents = 6.0f;    // per-voice detune + slow drift (the "not quite a machine" feel)
    float natural = 0.0f;       // 0 = locked to the note, 1 = follows the singer's vibrato and scoops
    float attackMs = 15.0f;
    float releaseMs = 150.0f;
    float consonants = 0.5f;    // level of unpitched sounds (s, t, breaths) in harmony voices
};

// What a voice sings.
struct Target
{
    enum Kind { Midi, Degree, Fixed };
    Kind kind = Midi;
    int value = 60; // Midi/Fixed: MIDI note. Degree: scale steps away from the sung note (0 = tuned lead).
};

// One harmony voice. It re-synthesises the singer's voice at a target pitch
// with pitch-synchronous overlap-add (TD-PSOLA). Each grain is two periods of
// the real input, so the voice keeps its own vowels and character (formants);
// only the spacing of the grains, i.e. the pitch, changes.
class PsolaVoice
{
public:
    void prepare (const VoiceAnalyzer& analyzer, uint32_t seed);

    void start (const Target& t, float gain, int64_t outputStart);
    void retarget (const Target& t, float gain);
    void release();
    void kill();

    bool isActive() const { return active; }
    bool isReleasing() const { return releasing; }
    const Target& getTarget() const { return target; }

    uint64_t order = 0; // allocation order, for voice stealing

    // Renders `n` samples starting at input-timeline position `outputStart`
    // (= samples written - latency) and adds them, panned, into outL/outR.
    void render (const VoiceAnalyzer& analyzer, const VoiceSettings& s,
                 int64_t outputStart, int n, float pan, float* outL, float* outR);

private:
    double targetMidi (const Mark& m, const VoiceSettings& s);
    void clearAhead();

    double fs = 48000.0;
    int maxHalf = 600;
    std::vector<float> acc, wsum;
    int64_t mask = 0;

    bool active = false, releasing = false;
    Target target;
    float gain = 1.0f;
    float env = 0.0f;

    double synthPos = 0.0;    // input-timeline position of the next grain centre
    double currentMidi = -1;  // glides towards the target
    int64_t renderedTo = 0;
    int heldNote = -1000;     // quantiser state for Degree targets

    float detuneCents = 0.0f;
    double driftPhase = 0.0, driftRate = 0.2;
    std::minstd_rand rng;
};

} // namespace voxbrew
