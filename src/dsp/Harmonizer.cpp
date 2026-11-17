#include "Harmonizer.h"

#include <algorithm>
#include <cmath>

namespace voxbrew
{

void Harmonizer::prepare (double sampleRate, float minF0, float maxF0)
{
    analyzer.prepare (sampleRate, minF0, maxF0);
    // The newest usable pitch mark trails the input by up to ~2.35 periods and a
    // grain reaches one period ahead of its centre. 2.5 periods keeps latency
    // down; when the lowest notes run ahead, the voice reuses the newest mark.
    fullLatency = (int) std::ceil (2.5 * analyzer.getMaxPeriod()) + 64;
    shortLatency = (int) std::ceil (sampleRate * 0.003);
    latency = lowLatency ? shortLatency : fullLatency;

    for (int i = 0; i < kNumVoices; ++i)
        voices[(size_t) i].prepare (analyzer, 0x9E3779B9u * (uint32_t) (i + 1));

    // Lead in the middle; everything else alternates outwards.
    static const float spreadTable[kNumVoices] = { 0.0f, -0.6f, 0.6f, -0.9f, 0.9f, 0.0f,
                                                   -0.3f, 0.3f, -0.75f, 0.75f, -0.5f, 0.5f,
                                                   -1.0f, 1.0f, -0.15f, 0.15f };
    std::copy (spreadTable, spreadTable + kNumVoices, pans.begin());

    leadL.assign (kChunk, 0.0f);
    leadR.assign (kChunk, 0.0f);
    harmL.assign (kChunk, 0.0f);
    harmR.assign (kChunk, 0.0f);
    reset();
}

void Harmonizer::reset()
{
    analyzer.reset();
    for (auto& v : voices)
        v.kill();
    midiNote.fill (-1);
    sustained.fill (false);
    sustain = false;
}

void Harmonizer::setLowLatency (bool on)
{
    if (on == lowLatency)
        return;
    lowLatency = on;
    latency = on ? shortLatency : fullLatency;
    params.voice.lowLatency = on;
    // Voice positions are tied to the old delay: restart them cleanly.
    for (auto& v : voices)
        v.kill();
    for (int i = kMidiSlot; i < kNumVoices; ++i)
        if (midiNote[(size_t) i] >= 0)
        {
            voices[(size_t) i].start ({ Target::Midi, midiNote[(size_t) i] }, 0.8f, analyzer.written() - latency);
            voices[(size_t) i].order = ++orderCounter;
        }
}

void Harmonizer::setParams (const HarmonizerParams& p)
{
    const bool midiWas = midiEnabled();
    params = p;
    params.voice.key = { std::clamp (p.key, 0, 11), scaleMask (p.scale) };
    params.voice.lowLatency = lowLatency;
    if (midiWas && ! midiEnabled())
        allNotesOff();
}

int Harmonizer::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        n += v.isActive() ? 1 : 0;
    return n;
}

void Harmonizer::noteOn (int note, float velocity)
{
    if (! midiEnabled())
        return;

    const float gain = 0.4f + 0.6f * std::clamp (velocity, 0.0f, 1.0f);
    const Target t { Target::Midi, note };
    const int64_t outputStart = analyzer.written() - latency;

    int slot = -1;
    for (int i = kMidiSlot; i < kNumVoices; ++i)
        if (midiNote[(size_t) i] == note && voices[(size_t) i].isActive()) { slot = i; break; }

    if (slot < 0)
        for (int i = kMidiSlot; i < kNumVoices; ++i)
            if (! voices[(size_t) i].isActive()) { slot = i; break; }

    if (slot < 0)
    {
        // Steal: prefer the oldest releasing voice, then the oldest one.
        uint64_t best = UINT64_MAX;
        for (int pass = 0; pass < 2 && slot < 0; ++pass)
            for (int i = kMidiSlot; i < kNumVoices; ++i)
            {
                const auto& v = voices[(size_t) i];
                if ((pass == 1 || v.isReleasing()) && v.order < best) { best = v.order; slot = i; }
            }
        voices[(size_t) slot].kill();
    }

    auto& v = voices[(size_t) slot];
    v.start (t, gain, outputStart);
    v.order = ++orderCounter;
    midiNote[(size_t) slot] = note;
    sustained[(size_t) slot] = false;
}

