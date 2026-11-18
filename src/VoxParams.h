#pragma once
// Parameter list shared by the plugin and its editor.

#include "dsp/Harmonizer.h"

#include <cmath>
#include <cstdio>

namespace voxbrew
{

enum Params
{
    pMode = 0, pKey, pScale,
    pAutoVoices, pAuto1, pAuto2, pAuto3, pAuto4,
    pTonic, pTonicOctave,
    pDry, pLead, pHarmony, pSpread,
    pFormant, pNatural, pLoose, pGlide, pAttack, pRelease, pConsonants,
    pOutput,
    pOutPitch,     // output: detected pitch in Hz
    pLowLatency,
    pOutVoices,    // output: number of sounding voices
    pCount
};

enum ParamKind { kFloat, kInt, kBool, kChoice, kOutput };

struct ParamSpec
{
    const char* name;
    const char* shortName;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* description;
};

static const ParamSpec kSpecs[pCount] = {
    { "Harmony Source", "Source", "mode", "", 0, kModeCount - 1, kModeAuto, kChoice,
      "Auto: harmonies in key follow your voice. MIDI: voices play the notes you hold. Auto + MIDI: both. "
      "Notes: voices play the notes written on the plugin's own timeline." },
    { "Key", "Key", "key", "", 0, 11, 0, kChoice, "" },
    { "Scale", "Scale", "scale", "", 0, kScaleCount - 1, kMajor, kChoice, "" },
    { "Auto Voices", "Voices", "auto_voices", "", 0, 4, 2, kInt, "How many automatic harmony voices sing" },
    { "Auto Voice 1", "Voice 1", "auto1", "", -14, 14, 2, kChoice, "Scale steps from the note you sing: +2 = 3rd up, +4 = 5th up, -7 = octave down" },
    { "Auto Voice 2", "Voice 2", "auto2", "", -14, 14, 4, kChoice, "Scale steps from the note you sing" },
    { "Auto Voice 3", "Voice 3", "auto3", "", -14, 14, -3, kChoice, "Scale steps from the note you sing" },
    { "Auto Voice 4", "Voice 4", "auto4", "", -14, 14, -7, kChoice, "Scale steps from the note you sing" },
    { "Tonic Drone", "Tonic", "tonic", "", 0, 1, 0, kBool, "Adds a voice that holds the root of the key, like the Messina's tonic note" },
    { "Tonic Octave", "Octave", "tonic_octave", "", 1, 5, 2, kInt, "" },
    { "Dry Voice", "Dry", "dry", "dB", -60, 6, -60, kFloat, "Your untouched voice (-60 = off)" },
    { "Tuned Lead", "Lead", "lead", "dB", -60, 6, 0, kFloat, "Your voice snapped to the key (-60 = off)" },
    { "Harmony Level", "Harmony", "harmony", "dB", -60, 6, -3, kFloat, "Level of all harmony voices" },
    { "Stereo Spread", "Spread", "spread", "%", 0, 100, 70, kFloat, "Width of the harmony voices" },
    { "Formant", "Formant", "formant", "st", -12, 12, 0, kFloat, "Vowel character of the voices: up = smaller/brighter, down = bigger/darker" },
    { "Natural", "Natural", "natural", "%", 0, 100, 30, kFloat, "0 = voices locked to their notes; 100 = they keep your vibrato and scoops" },
    { "Loose", "Loose", "loose", "ct", 0, 30, 6, kFloat, "Per-voice detune and slow drift, so the stack sounds like several singers" },
    { "Glide", "Glide", "glide", "ms", 0, 500, 30, kFloat, "How quickly voices slide to a new note" },
    { "Attack", "Attack", "attack", "ms", 1, 500, 15, kFloat, "" },
    { "Release", "Release", "release", "ms", 10, 3000, 150, kFloat, "" },
    { "Consonants", "Conson.", "consonants", "%", 0, 100, 50, kFloat, "How much of your s, t and breath sounds the harmony voices copy" },
    { "Output", "Output", "output", "dB", -24, 12, 0, kFloat, "" },
    { "Detected Pitch", "Pitch", "detected_hz", "Hz", 0, 1200, 0, kOutput, "" },
    { "Low Latency", "Low Lat.", "low_latency", "", 0, 1, 0, kBool,
      "For monitoring while you sing: about 14 ms instead of 35 ms. Turn off for mixing." },
    { "Active Voices", "Voices", "active_voices", "", 0, Harmonizer::kNumVoices, 0, kOutput, "" },
};

static const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* const kScaleNames[kScaleCount] = {
    "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
    "Major Pentatonic", "Minor Pentatonic", "Blues", "Dorian", "Mixolydian"
};
static const char* const kModeNames[kModeCount] = { "Auto", "MIDI", "Auto + MIDI", "Notes" };

// "3rd up" style name for a number of scale steps (exact for 7-note scales).
inline void stepLabel (int steps, char* buf, size_t n, bool withNumber)
{
    static const char* const ord[] = { "", "unison", "2nd", "3rd", "4th", "5th", "6th", "7th", "octave",
                                       "9th", "10th", "11th", "12th", "13th", "14th", "2 octaves" };
    const int a = std::abs (steps) + 1;
    if (steps == 0)
        std::snprintf (buf, n, withNumber ? "0: unison" : "unison");
    else if (withNumber)
        std::snprintf (buf, n, "%+d: %s %s", steps, ord[a], steps > 0 ? "up" : "down");
    else
        std::snprintf (buf, n, "%s %s", ord[a], steps > 0 ? "up" : "down");
}

inline void choiceLabel (int param, int value, char* buf, size_t n)
{
    switch (param)
    {
        case pMode:  std::snprintf (buf, n, "%s", kModeNames[value]); break;
        case pKey:   std::snprintf (buf, n, "%s", kKeyNames[value]); break;
        case pScale: std::snprintf (buf, n, "%s", kScaleNames[value]); break;
        default:     stepLabel (value, buf, n, true); break;
    }
}

inline void noteName (int midi, char* buf, size_t n)
{
    std::snprintf (buf, n, "%s%d", kKeyNames[((midi % 12) + 12) % 12], midi / 12 - 1);
}

inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }

inline HarmonizerParams toEngine (const float* v)
{
    HarmonizerParams p;
    p.mode = (int) v[pMode];
    p.key = (int) v[pKey];
    p.scale = (int) v[pScale];
    p.autoVoices = (int) v[pAutoVoices];
    for (int i = 0; i < 4; ++i)
        p.autoSteps[(size_t) i] = (int) std::lround (v[pAuto1 + i]);
    p.tonic = v[pTonic] > 0.5f;
    p.tonicOctave = (int) v[pTonicOctave];
    p.dry = dbToGain (v[pDry]);
    p.lead = dbToGain (v[pLead]);
    p.harmony = dbToGain (v[pHarmony]);
    p.spread = v[pSpread] * 0.01f;
    p.output = std::pow (10.0f, v[pOutput] / 20.0f);
    p.voice.formant = std::exp2 (v[pFormant] / 12.0f);
    p.voice.natural = v[pNatural] * 0.01f;
    p.voice.looseCents = v[pLoose];
    p.voice.glideMs = v[pGlide];
    p.voice.attackMs = v[pAttack];
    p.voice.releaseMs = v[pRelease];
    p.voice.consonants = v[pConsonants] * 0.01f;
    return p;
}

} // namespace voxbrew
