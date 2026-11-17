#pragma once

#include "PitchTracker.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace voxbrew
{

// A pitch mark: one analysis point in the input, used as the centre of a
// PSOLA grain. Voiced marks sit on the waveform peak of each glottal period.
struct Mark
{
    int64_t pos = 0;     // absolute input sample index
    float period = 0.0f; // grain half length in samples (the local pitch period when voiced)
    bool voiced = false;
};

// Holds the input history and the pitch marks shared by every harmony voice.
class VoiceAnalyzer
{
public:
    void prepare (double sampleRate, float minF0, float maxF0);
    void reset();

    // Append input and update pitch + marks.
    void process (const float* in, int n);

    int64_t written() const { return total; }
    double getSampleRate() const { return fs; }
    int getMaxPeriod() const { return maxPeriod; }
    int getUnvoicedSpacing() const { return unvoicedSpacing; }
    float getF0() const { return tracker.getF0(); }

    float sample (int64_t pos) const
    {
        return ring[(size_t) (pos & mask)];
    }

    float sampleInterp (double pos) const
    {
        const double fl = std::floor (pos);
        const auto i = (int64_t) fl;
        const float frac = (float) (pos - fl);
        const float a = sample (i), b = sample (i + 1);
        return a + (b - a) * frac;
    }

    // The mark nearest `pos` whose full grain is already in the buffer.
    // Returns nullptr when none exists yet.
    const Mark* findMark (double pos) const;

    // Drop marks older than `pos`.
    void pruneBefore (int64_t pos);

    PitchTracker& getTracker() { return tracker; }

private:
    void placeMarks();

    double fs = 48000.0;
    PitchTracker tracker;
    Biquad markLp1, markLp2;

    std::vector<float> ring, lpRing;
    int64_t mask = 0;
    int64_t total = 0;

    int maxPeriod = 600;
    int unvoicedSpacing = 256;

    std::vector<Mark> marks; // ascending by pos, capacity reserved up front
};

} // namespace voxbrew
