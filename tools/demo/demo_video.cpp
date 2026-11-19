// Makes the pictures and sound for the VoxBrew demo video without a DAW.
//
// A dry vocal is looped through the real engine (src/dsp) once per scene, each
// scene with different settings, while the real editor (src/VoxView.h) is drawn
// 30 times a second with those settings and the engine's live meters.
//
//   demo_video voice.wav out.wav [key] [synthetic] | ffmpeg -f rawvideo -pix_fmt bgra -s 1920x1080 -r 30 -i - ...
//
// `key` is the major key of the vocal (C, C#, ... B; default A). Add the word
// "synthetic" when the vocal is the stand-in from make_voice.py, so the captions say so.
// "out=-3" sets the Output knob in dB; "measure" draws nothing and prints the peak level.
// Raw BGRA frames go to stdout, the plugin's output goes to out.wav.
// See tools/demo/make_demo.sh.

#include "../../src/VoxView.h"
#include "../Wav.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace voxbrew;

struct NullHost : VoxView::Host
{
    void setParam (int, float) override {}
    void beginEdit (int) override {}
    void endEdit (int) override {}
    void notesChanged (const NoteSet&) override {}
};

struct Change { int param; float value; };

struct Scene
{
    const char* title;
    const char* sub;
    std::vector<Change> changes;   // on top of the scene before
    bool deck;
    float justMangleTo;            // Just Mangle slides up to this over the scene (< 0: leave it)
    double seconds;                // 0 = one pass of the vocal; otherwise silence in, to hear the tail
};

static std::vector<Scene> kScenes = {
    { "1. The dry voice going in", "The raw vocal, untouched",
      { { pScale, kMajor }, { pMode, kModeAuto }, { pAutoVoices, 0 }, { pTonic, 0 },
        { pDry, 0 }, { pLead, -60 }, { pNatural, 10 }, { pSpread, 100 } }, false, -1, 0 },
    { "2. Tuned lead", "VoxBrew snaps the voice to the notes of the key",
      { { pDry, -60 }, { pLead, 0 } }, false, -1, 0 },
    { "3. Harmony voices on their own", "Two new voices made from yours, a 3rd and a 5th above the melody, in key",
      { { pLead, -60 }, { pAutoVoices, 2 }, { pAuto1, 2 }, { pAuto2, 4 }, { pHarmony, 0 } }, false, -1, 0 },
    { "4. Lead and harmonies together", "The tuned lead in the middle, the harmony voices either side",
      { { pLead, 0 }, { pHarmony, -2 } }, false, -1, 0 },
    { "5. Full stack with a tonic drone", "Four voices plus a drone on the root note. Loose lets them drift like separate singers",
      { { pAutoVoices, 4 }, { pAuto3, -3 }, { pAuto4, -7 }, { pTonic, 1 }, { pTonicOctave, 2 },
        { pLoose, 18 }, { pHarmony, -4 } }, false, -1, 0 },
    { "6. MANGLE opens the Tape Mangler", "Wobble, hiss, crackle and crush, with a tape echo",
      { { pMangle, 45 }, { pScatter, 20 }, { pWobble, 70 }, { pCrush, 25 }, { pHiss, 35 }, { pCrackle, 20 },
        { pEcho, 40 }, { pEchoRepeats, 45 }, { pEchoAge, 50 } }, true, -1, 0 },
    { "7. Just Mangle", "One slider pushes all of the damage from worn to wrecked",
      {}, true, 70, 0 },
    { "VoxBrew by Adin Walls", "Every sound here is the plugin's own engine at work on one raw vocal",
      {}, true, -1, 3.5 },
};

