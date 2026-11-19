#pragma once
// Clipping protection, in two parts.
//
// StackTrim sits before Mangle and the tape echo. Every voice you add makes
// the mix louder, so a full stack used to hit Mangle's fuzz and the echo's
// tape far harder than the knobs suggest. StackTrim works out how loud the
// mix is from what is switched on and turns a big stack down to the level of
// the default one (tuned lead + two harmonies at -3 dB). It never turns
// anything up, and a mix no bigger than the default is left exactly alone.
//
// Limiter is the last thing in the plugin, after the Output knob: a safety
// net that turns the sound down for a moment whenever a peak would pass the
// ceiling, so the plugin cannot hand the host a clipped signal. It adds no
// delay. Below the ceiling the sound passes through bit for bit.
// Header only, no allocation.

#include <algorithm>
#include <cmath>

namespace voxbrew
{

class StackTrim
{
public:
    // Power of the default mix per channel: lead at 0 dB plus two harmony
    // voices at -3 dB, each harmony voice sharing its power between the sides.
    static constexpr float kReference = 1.0f + 2.0f * 0.5f * 0.5011872f;

    void prepare (double sampleRate)
    {
        coef = 1.0f - (float) std::exp (-1.0 / (0.03 * sampleRate));
        reset();
    }

    void reset() { gain = 1.0f; }

    // `power` is the expected power of the mix against one voice at full level.
    float target (float power) const
    {
        return power > kReference ? std::sqrt (kReference / power) : 1.0f;
    }

    // Eases to the new trim over the block, in place.
    void process (float* l, float* r, int n, float want)
    {
        if (gain == 1.0f && want == 1.0f)
            return;
        for (int i = 0; i < n; ++i)
        {
            gain += (want - gain) * coef;
            l[i] *= gain;
            r[i] *= gain;
        }
        if (std::abs (gain - want) < 1.0e-5f)
            gain = want;
    }

    float current() const { return gain; }

private:
    float gain = 1.0f, coef = 0.001f;
};

class Limiter
{
public:
    static constexpr float kCeiling = 0.966f;   // -0.3 dBFS

    void prepare (double sampleRate)
    {
        holdLen = (int) (0.012 * sampleRate);
        relCoef = 1.0f - (float) std::exp (-1.0 / (0.15 * sampleRate));
        reset();
    }

    void reset() { gain = 1.0f; hold = 0; }

    // Stereo block in place, both sides turned down together so the image
    // stays put. Returns the lowest gain used in the block (1 = untouched).
    float process (float* l, float* r, int n)
    {
        float low = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
            if (! (peak < 1.0e6f))
            {
                // a broken number from somewhere upstream: silence, never noise
                l[i] = r[i] = 0.0f;
                continue;
            }
            // let go slowly once the hold has run out, then check this sample:
            // it always has the last word, so nothing passes the ceiling
            if (hold > 0)
                --hold;
            else if (gain < 1.0f)
            {
                gain += (1.0f - gain) * relCoef;
                if (gain > 0.99999f) gain = 1.0f;
            }
            if (peak * gain > kCeiling)
            {
                gain = kCeiling / peak;
                hold = holdLen;
            }
            if (gain < 1.0f)
            {
                l[i] *= gain;
                r[i] *= gain;
                low = std::min (low, gain);
            }
        }
        return low;
    }

private:
    float gain = 1.0f, relCoef = 0.001f;
    int hold = 0, holdLen = 512;
};

} // namespace voxbrew
