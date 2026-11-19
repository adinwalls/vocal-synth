#include "Harmonizer.h"

#include <algorithm>
#include <cmath>

namespace voxbrew
{

void Harmonizer::prepare (double sampleRate, float minF0, float maxF0)
{
    fs = sampleRate;
    analyzer.prepare (sampleRate, minF0, maxF0);
    mangler.prepare (sampleRate);
    echo.prepare (sampleRate);
    trim.prepare (sampleRate);
    limiter.prepare (sampleRate);
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
    mangler.reset();
    echo.reset();
    trim.reset();
    limiter.reset();
    for (auto& v : voices)
        v.kill();
    midiNote.fill (-1);
    sustained.fill (false);
    sustain = false;
    written.fill (false);
    anyWritten = false;
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
    if (midiEnabled())
        startMidiVoice (note, velocity);
}

void Harmonizer::startMidiVoice (int note, float velocity)
{
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

void Harmonizer::releaseMidiVoice (int note)
{
    for (int i = kMidiSlot; i < kNumVoices; ++i)
        if (midiNote[(size_t) i] == note && voices[(size_t) i].isActive())
        {
            voices[(size_t) i].release();
            midiNote[(size_t) i] = -1;
            sustained[(size_t) i] = false;
        }
}

void Harmonizer::setTimeline (bool known, bool playing, double seconds)
{
    tlValid = known && playing;
    tlSeconds = seconds;
    meterPlaying.store (tlValid, std::memory_order_relaxed);
    meterPos.store (known ? seconds : -1.0, std::memory_order_relaxed);
}

// Starts and stops voices for the notes written on the timeline. `seconds` is
// the song position of the sound leaving the plugin now, or negative to stop.
void Harmonizer::updateWrittenNotes (double seconds)
{
    std::array<bool, 128> want {};
    bool any = false;
    if (seconds >= 0.0 && params.mode == kModeNotes)
    {
        const NoteSet& ns = notes();
        for (int i = 0; i < ns.n; ++i)
        {
            if (ns.r[i].start > seconds) break; // sorted by start
            if (seconds < ns.r[i].end) { want[(size_t) ns.r[i].note] = true; any = true; }
        }
    }
    if (! any && ! anyWritten)
        return;
    for (int n = 0; n < 128; ++n)
    {
        if (want[(size_t) n] && ! written[(size_t) n]) startMidiVoice (n, 0.8f);
        else if (! want[(size_t) n] && written[(size_t) n]) releaseMidiVoice (n);
    }
    written = want;
    anyWritten = any;
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
    // The sound leaving now is the input from exactly `latency` samples ago,
    // whatever the size of this chunk. (Measuring back from the end of the chunk
    // made the delay depend on the chunk size, so any host block that is not a
    // multiple of kChunk skipped or repeated a few samples at every block: a
    // crackle on everything, the dry voice included.)
    const int64_t outputStart = analyzer.written() - n - latency;
    analyzer.pruneBefore (outputStart - 4 * analyzer.getMaxPeriod());

    if (tlValid)
    {
        // Learn the melody where it was sung (the tracker answers about 20 ms late),
        // and play written notes for the sound that is leaving the plugin now.
        const float hzNow = analyzer.getF0();
        scan.write (tlSeconds - 0.02, hzNow > 0.0f ? hzToMidi (hzNow) : 0.0);
        updateWrittenNotes (std::max (0.0, tlSeconds - latency / fs));
        tlSeconds += n / fs;
    }
    else
        updateWrittenNotes (-1.0);

    updateFixedVoices (outputStart);

    const MangleSettings& mg = params.mangle;
    const float mangle = std::clamp (mg.amount, 0.0f, 1.0f);
    params.voice.scatter = mangle * std::clamp (mg.scatter, 0.0f, 1.0f);
    params.voice.wobbleCents = mangler.wobble (mangle * std::clamp (mg.wobble, 0.0f, 1.0f), n);

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
        outL[i] = d + leadL[(size_t) i] * leadG + harmL[(size_t) i] * params.harmony;
        outR[i] = d + leadR[(size_t) i] * leadG + harmR[(size_t) i] * params.harmony;
    }

    // Clipping protection, part one: a stack bigger than the default one is
    // turned down to the default's level, so Mangle and the echo see a level
    // that does not depend on how many voices are on.
    if (params.protect)
    {
        float harm = 0.0f;
        for (int i = 0; i < kNumVoices; ++i)
            if (i != kLeadSlot)
                harm += voices[(size_t) i].level() * voices[(size_t) i].level();
        const float lead = params.lead * voices[(size_t) kLeadSlot].level();
        const float power = params.dry * params.dry + lead * lead + 0.5f * params.harmony * params.harmony * harm;
        trim.process (outL, outR, n, trim.target (power));
        meterTrimDb.store (20.0f * std::log10 (trim.current()), std::memory_order_relaxed);
    }

    mangler.process (outL, outR, n, mangle * std::clamp (mg.crush, 0.0f, 1.0f),
                     mangle * std::clamp (mg.hiss, 0.0f, 1.0f), mangle * std::clamp (mg.crackle, 0.0f, 1.0f));

    echo.process (outL, outR, n, params.echo);

    for (int i = 0; i < n; ++i)
    {
        outL[i] *= params.output;
        outR[i] *= params.output;
    }

    // Part two: nothing leaves the plugin above the ceiling.
    if (params.protect)
    {
        const float low = limiter.process (outL, outR, n);
        meterLimitDb.store (low < 1.0f ? 20.0f * std::log10 (low) : 0.0f, std::memory_order_relaxed);
    }
}

} // namespace voxbrew
