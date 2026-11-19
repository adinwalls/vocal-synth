#pragma once
// Factory presets. A preset starts from the defaults and changes only what it
// lists. Key and Low Latency are never touched: they belong to the song and to
// how you're working, not to the sound.

#include "VoxParams.h"

namespace voxbrew
{

struct PresetValue { int param; float value; };

struct Preset
{
    const char* name;
    PresetValue values[16];
    int count;
};

static const Preset kPresets[] = {
    { "Init", {}, 0 },

    // The Messina recipe: dry + hard-tuned lead + tonic + a few loose harmony voices,
    // with the keyboard free to add more.
    { "Messina Stack", {
        { pMode, kModeAutoAndMidi }, { pAutoVoices, 3 }, { pAuto1, 2 }, { pAuto2, 4 }, { pAuto3, -3 },
        { pTonic, 1 }, { pTonicOctave, 2 },
        { pDry, -6 }, { pLead, 0 }, { pHarmony, -4 }, { pSpread, 100 },
        { pNatural, 0 }, { pLoose, 12 }, { pGlide, 12 }, { pRelease, 250 } }, 15 },

    // Tuned lead over a low drone and an octave shimmer.
    { "Tonic Halo", {
        { pMode, kModeAuto }, { pAutoVoices, 1 }, { pAuto1, 7 },
        { pTonic, 1 }, { pTonicOctave, 2 },
        { pLead, 0 }, { pHarmony, -6 }, { pSpread, 60 },
        { pNatural, 10 }, { pLoose, 8 }, { pGlide, 40 }, { pRelease, 400 } }, 12 },

    // Everything locked to the grid: four tight voices, no drift, full consonants.
    { "Hard Stack", {
        { pMode, kModeAuto }, { pAutoVoices, 4 }, { pAuto1, 2 }, { pAuto2, 4 }, { pAuto3, -3 }, { pAuto4, -7 },
        { pLead, 0 }, { pHarmony, -3 }, { pSpread, 100 },
        { pNatural, 0 }, { pLoose, 2 }, { pGlide, 0 }, { pAttack, 5 }, { pRelease, 80 }, { pConsonants, 100 } }, 15 },

    // Play the chords yourself: your voice sings whatever you hold down.
    { "Keys Choir", {
        { pMode, kModeMidi }, { pAutoVoices, 0 },
        { pDry, -60 }, { pLead, 0 }, { pHarmony, 0 }, { pSpread, 100 },
        { pNatural, 20 }, { pLoose, 14 }, { pGlide, 60 }, { pAttack, 40 }, { pRelease, 700 }, { pConsonants, 30 } }, 12 },

    // A soft, natural pair either side of the lead that keeps your vibrato.
    { "Wide Thirds", {
        { pMode, kModeAuto }, { pAutoVoices, 2 }, { pAuto1, 2 }, { pAuto2, -5 },
        { pLead, 0 }, { pHarmony, -5 }, { pSpread, 100 },
        { pNatural, 70 }, { pLoose, 8 }, { pGlide, 50 }, { pAttack, 25 }, { pRelease, 200 } }, 12 },

    // A darker double an octave down.
    { "Low Shadow", {
        { pMode, kModeAuto }, { pAutoVoices, 1 }, { pAuto1, -7 },
        { pLead, 0 }, { pHarmony, -4 }, { pSpread, 20 }, { pFormant, -2 },
        { pNatural, 40 }, { pLoose, 5 }, { pGlide, 30 } }, 10 },

    // A cassette that has been played too often: drifting pitch, hiss, a little rasp.
    { "Worn Tape", {
        { pMode, kModeAuto }, { pAutoVoices, 2 }, { pAuto1, 2 }, { pAuto2, -3 },
        { pHarmony, -4 }, { pNatural, 40 }, { pLoose, 14 },
        { pMangle, 35 }, { pScatter, 25 }, { pWobble, 75 }, { pCrush, 20 }, { pHiss, 70 }, { pCrackle, 20 } }, 13 },

    // Tuned lead and a low drone played off an old record.
    { "Dusty Record", {
        { pMode, kModeAuto }, { pAutoVoices, 1 }, { pAuto1, 4 }, { pTonic, 1 }, { pTonicOctave, 2 },
        { pHarmony, -6 }, { pLoose, 10 }, { pRelease, 400 },
        { pMangle, 45 }, { pScatter, 15 }, { pWobble, 35 }, { pCrush, 35 }, { pHiss, 40 }, { pCrackle, 90 } }, 14 },

    // Four voices falling apart: stutters, gaps, crunch.
    { "Broken Choir", {
        { pMode, kModeAuto }, { pAutoVoices, 4 }, { pAuto1, 2 }, { pAuto2, 4 }, { pAuto3, -3 }, { pAuto4, -7 },
        { pDry, -9 }, { pSpread, 100 }, { pLoose, 18 },
        { pMangle, 85 }, { pScatter, 90 }, { pWobble, 60 }, { pCrush, 70 }, { pHiss, 45 }, { pCrackle, 50 } }, 15 },
};

static constexpr int kNumPresets = (int) (sizeof kPresets / sizeof kPresets[0]);

inline bool presetKeeps (int param) { return param == pKey || param == pLowLatency; }

// Writes preset `index` into a full parameter array, leaving Key and Low Latency as they are.
inline void applyPreset (int index, float* values)
{
    if (index < 0 || index >= kNumPresets)
        return;
    for (int i = 0; i < pCount; ++i)
        if (kSpecs[i].kind != kOutput && ! presetKeeps (i))
            values[i] = kSpecs[i].def;
    const Preset& p = kPresets[index];
    for (int i = 0; i < p.count; ++i)
        values[p.values[i].param] = p.values[i].value;
}

} // namespace voxbrew
