// Renders the VoxBrew editor to a PNG without a DAW, with the engine running
// a short sung phrase so the display has something to show.
//   ui_snapshot out.png [scale] [menu|notes]

#include "../src/VoxView.h"
#include "SynthVoice.h"

#include <cstdlib>
#include <string>

using namespace voxbrew;

struct NullHost : VoxView::Host
{
    VoxView* view = nullptr;
    void setParam (int i, float v) override { if (view) view->setValue (i, v); }
    void beginEdit (int) override {}
    void endEdit (int) override {}
    Harmonizer* engine = nullptr;
    void notesChanged (const NoteSet& n) override { if (engine) engine->setNotes (n); }
};

int main (int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "voxbrew.png";
    const double scale = argc > 2 ? std::atof (argv[2]) : 1.0;
    const double fs = 48000.0;
    const std::string what = argc > 3 ? argv[3] : "";

    float values[pCount];
    for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
    values[pKey] = 9;
    values[pMode] = kModeAutoAndMidi;
    values[pAutoVoices] = 2;
    values[pTonic] = 1;
    if (what == "notes") { values[pMode] = kModeNotes; values[pTonic] = 0; }

    Harmonizer engine;
    engine.prepare (fs);
    engine.setParams (toEngine (values));

    NullHost host;
    VoxView view (&host);
    host.view = &view;
    host.engine = &engine;
    view.setEngine (&engine);
    for (int i = 0; i < pCount; ++i) view.setValue (i, values[i]);

    // A little melody in A major, with a MIDI note held for the second half.
    const int melody[] = { 57, 57, 59, 61, 61, 59, 57, 64, 64, 62, 61 };
    std::vector<float> l (1024), r (1024);
    double t = 0.0;
    int idx = 0;
    for (int note : melody)
    {
        if (idx++ == 6 && what != "notes") engine.noteOn (73, 0.9f);
        auto v = test::synthVowel (fs, 0.5, midiToHz (note) * 1.006, 18.0);
        if (what == "notes") v.resize (v.size() + 4096, 0.0f); // a breath between notes
        for (size_t i = 0; i + 512 <= v.size(); i += 512)
        {
            engine.setTimeline (true, true, 1.0 + t);
            engine.process (v.data() + i, l.data(), r.data(), 512);
            t += 512.0 / fs;
            view.tick (t);
        }
    }

    const int w = (int) (VoxView::kW * scale), h = (int) (VoxView::kH * scale);
    cairo_surface_t* s = cairo_image_surface_create (CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create (s);
    if (what == "notes")
    {
        view.mouseDown (26 + 684, 341, false, 0.0); view.mouseUp (0, 0);     // SUGGEST
        view.mouseDown (26 + 200, 196, false, 5.0); view.mouseUp (0, 0);     // select a note
        engine.setTimeline (true, false, 4.2);
        view.tick (t + 1.0);
    }
    if (what == "tape")
    {
        view.setValue (pEcho, 45); view.setValue (pEchoRepeats, 55); view.setValue (pMangle, 55); view.setValue (pJustMangle, 70);
        view.setDeck (true, true);
        view.tick (t + 0.4);
    }
    if (what == "half") { view.setValue (pEcho, 45); view.setDeck (true, true); view.setDeck (false); view.tick (t + 0.01); view.tick (t + 0.11); }
    if (what == "menu") { view.mouseDown (300, 30, false, 0.0); view.mouseMove (330, 150, false); } // open the scale menu
    view.draw (cr, w, h);
    cairo_destroy (cr);
    cairo_surface_write_to_png (s, out);
    cairo_surface_destroy (s);
    std::printf ("wrote %s\n", out);
    return 0;
}
