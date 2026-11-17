// Offline checks for the VoxBrew engine. Build and run with `make test`.

#include "../src/dsp/Harmonizer.h"
#include "../tools/SynthVoice.h"

#include <chrono>
#include <cstdio>
#include <functional>

using namespace voxbrew;

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

struct Render
{
    std::vector<float> l, r;
};

static Render run (const std::vector<float>& in, double fs, const HarmonizerParams& p,
                   const std::function<void (Harmonizer&)>& before = {})
{
    Harmonizer h;
    h.prepare (fs);
    h.setParams (p);
    if (before) before (h);
    Render out;
    out.l.assign (in.size(), 0.0f);
    out.r.assign (in.size(), 0.0f);
    const int block = 256;
    for (size_t i = 0; i < in.size(); i += block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - i);
        h.process (in.data() + i, out.l.data() + i, out.r.data() + i, n);
    }
    return out;
}

static std::vector<float> mono (const Render& r)
{
    std::vector<float> m (r.l.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (r.l[i] + r.r[i]);
    return m;
}

static HarmonizerParams only (float lead, float harmony)
{
    HarmonizerParams p;
    p.key = 9; // A
    p.scale = kMajor;
    p.lead = lead;
    p.harmony = harmony;
    p.dry = 0.0f;
    p.autoVoices = 0;
    p.voice.looseCents = 0.0f;
    p.voice.glideMs = 5.0f;
    return p;
}

int main()
{
    const double fs = 48000.0;

    // 1. Tuned lead: a slightly sharp A3 comes out on A3.
    {
        const auto in = test::synthVowel (fs, 2.0, 220.0 * std::exp2 (30.0 / 1200.0));
        const auto out = mono (run (in, fs, only (1.0f, 0.0f)));
        const double hz = test::medianPitch (out, fs, 0.5, 1.8);
        std::printf ("      lead: %.2f Hz (%.1f c from A3)\n", hz, cents (hz, 220.0));
        check (std::abs (cents (hz, 220.0)) < 8.0, "tuned lead pulls +30 c back to A3");
    }

    // 2. MIDI: sing A3, hold E4 -> the harmony voice sings E4.
    {
        const auto in = test::synthVowel (fs, 2.0, 220.0);
        auto p = only (0.0f, 1.0f);
        p.mode = kModeMidi;
        const auto out = mono (run (in, fs, p, [] (Harmonizer& h) { h.noteOn (64, 1.0f); }));
        const double hz = test::medianPitch (out, fs, 0.5, 1.8);
        std::printf ("      midi E4: %.2f Hz (%.1f c)\n", hz, cents (hz, midiToHz (64)));
        check (std::abs (cents (hz, midiToHz (64))) < 8.0, "MIDI note E4 from a sung A3");
    }

    // 3. MIDI down a fifth: sing A3, hold D3.
    {
        const auto in = test::synthVowel (fs, 2.0, 220.0);
        auto p = only (0.0f, 1.0f);
        p.mode = kModeMidi;
        const auto out = mono (run (in, fs, p, [] (Harmonizer& h) { h.noteOn (50, 1.0f); }));
        const double hz = test::medianPitch (out, fs, 0.5, 1.8);
        std::printf ("      midi D3: %.2f Hz (%.1f c)\n", hz, cents (hz, midiToHz (50)));
        check (std::abs (cents (hz, midiToHz (50))) < 8.0, "MIDI note D3 from a sung A3");
    }

    // 4. Auto harmony: a third above A3 in A major is C#4.
    {
        const auto in = test::synthVowel (fs, 2.0, 220.0);
        auto p = only (0.0f, 1.0f);
        p.autoVoices = 1;
        p.autoSteps = { 2, 0, 0, 0 };
        const auto out = mono (run (in, fs, p));
        const double hz = test::medianPitch (out, fs, 0.5, 1.8);
        std::printf ("      auto +3rd: %.2f Hz (%.1f c from C#4)\n", hz, cents (hz, midiToHz (61)));
        check (std::abs (cents (hz, midiToHz (61))) < 8.0, "auto third above in A major = C#4");
    }

    // 5. Tonic drone: key A, octave 2 -> A2 whatever is sung.
    {
        const auto in = test::synthVowel (fs, 2.0, midiToHz (64));
        auto p = only (0.0f, 1.0f);
        p.tonic = true;
        p.tonicOctave = 2;
        const auto out = mono (run (in, fs, p));
        const double hz = test::medianPitch (out, fs, 0.5, 1.8);
        std::printf ("      tonic: %.2f Hz (%.1f c from A2)\n", hz, cents (hz, 110.0));
        check (std::abs (cents (hz, 110.0)) < 8.0, "tonic drone sits on A2");
    }

    // 6. MIDI off means silence from the harmony bus, and it releases.
    {
        const auto in = test::synthVowel (fs, 3.0, 220.0);
        auto p = only (0.0f, 1.0f);
        p.mode = kModeMidi;
        Harmonizer h;
        h.prepare (fs);
        h.setParams (p);
        std::vector<float> l (in.size()), r (in.size());
        h.noteOn (64, 1.0f);
        h.process (in.data(), l.data(), r.data(), 48000);
        h.noteOff (64);
        h.process (in.data() + 48000, l.data() + 48000, r.data() + 48000, (int) in.size() - 48000);
        double tail = 0.0;
        for (size_t i = in.size() - 4800; i < in.size(); ++i) tail = std::max (tail, (double) std::abs (l[i]));
        std::printf ("      tail peak %.5f, active voices %d\n", tail, h.activeVoices());
        check (tail < 1e-3 && h.activeVoices() == 0, "note off releases the voice to silence");
    }

    // 7. Formants stay put: compare spectral centroid of a +7 st shift with the input.
    {
        const auto in = test::synthVowel (fs, 2.0, 165.0);
        auto p = only (0.0f, 1.0f);
        p.mode = kModeMidi;
        const auto out = mono (run (in, fs, p, [] (Harmonizer& h) { h.noteOn (59, 1.0f); }));
        auto centroid = [&] (const std::vector<float>& x, size_t start)
        {
            // Goertzel-free crude centroid from a 4096 DFT.
            const int N = 4096;
            double num = 0, den = 0;
            for (int k = 1; k < N / 2; k += 2)
            {
                double re = 0, im = 0;
                for (int i = 0; i < N; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (2 * M_PI * i / N);
                    re += w * x[start + i] * std::cos (2 * M_PI * k * i / N);
                    im -= w * x[start + i] * std::sin (2 * M_PI * k * i / N);
                }
                const double mag = std::sqrt (re * re + im * im);
                num += mag * k * fs / N;
                den += mag;
            }
            return num / den;
        };
        const double cIn = centroid (in, 48000), cOut = centroid (out, 48000);
        std::printf ("      centroid in %.0f Hz, shifted +7 st %.0f Hz\n", cIn, cOut);
        // A plain resample up a fifth would move it by 1.5x.
        check (cOut / cIn < 1.2 && cOut / cIn > 0.83, "spectral envelope preserved on a +7 st shift");
    }

    // 8. CPU: tuned lead + 4 auto + tonic + 4 MIDI voices.
    {
        const auto in = test::synthVowel (fs, 10.0, 220.0, 30.0);
        auto p = only (1.0f, 0.7f);
        p.mode = kModeAutoAndMidi;
        p.autoVoices = 4;
        p.tonic = true;
        const auto t0 = std::chrono::steady_clock::now();
        run (in, fs, p, [] (Harmonizer& h) { for (int n : { 57, 61, 64, 69 }) h.noteOn (n, 0.8f); });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("      10 voices: %.0fx real time (%.2f%% of one core)\n", 10.0 / secs, 100.0 * secs / 10.0);
        check (secs < 1.0, "10 voices run at least 10x faster than real time");
    }

    Harmonizer h;
    h.prepare (fs);
    std::printf ("\nLatency at 48 kHz: %d samples (%.1f ms)\n", h.getLatencySamples(), 1000.0 * h.getLatencySamples() / fs);
    std::printf ("%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
