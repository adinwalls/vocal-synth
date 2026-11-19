// Offline checks for the VoxBrew engine. Build and run with `make test`.

#include "../src/VoxPresets.h"
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

static bool gLowLatency = false;

static Render run (const std::vector<float>& in, double fs, const HarmonizerParams& p,
                   const std::function<void (Harmonizer&)>& before = {})
{
    Harmonizer h;
    h.prepare (fs);
    h.setLowLatency (gLowLatency);
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

    for (int pass = 0; pass < 2; ++pass)
    {
    gLowLatency = pass == 1;
    std::printf ("%s\n", pass ? "-- low latency --" : "-- full latency --");
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

    }
    gLowLatency = false;

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

    // 9. Measured delay: silence then a vowel; when does the tuned lead reach half level?
    {
        std::vector<float> in (24000, 0.0f);
        const auto v = test::synthVowel (fs, 1.5, 220.0);
        in.insert (in.end(), v.begin(), v.end());
        auto onset = [&] (const std::vector<float>& x)
        {
            float peak = 0.0f;
            for (float s : x) peak = std::max (peak, std::abs (s));
            for (size_t i = 0; i < x.size(); ++i)
                if (std::abs (x[i]) > 0.5f * peak) return (double) i;
            return 0.0;
        };
        const double t0 = onset (in);
        double d[2];
        for (int pass = 0; pass < 2; ++pass)
        {
            gLowLatency = pass == 1;
            d[pass] = 1000.0 * (onset (mono (run (in, fs, only (1.0f, 0.0f)))) - t0) / fs;
        }
        gLowLatency = false;
        std::printf ("      onset delay at A3: full %.1f ms, low latency %.1f ms\n", d[0], d[1]);
        check (d[1] < 20.0 && d[1] < d[0] - 10.0, "low latency mode responds in under 20 ms at A3");
    }

    // 10. Low latency stays clean on a moving pitch: harmony voice follows a wide vibrato.
    {
        const auto in = test::synthVowel (fs, 3.0, 196.0, 60.0);
        auto p = only (0.0f, 1.0f);
        p.mode = kModeMidi;
        double hz[2];
        for (int pass = 0; pass < 2; ++pass)
        {
            gLowLatency = pass == 1;
            const auto out = mono (run (in, fs, p, [] (Harmonizer& h) { h.noteOn (62, 1.0f); }));
            hz[pass] = test::medianPitch (out, fs, 0.5, 2.8);
        }
        gLowLatency = false;
        std::printf ("      D4 from G3 with 60 c vibrato: full %.1f c, low latency %.1f c\n",
                     cents (hz[0], midiToHz (62)), cents (hz[1], midiToHz (62)));
        check (std::abs (cents (hz[1], midiToHz (62))) < 8.0, "low latency holds the note under vibrato");
    }

    // 11. Factory presets: values in range, key left alone, and each one makes sound in tune.
    {
        bool inRange = true, keepsKey = true, sounds = true;
        const auto in = test::synthVowel (fs, 1.5, 220.0);
        for (int i = 0; i < kNumPresets; ++i)
        {
            float v[pCount];
            for (int k = 0; k < pCount; ++k) v[k] = kSpecs[k].def;
            v[pKey] = 9; v[pLowLatency] = 1;
            applyPreset (i, v);
            for (int k = 0; k < pCount; ++k)
                inRange = inRange && v[k] >= kSpecs[k].min && v[k] <= kSpecs[k].max;
            keepsKey = keepsKey && v[pKey] == 9 && v[pLowLatency] == 1;
            const auto out = mono (run (in, fs, toEngine (v), [] (Harmonizer& h) { h.noteOn (64, 0.9f); }));
            double peak = 0.0;
            for (size_t k = 24000; k < out.size(); ++k) peak = std::max (peak, (double) std::abs (out[k]));
            std::printf ("      %-14s peak %.2f\n", kPresets[i].name, peak);
            sounds = sounds && peak > 0.05 && peak < 1.0;
        }
        check (inRange, "every preset value is inside its control's range");
        check (keepsKey, "presets leave Key and Low Latency alone");
        check (sounds, "every preset produces sound without clipping on a -12 dB voice");
    }

    // 12. Suggested notes: play a three-note melody along the timeline, then ask for a third above.
    {
        std::vector<float> in (48000, 0.0f);           // song starts with a second of silence
        const int melody[3] = { 57, 59, 61 };          // A3 B3 C#4, slightly out of tune, with gaps
        for (int m : melody)
        {
            const auto v = test::synthVowel (fs, 0.6, midiToHz (m) * 1.008, 20.0);
            in.insert (in.end(), v.begin(), v.end());
            in.insert (in.end(), 9600, 0.0f);
        }
        Harmonizer h;
        h.prepare (fs);
        h.setParams (only (1.0f, 0.0f));
        std::vector<float> l (512), r (512);
        for (size_t i = 0; i + 512 <= in.size(); i += 512)
        {
            h.setTimeline (true, true, (double) i / fs);
            h.process (in.data() + i, l.data(), r.data(), 512);
        }
        const int steps[1] = { 2 };
        NoteSet ns;
        suggestNotes (h.scan, { 9, scaleMask (kMajor) }, steps, 1, ns);
        const int expect[3] = { 61, 62, 64 };          // C#4 D4 E4
        bool ok = ns.n == 3;
        for (int i = 0; i < ns.n; ++i)
        {
            char nb[16]; noteName (ns.r[i].note, nb, sizeof nb);
            std::printf ("      suggested %-4s %.2f - %.2f s\n", nb, ns.r[i].start, ns.r[i].end);
            const double start = 1.0 + i * 0.8;
            ok = ok && i < 3 && ns.r[i].note == expect[i]
                 && std::abs (ns.r[i].start - start) < 0.06 && std::abs (ns.r[i].end - (start + 0.6)) < 0.06;
        }
        check (ok, "a sung A3 B3 C#4 gets suggested thirds C#4 D4 E4 at the right times");

        // text round trip, as saved in the session
        NoteSet back; back.decode (ns.encode().c_str());
        bool same = back.n == ns.n;
        for (int i = 0; same && i < ns.n; ++i)
            same = back.r[i].note == ns.r[i].note && std::abs (back.r[i].start - ns.r[i].start) < 0.001
                   && std::abs (back.r[i].end - ns.r[i].end) < 0.001;
        MelodyScan scan2;
        const bool scanOk = scan2.decode (h.scan.encode().c_str()) && scan2.len.load() == h.scan.len.load()
                            && scan2.at (130) == h.scan.at (130);
        check (same && scanOk, "notes and the melody scan survive saving and loading");
    }

    // 13. Written notes play in time: E4 from 0.5 to 1.0 s over a held A3, silent before and after.
    {
        const auto in = test::synthVowel (fs, 2.0, 220.0);
        auto p = only (0.0f, 1.0f);
        p.mode = kModeNotes;
        p.voice.releaseMs = 30.0f;
        Harmonizer h;
        h.prepare (fs);
        h.setParams (p);
        NoteSet ns; ns.add (0.5, 1.0, 64);
        h.setNotes (ns);
        std::vector<float> l (in.size(), 0.0f), r (in.size(), 0.0f);
        for (size_t i = 0; i + 256 <= in.size(); i += 256)
        {
            h.setTimeline (true, true, (double) i / fs);
            h.process (in.data() + i, l.data() + i, r.data() + i, 256);
        }
        // undo the plugin's latency, as the host does
        const size_t lat = (size_t) h.getLatencySamples();
        std::vector<float> out (l.begin() + (long) lat, l.end());
        auto rms = [&] (double from, double to)
        {
            double e = 0.0; size_t n = 0;
            for (size_t i = (size_t) (from * fs); i < (size_t) (to * fs) && i < out.size(); ++i, ++n) e += out[i] * out[i];
            return std::sqrt (e / std::max<size_t> (1, n));
        };
        const double hz = test::medianPitch (out, fs, 0.6, 0.95);
        const double before = rms (0.1, 0.45), during = rms (0.6, 0.95), after = rms (1.3, 1.8);
        std::printf ("      written E4: %.1f c, level before %.4f during %.4f after %.4f\n",
                     cents (hz, midiToHz (64)), before, during, after);
        check (std::abs (cents (hz, midiToHz (64))) < 8.0 && before < during * 0.02 && after < during * 0.02,
               "a written note sounds on pitch between its start and end only");

        // stopping the transport lets go of the note
        h.setTimeline (true, true, 0.7);
        h.process (in.data(), l.data(), r.data(), 4800);
        h.setTimeline (true, false, 0.8);
        h.process (in.data(), l.data(), r.data(), 48000);
        check (h.activeVoices() == 0, "written notes stop when the song stops");
    }

    // 14. Mangle: off is exactly the clean sound, on stays sane all the way up.
    {
        auto in = test::synthVowel (fs, 2.0, 220.0);
        in.resize (in.size() + 48000, 0.0f);   // then a second of silence
        auto p = only (1.0f, 0.7f);
        p.autoVoices = 2;
        const auto clean = run (in, fs, p);

        auto off = p;
        off.mangle = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        const auto offOut = run (in, fs, off);
        check (offOut.l == clean.l && offOut.r == clean.r, "Mangle at 0 leaves the sound untouched, whatever its other knobs say");

        auto rms = [&] (const Render& x, double from, double to)
        {
            double e = 0.0;
            const auto a = (size_t) (from * fs), b = (size_t) (to * fs);
            for (size_t i = a; i < b; ++i) e += 0.5 * (x.l[i] * x.l[i] + x.r[i] * x.r[i]);
            return std::sqrt (e / (double) (b - a));
        };
        const double ref = rms (clean, 0.5, 1.9);
        bool finite = true, level = true, heard = true;
        for (float amount : { 0.1f, 0.5f, 1.0f })
        {
            auto m = p;
            m.mangle = { amount, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
            const auto out = run (in, fs, m);
            double peak = 0.0, diff = 0.0;
            for (size_t i = 0; i < out.l.size(); ++i)
            {
                finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
                peak = std::max (peak, (double) std::max (std::abs (out.l[i]), std::abs (out.r[i])));
                diff += std::abs (out.l[i] - clean.l[i]);
            }
            diff /= (double) out.l.size();
            const double sung = rms (out, 0.5, 1.9), quiet = rms (out, 2.6, 2.95);
            std::printf ("      mangle %3.0f%%: sung %+.1f dB vs clean, peak %.2f, in the gaps %.1f dB\n",
                         amount * 100.0, 20.0 * std::log10 (sung / ref), peak, 20.0 * std::log10 (quiet + 1e-9));
            level = level && sung > ref * 0.35 && sung < ref * 2.0 && peak < 1.0 && quiet < ref;
            heard = heard && diff > 1.0e-4;
        }
        check (finite, "full Mangle never produces a broken number");
        check (level, "Mangle keeps the level within 9 dB of the clean sound and never clips");
        check (heard, "even a little Mangle is audible");

        auto full = p;
        full.mode = kModeMidi;
        full.autoVoices = 0;
        full.mangle = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        const auto ten = test::synthVowel (fs, 10.0, 220.0);
        const auto t0 = std::chrono::steady_clock::now();
        run (ten, fs, full, [] (Harmonizer& h) { for (int n = 0; n < 10; ++n) h.noteOn (52 + 2 * n, 1.0f); });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("      10 voices, full Mangle: %.0fx real time (%.2f%% of one core)\n", 10.0 / secs, 100.0 * secs / 10.0);
        check (secs < 1.0, "full Mangle on 10 voices still runs at least 10x faster than real time");
    }

    // 15. Tape echo: off is exactly the clean sound; on, a repeat arrives after Time and they die away.
    {
        auto in = test::synthVowel (fs, 0.2, 220.0);
        in.resize ((size_t) (fs * 4.0), 0.0f);
        auto p = only (1.0f, 0.0f);
        const auto clean = run (in, fs, p);
        auto off = p;
        off.echo = { 0.0f, 300.0f, 0.9f, 1.0f };
        const auto offOut = run (in, fs, off);
        check (offOut.l == clean.l && offOut.r == clean.r, "tape echo at 0 leaves the sound untouched");

        auto on = p;
        on.echo = { 1.0f, 500.0f, 0.5f, 0.3f };
        const auto out = run (in, fs, on);
        auto rms = [&] (const Render& x, double from, double to)
        {
            double e = 0.0;
            const auto a = (size_t) (from * fs), b = (size_t) (to * fs);
            for (size_t i = a; i < b; ++i) e += x.l[i] * x.l[i];
            return std::sqrt (e / (double) (b - a));
        };
        // the note sounds from about 0.03 to 0.25 s; repeats follow every 0.5 s
        const double note = rms (out, 0.08, 0.2), gap = rms (out, 0.35, 0.5),
                     first = rms (out, 0.58, 0.7), second = rms (out, 1.08, 1.2), late = rms (out, 3.58, 3.7);
        bool finite = true;
        double peak = 0.0;
        for (float v : out.l) { finite = finite && std::isfinite (v); peak = std::max (peak, (double) std::abs (v)); }
        std::printf ("      note %.4f, gap %.4f, repeat 1 %.4f, repeat 2 %.4f, after 3.5 s %.5f\n", note, gap, first, second, late);
        check (finite && peak < 1.0 && first > note * 0.3 && gap < first * 0.2 && second < first * 0.8 && second > first * 0.2
               && late < first * 0.05, "tape echo repeats arrive on time and fade away");

        auto wild = p;
        wild.echo = { 1.0f, 60.0f, 0.95f, 1.0f };
        const auto ten = test::synthVowel (fs, 10.0, 220.0);
        const auto w = run (ten, fs, wild);
        double wp = 0.0;
        for (float v : w.l) wp = std::max (wp, (double) std::abs (v));
        std::printf ("      shortest time, most repeats, oldest tape: peak %.2f\n", wp);
        check (wp < 1.5, "tape echo never runs away, even at its most extreme");
    }

    // 16. Just Mangle: at 0 the knobs stand as they are; at full everything is at full.
    {
        float v[pCount];
        for (int k = 0; k < pCount; ++k) v[k] = kSpecs[k].def;
        v[pMangle] = 20; v[pHiss] = 30;
        const auto a = toEngine (v);
        v[pJustMangle] = 50;
        const auto b = toEngine (v);
        v[pJustMangle] = 100;
        const auto c = toEngine (v);
        check (std::abs (a.mangle.amount - 0.2f) < 1e-6f && std::abs (a.mangle.hiss - 0.3f) < 1e-6f
               && b.mangle.amount > a.mangle.amount && b.mangle.hiss > a.mangle.hiss && b.mangle.amount < 1.0f
               && c.mangle.amount == 1.0f && c.mangle.scatter == 1.0f && c.mangle.crackle == 1.0f,
               "Just Mangle lifts every Mangle setting from its knob towards full");
    }

    Harmonizer h;
    h.prepare (fs);
    std::printf ("\nReported latency at 48 kHz: %d samples (%.1f ms)", h.getLatencySamples(), 1000.0 * h.getLatencySamples() / fs);
    h.setLowLatency (true);
    std::printf (", low latency mode %d samples (%.1f ms)\n", h.getLatencySamples(), 1000.0 * h.getLatencySamples() / fs);
    std::printf ("%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
