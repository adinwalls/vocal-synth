// Renders the VoxBrew editor to a PNG without a DAW, with the engine running
// a short sung phrase so the display has something to show.
//   ui_snapshot out.png [scale]

#include "../src/VoxView.h"
#include "SynthVoice.h"

#include <cstdlib>

using namespace voxbrew;

struct NullHost : VoxView::Host
{
    VoxView* view = nullptr;
    void setParam (int i, float v) override { if (view) view->setValue (i, v); }
    void beginEdit (int) override {}
    void endEdit (int) override {}
};

int main (int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "voxbrew.png";
    const double scale = argc > 2 ? std::atof (argv[2]) : 1.0;
    const double fs = 48000.0;

    float values[pCount];
    for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
    values[pKey] = 9;
    values[pMode] = kModeAutoAndMidi;
    values[pAutoVoices] = 2;
    values[pTonic] = 1;

    Harmonizer engine;
    engine.prepare (fs);
    engine.setParams (toEngine (values));

    NullHost host;
    VoxView view (&host);
    host.view = &view;
    view.setEngine (&engine);
    for (int i = 0; i < pCount; ++i) view.setValue (i, values[i]);

    // A little melody in A major, with a MIDI note held for the second half.
    const int melody[] = { 57, 57, 59, 61, 61, 59, 57, 64, 64, 62, 61 };
    std::vector<float> l (1024), r (1024);
    double t = 0.0;
    int idx = 0;
    for (int note : melody)
    {
        if (idx++ == 6) engine.noteOn (73, 0.9f);
        const auto v = test::synthVowel (fs, 0.5, midiToHz (note) * 1.006, 18.0);
        for (size_t i = 0; i + 512 <= v.size(); i += 512)
        {
            engine.process (v.data() + i, l.data(), r.data(), 512);
            t += 512.0 / fs;
            view.tick (t);
        }
    }

    const int w = (int) (VoxView::kW * scale), h = (int) (VoxView::kH * scale);
    cairo_surface_t* s = cairo_image_surface_create (CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create (s);
    if (argc > 3) { view.mouseDown (300, 30, false, 0.0); view.mouseMove (330, 150, false); } // open the scale menu
    view.draw (cr, w, h);
    cairo_destroy (cr);
    cairo_surface_write_to_png (s, out);
    cairo_surface_destroy (s);
    std::printf ("wrote %s\n", out);
    return 0;
}
