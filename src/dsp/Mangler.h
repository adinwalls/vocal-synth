#pragma once
// Mangle: the lo-fi / damage section. Part of it lives in the voices (Scatter
// and Wobble act on the PSOLA grains, see PsolaVoice), the rest is here and
// works on the finished mix: Crush, Hiss and Crackle.
// Header only, no allocation, nothing runs while every amount is zero.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace voxbrew
{

struct MangleSettings
{
    float amount = 0.0f;    // master: 0 = off. Scales every flavour below.
    float scatter = 0.5f;   // grains played roughly, shuffled, repeated, dropped
    float wobble = 0.4f;    // tape-style pitch wow and flutter, shared by all voices
    float crush = 0.5f;     // fuzz, then fewer samples and fewer bits
    float hiss = 0.5f;      // noise bed that swells with the voice
    float crackle = 0.5f;   // record-style ticks and pops
};

// Tiny fast random source (xorshift32).
struct Noise
{
    uint32_t s = 0x1234567u;
    void seed (uint32_t v) { s = v ? v : 0x1234567u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0 .. 1
    float bi() { return uni() * 2.0f - 1.0f; }                             // -1 .. 1
};

class Mangler
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        invFs = (float) (1.0 / sampleRate);
        envRel = (float) std::exp (-1.0 / (0.15 * fs));
        hissLpCoef = 1.0f - (float) std::exp (-2.0 * M_PI * 900.0 / fs);
        reset();
    }

    void reset()
    {
        rng.seed (0xC0FFEE11u);
        cCrush = cHiss = cCrackle = 0.0f;
        env = 0.0f;
        holdPhase = 0.0f; holdLen = 1.0f; heldL = heldR = 0.0f;
        hissLpL = hissLpR = 0.0f;
        popL = popR = 0.0f; popDecay = 0.0f; wait = 0.0f;
        wowPhase = flutterPhase = 0.0; wowRate = 0.6; wowCount = 0;
    }

    // Tape wow and flutter for the next `n` samples, in cents. `depth` is 0..1.
    float wobble (float depth, int n)
    {
        if (depth <= 0.0005f)
            return 0.0f;
        wowCount -= n;
        if (wowCount <= 0)
        {
            // the "motor" speed wanders, so the wow never settles into a steady vibrato
            wowRate = 0.35 + 0.75 * rng.uni();
            wowCount = (int) (fs * (0.4 + 1.2 * rng.uni()));
        }
        wowPhase += 2.0 * M_PI * wowRate * n / fs;
        flutterPhase += 2.0 * M_PI * 6.7 * n / fs;
        if (wowPhase > 2.0 * M_PI) wowPhase -= 2.0 * M_PI;
        if (flutterPhase > 2.0 * M_PI) flutterPhase -= 2.0 * M_PI;
        const float d = depth * std::sqrt (depth);
        return d * (55.0f * (float) std::sin (wowPhase) + 12.0f * (float) std::sin (flutterPhase));
    }

    // Crush, Hiss and Crackle on a stereo block, in place. Amounts are 0..1.
    void process (float* l, float* r, int n, float crush, float hiss, float crackle)
    {
        // ease towards the knob positions so a turn never jumps
        cCrush += (crush - cCrush) * 0.2f;
        cHiss += (hiss - cHiss) * 0.2f;
        cCrackle += (crackle - cCrackle) * 0.2f;
        if (cCrush < 1.0e-4f && cHiss < 1.0e-4f && cCrackle < 1.0e-4f)
        {
            cCrush = cHiss = cCrackle = 0.0f;
            return;
        }

        const bool doCrush = cCrush >= 1.0e-4f;
        const float drive = 1.0f + 3.0f * cCrush;
        const float driveMix = std::min (1.0f, cCrush * 4.0f);
        const float driveGain = 1.0f / std::pow (drive, 0.8f);
        const float rate = 22000.0f * std::pow (2200.0f / 22000.0f, cCrush);   // 22 kHz .. 2.2 kHz
        const float baseLen = std::max (1.0f, (float) fs / rate);
        const float steps = std::exp2 (13.0f - 10.0f * cCrush);               // 14 bits .. 4 bits
        const float holdMix = std::min (1.0f, cCrush * 3.0f);

        const float hissFloor = cHiss * cHiss * 0.02f, hissFollow = cHiss * cHiss * 0.13f;

        const bool doCrackle = cCrackle >= 1.0e-4f;
        const float popsPerSec = cCrackle * (2.0f + 70.0f * cCrackle);

        for (int i = 0; i < n; ++i)
        {
            float xl = l[i], xr = r[i];

            // how loud the voice is right now: hiss and crackle swell with it
            const float a = std::abs (xl + xr) * 1.5f;
            env = a > env ? a : env * envRel;
            const float e = std::min (1.0f, env);

            if (doCrush)
            {
                // soft fuzz first
                xl += (std::tanh (xl * drive) * driveGain - xl) * driveMix;
                xr += (std::tanh (xr * drive) * driveGain - xr) * driveMix;
                // then an old sampler: hold each sample for a while and round it off
                holdPhase += 1.0f;
                if (holdPhase >= holdLen)
                {
                    holdPhase -= holdLen;
                    holdLen = baseLen * (1.0f + 0.25f * cCrush * rng.bi());
                    heldL = std::round (xl * steps) / steps;
                    heldR = std::round (xr * steps) / steps;
                }
                xl += (heldL - xl) * holdMix;
                xr += (heldR - xr) * holdMix;
            }

            if (doCrackle)
            {
                // `wait` counts down in "expected pops", so the pace follows the knob at once
                wait -= popsPerSec * (0.3f + 0.7f * e) * invFs;
                if (wait <= 0.0f)
                {
                    wait = -std::log (std::max (1.0e-6f, rng.uni()));
                    const bool pop = rng.uni() < 0.15f;
                    const float u = rng.uni();
                    const float amp = cCrackle * (pop ? 0.18f + 0.45f * u : 0.02f + 0.16f * u * u)
                                      * (rng.uni() < 0.5f ? -1.0f : 1.0f);
                    const float ms = pop ? 0.6f + 1.8f * rng.uni() : 0.04f + 0.25f * rng.uni();
                    popDecay = (float) std::exp (-1.0 / (ms * 0.001 * fs));
                    const float pan = rng.uni();
                    popL += amp * std::sqrt (1.0f - pan);
                    popR += amp * std::sqrt (pan);
                }
                xl += popL; xr += popR;
                popL *= popDecay; popR *= popDecay;
            }

            // bright tape hiss, a little different left and right
            const float g = hissFloor + hissFollow * e;
            const float nl = rng.bi(), nr = rng.bi();
            hissLpL += (nl - hissLpL) * hissLpCoef;
            hissLpR += (nr - hissLpR) * hissLpCoef;
            xl += (nl - hissLpL) * g;
            xr += (nr - hissLpR) * g;

            l[i] = xl; r[i] = xr;
        }
    }

private:
    double fs = 48000.0;
    Noise rng;
    float cCrush = 0.0f, cHiss = 0.0f, cCrackle = 0.0f;
    float env = 0.0f, envRel = 0.999f;
    float holdPhase = 0.0f, holdLen = 1.0f, heldL = 0.0f, heldR = 0.0f;
    float hissLpL = 0.0f, hissLpR = 0.0f, hissLpCoef = 0.1f;
    float popL = 0.0f, popR = 0.0f, popDecay = 0.0f;
    float wait = 0.0f, invFs = 1.0f / 48000.0f;
    double wowPhase = 0.0, flutterPhase = 0.0, wowRate = 0.6;
    int wowCount = 0;
};

} // namespace voxbrew
