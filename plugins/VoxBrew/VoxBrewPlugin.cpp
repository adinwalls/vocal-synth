// VoxBrew Harmonizer: DPF wrapper around voxbrew::Harmonizer.

#include "DistrhoPlugin.hpp"
#include "../../src/dsp/Harmonizer.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

START_NAMESPACE_DISTRHO

using namespace voxbrew;

enum Params
{
    pMode = 0, pKey, pScale,
    pAutoVoices, pAuto1, pAuto2, pAuto3, pAuto4,
    pTonic, pTonicOctave,
    pDry, pLead, pHarmony, pSpread,
    pFormant, pNatural, pLoose, pGlide, pAttack, pRelease, pConsonants,
    pOutput,
    pOutPitch,
    pCount
};

static const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* const kScaleNames[kScaleCount] = {
    "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
    "Major Pentatonic", "Minor Pentatonic", "Blues", "Dorian", "Mixolydian"
};

// "+2: 3rd up" style names for scale steps (exact for 7-note scales).
static String stepName (int steps)
{
    if (steps == 0) return String ("0: unison");
    static const char* const ord[] = { "", "", "2nd", "3rd", "4th", "5th", "6th", "7th", "octave",
                                       "9th", "10th", "11th", "12th", "13th", "14th", "2 octaves" };
    const int n = std::abs (steps) + 1;
    char buf[48];
    std::snprintf (buf, sizeof buf, "%+d: %s %s", steps, ord[n], steps > 0 ? "up" : "down");
    return String (buf);
}

static float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }

