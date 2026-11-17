// Render a vocal through the VoxBrew engine offline, to listen to settings
// without a DAW.
//
//   voxbrew_render in.wav out.wav --key A --scale major --auto 2,4 --tonic 2
//   voxbrew_render in.wav out.wav --mode midi --notes 57,61,64,69
//   voxbrew_render --synth 220 4 out.wav --auto 2,4,-7
//
// MIDI notes given with --notes are held for the whole file.

#include "../src/dsp/Harmonizer.h"
#include "SynthVoice.h"
#include "Wav.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

using namespace voxbrew;

static std::vector<int> intList (const char* s)
{
    std::vector<int> v;
    std::stringstream ss (s);
    std::string item;
    while (std::getline (ss, item, ','))
        if (! item.empty()) v.push_back (std::atoi (item.c_str()));
    return v;
}

static int parseKey (std::string k)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    if (! k.empty()) k[0] = (char) std::toupper (k[0]);
    for (int i = 0; i < 12; ++i)
        if (k == names[i] || k == flats[i]) return i;
    std::fprintf (stderr, "unknown key %s\n", k.c_str());
    std::exit (2);
}

static int parseScale (const std::string& s)
{
    static const char* names[] = { "chromatic", "major", "minor", "harmonic-minor", "melodic-minor",
                                   "major-pent", "minor-pent", "blues", "dorian", "mixolydian" };
    for (int i = 0; i < kScaleCount; ++i)
        if (s == names[i]) return i;
    std::fprintf (stderr, "unknown scale %s\n", s.c_str());
    std::exit (2);
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr,
            "usage: voxbrew_render in.wav out.wav [options]\n"
            "       voxbrew_render --synth HZ SECONDS out.wav [options]\n"
            "options: --key A --scale major --mode auto|midi|both --auto 2,4,-3\n"
            "         --notes 57,61,64 --tonic OCTAVE --dry X --lead X --harmony X\n"
            "         --formant X --natural X --loose CENTS --glide MS --spread X\n"
            "         --low-latency\n");
        return 2;
    }

    wav::Audio in;
    std::string outPath;
    int argi = 1;
    if (! std::strcmp (argv[1], "--synth"))
    {
        if (argc < 5) return 2;
        in.sampleRate = 48000;
        in.channels = 1;
        in.data = test::synthVowel (48000.0, std::atof (argv[3]), std::atof (argv[2]), 25.0);
        outPath = argv[4];
        argi = 5;
    }
    else
    {
        if (! wav::read (argv[1], in)) { std::fprintf (stderr, "can't read %s\n", argv[1]); return 1; }
        outPath = argv[2];
        argi = 3;
    }

    HarmonizerParams p;
    std::vector<int> notes;
    bool lowLatency = false;
    for (; argi < argc; argi += 2)
    {
        const std::string o = argv[argi];
        if (o == "--low-latency") { lowLatency = true; --argi; continue; }
        if (argi + 1 >= argc) { std::fprintf (stderr, "%s needs a value\n", o.c_str()); return 2; }
        const char* v = argv[argi + 1];
        if (o == "--key") p.key = parseKey (v);
        else if (o == "--scale") p.scale = parseScale (v);
        else if (o == "--mode") p.mode = ! std::strcmp (v, "midi") ? kModeMidi : ! std::strcmp (v, "both") ? kModeAutoAndMidi : kModeAuto;
        else if (o == "--auto")
        {
            const auto steps = intList (v);
            p.autoVoices = (int) std::min<size_t> (4, steps.size());
            for (int i = 0; i < p.autoVoices; ++i) p.autoSteps[(size_t) i] = steps[(size_t) i];
        }
        else if (o == "--notes") notes = intList (v);
        else if (o == "--tonic") { p.tonic = true; p.tonicOctave = std::atoi (v); }
        else if (o == "--dry") p.dry = (float) std::atof (v);
        else if (o == "--lead") p.lead = (float) std::atof (v);
        else if (o == "--harmony") p.harmony = (float) std::atof (v);
        else if (o == "--formant") p.voice.formant = (float) std::atof (v);
        else if (o == "--natural") p.voice.natural = (float) std::atof (v);
        else if (o == "--loose") p.voice.looseCents = (float) std::atof (v);
        else if (o == "--glide") p.voice.glideMs = (float) std::atof (v);
        else if (o == "--spread") p.spread = (float) std::atof (v);
        else { std::fprintf (stderr, "unknown option %s\n", o.c_str()); return 2; }
    }
    if (! notes.empty() && p.mode == kModeAuto)
        p.mode = kModeAutoAndMidi;

    // Mix the input down to mono.
    const size_t frames = in.frames();
    std::vector<float> mono (frames);
    for (size_t i = 0; i < frames; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < in.channels; ++c) s += in.data[i * (size_t) in.channels + (size_t) c];
        mono[i] = s / (float) in.channels;
    }

    Harmonizer h;
    h.prepare (in.sampleRate);
    h.setLowLatency (lowLatency);
    h.setParams (p);
    for (int n : notes) h.noteOn (n, 0.9f);

    // Feed extra silence so the output isn't cut short by the latency, then drop the latency.
    const int lat = h.getLatencySamples();
    mono.resize (frames + (size_t) lat, 0.0f);
    std::vector<float> l (mono.size()), r (mono.size());
    for (size_t i = 0; i < mono.size(); i += 512)
    {
        const int n = (int) std::min<size_t> (512, mono.size() - i);
        h.process (mono.data() + i, l.data() + i, r.data() + i, n);
    }

    wav::Audio out;
    out.sampleRate = in.sampleRate;
    out.channels = 2;
    out.data.resize (frames * 2);
    for (size_t i = 0; i < frames; ++i)
    {
        out.data[2 * i] = l[i + (size_t) lat];
        out.data[2 * i + 1] = r[i + (size_t) lat];
    }
    if (! wav::writeFloat (outPath, out)) { std::fprintf (stderr, "can't write %s\n", outPath.c_str()); return 1; }
    std::printf ("wrote %s (%.1f s, latency %d samples)\n", outPath.c_str(), (double) frames / in.sampleRate, lat);
    return 0;
}
