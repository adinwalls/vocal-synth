#pragma once

#include <array>
#include <vector>

namespace voxbrew
{

// Simple RBJ biquad, used for anti-alias filtering before decimation and for
// the low-passed signal used to place pitch marks.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void setLowpass (double fs, double cutoff, double q = 0.7071);
    void reset() { z1 = z2 = 0; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Monophonic pitch tracker based on YIN, run on a decimated copy of the input
// (around 16 kHz) to keep CPU low. The period it reports is in samples at the
// full sample rate.
class PitchTracker
{
public:
    void prepare (double sampleRate, float minF0, float maxF0);
    void reset();

    void push (const float* x, int n);

    // Period in full-rate samples, or 0 when the input is unvoiced / silent.
    float getPeriod() const { return period; }
    float getF0() const { return period > 0.0f ? (float) (fs / period) : 0.0f; }
    float getConfidence() const { return confidence; }

    // Below this RMS the input counts as silence.
    void setGate (float rms) { gateRms = rms; }

private:
    void analyse();

    double fs = 48000.0;
    int decim = 3;
    int decimCounter = 0;
    Biquad aa1, aa2;

    int tauMin = 16, tauMax = 200, window = 400;
    std::vector<float> ring;   // decimated history
    int ringPos = 0;
    int hop = 128;
    int sinceHop = 0;

    std::vector<float> lin, diff;

    float period = 0.0f;
    float confidence = 0.0f;
    float gateRms = 0.003f;
    int unvoicedHops = 0;
    std::array<float, 3> history { 0, 0, 0 };
};

} // namespace voxbrew