static void text (cairo_t* cr, const char* s, double cx, double y, double size, bool bold, double r, double g, double b)
{
    cairo_select_font_face (cr, "Inter", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size (cr, size);
    cairo_text_extents_t e;
    cairo_text_extents (cr, s, &e);
    cairo_move_to (cr, cx - e.width / 2 - e.x_bearing, y);
    cairo_set_source_rgb (cr, r, g, b);
    cairo_show_text (cr, s);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: demo_video voice.wav out.wav [key] [synthetic] > frames.bgra\n"); return 2; }
    wav::Audio in;
    if (! wav::read (argv[1], in)) { std::fprintf (stderr, "can't read %s\n", argv[1]); return 1; }
    const double fs = in.sampleRate;
    std::vector<float> voice (in.frames());
    for (size_t i = 0; i < voice.size(); ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < in.channels; ++c) s += in.data[i * (size_t) in.channels + (size_t) c];
        voice[i] = s / (float) in.channels;
    }

    int key = 9;
    bool synthetic = false, measure = false;
    float outDb = 0.0f;
    for (int a = 3; a < argc; ++a)
    {
        const std::string o = argv[a];
        if (o == "synthetic") { synthetic = true; continue; }
        if (o == "measure") { measure = true; continue; }
        if (o.rfind ("out=", 0) == 0) { outDb = (float) std::atof (o.c_str() + 4); continue; }
        key = -1;
        for (int k = 0; k < 12; ++k) if (o == kKeyNames[k]) key = k;
        if (key < 0) { std::fprintf (stderr, "unknown key %s (use C, C#, D ... B)\n", o.c_str()); return 2; }
    }
    const std::string tuned = std::string ("VoxBrew snaps the voice to the key: ") + kKeyNames[key] + " major";
    kScenes[0].changes.push_back ({ pKey, (float) key });
    kScenes[0].changes.push_back ({ pOutput, outDb });
    kScenes[1].sub = tuned.c_str();
    if (synthetic)
    {
        kScenes[0].sub = "A synthetic stand-in singer, untouched and a little out of tune on purpose";
        kScenes.back().sub = "All sound is the plugin's own engine. The singer is a synthetic stand-in";
    }

    const int W = 1920, H = 1080, fps = 30;
    const double sc = 1.6, px = (W - VoxView::kW * sc) / 2, py = 34;
    const int block = (int) (fs / fps / 4);          // four audio blocks per picture

    float target[pCount], values[pCount];
    for (int i = 0; i < pCount; ++i) target[i] = values[i] = kSpecs[i].def;
    for (const Change& c : kScenes[0].changes) target[c.param] = values[c.param] = c.value;

    Harmonizer engine;
    engine.prepare (fs);
    engine.setParams (toEngine (values));
    NullHost host;
    VoxView view (&host);
    view.setEngine (&engine);
    for (int i = 0; i < pCount; ++i) view.setValue (i, values[i]);

    cairo_surface_t* surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, W, H);
    cairo_surface_t* plug = cairo_image_surface_create (CAIRO_FORMAT_RGB24, (int) (VoxView::kW * sc), (int) (VoxView::kH * sc));
    cairo_t* cr = cairo_create (surf);
    cairo_t* pcr = cairo_create (plug);

    std::vector<float> outL, outR, bl ((size_t) block), br ((size_t) block), silence ((size_t) block, 0.0f);
    double t = 0.0;
    long frames = 0;

    for (size_t si = 0; si < kScenes.size(); ++si)
    {
        const Scene& scene = kScenes[si];
        for (const Change& c : scene.changes) target[c.param] = c.value;
        const size_t len = scene.seconds > 0 ? (size_t) (scene.seconds * fs) : voice.size();
        const double sceneStart = t;
        int inBlock = 0;
        for (size_t pos = 0; pos + (size_t) block <= len; pos += (size_t) block)
        {
            const double st = t - sceneStart;
            if (st > 0.25) view.setDeck (scene.deck);
            if (scene.justMangleTo >= 0)
                target[pJustMangle] = scene.justMangleTo * (float) std::clamp ((st - 0.5) / (len / fs - 2.0), 0.0, 1.0);
            // knobs turn to their new places over a moment; switches flip at once
            for (int i = 0; i < pCount; ++i)
            {
                if (kSpecs[i].kind == kFloat && i != pDry && i != pLead)
                    values[i] += (target[i] - values[i]) * 0.12f;
                else
                    values[i] = target[i];
                view.setValue (i, values[i]);
            }
            engine.setParams (toEngine (values));
            engine.process (scene.seconds > 0 ? silence.data() : voice.data() + pos, bl.data(), br.data(), block);
            outL.insert (outL.end(), bl.begin(), bl.end());
            outR.insert (outR.end(), br.begin(), br.end());
            t += block / fs;
            view.tick (t);

            if (measure || ++inBlock < 4) continue;
            inBlock = 0;
            view.draw (pcr, VoxView::kW * sc, VoxView::kH * sc);
            cairo_surface_flush (plug);
            cairo_set_source_rgb (cr, 0.030, 0.045, 0.075);
            cairo_paint (cr);
            cairo_set_source_surface (cr, plug, px, py);
            cairo_paint (cr);
            const double fade = std::clamp (std::min (st / 0.3, (len / fs - st) / 0.3), 0.0, 1.0);
            text (cr, scene.title, W / 2.0, 968, 46, true, 1.0 * fade, 0.69 * fade + 0.045 * (1 - fade), 0.25 * fade + 0.075 * (1 - fade));
            text (cr, scene.sub, W / 2.0, 1022, 28, false, 0.03 + 0.83 * fade, 0.045 + 0.84 * fade, 0.075 + 0.84 * fade);
            cairo_surface_flush (surf);
            std::fwrite (cairo_image_surface_get_data (surf), 1, (size_t) cairo_image_surface_get_stride (surf) * H, stdout);
            ++frames;
        }
    }

    // Drop the plugin's latency so sound and picture line up.
    const size_t lat = (size_t) engine.getLatencySamples();
    wav::Audio out;
    out.sampleRate = in.sampleRate;
    out.channels = 2;
    float peak = 0.0f;
    for (size_t i = lat; i < outL.size(); ++i)
    {
        out.data.push_back (outL[i]);
        out.data.push_back (outR[i]);
        peak = std::max ({ peak, std::abs (outL[i]), std::abs (outR[i]) });
    }
    if (measure) { std::printf ("%.4f\n", peak); return 0; }
    if (! wav::writeFloat (argv[2], out)) { std::fprintf (stderr, "can't write %s\n", argv[2]); return 1; }
    std::fprintf (stderr, "%ld frames, %.1f s, peak %.2f\n", frames, t, peak);
    return 0;
}