void Harmonizer::noteOff (int note)
{
    for (int i = kMidiSlot; i < kNumVoices; ++i)
    {
        if (midiNote[(size_t) i] != note || ! voices[(size_t) i].isActive())
            continue;
        if (sustain)
            sustained[(size_t) i] = true;
        else
        {
            voices[(size_t) i].release();
            midiNote[(size_t) i] = -1;
        }
    }
}

void Harmonizer::setSustain (bool down)
{
    sustain = down;
    if (down)
        return;
    for (int i = kMidiSlot; i < kNumVoices; ++i)
        if (sustained[(size_t) i])
        {
            voices[(size_t) i].release();
            midiNote[(size_t) i] = -1;
            sustained[(size_t) i] = false;
        }
}

void Harmonizer::allNotesOff()
{
    for (int i = kMidiSlot; i < kNumVoices; ++i)
    {
        if (voices[(size_t) i].isActive())
            voices[(size_t) i].release();
        midiNote[(size_t) i] = -1;
        sustained[(size_t) i] = false;
    }
}

void Harmonizer::updateFixedVoices (int64_t outputStart)
{
    auto want = [&] (int slot, bool on, const Target& t)
    {
        auto& v = voices[(size_t) slot];
        if (on)
        {
            if (! v.isActive() || v.isReleasing()
                || v.getTarget().kind != t.kind || v.getTarget().value != t.value)
                v.start (t, 1.0f, outputStart);
        }
        else if (v.isActive() && ! v.isReleasing())
            v.release();
    };

    want (kLeadSlot, params.lead > 0.0001f, { Target::Degree, 0 });

    for (int i = 0; i < 4; ++i)
        want (kAutoSlot + i, autoEnabled() && i < params.autoVoices,
              { Target::Degree, params.autoSteps[(size_t) i] });

    const int tonicNote = 12 * (params.tonicOctave + 1) + params.voice.key.key;
    want (kTonicSlot, params.tonic, { Target::Fixed, tonicNote });
}

void Harmonizer::process (const float* in, float* outL, float* outR, int n)
{
    for (int done = 0; done < n;)
    {
        const int len = std::min (kChunk, n - done);
        processChunk (in + done, outL + done, outR + done, len);
        done += len;
    }
}

void Harmonizer::processChunk (const float* in, float* outL, float* outR, int n)
{
    analyzer.process (in, n);
    const int64_t outputStart = analyzer.written() - latency;
    analyzer.pruneBefore (outputStart - 4 * analyzer.getMaxPeriod());

    updateFixedVoices (outputStart);

    std::fill (leadL.begin(), leadL.begin() + n, 0.0f);
    std::fill (leadR.begin(), leadR.begin() + n, 0.0f);
    std::fill (harmL.begin(), harmL.begin() + n, 0.0f);
    std::fill (harmR.begin(), harmR.begin() + n, 0.0f);

    for (int i = 0; i < kNumVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.isActive())
            continue;
        if (i == kLeadSlot)
            v.render (analyzer, params.voice, outputStart, n, 0.0f, leadL.data(), leadR.data());
        else
            v.render (analyzer, params.voice, outputStart, n, pans[(size_t) i] * params.spread,
                      harmL.data(), harmR.data());
    }

    for (int i = 0; i < kNumVoices; ++i)
        meterVoice[(size_t) i].store (voices[(size_t) i].getMeterMidi(), std::memory_order_relaxed);
    const float hz = analyzer.getF0();
    meterInput.store (hz > 0.0f ? (float) hzToMidi (hz) : 0.0f, std::memory_order_relaxed);

    // Lead is rendered centred (pan 0 = -3 dB per side); undo that so a
    // centred lead keeps the input's level.
    const float leadG = params.lead * (float) M_SQRT2;
    for (int i = 0; i < n; ++i)
    {
        const float d = analyzer.sample (outputStart + i) * params.dry;
        outL[i] = params.output * (d + leadL[(size_t) i] * leadG + harmL[(size_t) i] * params.harmony);
        outR[i] = params.output * (d + leadR[(size_t) i] * leadG + harmR[(size_t) i] * params.harmony);
    }
}

} // namespace voxbrew
