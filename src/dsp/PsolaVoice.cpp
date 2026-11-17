#include "PsolaVoice.h"

#include <algorithm>
#include <cmath>

namespace voxbrew
{

void PsolaVoice::prepare (const VoiceAnalyzer& analyzer, uint32_t seed)
{
    fs = analyzer.getSampleRate();
    maxHalf = analyzer.getMaxPeriod();

    size_t size = 1;
    while (size < (size_t) (8 * maxHalf + 8192))
        size <<= 1;
    acc.assign (size, 0.0f);
    wsum.assign (size, 0.0f);
    mask = (int64_t) size - 1;

    rng.seed (seed + 1);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    detuneCents = uni (rng);                 // unit value, scaled by the Loose control
    driftPhase = (uni (rng) + 1.0) * M_PI;
    driftRate = 0.15 + 0.1 * (uni (rng) + 1.0); // 0.15 .. 0.35 Hz
    kill();
}

void PsolaVoice::start (const Target& t, float g, int64_t outputStart)
{
    if (active)
    {
        retarget (t, g);
        return;
    }
    active = true;
    releasing = false;
    target = t;
    gain = g;
    env = 0.0f;
    synthPos = (double) outputStart;
    renderedTo = outputStart;
    currentMidi = -1.0;
    heldNote = -1000;
}

void PsolaVoice::retarget (const Target& t, float g)
{
    target = t;
    gain = g;
    releasing = false;
}

void PsolaVoice::release()
{
    releasing = true;
}

void PsolaVoice::kill()
{
    active = false;
    releasing = false;
    env = 0.0f;
    std::fill (acc.begin(), acc.end(), 0.0f);
    std::fill (wsum.begin(), wsum.end(), 0.0f);
}

double PsolaVoice::targetMidi (const Mark& m, const VoiceSettings& s)
{
    const double sung = hzToMidi (fs / m.period);
    double base = 0.0, deviation = 0.0;

    if (target.kind == Target::Degree)
    {
        const int cand = s.key.nearest (sung);
        if (heldNote < -100 || ! s.key.inScale (heldNote)
            || std::abs (sung - heldNote) - std::abs (sung - cand) > 0.24)
            heldNote = cand;
        base = s.key.shiftDegrees (heldNote, target.value);
        deviation = sung - heldNote;
    }
    else
    {
        base = target.value;
        deviation = sung - std::round (sung);
    }

    return base + s.natural * deviation;
}

void PsolaVoice::render (const VoiceAnalyzer& analyzer, const VoiceSettings& s,
                         int64_t outputStart, int n, float pan, float* outL, float* outR)
{
    if (! active)
        return;

    const int64_t end = outputStart + n;
    if (synthPos < (double) (outputStart - maxHalf))
        synthPos = (double) outputStart;

    const int spacing = analyzer.getUnvoicedSpacing();
    const double formant = std::clamp ((double) s.formant, 0.5, 2.0);

    while (synthPos - maxHalf < (double) end)
    {
        const Mark* m = analyzer.findMark (synthPos);
        if (m == nullptr)
        {
            synthPos += spacing;
            continue;
        }

        double step, half, f = 1.0;
        float level = 1.0f;
        if (m->voiced)
        {
            const double want = targetMidi (*m, s);
            if (currentMidi < 0.0)
                currentMidi = want;
            else
            {
                const double glideSamples = std::max (1.0, s.glideMs * 0.001 * fs);
                currentMidi += (want - currentMidi) * (1.0 - std::exp (-m->period / glideSamples));
            }

            driftPhase += 2.0 * M_PI * driftRate * m->period / fs;
            const double cents = s.looseCents * (detuneCents + 0.5 * std::sin (driftPhase));
            const double hz = midiToHz (currentMidi + cents * 0.01);
            const double ratio = std::clamp (hz * m->period / fs, 0.25, 4.0);

            half = m->period;
            step = m->period / ratio;
            f = formant;
        }
        else
        {
            half = spacing;
            step = spacing;
            const bool lead = target.kind == Target::Degree && target.value == 0;
            level = lead ? 1.0f : s.consonants;
        }

        const int gHalf = std::max (2, (int) std::min ((double) maxHalf, half / f));
        const auto c = (int64_t) std::lround (synthPos);
        const double invHalf = M_PI / gHalf;
        for (int j = -gHalf + 1; j < gHalf; ++j)
        {
            const int64_t p = c + j;
            if (p < renderedTo)
                continue; // already played; never write behind the read head
            const float w = (float) (0.5 + 0.5 * std::cos (j * invHalf));
            const float x = analyzer.sampleInterp ((double) m->pos + j * f);
            const auto idx = (size_t) (p & mask);
            acc[idx] += w * x * level;
            wsum[idx] += w;
        }
        synthPos += step;
    }

    const float aCoef = 1.0f - (float) std::exp (-1.0 / (std::max (0.5f, s.attackMs) * 0.001 * fs));
    const float rCoef = 1.0f - (float) std::exp (-1.0 / (std::max (0.5f, s.releaseMs) * 0.001 * fs));
    const float panL = (float) std::cos ((pan + 1.0) * M_PI * 0.25);
    const float panR = (float) std::sin ((pan + 1.0) * M_PI * 0.25);

    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((outputStart + i) & mask);
        const float v = acc[idx] / std::max (wsum[idx], 1.0f);
        acc[idx] = 0.0f;
        wsum[idx] = 0.0f;

        if (releasing) env -= env * rCoef;
        else           env += (1.0f - env) * aCoef;

        const float y = v * env * gain;
        outL[i] += y * panL;
        outR[i] += y * panR;
    }
    renderedTo = end;

    if (releasing && env < 1.0e-3f)
        kill();
}

} // namespace voxbrew
