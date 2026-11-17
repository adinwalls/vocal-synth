#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace voxbrew
{

// Same scale list and order as TuneBrew, so presets/keys read the same.
enum Scale
{
    kChromatic = 0, kMajor, kMinor, kHarmonicMinor, kMelodicMinor,
    kMajorPent, kMinorPent, kBlues, kDorian, kMixolydian, kScaleCount
};

inline uint16_t scaleMask (int scale)
{
    auto m = [] (std::initializer_list<int> steps)
    {
        uint16_t r = 0;
        for (int s : steps) r |= (uint16_t) (1u << s);
        return r;
    };
    switch (scale)
    {
        case kMajor:          return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case kMinor:          return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case kHarmonicMinor:  return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case kMelodicMinor:   return m ({ 0, 2, 3, 5, 7, 9, 11 });
        case kMajorPent:      return m ({ 0, 2, 4, 7, 9 });
        case kMinorPent:      return m ({ 0, 3, 5, 7, 10 });
        case kBlues:          return m ({ 0, 3, 5, 6, 7, 10 });
        case kDorian:         return m ({ 0, 2, 3, 5, 7, 9, 10 });
        case kMixolydian:     return m ({ 0, 2, 4, 5, 7, 9, 10 });
        default:              return 0xFFF;
    }
}

struct KeyContext
{
    int key = 0;          // 0 = C .. 11 = B
    uint16_t mask = 0xFFF;

    bool inScale (int note) const
    {
        const int pc = ((note - key) % 12 + 12) % 12;
        return (mask >> pc) & 1u;
    }

    // Nearest in-scale note to a fractional MIDI pitch.
    int nearest (double midi) const
    {
        const int base = (int) std::lround (midi);
        for (int d = 0; d < 12; ++d)
        {
            const int up = base + d, dn = base - d;
            const bool okUp = inScale (up), okDn = inScale (dn);
            if (okUp && okDn)
                return std::abs (midi - up) <= std::abs (midi - dn) ? up : dn;
            if (okUp) return up;
            if (okDn) return dn;
        }
        return base;
    }

    // Move `steps` scale degrees from an in-scale note.
    int shiftDegrees (int note, int steps) const
    {
        const int dir = steps > 0 ? 1 : -1;
        for (int s = 0; s != steps; s += dir)
        {
            do { note += dir; } while (! inScale (note));
        }
        return note;
    }
};

inline double hzToMidi (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline double midiToHz (double m)  { return 440.0 * std::exp2 ((m - 69.0) / 12.0); }

} // namespace voxbrew
