#include "PitchTracker.h"

#include <algorithm>
#include <cmath>

namespace voxbrew
{

void Biquad::setLowpass (double fs, double cutoff, double q)
{
    const double w0 = 2.0 * M_PI * cutoff / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (float) ((1.0 - cw) * 0.5 / a0);
    b1 = (float) ((1.0 - cw) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * cw / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void PitchTracker::prepare (double sampleRate, float minF0, float maxF0)
{
    fs = sampleRate;
    decim = std::max (1, (int) std::lround (fs / 16000.0));
    const double fsd = fs / decim;

    aa1.setLowpass (fs, fsd * 0.4);
    aa2.setLowpass (fs, fsd * 0.4);

    tauMin = std::max (2, (int) std::floor (fsd / maxF0));
    tauMax = (int) std::ceil (fsd / minF0) + 2;
    window = tauMax;                   // one longest period of integration
    hop = std::max (32, (int) (fsd * 0.004)); // ~4 ms between estimates

    ring.assign ((size_t) (window + tauMax + 2), 0.0f);
    lin.assign (ring.size(), 0.0f);
    diff.assign ((size_t) tauMax + 2, 0.0f);
    reset();
}

void PitchTracker::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = 0;
    sinceHop = 0;
    decimCounter = 0;
    aa1.reset();
    aa2.reset();
    period = 0.0f;
    confidence = 0.0f;
    unvoicedHops = 0;
    history = { 0, 0, 0 };
}

void PitchTracker::push (const float* x, int n)
{
    const int size = (int) ring.size();
    for (int i = 0; i < n; ++i)
    {
        const float y = aa2.process (aa1.process (x[i]));
        if (++decimCounter < decim)
            continue;
        decimCounter = 0;

        ring[(size_t) ringPos] = y;
        ringPos = (ringPos + 1) % size;

        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            analyse();
        }
    }
}

void PitchTracker::analyse()
{
    const int size = (int) ring.size();
    for (int i = 0; i < size; ++i)
        lin[(size_t) i] = ring[(size_t) ((ringPos + i) % size)];

    // Energy gate on the most recent window.
    double energy = 0.0;
    for (int i = size - window; i < size; ++i)
        energy += (double) lin[(size_t) i] * lin[(size_t) i];
    const float rms = (float) std::sqrt (energy / window);

    float found = 0.0f;
    float best = 1.0f;

    if (rms > gateRms)
    {
        // YIN difference function over the oldest `window` samples vs lag tau.
        const float* xs = lin.data();
        diff[0] = 1.0f;
        double running = 0.0;
        int candidate = -1;
        float globalMin = 1e9f;
        int globalArg = -1;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const float e = xs[j] - xs[j + tau];
                d += (double) e * e;
            }
            running += d;
            const float cmnd = running > 0.0 ? (float) (d * tau / running) : 1.0f;
            diff[(size_t) tau] = cmnd;

            if (tau >= tauMin && tau < tauMax)
            {
                if (cmnd < globalMin) { globalMin = cmnd; globalArg = tau; }
            }
        }

        // Take the first dip that is nearly as deep as the deepest one. (Taking the
        // first dip under a fixed threshold, as plain YIN does, jumps up an octave
        // on real voices with a strong second harmonic, and falling back to the
        // deepest dip drops an octave on breathy or changing notes.)
        if (globalMin < 0.3f)
        {
            for (int tau = tauMin + 1; tau < tauMax - 1; ++tau)
                if (diff[(size_t) tau] < globalMin + 0.06f
                    && diff[(size_t) tau] <= diff[(size_t) tau - 1] && diff[(size_t) tau] <= diff[(size_t) tau + 1])
                {
                    candidate = tau;
                    break;
                }
            if (candidate < 0)
                candidate = globalArg;
        }

        if (candidate > 0)
        {
            // Parabolic interpolation around the minimum.
            const float a = diff[(size_t) candidate - 1];
            const float b = diff[(size_t) candidate];
            const float c = diff[(size_t) candidate + 1];
            const float denom = a - 2.0f * b + c;
            float offset = 0.0f;
            if (std::abs (denom) > 1e-9f)
                offset = std::clamp (0.5f * (a - c) / denom, -0.5f, 0.5f);
            found = ((float) candidate + offset) * (float) decim;
            best = b;
        }
    }

    if (found > 0.0f)
    {
        unvoicedHops = 0;
        history = { history[1], history[2], found };
        // Median of the last three voiced estimates removes single-hop octave slips.
        std::array<float, 3> s = history;
        if (s[0] <= 0.0f) s[0] = found;
        if (s[1] <= 0.0f) s[1] = found;
        std::sort (s.begin(), s.end());
        period = s[1];
        confidence = 1.0f - best;
    }
    else
    {
        // Short hangover keeps marks continuous through brief dropouts.
        if (++unvoicedHops > 3)
        {
            period = 0.0f;
            confidence = 0.0f;
            history = { 0, 0, 0 };
        }
    }
}

} // namespace voxbrew
