#pragma once

#include "Mangler.h"
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
    float consonants = 0.5f;
    bool lowLatency = false;    // place grains as late as possible (set by the Harmonizer)
    // Mangle (set by the Harmonizer for each block)
    float scatter = 0.0f;       // 0..1: grains played roughly, shuffled, repeated and dropped
    float wobbleCents = 0.0f;   // tape wow and flutter, the same for every voice
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

    // For the display: the pitch this voice is singing right now as a
    // fractional MIDI note, or 0 while it is silent or on a consonant.
    float getMeterMidi() const { return (active && pitched && env > 0.02f) ? (float) currentMidi : 0.0f; }

    // Renders `n` samples starting at input-timeline position `outputStart`
    // (= samples written - latency) and adds them, panned, into outL/outR.
    void render (const VoiceAnalyzer& analyzer, const VoiceSettings& s,
                 int64_t outputStart, int n, float pan, float* outL, float* outR);

private:
    double targetMidi (const Mark& m, const VoiceSettings& s);
    void clearAhead();

    double fs = 48000.0;
    int maxHalf = 600;
    int lastHalf = 600;       // half length of the previous grain
    std::vector<float> acc, wsum;
    int64_t mask = 0;

    bool active = false, releasing = false, pitched = false;
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

    // Scatter state
    Noise nz;
    int stutterLeft = 0, dropLeft = 0, flickLeft = 0;
    double stutterPos = 0.0, flickRatio = 1.0;
};

} // namespace voxbrew
