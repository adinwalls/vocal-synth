#include "VoiceAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace voxbrew
{

void VoiceAnalyzer::prepare (double sampleRate, float minF0, float maxF0)
{
    fs = sampleRate;
    tracker.prepare (fs, minF0, maxF0);
    maxPeriod = (int) std::ceil (fs / minF0);
    unvoicedSpacing = std::max (32, (int) (fs * 0.005));

    // Low-passed copy of the input for placing marks on the fundamental's peaks
    // rather than on formant ripple.
    markLp1.setLowpass (fs, 900.0);
    markLp2.setLowpass (fs, 900.0);

    size_t size = 1;
    while (size < (size_t) (fs * 0.5)) // half a second of history is plenty
        size <<= 1;
    ring.assign (size, 0.0f);
    lpRing.assign (size, 0.0f);
    mask = (int64_t) size - 1;

    marks.clear();
    marks.reserve (8192);
    reset();
}

void VoiceAnalyzer::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (lpRing.begin(), lpRing.end(), 0.0f);
    tracker.reset();
    markLp1.reset();
    markLp2.reset();
    total = 0;
    marks.clear();
}

void VoiceAnalyzer::process (const float* in, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((total + i) & mask);
        ring[idx] = in[i];
        lpRing[idx] = markLp2.process (markLp1.process (in[i]));
    }
    total += n;
    tracker.push (in, n);
    placeMarks();
}

void VoiceAnalyzer::placeMarks()
{
    const float period = tracker.getPeriod();
    const bool voiced = period > 0.0f;

    if (marks.empty())
        marks.push_back ({ total - (int64_t) maxPeriod * 2, (float) unvoicedSpacing, false });

    for (;;)
    {
        if (marks.size() >= marks.capacity())
            break; // pruning keeps this from happening in practice

        const Mark& last = marks.back();

        if (voiced)
        {
            const auto p = (int64_t) std::lround (period);
            const auto half = (int64_t) std::lround (period * 0.35f);
            const int64_t cand = last.pos + p;
            if (cand + half >= total)
                break;

            int64_t lo = std::max (cand - half, last.pos + p / 2);
            int64_t best = lo;
            float bestVal = -1e9f;
            for (int64_t i = lo; i <= cand + half; ++i)
            {
                const float v = lpRing[(size_t) (i & mask)];
                if (v > bestVal) { bestVal = v; best = i; }
            }
            marks.push_back ({ best, period, true });
        }
        else
        {
            const int64_t cand = last.pos + unvoicedSpacing;
            if (cand >= total)
                break;
            marks.push_back ({ cand, (float) unvoicedSpacing, false });
        }
    }
}

const Mark* VoiceAnalyzer::findMark (double pos) const
{
    if (marks.empty())
        return nullptr;

    // Binary search for the first mark at or after pos.
    auto it = std::lower_bound (marks.begin(), marks.end(), pos,
                                [] (const Mark& m, double p) { return (double) m.pos < p; });

    auto usable = [this] (const Mark& m) { return m.pos + (int64_t) std::ceil (m.period) + 1 < total; };

    const Mark* best = nullptr;
    if (it != marks.end() && usable (*it))
        best = &*it;
    if (it != marks.begin())
    {
        const Mark& prev = *(it - 1);
        if (usable (prev) && (best == nullptr || pos - (double) prev.pos < (double) best->pos - pos))
            best = &prev;
    }

    if (best == nullptr)
    {
        // Running ahead of the analysis: fall back to the newest complete mark.
        for (auto r = marks.rbegin(); r != marks.rend(); ++r)
            if (usable (*r))
                return &*r;
    }
    return best;
}

void VoiceAnalyzer::pruneBefore (int64_t pos)
{
    auto it = std::lower_bound (marks.begin(), marks.end(), pos,
                                [] (const Mark& m, int64_t p) { return m.pos < p; });
    // Always keep at least one mark so placement can continue from it.
    if (it == marks.end() && ! marks.empty())
        --it;
    marks.erase (marks.begin(), it);
}

} // namespace voxbrew
