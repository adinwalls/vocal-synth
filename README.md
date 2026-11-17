# VoxBrew Harmonizer

*A live vocal harmonizer for Linux, in the spirit of Bon Iver's "Messina".*

Sing into it and it splits your one voice into a stack: a tuned lead, harmonies in key, an optional tonic drone, and voices you play from a MIDI keyboard. Every voice is made from your own voice, so it keeps your vowels and character instead of sounding like a vocoder. MIDI is optional: in **Auto** mode the harmonies follow your singing on their own.

Built on [DPF](https://github.com/DISTRHO/DPF), the same way as the [Adin Brew](https://github.com/adinwalls/adin-brew) plugins. Formats: LV2, VST3 and CLAP (64-bit Linux, glibc 2.35+). Made for Harrison Mixbus 12 on Pop!_OS.

## How it works

```
voice in ──► pitch tracker (YIN, shared) ──► pitch marks
                                              │
            ┌───────────────┬─────────────────┼──────────────┬─────────────────┐
            ▼               ▼                 ▼              ▼                 ▼
       tuned lead     auto voice 1..4     tonic drone    MIDI voices      dry voice
      (snap to key)  (scale steps from   (key root in   (notes you hold,  (delayed to
                      the sung note)     an octave)     up to 10)         line up)
            └──────── each voice: TD-PSOLA grains of your real voice ────────┘
                                         │
                              pan spread + levels ──► stereo out
```

* **One analysis, many cheap voices.** Pitch is tracked once. Each voice only re-spaces two-period grains of the input (TD-PSOLA), which keeps the formants and costs very little: 10 voices use about 3% of one CPU core in the offline test.
* **Tuned lead** is a voice at "0 scale steps", so it's the sung note snapped to the key, like the first Auto-Tune in the Messina chain.
* **Tonic drone** holds the key's root, like the Messina's second Auto-Tune that fed the H8000.
* **Loose** gives each voice its own small detune and slow drift so the stack sounds like a group, not a machine.
* **Latency:** 1664 samples at 48 kHz (35 ms), reported to the host so Mixbus lines up playback. It's set by the lowest pitch tracked (75 Hz).

## Controls

| Control | What it does |
|---|---|
| Harmony Source | **Auto**: harmonies in key follow your voice. **MIDI**: voices play the notes you hold. **Auto + MIDI**: both. |
| Key, Scale | The key the lead snaps to and the auto voices move in. |
| Auto Voices, Auto Voice 1–4 | How many auto voices, and each one's distance in scale steps: +2 = 3rd up, +4 = 5th up, -3 = 4th down, -7 = octave down. |
| Tonic Drone, Tonic Octave | A voice holding the key's root (octave 2 = A2 in A). |
| Dry Voice, Tuned Lead, Harmony Level | Mix levels (-60 dB = off). |
| Stereo Spread | Width of the harmony voices. |
| Formant | Vowel character of the harmonies: up = smaller/brighter, down = bigger/darker. |
| Natural | 0 = voices locked to their notes, 100 = they keep your vibrato and scoops. |
| Loose | Per-voice detune and drift, in cents. |
| Glide, Attack, Release | Note movement and how voices fade in and out. |
| Consonants | How much of your s, t and breath sounds the harmony voices copy. |

## Build and install

```bash
sudo apt install git build-essential pkg-config
git clone --recursive https://github.com/adinwalls/vocal-synth.git
cd vocal-synth
./install.sh        # builds, then copies to ~/.lv2, ~/.vst3, ~/.clap
```

Then rescan plugins in Mixbus and add **VoxBrew Harmonizer** to a vocal track.

### Playing it from a MIDI keyboard in Mixbus

The plugin takes audio and MIDI at the same time. On an audio track, open the plugin's **Pin Connections** (right-click the plugin), add a MIDI side-chain input, and connect your keyboard to it. Set **Harmony Source** to MIDI or Auto + MIDI.

## Offline tests and renders

No DAW needed:

```bash
make test                                   # pitch accuracy, release, formants, CPU
make render
./build/voxbrew_render take.wav out.wav --key A --scale major --auto 2,4 --tonic 2
./build/voxbrew_render take.wav out.wav --mode midi --notes 57,61,64,69 --lead 0
./build/voxbrew_render --synth 220 4 out.wav --auto 2,4,-7     # synthetic test vowel
```

## Code

* `src/dsp/` — the engine, plain C++17 with no framework, so it runs in the plugin and in the tools.
  * `PitchTracker` — YIN on a ~16 kHz decimated copy.
  * `VoiceAnalyzer` — input history and pitch marks shared by all voices.
  * `PsolaVoice` — one voice: TD-PSOLA, glide, detune, envelope.
  * `Harmonizer` — voice slots (lead, auto, tonic, MIDI), MIDI handling, mix.
* `plugins/VoxBrew/` — the DPF plugin wrapper.
* `tests/`, `tools/` — offline tests and the render tool.

Licence: GPL-3.0-or-later, same as Adin Brew.