class VoxBrewPlugin : public Plugin
{
public:
    VoxBrewPlugin() : Plugin (pCount, 0, 0)
    {
        for (int i = 0; i < pCount; ++i)
        {
            Parameter p;
            initParameter ((uint32_t) i, p);
            values[i] = p.ranges.def;
        }
        engine.prepare (getSampleRate());
        apply();
        setLatency ((uint32_t) engine.getLatencySamples());
    }

protected:
    const char* getLabel() const override { return "VoxBrewHarmonizer"; }
    const char* getDescription() const override
    {
        return "Live vocal harmonizer: splits one voice into tuned lead, in-key harmonies, a tonic drone "
               "and MIDI-played voices, all formant-preserving.";
    }
    const char* getMaker() const override { return "Adin Walls"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    const char* getHomePage() const override { return "https://github.com/adinwalls/vocal-synth"; }
    uint32_t getVersion() const override { return d_version (0, 1, 0); }

    void initAudioPort (bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort (input, index, port);
    }

    static void enumerate (Parameter& p, int first, int count, const std::function<String (int)>& label)
    {
        auto* v = new ParameterEnumerationValue[(size_t) count];
        for (int i = 0; i < count; ++i) { v[i].value = (float) (first + i); v[i].label = label (first + i); }
        p.enumValues.count = (uint8_t) count;
        p.enumValues.restrictedMode = true;
        p.enumValues.values = v;
    }

    void initParameter (uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        switch (index)
        {
        case pMode:
            p.name = "Harmony Source"; p.shortName = "Source"; p.symbol = "mode";
            p.description = "Auto: harmonies in key follow your voice. MIDI: voices play the notes you hold. Both: auto voices plus MIDI.";
            p.hints |= kParameterIsInteger;
            p.ranges.def = kModeAuto; p.ranges.min = 0; p.ranges.max = kModeCount - 1;
            enumerate (p, 0, kModeCount, [] (int i) { return String (i == 0 ? "Auto" : i == 1 ? "MIDI" : "Auto + MIDI"); });
            break;
        case pKey:
            p.name = "Key"; p.symbol = "key";
            p.hints |= kParameterIsInteger;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 11;
            enumerate (p, 0, 12, [] (int i) { return String (kKeyNames[i]); });
            break;
        case pScale:
            p.name = "Scale"; p.symbol = "scale";
            p.hints |= kParameterIsInteger;
            p.ranges.def = kMajor; p.ranges.min = 0; p.ranges.max = kScaleCount - 1;
            enumerate (p, 0, kScaleCount, [] (int i) { return String (kScaleNames[i]); });
            break;
        case pAutoVoices:
            p.name = "Auto Voices"; p.symbol = "auto_voices";
            p.hints |= kParameterIsInteger;
            p.ranges.def = 2; p.ranges.min = 0; p.ranges.max = 4;
            break;
        case pAuto1: case pAuto2: case pAuto3: case pAuto4:
        {
            const int n = (int) (index - pAuto1);
            static const int defs[4] = { 2, 4, -3, -7 };
            static const char* const names[4] = { "Auto Voice 1", "Auto Voice 2", "Auto Voice 3", "Auto Voice 4" };
            static const char* const syms[4] = { "auto1", "auto2", "auto3", "auto4" };
            p.name = names[n]; p.symbol = syms[n];
            p.description = "Scale steps from the note you sing: +2 = 3rd up, +4 = 5th up, -7 = octave down";
            p.hints |= kParameterIsInteger;
            p.ranges.def = (float) defs[n]; p.ranges.min = -14; p.ranges.max = 14;
            enumerate (p, -14, 29, stepName);
            break;
        }
        case pTonic:
            p.name = "Tonic Drone"; p.shortName = "Tonic"; p.symbol = "tonic";
            p.description = "Adds a voice that holds the root of the key, like the Messina's tonic note";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 1;
            break;
        case pTonicOctave:
            p.name = "Tonic Octave"; p.symbol = "tonic_octave";
            p.hints |= kParameterIsInteger;
            p.ranges.def = 2; p.ranges.min = 1; p.ranges.max = 5;
            break;
        case pDry:
            p.name = "Dry Voice"; p.shortName = "Dry"; p.symbol = "dry"; p.unit = "dB";
            p.description = "Your untouched voice (-60 = off)";
            p.ranges.def = -60; p.ranges.min = -60; p.ranges.max = 6;
            break;
        case pLead:
            p.name = "Tuned Lead"; p.shortName = "Lead"; p.symbol = "lead"; p.unit = "dB";
            p.description = "Your voice snapped to the key (-60 = off)";
            p.ranges.def = 0; p.ranges.min = -60; p.ranges.max = 6;
            break;
        case pHarmony:
            p.name = "Harmony Level"; p.shortName = "Harmony"; p.symbol = "harmony"; p.unit = "dB";
            p.ranges.def = -3; p.ranges.min = -60; p.ranges.max = 6;
            break;
        case pSpread:
            p.name = "Stereo Spread"; p.shortName = "Spread"; p.symbol = "spread"; p.unit = "%";
            p.ranges.def = 70; p.ranges.min = 0; p.ranges.max = 100;
            break;
        case pFormant:
            p.name = "Formant"; p.symbol = "formant"; p.unit = "st";
            p.description = "Shifts the vowel character of the harmonies: up = smaller/brighter, down = bigger/darker";
            p.ranges.def = 0; p.ranges.min = -12; p.ranges.max = 12;
            break;
        case pNatural:
            p.name = "Natural"; p.symbol = "natural"; p.unit = "%";
            p.description = "0 = voices locked to their notes; 100 = they keep your vibrato and scoops";
            p.ranges.def = 30; p.ranges.min = 0; p.ranges.max = 100;
            break;
        case pLoose:
            p.name = "Loose"; p.symbol = "loose"; p.unit = "ct";
            p.description = "Per-voice detune and slow drift, so the stack sounds like several singers";
            p.ranges.def = 6; p.ranges.min = 0; p.ranges.max = 30;
            break;
        case pGlide:
            p.name = "Glide"; p.symbol = "glide"; p.unit = "ms";
            p.ranges.def = 30; p.ranges.min = 0; p.ranges.max = 500;
            break;
        case pAttack:
            p.name = "Attack"; p.symbol = "attack"; p.unit = "ms";
            p.ranges.def = 15; p.ranges.min = 1; p.ranges.max = 500;
            break;
        case pRelease:
            p.name = "Release"; p.symbol = "release"; p.unit = "ms";
            p.ranges.def = 150; p.ranges.min = 10; p.ranges.max = 3000;
            break;
        case pConsonants:
            p.name = "Consonants"; p.symbol = "consonants"; p.unit = "%";
            p.description = "How much of your s, t and breath sounds the harmony voices copy";
            p.ranges.def = 50; p.ranges.min = 0; p.ranges.max = 100;
            break;
        case pOutput:
            p.name = "Output"; p.symbol = "output"; p.unit = "dB";
            p.ranges.def = 0; p.ranges.min = -24; p.ranges.max = 12;
            break;
        case pOutPitch:
            p.name = "Detected Pitch"; p.symbol = "detected_hz"; p.unit = "Hz";
            p.hints = kParameterIsOutput;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 1200;
            break;
        }
    }

    float getParameterValue (uint32_t index) const override
    {
        if (index == pOutPitch)
            return engine.getInputHz();
        return index < pCount ? values[index] : 0.0f;
    }

    void setParameterValue (uint32_t index, float value) override
    {
        if (index >= pCount || index == pOutPitch)
            return;
        values[index] = value;
        apply();
    }

    void sampleRateChanged (double newSampleRate) override
    {
        engine.prepare (newSampleRate);
        apply();
        setLatency ((uint32_t) engine.getLatencySamples());
    }

    void activate() override
    {
        engine.reset();
    }

    void run (const float** inputs, float** outputs, uint32_t frames,
              const MidiEvent* midi, uint32_t midiCount) override
    {
        uint32_t pos = 0, ev = 0;
        while (pos < frames)
        {
            // Apply every MIDI event due now, then render up to the next one.
            while (ev < midiCount && midi[ev].frame <= pos)
                handleMidi (midi[ev++]);
            uint32_t next = frames;
            if (ev < midiCount)
                next = std::min (frames, std::max (pos + 1, midi[ev].frame));

            const uint32_t n = next - pos;
            for (uint32_t done = 0; done < n;)
            {
                const uint32_t len = std::min<uint32_t> (n - done, kScratch);
                const uint32_t at = pos + done;
                for (uint32_t i = 0; i < len; ++i)
                    mono[i] = 0.5f * (inputs[0][at + i] + inputs[1][at + i]);
                engine.process (mono, outputs[0] + at, outputs[1] + at, (int) len);
                done += len;
            }
            pos = next;
        }
        while (ev < midiCount)
            handleMidi (midi[ev++]);
    }

private:
    void handleMidi (const MidiEvent& e)
    {
        if (e.size < 2 || e.size > MidiEvent::kDataSize)
            return;
        const uint8_t status = e.data[0] & 0xF0;
        const uint8_t d1 = e.data[1];
        const uint8_t d2 = e.size > 2 ? e.data[2] : 0;
        if (status == 0x90 && d2 > 0)            engine.noteOn (d1, d2 / 127.0f);
        else if (status == 0x80 || status == 0x90) engine.noteOff (d1);
        else if (status == 0xB0 && d1 == 64)     engine.setSustain (d2 >= 64);
        else if (status == 0xB0 && (d1 == 123 || d1 == 120)) engine.allNotesOff();
    }

    void apply()
    {
        HarmonizerParams p;
        p.mode = (int) values[pMode];
        p.key = (int) values[pKey];
        p.scale = (int) values[pScale];
        p.autoVoices = (int) values[pAutoVoices];
        for (int i = 0; i < 4; ++i)
            p.autoSteps[(size_t) i] = (int) values[pAuto1 + i];
        p.tonic = values[pTonic] > 0.5f;
        p.tonicOctave = (int) values[pTonicOctave];
        p.dry = dbToGain (values[pDry]);
        p.lead = dbToGain (values[pLead]);
        p.harmony = dbToGain (values[pHarmony]);
        p.spread = values[pSpread] * 0.01f;
        p.output = std::pow (10.0f, values[pOutput] / 20.0f);
        p.voice.formant = std::exp2 (values[pFormant] / 12.0f);
        p.voice.natural = values[pNatural] * 0.01f;
        p.voice.looseCents = values[pLoose];
        p.voice.glideMs = values[pGlide];
        p.voice.attackMs = values[pAttack];
        p.voice.releaseMs = values[pRelease];
        p.voice.consonants = values[pConsonants] * 0.01f;
        engine.setParams (p);
    }

    static constexpr uint32_t kScratch = 1024;
    float values[pCount] {};
    float mono[kScratch] {};
    Harmonizer engine;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxBrewPlugin)
};

Plugin* createPlugin()
{
    return new VoxBrewPlugin();
}

END_NAMESPACE_DISTRHO
