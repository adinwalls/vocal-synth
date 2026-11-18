#pragma once
// Suggested notes: data shared by the VoxBrew engine and editor.
//   * MelodyScan: your sung pitch along the song timeline, learned while the
//     song plays, so the editor can show the take and suggest harmonies for it
//   * NoteSet: harmony notes on the song timeline. They start life as
//     suggestions and can then be moved, stretched, added or deleted.
// Both are saved with the session as short text. The scan and the text
// formats follow TuneBrew's Phrase Pull (adin-brew, same author).

#include "Scale.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace voxbrew
{

struct Note
{
    double start = 0.0, end = 0.0; // seconds on the song timeline
    int note = 60;                 // MIDI note
};

struct NoteSet
{
    static constexpr int kMax = 512;
    int n = 0;
    Note r[kMax];

    void sort()
    {
        std::sort (r, r + n, [] (const Note& a, const Note& b)
                   { return a.start != b.start ? a.start < b.start : a.note < b.note; });
    }

    bool add (double start, double end, int note)
    {
        if (n >= kMax || end <= start)
            return false;
        r[n++] = { start, end, std::clamp (note, 0, 127) };
        return true;
    }

    void remove (int i)
    {
        if (i < 0 || i >= n) return;
        for (int k = i; k + 1 < n; ++k) r[k] = r[k + 1];
        --n;
    }

    // "start_ms,end_ms,note;..."
    std::string encode() const
    {
        std::string s;
        char buf[64];
        for (int i = 0; i < n; ++i)
        {
            std::snprintf (buf, sizeof buf, "%ld,%ld,%d;", std::lround (r[i].start * 1000.0),
                           std::lround (r[i].end * 1000.0), r[i].note);
            s += buf;
        }
        return s;
    }

    void decode (const char* c)
    {
        n = 0;
        if (! c) return;
        while (*c && n < kMax)
        {
            long v[3]; int k = 0;
            for (; k < 3; ++k)
            {
                char* e = nullptr;
                v[k] = std::strtol (c, &e, 10);
                if (e == c) break;
                c = e;
                if (*c == ',') ++c;
            }
            if (k < 3) break;
            if (*c == ';') ++c;
            if (v[1] <= v[0] || v[2] < 0 || v[2] > 127) continue;
            r[n++] = { v[0] * 0.001, v[1] * 0.001, (int) v[2] };
        }
        sort();
    }
};

struct MelodyScan
{
    static constexpr double kHop = 0.01;      // seconds per entry
    static constexpr int kMax = 180000;       // 30 minutes
    // pitch in hundredths of a semitone (MIDI note x 100); 0 = no singing
    std::vector<uint16_t> in;
    std::atomic<int> len { 0 };
    std::atomic<uint32_t> version { 0 };

    MelodyScan() : in (kMax, 0) {}

    static uint16_t pack (double midi)
    {
        return midi <= 0.0 ? 0 : (uint16_t) std::clamp (std::lround (midi * 100.0), 1L, 65535L);
    }
    static float unpack (uint16_t v) { return (float) v * 0.01f; }

    float at (int i) const { return i >= 0 && i < len.load (std::memory_order_relaxed) ? unpack (in[(size_t) i]) : 0.0f; }

    // audio thread
    void write (double sec, double midi)
    {
        const long i = (long) (sec / kHop);
        if (i < 0 || i >= kMax) return;
        const uint16_t v = pack (midi);
        if (in[(size_t) i] == v && i < len.load (std::memory_order_relaxed)) return;
        in[(size_t) i] = v;
        if (i >= len.load (std::memory_order_relaxed)) len.store ((int) i + 1, std::memory_order_relaxed);
        version.fetch_add (1, std::memory_order_relaxed);
    }

    void clear()
    {
        std::fill (in.begin(), in.end(), 0);
        len.store (0); version.fetch_add (1);
    }

    // "VB1 <entries> <base64 of 16-bit little-endian pitch>"
    std::string encode() const
    {
        const int n = len.load();
        std::string bin ((size_t) n * 2, '\0');
        for (int i = 0; i < n; ++i) { bin[(size_t) (2 * i)] = (char) (in[(size_t) i] & 0xFF); bin[(size_t) (2 * i + 1)] = (char) (in[(size_t) i] >> 8); }
        char hdr[32]; std::snprintf (hdr, sizeof hdr, "VB1 %d ", n);
        return std::string (hdr) + base64 (bin);
    }

    bool decode (const char* c)
    {
        if (! c || std::string (c).compare (0, 4, "VB1 ") != 0) return false;
        char* e = nullptr;
        const long n = std::strtol (c + 4, &e, 10);
        if (e == c + 4 || n < 0 || n > kMax) return false;
        while (*e == ' ') ++e;
        const std::string bin = unbase64 (e);
        if ((long) bin.size() < n * 2) return false;
        clear();
        for (long i = 0; i < n; ++i)
            in[(size_t) i] = (uint16_t) ((unsigned char) bin[(size_t) (2 * i)] | ((unsigned char) bin[(size_t) (2 * i + 1)] << 8));
        len.store ((int) n); version.fetch_add (1);
        return true;
    }

private:
    static std::string base64 (const std::string& s)
    {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o; o.reserve ((s.size() + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < s.size(); i += 3)
        {
            const unsigned v = ((unsigned) (unsigned char) s[i] << 16) | ((unsigned) (unsigned char) s[i + 1] << 8) | (unsigned char) s[i + 2];
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
        }
        if (i + 1 == s.size())
        {
            const unsigned v = (unsigned) (unsigned char) s[i] << 16;
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
        }
        else if (i + 2 == s.size())
        {
            const unsigned v = ((unsigned) (unsigned char) s[i] << 16) | ((unsigned) (unsigned char) s[i + 1] << 8);
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
        }
        return o;
    }

    static std::string unbase64 (const char* c)
    {
        std::string o; unsigned acc = 0; int bits = 0;
        for (; *c; ++c)
        {
            int v;
            const char ch = *c;
            if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
            else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
            else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
            else if (ch == '+') v = 62;
            else if (ch == '/') v = 63;
            else continue;
            acc = (acc << 6) | (unsigned) v; bits += 6;
            if (bits >= 8) { bits -= 8; o += (char) ((acc >> bits) & 0xFF); }
        }
        return o;
    }
};

// Turns the scanned melody into harmony-note suggestions.
//
// 1. The melody is cut into notes: each moment snaps to the nearest note of
//    the key (with a little stickiness so vibrato doesn't flip notes), short
//    dropouts are bridged and blips shorter than `minLen` are absorbed.
// 2. For every melody note, each requested voice gets a note `steps[v]` scale
//    steps away, the same rule the live Auto voices use.
// 3. Within a voice, back-to-back notes of the same pitch join into one block,
//    so there is less to edit.
// Not for the audio thread.
inline void suggestNotes (const MelodyScan& scan, const KeyContext& key, const int* steps, int numSteps,
                          NoteSet& out, double minLen = 0.12, double bridge = 0.08)
{
    out.n = 0;
    const int len = scan.len.load();
    const int minN = std::max (1, (int) std::lround (minLen / MelodyScan::kHop));
    const int bridgeN = std::max (0, (int) std::lround (bridge / MelodyScan::kHop));

    struct Run { int a, b, note; };
    std::vector<Run> runs;
    int held = -1000, gap = 0;
    for (int i = 0; i < len; ++i)
    {
        const float m = scan.at (i);
        if (m <= 0.0f)
        {
            if (++gap > bridgeN) held = -1000;
            continue;
        }
        const int cand = key.nearest (m);
        if (held < -100 || std::abs (m - held) - std::abs (m - cand) > 0.24f)
            held = cand;
        if (! runs.empty() && runs.back().note == held && i - runs.back().b <= bridgeN + 1)
            runs.back().b = i + 1;
        else
            runs.push_back ({ i, i + 1, held });
        gap = 0;
    }

    // absorb blips: a too-short run joins a neighbour it touches, else it goes
    std::vector<Run> notes;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        Run r = runs[i];
        if (r.b - r.a >= minN) { notes.push_back (r); continue; }
        if (! notes.empty() && r.a - notes.back().b <= bridgeN) { notes.back().b = r.b; continue; }
        if (i + 1 < runs.size() && runs[i + 1].a - r.b <= bridgeN) { runs[i + 1].a = r.a; continue; }
    }

    for (int v = 0; v < numSteps; ++v)
    {
        int first = out.n;
        for (const Run& r : notes)
        {
            const int n = key.shiftDegrees (r.note, steps[v]);
            const double a = r.a * MelodyScan::kHop, b = r.b * MelodyScan::kHop;
            if (out.n > first && out.r[out.n - 1].note == n && a - out.r[out.n - 1].end <= bridge + 1e-9)
                out.r[out.n - 1].end = b;
            else if (! out.add (a, b, n))
                break;
        }
    }
    out.sort();
}

} // namespace voxbrew
