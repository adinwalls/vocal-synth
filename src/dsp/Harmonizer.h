#pragma once

#include "Mangler.h"
#include "NoteTrack.h"
#include "PsolaVoice.h"
#include "VoiceAnalyzer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace voxbrew
{

// Notes: harmonies come from the notes written on the plugin's own timeline
// (the suggested-notes editor) and play back in time with the song.
enum HarmonyMode { kModeAuto = 0, kModeMidi, kModeAutoAndMidi, kModeNotes, kModeCount };

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

    // Lo-fi damage: Scatter and Wobble act inside the voices, Crush, Hiss and
    // Crackle on the whole mix (dry voice included). Off while amount is 0.
    MangleSettings mangle;
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

    // Low latency is for monitoring while you sing. The plugin then adds only a
    // few milliseconds of fixed delay and each voice builds its next grain from
    // the newest complete period it has, so the real delay follows the pitch
    // (about two periods: ~10 ms at A3) instead of being sized for the lowest
    // note. Full latency keeps every grain exactly time-aligned, which is the
    // better choice for mixing.
    void setLowLatency (bool on);
    bool isLowLatency() const { return lowLatency; }
    void setParams (const HarmonizerParams& p);
    const HarmonizerParams& getParams() const { return params; }

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void setSustain (bool down);
    void allNotesOff();

    // ---- song timeline (suggested notes) ----
    // Audio thread, before each process(): where this block sits in the song.
    // `known` is false when the host gives no position.
    void setTimeline (bool known, bool playing, double seconds);

    // Your sung pitch along the song, learned while the song plays.
    MelodyScan scan;

    // Any thread except the audio thread.
    void setNotes (const NoteSet& ns)
    {
        const int next = 1 - noteIdx.load();
        noteBuf[next] = ns;
        noteIdx.store (next);
    }
    const NoteSet& notes() const { return noteBuf[noteIdx.load()]; }

    std::atomic<double> meterPos { -1.0 };   // song position in seconds, -1 if the host gives none
    std::atomic<bool> meterPlaying { false };

    // Mono in, stereo out. `n` can be any size.
    void process (const float* in, float* outL, float* outR, int n);

    float getInputHz() const { return analyzer.getF0(); }

    // Meters for the editor (written on the audio thread, read from the UI).
    // Fractional MIDI note per voice slot, 0 when the slot is silent.
    std::array<std::atomic<float>, kNumVoices> meterVoice {};
    std::atomic<float> meterInput { 0.0f };
    int activeVoices() const;

private:
    void processChunk (const float* in, float* outL, float* outR, int n);
    void updateFixedVoices (int64_t outputStart);
    void updateWrittenNotes (double seconds);
    void startMidiVoice (int note, float velocity);
    void releaseMidiVoice (int note);
    bool midiEnabled() const { return params.mode != kModeAuto; }
    bool autoEnabled() const { return params.mode == kModeAuto || params.mode == kModeAutoAndMidi; }

    HarmonizerParams params;
    VoiceAnalyzer analyzer;
    Mangler mangler;
    std::array<PsolaVoice, kNumVoices> voices;
    std::array<float, kNumVoices> pans {};
    std::array<int, kNumVoices> midiNote {};
    std::array<bool, kNumVoices> sustained {};
    bool sustain = false;
    uint64_t orderCounter = 0;

    NoteSet noteBuf[2];
    std::atomic<int> noteIdx { 0 };
    std::array<bool, 128> written {};   // notes currently held by the written-note track
    bool anyWritten = false;
    bool tlValid = false;
    double tlSeconds = 0.0, fs = 48000.0;

    int latency = 0, fullLatency = 0, shortLatency = 0;
    bool lowLatency = false;
    std::vector<float> leadL, leadR, harmL, harmR;
};

} // namespace voxbrew
