#pragma once
// A crude synthetic sung vowel for tests: band-limited glottal pulse train
// through three formant resonators ("ah"), with optional vibrato.

#include "../src/dsp/PitchTracker.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace voxbrew::test
{

struct Resonator
{
    double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0;
    void set (double fs, double freq, double bw)
    {
        const double r = std::exp (-M_PI * bw / fs);
        a1 = 2.0 * r * std::cos (2.0 * M_PI * freq / fs);
        a2 = -r * r;
        g = 1.0 - r;
    }
    double process (double x)
    {
        const double y = g * x + a1 * y1 + a2 * y2;
        y2 = y1; y1 = y;
        return y;
    }
};

inline std::vector<float> synthVowel (double fs, double seconds, double hz,
                                      double vibratoCents = 0.0, double vibratoHz = 5.5)
{
    const auto n = (size_t) (fs * seconds);
    std::vector<float> out (n);
    Resonator f1, f2, f3;
    f1.set (fs, 700, 110);
    f2.set (fs, 1220, 120);
    f3.set (fs, 2600, 160);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double f0 = hz * std::exp2 (vibratoCents / 1200.0 * std::sin (2.0 * M_PI * vibratoHz * t));
        phase += f0 / fs;
        phase -= std::floor (phase);
        double x = 0.0;
        const int harmonics = (int) (5000.0 / f0);
        for (int k = 1; k <= harmonics; ++k)
            x += std::sin (2.0 * M_PI * k * phase) / k;
        const double y = f1.process (x) * 1.0 + f2.process (x) * 0.6 + f3.process (x) * 0.3;
        const double fade = std::min (1.0, std::min (t, seconds - t) / 0.02);
        out[i] = (float) (y * fade);
    }
    // Normalise to about -12 dBFS peak.
    float peak = 1e-9f;
    for (float v : out) peak = std::max (peak, std::abs (v));
    for (float& v : out) v *= 0.25f / peak;
    return out;
}

// Median detected pitch over [from, to) seconds of a signal.
inline double medianPitch (const std::vector<float>& x, double fs, double from, double to)
{
    voxbrew::PitchTracker t;
    t.prepare (fs, 60.0f, 1200.0f);
    std::vector<double> est;
    const int hop = 256;
    for (size_t i = 0; i + hop <= x.size(); i += hop)
    {
        t.push (x.data() + i, hop);
        const double time = (double) (i + hop) / fs;
        if (time >= from && time < to && t.getF0() > 0)
            est.push_back (t.getF0());
    }
    if (est.empty()) return 0.0;
    std::sort (est.begin(), est.end());
    return est[est.size() / 2];
}

} // namespace voxbrew::test
