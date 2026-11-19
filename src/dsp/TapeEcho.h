#pragma once
// Tape echo: the job of the slide-out tape deck. A loop of "tape" that plays
// the sound back a little later and feeds it round again. Each time round the
// repeat gets duller and softer-edged, and Age makes the tape speed waver.
// Header only. Nothing runs, and the sound is untouched, while Level is 0.

#include <algorithm>
#include <cmath>
#include <vector>

namespace voxbrew
{

struct EchoSettings
{
    float level = 0.0f;      // 0 = off
    float timeMs = 380.0f;   // gap between repeats
    float repeats = 0.4f;    // how much goes round again (0 .. 0.95)
    float age = 0.4f;        // worn tape: darker, wobblier, more squashed repeats
};

class TapeEcho
{
public:
    static constexpr float kMaxMs = 1200.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size_t size = 1;
        while (size < (size_t) (fs * (kMaxMs * 0.001 + 0.1)))
            size <<= 1;
        bufL.assign (size, 0.0f);
        bufR.assign (size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        w = 0;
        delay = -1.0;
        lpL = lpR = hpL = hpR = 0.0f;
        cLevel = 0.0f;
        wow = flutter = 0.0;
        running = false;
    }

    void process (float* l, float* r, int n, const EchoSettings& s)
    {
        const float level = std::clamp (s.level, 0.0f, 1.0f);
        if (level < 1.0e-4f && cLevel < 1.0e-4f)
        {
            if (running) reset();   // start from clean tape next time
            return;
        }
        running = true;

        const float age = std::clamp (s.age, 0.0f, 1.0f);
        const float fb = std::clamp (s.repeats, 0.0f, 0.95f);
        const double want = std::clamp (s.timeMs, 40.0f, kMaxMs) * 0.001 * fs;
        if (delay < 0.0) delay = want;
        // the "motor" takes a moment to reach a new speed, so turning Time bends the pitch
        const double slew = 1.0 - std::exp (-1.0 / (0.12 * fs));
        const float lpCoef = 1.0f - (float) std::exp (-2.0 * M_PI * (7000.0 - 5200.0 * age) / fs);
        const float hpCoef = 1.0f - (float) std::exp (-2.0 * M_PI * 110.0 / fs);
        const float drive = 1.0f + 2.5f * age;
        const double wowDepth = age * age * 0.0035 * fs, flutterDepth = age * 0.00025 * fs;
        const double wowInc = 2.0 * M_PI * 0.7 / fs, flutterInc = 2.0 * M_PI * 6.1 / fs;

        for (int i = 0; i < n; ++i)
        {
            cLevel += (level - cLevel) * 0.001f;
            delay += (want - delay) * slew;
            wow += wowInc; flutter += flutterInc;
            if (wow > 2.0 * M_PI) wow -= 2.0 * M_PI;
            if (flutter > 2.0 * M_PI) flutter -= 2.0 * M_PI;
            const double mod = wowDepth * (1.0 + std::sin (wow)) + flutterDepth * (1.0 + std::sin (flutter));

            float yl = read (bufL, delay + mod);
            float yr = read (bufR, delay + mod * 0.8 + 0.0004 * fs);   // the two channels never quite agree

            // what the tape does to a sound each time round: duller, thinner, softer-edged
            lpL += (yl - lpL) * lpCoef; lpR += (yr - lpR) * lpCoef;
            hpL += (lpL - hpL) * hpCoef; hpR += (lpR - hpR) * hpCoef;
            yl = std::tanh ((lpL - hpL) * drive) / drive;
            yr = std::tanh ((lpR - hpR) * drive) / drive;

            bufL[w] = l[i] + yl * fb;
            bufR[w] = r[i] + yr * fb;
            w = (w + 1) & mask;

            l[i] += yl * cLevel;
            r[i] += yr * cLevel;
        }
    }

private:
    float read (const std::vector<float>& b, double d) const
    {
        const double p = (double) w - d;
        const double fl = std::floor (p);
        const float frac = (float) (p - fl);
        const auto i = (size_t) ((long long) fl) & mask;
        const float a = b[i], c = b[(i + 1) & mask];
        return a + (c - a) * frac;
    }

    double fs = 48000.0;
    std::vector<float> bufL, bufR;
    size_t mask = 0, w = 0;
    double delay = -1.0, wow = 0.0, flutter = 0.0;
    float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f, cLevel = 0.0f;
    bool running = false;
};

} // namespace voxbrew
