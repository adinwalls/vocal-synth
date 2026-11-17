#pragma once

#include "PsolaVoice.h"
#include "VoiceAnalyzer.h"

#include <array>
#include <cstdint>
#include <vector>

namespace voxbrew
{

enum HarmonyMode { kModeAuto = 0, kModeMidi, kModeAutoAndMidi, kModeCount };

struct HarmonizerParams
{
    int mode = kModeAuto;
    int key = 0;
    int scale = kMajor;

    // Auto harmony: up to four voices, each a number of scale steps from the
    // sung note (+2 = third above, +4 = fifth above, -7 = octave below ...).
    int autoVoices = 2;
    std::array<int, 4> autoSteps { 2, 4, -3, -7 };

    // Tonic drone, like the second Auto-Tune in the Messina chain.
    bool tonic = false;
    int tonicOctave = 2; // root of the key in this octave (A2 = 45 for key A)

    float dry = 0.0f;       // untouched voice
    float lead = 1.0f;      // tuned lead (the sung note snapped to the key)
    float harmony = 0.7f;   // all harmony voices (auto, MIDI, tonic)
    float spread = 0.7f;    // stereo width of the harmony voices
    float output = 1.0f;

    VoiceSettings voice;
};

// The whole VoxBrew engine: one shared analyser feeding a pool of PSOLA voices.
// Plain C++, no framework, no allocation after prepare(), so it can sit inside
// any plugin wrapper and in the offline test tools.
class Harmonizer
{
public:
    static constexpr int kLeadSlot = 0;
    static constexpr int kAutoSlot = 1;   // 4 auto slots: 1..4
    static constexpr int kTonicSlot = 5;
    static constexpr int kMidiSlot = 6;   // MIDI voices: 6..
    static constexpr int kNumVoices = 16;
    static constexpr int kChunk = 128;

    void prepare (double sampleRate, float minF0 = 75.0f, float maxF0 = 1100.0f);
    void reset();

    int getLatencySamples() const { return latency; }
    void setParams (const HarmonizerParams& p);
    const HarmonizerParams& getParams() const { return params; }

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void setSustain (bool down);
    void allNotesOff();

    // Mono in, stereo out. `n` can be any size.
    void process (const float* in, float* outL, float* outR, int n);

    float getInputHz() const { return analyzer.getF0(); }
    int activeVoices() const;

private:
    void processChunk (const float* in, float* outL, float* outR, int n);
    void updateFixedVoices (int64_t outputStart);
    bool midiEnabled() const { return params.mode != kModeAuto; }
    bool autoEnabled() const { return params.mode != kModeMidi; }

    HarmonizerParams params;
    VoiceAnalyzer analyzer;
    std::array<PsolaVoice, kNumVoices> voices;
    std::array<float, kNumVoices> pans {};
    std::array<int, kNumVoices> midiNote {};
    std::array<bool, kNumVoices> sustained {};
    bool sustain = false;
    uint64_t orderCounter = 0;

    int latency = 0;
    std::vector<float> leadL, leadR, harmL, harmR;
};

} // namespace voxbrew
