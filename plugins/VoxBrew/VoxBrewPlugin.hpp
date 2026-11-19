// VoxBrew Harmonizer: DPF wrapper around voxbrew::Harmonizer.
// Kept in a header so the editor, which runs in the same process, can read
// the live voice pitches straight from the engine.
#pragma once

#include "DistrhoPlugin.hpp"
#include "../../src/VoxPresets.h"

#include <cstring>

START_NAMESPACE_DISTRHO

using namespace voxbrew;

class VoxBrewPlugin : public Plugin
{
public:
    VoxBrewPlugin() : Plugin (pCount, kNumPresets, 2)
    {
        for (int i = 0; i < pCount; ++i)
            values[i] = kSpecs[i].def;
        engine.prepare (getSampleRate());
        apply();
        setLatency ((uint32_t) engine.getLatencySamples());
    }

    // for the editor (same process)
    const Harmonizer* enginePtr() const { return &engine; }

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
    uint32_t getVersion() const override { return d_version (0, 8, 0); }

    void initAudioPort (bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort (input, index, port);
    }

    void initParameter (uint32_t index, Parameter& p) override
    {
        const ParamSpec& s = kSpecs[index];
        p.name = s.name;
        p.shortName = s.shortName;
        p.symbol = s.symbol;
        p.unit = s.unit;
        p.description = s.description;
        p.ranges.min = s.min;
        p.ranges.max = s.max;
        p.ranges.def = s.def;

        switch (s.kind)
        {
            case kOutput: p.hints = kParameterIsOutput; break;
            case kFloat:  p.hints = kParameterIsAutomatable; break;
            case kInt:    p.hints = kParameterIsAutomatable | kParameterIsInteger; break;
            case kBool:   p.hints = kParameterIsAutomatable | kParameterIsInteger | kParameterIsBoolean; break;
            case kChoice:
            {
                p.hints = kParameterIsAutomatable | kParameterIsInteger;
                const int first = (int) s.min, count = (int) (s.max - s.min) + 1;
                auto* v = new ParameterEnumerationValue[(size_t) count];
                for (int i = 0; i < count; ++i)
                {
                    char buf[48];
                    choiceLabel ((int) index, first + i, buf, sizeof buf);
                    v[i].value = (float) (first + i);
                    v[i].label = buf;
                }
                p.enumValues.count = (uint8_t) count;
                p.enumValues.restrictedMode = true;
                p.enumValues.values = v;
                break;
            }
        }
    }

    void initProgramName (uint32_t index, String& name) override
    {
        name = kPresets[index].name;
    }

    void loadProgram (uint32_t index) override
    {
        applyPreset ((int) index, values);
        apply();
    }

    // ---- session state: the written notes and the melody scan ----
    void initState (uint32_t index, State& state) override
    {
        if (index == 0)
        {
            state.key = "notes"; state.label = "Harmony Notes"; state.defaultValue = "";
            state.description = "Harmony notes on the song timeline (start ms, end ms, MIDI note)";
        }
        else
        {
            state.key = "scan"; state.label = "Melody Scan"; state.defaultValue = "";
            state.hints = kStateIsOnlyForDSP;
            state.description = "Your sung pitch along the song, shown in the Notes view";
        }
    }

    String getState (const char* key) const override
    {
        if (std::strcmp (key, "notes") == 0) return String (engine.notes().encode().c_str());
        if (std::strcmp (key, "scan") == 0) return String (engine.scan.encode().c_str());
        return String();
    }

    void setState (const char* key, const char* value) override
    {
        if (std::strcmp (key, "notes") == 0)
        {
            NoteSet ns; ns.decode (value);
            engine.setNotes (ns);
        }
        else if (std::strcmp (key, "scan") == 0)
        {
            if (value && *value) engine.scan.decode (value);
        }
    }

    float getParameterValue (uint32_t index) const override
    {
        if (index == pOutPitch)  return engine.getInputHz();
        if (index == pOutVoices) return (float) engine.activeVoices();
        return index < pCount ? values[index] : 0.0f;
    }

    void setParameterValue (uint32_t index, float value) override
    {
        if (index >= pCount || kSpecs[index].kind == kOutput)
            return;
        values[index] = value;
        if (index == pLowLatency)
        {
            engine.setLowLatency (value > 0.5f);
            setLatency ((uint32_t) engine.getLatencySamples());
        }
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
#if defined(DISTRHO_PLUGIN_TARGET_CLAP)
        engine.setTimeline (false, false, 0.0);   // DPF gives CLAP plugins no song position
#else
        const TimePosition& tp = getTimePosition();
        engine.setTimeline (true, tp.playing, (double) tp.frame / getSampleRate());
#endif
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
        if (status == 0x90 && d2 > 0)              engine.noteOn (d1, d2 / 127.0f);
        else if (status == 0x80 || status == 0x90) engine.noteOff (d1);
        else if (status == 0xB0 && d1 == 64)       engine.setSustain (d2 >= 64);
        else if (status == 0xB0 && (d1 == 123 || d1 == 120)) engine.allNotesOff();
    }

    void apply()
    {
        engine.setParams (toEngine (values));
    }

    static constexpr uint32_t kScratch = 1024;
    float values[pCount] {};
    float mono[kScratch] {};
    Harmonizer engine;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxBrewPlugin)
};

END_NAMESPACE_DISTRHO
