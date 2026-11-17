#pragma once
// Minimal WAV reader/writer for the offline tools (PCM 16/24/32 and float32).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wav
{

struct Audio
{
    int sampleRate = 48000;
    int channels = 1;
    std::vector<float> data; // interleaved
    size_t frames() const { return channels ? data.size() / (size_t) channels : 0; }
};

inline bool read (const std::string& path, Audio& out)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    size_t r;
    while ((r = std::fread (buf, 1, sizeof buf, f)) > 0) b.insert (b.end(), buf, buf + r);
    std::fclose (f);
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) || std::memcmp (b.data() + 8, "WAVE", 4)) return false;

    auto u16 = [&] (size_t o) { return (uint32_t) b[o] | ((uint32_t) b[o + 1] << 8); };
    auto u32 = [&] (size_t o) { return u16 (o) | (u16 (o + 2) << 16); };

    int fmt = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= b.size())
    {
        const uint32_t len = u32 (pos + 4);
        const size_t body = pos + 8;
        if (! std::memcmp (b.data() + pos, "fmt ", 4))
        {
            fmt = (int) u16 (body);
            out.channels = (int) u16 (body + 2);
            out.sampleRate = (int) u32 (body + 4);
            bits = (int) u16 (body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = (int) u16 (body + 24);
        }
        else if (! std::memcmp (b.data() + pos, "data", 4))
        {
            const size_t bytes = std::min ((size_t) len, b.size() - body);
            const int bps = bits / 8;
            if (bps == 0) return false;
            const size_t n = bytes / (size_t) bps;
            out.data.resize (n);
            const uint8_t* p = b.data() + body;
            for (size_t i = 0; i < n; ++i, p += bps)
            {
                if (fmt == 3 && bits == 32) { float v; std::memcpy (&v, p, 4); out.data[i] = v; }
                else if (bits == 16) out.data[i] = (int16_t) (p[0] | (p[1] << 8)) / 32768.0f;
                else if (bits == 24) out.data[i] = (int32_t) ((uint32_t) (p[0] << 8 | p[1] << 16 | p[2] << 24)) / 2147483648.0f;
                else if (bits == 32) out.data[i] = (int32_t) (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24) / 2147483648.0f;
                else return false;
            }
            return true;
        }
        pos = body + len + (len & 1);
    }
    return false;
}

inline bool writeFloat (const std::string& path, const Audio& a)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const uint32_t bytes = (uint32_t) (a.data.size() * 4);
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 ((uint16_t) a.channels);
    w32 ((uint32_t) a.sampleRate); w32 ((uint32_t) (a.sampleRate * a.channels * 4));
    w16 ((uint16_t) (a.channels * 4)); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    std::fwrite (a.data.data(), 4, a.data.size(), f);
    std::fclose (f);
    return true;
}

} // namespace wav
