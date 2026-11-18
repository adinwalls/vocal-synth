// VoxView: the VoxBrew editor, drawn with Cairo.
// It follows the Adin Brew house style (wood cheeks, navy faceplate, screen
// windows, push buttons with lamps, coloured knob caps). The hardware drawing
// primitives are taken from TuneBrew's TuneView.hpp in adin-brew (GPL-3.0,
// same author) so the plugins look like one family.
#pragma once

#include "VoxParams.h"

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace voxbrew
{

class VoxView
{
public:
    static constexpr double kCheek = 26;                       // wooden side panels
    static constexpr double kFaceW = 860, kFaceH = 540;
    static constexpr double kW = kFaceW + 2 * kCheek, kH = kFaceH;

    struct Host
    {
        virtual void setParam (int index, float value) = 0;
        virtual void beginEdit (int index) = 0;
        virtual void endEdit (int index) = 0;
        virtual ~Host() {}
    };

    explicit VoxView (Host* h) : host (h)
    {
        for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
        hist.resize (kHistSize);
    }
    ~VoxView() { if (bg) cairo_surface_destroy (bg); }
    VoxView (const VoxView&) = delete;
    VoxView& operator= (const VoxView&) = delete;

    // The editor reads live voice pitches straight from the engine (same process).
    void setEngine (const Harmonizer* e) { eng = e; dirty = true; }
    void setValue (int i, float v) { if (i >= 0 && i < pCount && values[i] != v) { values[i] = v; if (kSpecs[i].kind != kOutput) dirty = true; } }

    float getValue (int i) const { return i >= 0 && i < pCount ? values[i] : 0.0f; }

    // Call regularly with a running clock in seconds; true means "repaint".
    bool tick (double now)
    {
        nowT = now;
        if (now - lastSample >= 1.0 / 30.0)
        {
            lastSample = now;
            Sample s;
            s.t = now;
            bool any = false;
            if (eng != nullptr)
            {
                s.in = eng->meterInput.load (std::memory_order_relaxed);
                for (int i = 0; i < kSlots; ++i)
                {
                    s.v[i] = eng->meterVoice[(size_t) i].load (std::memory_order_relaxed);
                    any = any || s.v[i] > 0.0f;
                }
            }
            else if (values[pOutPitch] > 0.0f)
                s.in = (float) hzToMidi (values[pOutPitch]);
            any = any || s.in > 0.0f;

            hist[(size_t) histHead] = s;
            histHead = (histHead + 1) % kHistSize;
            histCount = std::min (histCount + 1, kHistSize);

            // follow the singer: ease the graph centre towards the sung note
            if (s.in > 0.0f)
            {
                if (center < 1.0) center = s.in;
                const double d = s.in - center;
                if (std::abs (d) > 4.0) center += (d > 0 ? d - 4.0 : d + 4.0) * 0.2;
            }
            if (any) lastActive = now;
            if (now - lastActive < kWindow + 0.5) dirty = true;
        }
        const bool d = dirty;
        dirty = false;
        return d;
    }

    void draw (cairo_t* cr, double pixelW, double pixelH)
    {
        const double sc = std::min (pixelW / kW, pixelH / kH);
        // the wood, faceplates and printing never change: paint them once
        const int bw = (int) std::ceil (pixelW), bh = (int) std::ceil (pixelH);
        if (! bg || bw != bgW || bh != bgH)
        {
            if (bg) cairo_surface_destroy (bg);
            bg = cairo_surface_create_similar (cairo_get_target (cr), CAIRO_CONTENT_COLOR, bw, bh);
            bgW = bw; bgH = bh;
            cairo_t* b = cairo_create (bg);
            cairo_set_source_rgb (b, 0.05, 0.04, 0.035); cairo_paint (b);
            cairo_scale (b, sc, sc);
            cairo_select_font_face (b, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            drawCheek (b, 0, false);
            drawCheek (b, kW - kCheek, true);
            cairo_translate (b, kCheek, 0);
            drawFaceplate (b);
            drawModules (b);
            cairo_destroy (b);
        }
        cairo_save (cr);
        cairo_set_source_surface (cr, bg, 0, 0); cairo_paint (cr);
        cairo_scale (cr, sc, sc);
        cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_translate (cr, kCheek, 0);
        drawHeader (cr);
        drawKeyStrip (cr);
        drawGraph (cr);
        drawVoicePanel (cr);
        pushButton (cr, kLowLatBtn, "LOW LATENCY", values[pLowLatency] > 0.5f, kAmber, 9.5, 12);
        for (int k = 0; k < kNumKnobs; ++k) drawKnob (cr, k);
        if (menuOpen) drawMenu (cr);
        cairo_restore (cr);
    }

    // ---------------- mouse (logical window coords) ----------------
    bool mouseDown (double x, double y, bool fine, double timeSec)
    {
        x -= kCheek;
        dirty = true;
        if (menuOpen)
        {
            const int item = menuItemAt (x, y);
            if (item >= 0) commit (pScale, (float) item);
            menuOpen = false;
            return true;
        }
        if (inRect (x, y, kScaleBox)) { menuOpen = true; return true; }
        for (int m = 0; m < kModeCount; ++m)
            if (inRect (x, y, modeBtn (m))) { commit (pMode, (float) m); return true; }
        if (inRect (x, y, kLowLatBtn)) { commit (pLowLatency, values[pLowLatency] > 0.5f ? 0.f : 1.f); return true; }

        const int cell = keyCellAt (x, y);
        if (cell >= 0) { commit (pKey, (float) cell); return true; }

        for (int r = 0; r < kRows; ++r)
        {
            if (inRect (x, y, rowToggle (r)))
            {
                if (r < 4)
                {
                    const int n = (int) values[pAutoVoices];
                    commit (pAutoVoices, (float) (r < n ? r : r + 1));
                }
                else
                    commit (pTonic, values[pTonic] > 0.5f ? 0.f : 1.f);
                return true;
            }
            const int dir = inRect (x, y, rowDown (r)) ? -1 : inRect (x, y, rowUp (r)) ? 1 : 0;
            if (dir != 0) { nudgeRow (r, dir); return true; }
        }

        const int k = knobAt (x, y);
        if (k >= 0)
        {
            const int p = kKnobParams[k];
            if (lastClickKnob == k && timeSec - lastClickTime < 0.35)   // double-click = default
            {
                commit (p, kSpecs[p].def);
                lastClickKnob = -1;
                return true;
            }
            lastClickKnob = k; lastClickTime = timeSec;
            dragKnob = k; dragY = y; dragNorm = toNorm (p, values[p]); dragFine = fine;
            host->beginEdit (p);
            return true;
        }
        return false;
    }

    bool mouseUp (double, double)
    {
        if (dragKnob >= 0) { host->endEdit (kKnobParams[dragKnob]); dragKnob = -1; dirty = true; return true; }
        return false;
    }

    bool mouseMove (double x, double y, bool fine)
    {
        x -= kCheek;
        const int hk = knobAt (x, y), hc = keyCellAt (x, y), hm = menuOpen ? menuItemAt (x, y) : -1;
        if (hk != hoverKnob || hc != hoverCell || hm != hoverMenu) dirty = true;
        hoverKnob = hk; hoverCell = hc; hoverMenu = hm;
        if (dragKnob < 0) return false;
        const int p = kKnobParams[dragKnob];
        if (fine != dragFine) { dragNorm = toNorm (p, values[p]); dragY = y; dragFine = fine; }
        const double n = std::clamp (dragNorm + (dragY - y) / (fine ? 1200.0 : 220.0), 0.0, 1.0);
        const float v = fromNorm (p, n);
        if (v != values[p]) { values[p] = v; host->setParam (p, v); dirty = true; }
        return true;
    }

    bool scroll (double x, double y, double dy)
    {
        x -= kCheek;
        if (dy == 0.0) return false;
        dirty = true;
        if (inRect (x, y, kScaleBox))
        {
            const int s = (int) values[pScale] + (dy > 0 ? -1 : 1);
            commit (pScale, (float) ((s + kScaleCount) % kScaleCount));
            return true;
        }
        if (keyCellAt (x, y) >= 0)
        {
            const int s = (int) values[pKey] + (dy > 0 ? 1 : -1);
            commit (pKey, (float) ((s + 12) % 12));
            return true;
        }
        for (int r = 0; r < kRows; ++r)
            if (inRect (x, y, rowRect (r))) { nudgeRow (r, dy > 0 ? 1 : -1); return true; }
        const int k = knobAt (x, y);
        if (k < 0) return false;
        const int p = kKnobParams[k];
        commit (p, fromNorm (p, std::clamp (toNorm (p, values[p]) + (dy > 0 ? 0.02 : -0.02), 0.0, 1.0)));
        return true;
    }

private:
    // ---------------- data ----------------
    static constexpr int kSlots = Harmonizer::kNumVoices;
    struct Sample { double t = -1; float in = 0; float v[kSlots] = {}; };
    static constexpr int kHistSize = 512;
    std::vector<Sample> hist;
    int histHead = 0, histCount = 0;
    const Sample& at (int k) const { return hist[(size_t) ((histHead - 1 - k + kHistSize) % kHistSize)]; }

    Host* host;
    const Harmonizer* eng = nullptr;
    float values[pCount];
    double nowT = 0, lastSample = -1, lastActive = -100, center = 0;
    bool dirty = true, menuOpen = false;
    int hoverKnob = -1, dragKnob = -1, hoverCell = -1, hoverMenu = -1;
    int lastClickKnob = -1;
    double lastClickTime = -1, dragY = 0, dragNorm = 0;
    bool dragFine = false;
    cairo_surface_t* bg = nullptr;
    int bgW = 0, bgH = 0;

    // ---------------- layout (faceplate coords, 860 x 540) ----------------
    struct Rect { double x, y, w, h; };
    static constexpr Rect kScaleBox  = { 254, 14, 220, 34 };
    static constexpr Rect kKeyStrip  = { 92, 68, 744, 44 };
    static constexpr Rect kGraph     = { 24, 124, 588, 240 };
    static constexpr Rect kPanel     = { 624, 124, 212, 240 };
    static constexpr double kModTop = 380, kSeamX = 506;
    static constexpr Rect kLowLatBtn = { 716, 386, 120, 22 };
    static constexpr int kRows = 5;            // 4 auto voices + tonic
    static constexpr double kRowTop = 150, kRowH = 33;
    static constexpr int kNumKnobs = 12;
    static constexpr int kVoiceKnobs = 7;
    static constexpr int kKnobParams[kNumKnobs] = { pFormant, pNatural, pLoose, pGlide, pAttack, pRelease, pConsonants,
                                                    pDry, pLead, pHarmony, pSpread, pOutput };
    static constexpr double kKnobY = 452, kKnobR = 18;
    static constexpr double kWindow = 6.0;   // seconds shown in the graph
    static constexpr double kSpan = 26.0;    // semitones shown in the graph

    static Rect modeBtn (int m)
    {
        static const Rect r[kModeCount] = { { 490, 14, 96, 34 }, { 594, 14, 96, 34 }, { 698, 14, 128, 34 } };
        return r[m];
    }
    static Rect rowRect (int r)   { return { kPanel.x + 6, kRowTop + r * kRowH + (r == 4 ? 8 : 0), kPanel.w - 12, kRowH - 3 }; }
    static Rect rowToggle (int r) { const Rect b = rowRect (r); return { b.x + 2, b.y + 3, 30, b.h - 6 }; }
    static Rect rowDown (int r)   { const Rect b = rowRect (r); return { b.x + 38, b.y + 3, 20, b.h - 6 }; }
    static Rect rowUp (int r)     { const Rect b = rowRect (r); return { b.x + 138, b.y + 3, 20, b.h - 6 }; }

    static double knobX (int k)
    {
        if (k < kVoiceKnobs) return 44 + k * 68.0;
        return kSeamX + 44 + (k - kVoiceKnobs) * 68.0;
    }
    int knobAt (double x, double y) const
    {
        for (int k = 0; k < kNumKnobs; ++k)
            if (std::hypot (x - knobX (k), y - kKnobY) <= kKnobR + 9) return k;
        return -1;
    }
    int keyCellAt (double x, double y) const
    {
        if (! inRect (x, y, kKeyStrip)) return -1;
        return std::clamp ((int) ((x - kKeyStrip.x) / (kKeyStrip.w / 12)), 0, 11);
    }
    static Rect menuRect() { return { kScaleBox.x, kScaleBox.y + kScaleBox.h + 4, kScaleBox.w, kScaleCount * 28.0 + 8 }; }
    int menuItemAt (double x, double y) const
    {
        const Rect m = menuRect();
        if (! inRect (x, y, m)) return -1;
        return std::clamp ((int) ((y - m.y - 4) / 28), 0, kScaleCount - 1);
    }

    // ---------------- palette ----------------
    struct C { double r, g, b; };
    static constexpr C kNavy    = { 0.118, 0.235, 0.384 };
    static constexpr C kNavyHi  = { 0.165, 0.298, 0.463 };
    static constexpr C kNavyLo  = { 0.075, 0.153, 0.259 };
    static constexpr C kScreen  = { 0.035, 0.050, 0.080 };
    static constexpr C kScreen2 = { 0.060, 0.090, 0.135 };
    static constexpr C kText    = { 0.960, 0.965, 0.975 };
    static constexpr C kPrint   = { 0.840, 0.870, 0.910 };   // faceplate silkscreen
    static constexpr C kDim     = { 0.560, 0.630, 0.720 };
    static constexpr C kAccent  = { 0.250, 0.880, 0.820 };   // harmony voices
    static constexpr C kAmber   = { 1.000, 0.690, 0.250 };   // your voice
    static constexpr C kRed     = { 1.000, 0.240, 0.200 };
    static constexpr C kYellow  = { 1.000, 0.860, 0.180 };
    static constexpr C kGreen   = { 0.300, 0.950, 0.350 };
    // knob caps
    static constexpr C kCapPink  = { 0.930, 0.310, 0.560 };
    static constexpr C kCapBlue  = { 0.280, 0.610, 0.860 };
    static constexpr C kCapGrey  = { 0.860, 0.870, 0.880 };
    static constexpr C kCapGreen = { 0.150, 0.640, 0.360 };
    static constexpr C kCapBlack = { 0.200, 0.210, 0.230 };

    static C slotColour (int slot)
    {
        if (slot == Harmonizer::kLeadSlot) return kText;
        if (slot == Harmonizer::kTonicSlot) return kGreen;
        if (slot >= Harmonizer::kMidiSlot) return kCapPink;
        return kAccent;
    }

    // ---------------- drawing primitives (from TuneBrew) ----------------
    static void rgb(cairo_t* cr, C c, double a = 1.0) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }
    static C mix(C a, C b, double t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
    static bool inRect(double x, double y, Rect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
    static void rrect(cairo_t* cr, double x, double y, double w, double h, double r)
    {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
        cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
        cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
        cairo_close_path(cr);
    }
    static void text(cairo_t* cr, const char* s, double x, double y, double size, int align /*-1 left,0 centre,1 right*/)
    {
        cairo_set_font_size(cr, size);
        cairo_text_extents_t e; cairo_text_extents(cr, s, &e);
        double tx = x;
        if (align == 0) tx = x - (e.width / 2 + e.x_bearing);
        else if (align == 1) tx = x - (e.width + e.x_bearing);
        cairo_move_to(cr, tx, y);
        cairo_show_text(cr, s);
        cairo_new_path(cr);
    }

    // ---------------- hardware primitives ----------------
    void drawCheek(cairo_t* cr, double x, bool right)
    {
        const double w = kCheek, h = kH;
        cairo_save(cr);
        rrect(cr, x, 0, w, h, 5); cairo_clip(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(x, 0, x + w, 0);
        cairo_pattern_add_color_stop_rgb(g, 0.0, right ? 0.50 : 0.43, right ? 0.24 : 0.20, right ? 0.10 : 0.08);
        cairo_pattern_add_color_stop_rgb(g, 0.5, 0.66, 0.35, 0.16);
        cairo_pattern_add_color_stop_rgb(g, 1.0, right ? 0.43 : 0.50, right ? 0.20 : 0.24, right ? 0.08 : 0.10);
        cairo_set_source(cr, g); cairo_paint(cr); cairo_pattern_destroy(g);
        // grain
        for (int i = 0; i < 22; ++i) {
            const double gx = x + 1.5 + std::fmod(i * 7.37 + (right ? 3.1 : 0.0), w - 3);
            const double a = 0.06 + 0.07 * (0.5 + 0.5 * std::sin(i * 2.17));
            cairo_set_source_rgba(cr, 0.25, 0.10, 0.03, a);
            cairo_set_line_width(cr, 0.6 + 0.8 * (0.5 + 0.5 * std::sin(i * 1.3)));
            cairo_move_to(cr, gx, 0);
            for (double yy = 0; yy <= h; yy += 30)
                cairo_line_to(cr, gx + 1.4 * std::sin(yy * 0.011 + i), yy);
            cairo_stroke(cr);
        }
        // edge shading
        cairo_set_source_rgba(cr, 1, 0.85, 0.6, 0.12); cairo_rectangle(cr, right ? x + w - 2 : x, 0, 2, h); cairo_fill(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35); cairo_rectangle(cr, right ? x : x + w - 3, 0, 3, h); cairo_fill(cr);
        cairo_restore(cr);
    }

    static void screw(cairo_t* cr, double x, double y, double ang)
    {
        cairo_pattern_t* g = cairo_pattern_create_radial(x - 1.5, y - 1.5, 0.5, x, y, 5.5);
        cairo_pattern_add_color_stop_rgb(g, 0, 0.92, 0.93, 0.95);
        cairo_pattern_add_color_stop_rgb(g, 1, 0.42, 0.45, 0.50);
        cairo_set_source(cr, g); cairo_arc(cr, x, y, 5, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45); cairo_set_line_width(cr, 1.3);
        for (int k = 0; k < 2; ++k) {
            const double a = ang + k * M_PI / 2;
            cairo_move_to(cr, x - 3.4 * std::cos(a), y - 3.4 * std::sin(a));
            cairo_line_to(cr, x + 3.4 * std::cos(a), y + 3.4 * std::sin(a));
        }
        cairo_stroke(cr);
    }

    static void led(cairo_t* cr, double x, double y, double r, C c, double on)
    {
        if (on > 0.02) {
            cairo_pattern_t* g = cairo_pattern_create_radial(x, y, r * 0.3, x, y, r * 4.6);
            cairo_pattern_add_color_stop_rgba(g, 0.0, c.r, c.g, c.b, 0.75 * on);
            cairo_pattern_add_color_stop_rgba(g, 0.3, c.r, c.g, c.b, 0.30 * on);
            cairo_pattern_add_color_stop_rgba(g, 1.0, c.r, c.g, c.b, 0.0);
            cairo_set_source(cr, g); cairo_arc(cr, x, y, r * 4.6, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
        }
        cairo_set_source_rgb(cr, 0.05, 0.05, 0.06); cairo_arc(cr, x, y, r + 1.2, 0, 2 * M_PI); cairo_fill(cr);
        const C off = mix(c, C{0.08, 0.08, 0.09}, 0.78);
        const C body = mix(off, c, on);
        cairo_pattern_t* g = cairo_pattern_create_radial(x - r * 0.35, y - r * 0.35, r * 0.1, x, y, r);
        const C hi = mix(body, C{1, 1, 1}, 0.25 + 0.45 * on);
        cairo_pattern_add_color_stop_rgb(g, 0, hi.r, hi.g, hi.b);
        cairo_pattern_add_color_stop_rgb(g, 1, body.r, body.g, body.b);
        cairo_set_source(cr, g); cairo_arc(cr, x, y, r, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
    }

    // lens lamp beside a button: dark tinted glass when off, glowing when on
    static void lamp(cairo_t* cr, Rect l, C c, double on)
    {
        if (on > 0.02) {
            const double cx = l.x + l.w / 2, cy = l.y + l.h / 2;
            // wide soft bloom + tight hot halo
            const double R = std::max(l.w * 2.4, l.h * 0.95);
            cairo_pattern_t* gl = cairo_pattern_create_radial(cx, cy, 1, cx, cy, R);
            cairo_pattern_add_color_stop_rgba(gl, 0.0, c.r, c.g, c.b, 0.55 * on);
            cairo_pattern_add_color_stop_rgba(gl, 0.4, c.r, c.g, c.b, 0.22 * on);
            cairo_pattern_add_color_stop_rgba(gl, 1.0, c.r, c.g, c.b, 0.0);
            cairo_set_source(cr, gl); cairo_rectangle(cr, cx - R, cy - R, 2 * R, 2 * R); cairo_fill(cr);
            cairo_pattern_destroy(gl);
            cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.35 * on);
            rrect(cr, l.x - 2.5, l.y - 2.5, l.w + 5, l.h + 5, 4); cairo_fill(cr);
        }
        const C off = mix(c, C{0.06, 0.05, 0.05}, 0.72);
        const C body = mix(off, c, on);
        cairo_pattern_t* g = cairo_pattern_create_linear(l.x, l.y, l.x + l.w, l.y + l.h);
        const C hi = mix(body, C{1, 1, 1}, 0.15 + 0.6 * on);
        cairo_pattern_add_color_stop_rgb(g, 0, hi.r, hi.g, hi.b);
        const C mid = mix(body, C{1, 1, 1}, 0.18 * on);
        cairo_pattern_add_color_stop_rgb(g, 0.55, mid.r, mid.g, mid.b);
        const C lo = mix(body, C{0, 0, 0}, 0.25);
        cairo_pattern_add_color_stop_rgb(g, 1, lo.r, lo.g, lo.b);
        cairo_set_source(cr, g); rrect(cr, l.x, l.y, l.w, l.h, 2); cairo_fill(cr); cairo_pattern_destroy(g);
        // glass highlight
        cairo_set_source_rgba(cr, 1, 1, 1, 0.18 + 0.25 * on);
        rrect(cr, l.x + 1.5, l.y + 1.5, l.w * 0.45, l.h * 0.35, 1.5); cairo_fill(cr);
    }

    // plastic push button with a lamp beside it that lights when the function is on
    static void pushButton(cairo_t* cr, Rect b, const char* label, bool lit, C litCol, double size = 11,
                           double lampW = 13, bool dimLabel = false, double litLevel = 1.0)
    {
        const double gap = 3;
        const Rect cap  = { b.x, b.y, b.w - lampW - gap, b.h };
        const Rect lens = { b.x + b.w - lampW, b.y + 2, lampW, b.h - 4 };
        // shared dark bezel
        cairo_set_source_rgb(cr, 0.03, 0.04, 0.06); rrect(cr, b.x - 2.5, b.y - 2.5, b.w + 5, b.h + 5, 5); cairo_fill(cr);
        // cap: off-white plastic, slightly domed
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5); rrect(cr, cap.x + 0.5, cap.y + 1.5, cap.w, cap.h, 3.5); cairo_fill(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(cap.x, cap.y, cap.x, cap.y + cap.h);
        cairo_pattern_add_color_stop_rgb(g, 0.00, 0.975, 0.975, 0.965);
        cairo_pattern_add_color_stop_rgb(g, 0.55, 0.915, 0.915, 0.905);
        cairo_pattern_add_color_stop_rgb(g, 1.00, 0.760, 0.765, 0.770);
        cairo_set_source(cr, g); rrect(cr, cap.x, cap.y, cap.w, cap.h, 3.5); cairo_fill(cr); cairo_pattern_destroy(g);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.8); cairo_set_line_width(cr, 1);
        cairo_move_to(cr, cap.x + 3.5, cap.y + 1.2); cairo_line_to(cr, cap.x + cap.w - 3.5, cap.y + 1.2); cairo_stroke(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18); rrect(cr, cap.x + .5, cap.y + .5, cap.w - 1, cap.h - 1, 3.5); cairo_stroke(cr);
        // printed label
        if (label && *label) {
            cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, dimLabel ? 0.45 : 1.0);
            text(cr, label, cap.x + cap.w / 2, cap.y + cap.h / 2 + size * 0.36, size, 0);
        }
        lamp(cr, lens, litCol, lit ? litLevel : 0.0);
    }

    // recessed dark window
    static void window(cairo_t* cr, Rect r, double rad = 6)
    {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.10); rrect(cr, r.x - 1, r.y - 1, r.w + 2, r.h + 2.5, rad + 1); cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.02, 0.03, 0.05); rrect(cr, r.x - 1, r.y - 1.5, r.w + 2, r.h + 2, rad + 1); cairo_fill(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
        cairo_pattern_add_color_stop_rgb(g, 0, kScreen.r, kScreen.g, kScreen.b);
        cairo_pattern_add_color_stop_rgb(g, 1, kScreen2.r, kScreen2.g, kScreen2.b);
        cairo_set_source(cr, g); rrect(cr, r.x, r.y, r.w, r.h, rad); cairo_fill(cr); cairo_pattern_destroy(g);
    }


    // ---------------- helpers ----------------
    void commit (int p, float v)
    {
        v = std::clamp (v, kSpecs[p].min, kSpecs[p].max);
        host->beginEdit (p);
        values[p] = v;
        host->setParam (p, v);
        host->endEdit (p);
        dirty = true;
    }

    void nudgeRow (int r, int dir)
    {
        const int p = r < 4 ? pAuto1 + r : pTonicOctave;
        commit (p, values[p] + (float) dir);
    }

    // Knob travel: time knobs get more room at the short end, level knobs at the loud end.
    static double curve (int p)
    {
        switch (p)
        {
            case pGlide: case pAttack: case pRelease: return 3.0;
            case pDry: case pLead: case pHarmony:     return 0.45;
            default:                                  return 1.0;
        }
    }
    static double toNorm (int p, float v)
    {
        const double n = (v - kSpecs[p].min) / (double) (kSpecs[p].max - kSpecs[p].min);
        return std::pow (std::clamp (n, 0.0, 1.0), 1.0 / curve (p));
    }
    static float fromNorm (int p, double n)
    {
        float v = (float) (kSpecs[p].min + (kSpecs[p].max - kSpecs[p].min) * std::pow (n, curve (p)));
        if (p == pFormant) v = std::round (v * 10.0f) / 10.0f;
        else if (kSpecs[p].unit[0] == 'm' || kSpecs[p].unit[0] == '%' || kSpecs[p].unit[0] == 'c') v = std::round (v);
        else v = std::round (v * 10.0f) / 10.0f;
        return std::clamp (v, kSpecs[p].min, kSpecs[p].max);
    }

    void formatValue (int p, char* buf, size_t n) const
    {
        const float v = values[p];
        switch (p)
        {
            case pDry: case pLead: case pHarmony:
                if (v <= -59.9f) std::snprintf (buf, n, "off");
                else std::snprintf (buf, n, "%+.1f dB", v);
                break;
            case pOutput:  std::snprintf (buf, n, "%+.1f dB", v); break;
            case pFormant: std::snprintf (buf, n, "%+.1f st", v); break;
            case pLoose:   std::snprintf (buf, n, "%.0f ct", v); break;
            case pGlide: case pAttack: case pRelease: std::snprintf (buf, n, "%.0f ms", v); break;
            default:       std::snprintf (buf, n, "%.0f%%", v); break;
        }
    }

    static const char* knobLabel (int p)
    {
        switch (p)
        {
            case pFormant: return "FORMANT";   case pNatural: return "NATURAL";
            case pLoose: return "LOOSE";       case pGlide: return "GLIDE";
            case pAttack: return "ATTACK";     case pRelease: return "RELEASE";
            case pConsonants: return "CONSONANTS";
            case pDry: return "DRY";           case pLead: return "LEAD";
            case pHarmony: return "HARMONY";   case pSpread: return "SPREAD";
            case pOutput: return "OUTPUT";
        }
        return "";
    }

    static C capColour (int p)
    {
        switch (p)
        {
            case pFormant:    return kCapPink;
            case pNatural:    return kCapBlue;
            case pLoose:      return kCapBlue;
            case pGlide:      return kCapGreen;
            case pAttack:     return kCapGrey;
            case pRelease:    return kCapGrey;
            case pConsonants: return kCapBlack;
            case pDry:        return kCapGrey;
            case pLead:       return kCapGrey;
            case pHarmony:    return kCapBlue;
            case pSpread:     return kCapBlack;
            case pOutput:     return kCapPink;
        }
        return kCapGrey;
    }

    KeyContext keyCtx() const
    {
        return { (int) values[pKey], scaleMask ((int) values[pScale]) };
    }
    bool autoOn() const { return (int) values[pMode] != kModeMidi; }
    bool midiOn() const { return (int) values[pMode] != kModeAuto; }
    float sungMidi() const { return histCount > 0 && nowT - at (0).t < 0.3 ? at (0).in : 0.0f; }

    // ---------------- drawing ----------------
    void drawFaceplate (cairo_t* cr)
    {
        cairo_set_source_rgb (cr, 0.06, 0.06, 0.07); cairo_rectangle (cr, 0, 0, kFaceW, kFaceH); cairo_fill (cr);
        auto plate = [&] (double x, double y, double w, double h)
        {
            cairo_pattern_t* g = cairo_pattern_create_linear (0, y, 0, y + h);
            cairo_pattern_add_color_stop_rgb (g, 0, kNavyHi.r, kNavyHi.g, kNavyHi.b);
            cairo_pattern_add_color_stop_rgb (g, 0.5, kNavy.r, kNavy.g, kNavy.b);
            cairo_pattern_add_color_stop_rgb (g, 1, kNavyLo.r, kNavyLo.g, kNavyLo.b);
            cairo_set_source (cr, g); cairo_rectangle (cr, x, y, w, h); cairo_fill (cr); cairo_pattern_destroy (g);
            cairo_set_source_rgba (cr, 1, 1, 1, 0.10); cairo_rectangle (cr, x, y, w, 1); cairo_fill (cr);
            cairo_set_source_rgba (cr, 1, 1, 1, 0.06); cairo_rectangle (cr, x, y, 1, h); cairo_fill (cr);
            cairo_set_source_rgba (cr, 0, 0, 0, 0.35); cairo_rectangle (cr, x + w - 1, y, 1, h); cairo_fill (cr);
        };
        plate (4, 0, kFaceW - 8, kModTop - 4);
        plate (4, kModTop, kSeamX - 6, kFaceH - kModTop);
        plate (kSeamX + 2, kModTop, kFaceW - kSeamX - 6, kFaceH - kModTop);
        screw (cr, 16, 12, 0.4); screw (cr, kFaceW - 16, 12, 1.1);
        screw (cr, 16, kModTop - 16, 0.9); screw (cr, kFaceW - 16, kModTop - 16, 0.2);
    }

    void drawModules (cairo_t* cr)
    {
        screw (cr, 16, kModTop + 12, 0.7);            screw (cr, kSeamX - 14, kModTop + 12, 0.2);
        screw (cr, kSeamX + 14, kModTop + 12, 1.0);   screw (cr, kFaceW - 16, kModTop + 12, 0.5);
        rgb (cr, kText); text (cr, "VOICES", 34, kModTop + 16, 11, -1);
        rgb (cr, kText); text (cr, "MIX", kSeamX + 30, kModTop + 16, 11, -1);
        cairo_set_source_rgba (cr, kPrint.r, kPrint.g, kPrint.b, 0.35); cairo_set_line_width (cr, 1);
        cairo_move_to (cr, 34, kModTop + 24.5); cairo_line_to (cr, kSeamX - 30, kModTop + 24.5); cairo_stroke (cr);
        cairo_move_to (cr, kSeamX + 30, kModTop + 24.5); cairo_line_to (cr, kLowLatBtn.x - 12, kModTop + 24.5); cairo_stroke (cr);
        rgb (cr, kPrint);
        text (cr, "VB 1", 16, kFaceH - 10, 9, -1); text (cr, "CHARACTER / MOVEMENT", kSeamX - 14, kFaceH - 10, 9, 1);
        text (cr, "VB 2", kSeamX + 14, kFaceH - 10, 9, -1); text (cr, "LEVELS / WIDTH", kFaceW - 16, kFaceH - 10, 9, 1);
        screw (cr, kSeamX / 2, kFaceH - 13, 0.3); screw (cr, (kSeamX + kFaceW) / 2, kFaceH - 13, 1.2);
    }

    void drawHeader (cairo_t* cr)
    {
        // logo mark: one voice fanning out into three
        {
            const double x0 = 30, x1 = 56, cy = 32;
            cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
            for (int pass = 0; pass < 2; ++pass)
            {
                if (pass == 0) { cairo_set_source_rgba (cr, kAmber.r, kAmber.g, kAmber.b, 0.18); cairo_set_line_width (cr, 7); }
                else           { rgb (cr, kAmber); cairo_set_line_width (cr, 3.0); }
                for (int i = -1; i <= 1; ++i)
                {
                    cairo_move_to (cr, x0, cy);
                    cairo_curve_to (cr, x0 + 12, cy, x0 + 12, cy + i * 15.0, x1, cy + i * 15.0);
                    cairo_stroke (cr);
                }
            }
            rgb (cr, kAmber);
            cairo_arc (cr, x0, cy, 3.6, 0, 2 * M_PI); cairo_fill (cr);
        }
        rgb (cr, kText); text (cr, "VoxBrew", 66, 33, 25, -1);
        cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_NORMAL);
        rgb (cr, kPrint); text (cr, "by Adin Walls", 68, 50, 11, -1);
        cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

        // status LEDs: signal, and harmony voices sounding
        const bool sig = sungMidi() > 0.0f;
        bool harm = false;
        if (histCount > 0 && nowT - at (0).t < 0.3)
            for (int i = 1; i < kSlots; ++i) harm = harm || at (0).v[i] > 0.0f;
        led (cr, 222, 21, 3.6, kGreen, sig ? 1.0 : 0.0);
        led (cr, 222, 39, 3.6, kAccent, harm ? 1.0 : 0.0);
        rgb (cr, kPrint); text (cr, "SIG", 230, 24, 8, -1); text (cr, "HRM", 230, 42, 8, -1);

        // scale selector (recessed window)
        const Rect b = kScaleBox;
        window (cr, b, 5);
        char buf[64];
        std::snprintf (buf, sizeof buf, "%s %s", kKeyNames[(int) values[pKey] % 12],
                       kScaleNames[std::clamp ((int) values[pScale], 0, kScaleCount - 1)]);
        rgb (cr, kDim);  text (cr, "SCALE", b.x + 12, b.y + 21, 9, -1);
        rgb (cr, kAmber); text (cr, buf, b.x + 56, b.y + 23, 14, -1);
        rgb (cr, kDim);
        cairo_move_to (cr, b.x + b.w - 22, b.y + 14); cairo_line_to (cr, b.x + b.w - 12, b.y + 14); cairo_line_to (cr, b.x + b.w - 17, b.y + 21);
        cairo_close_path (cr); cairo_fill (cr);

        // harmony source
        static const char* const names[kModeCount] = { "AUTO", "MIDI", "AUTO + MIDI" };
        static const C cols[kModeCount] = { kAccent, kCapPink, kAmber };
        for (int m = 0; m < kModeCount; ++m)
            pushButton (cr, modeBtn (m), names[m], (int) values[pMode] == m, cols[m], 10.5);
    }

    void drawKeyStrip (cairo_t* cr)
    {
        rgb (cr, kPrint); text (cr, "KEY", 34, 95, 11, -1);
        const double cw = kKeyStrip.w / 12;
        const KeyContext k = keyCtx();
        const float sm = sungMidi();
        const int sungPc = sm > 0.0f ? ((int) std::lround (sm) % 12 + 12) % 12 : -1;
        for (int i = 0; i < 12; ++i)
        {
            const Rect b = { kKeyStrip.x + i * cw + 3, kKeyStrip.y, cw - 6, kKeyStrip.h };
            const bool tonic = i == k.key;
            const bool in = k.inScale (i);
            const bool sung = sungPc == i;
            // lamp: green = key (home note), yellow = in scale, red = being sung now
            const C lc = sung ? kRed : tonic ? kGreen : kYellow;
            pushButton (cr, b, kKeyNames[i], sung || tonic || in, lc, 14, 11, ! in && ! tonic);
            if (hoverCell == i) { cairo_set_source_rgba (cr, 0.3, 0.6, 1.0, 0.16); rrect (cr, b.x, b.y, b.w - 14, b.h, 3.5); cairo_fill (cr); }
        }
    }

    double graphY (double midi) const
    {
        return kGraph.y + kGraph.h / 2 - (midi - center) * (kGraph.h / kSpan);
    }

    void drawGraph (cairo_t* cr)
    {
        const Rect g = kGraph;
        window (cr, g);
        cairo_save (cr);
        rrect (cr, g.x, g.y, g.w, g.h, 6); cairo_clip (cr);

        const KeyContext k = keyCtx();
        const double c = center > 1.0 ? center : 57.0;
        const double rowH = g.h / kSpan;
        const int lo = (int) std::floor (c - kSpan / 2) - 1, hi = (int) std::ceil (c + kSpan / 2) + 1;
        const double cy = g.y + g.h / 2;
        auto yOf = [&] (double midi) { return cy - (midi - c) * rowH; };

        // note lanes: scale notes lighter, the key's home note marked
        for (int n = lo; n <= hi; ++n)
        {
            const double y = yOf (n);
            const bool in = k.inScale (n), home = ((n - k.key) % 12 + 12) % 12 == 0;
            if (in)
            {
                cairo_set_source_rgba (cr, 1, 1, 1, home ? 0.075 : 0.035);
                cairo_rectangle (cr, g.x, y - rowH / 2, g.w, rowH - 0.5); cairo_fill (cr);
            }
            if (in && (home || kSpan < 30))
            {
                char nb[16]; noteName (n, nb, sizeof nb);
                rgb (cr, home ? kAmber : kDim, home ? 0.95 : 0.7);
                if (home || ((n - k.key) % 12 + 12) % 12 == 7 || ((n - k.key) % 12 + 12) % 12 == 4 || ((n - k.key) % 12 + 12) % 12 == 3)
                    text (cr, nb, g.x + 10, y + 3, 8, -1);
            }
        }
        // time grid
        cairo_set_source_rgba (cr, 1, 1, 1, 0.05); cairo_set_line_width (cr, 1);
        for (int s = 1; s < (int) kWindow; ++s)
        {
            const double x = std::floor (g.x + g.w - s * g.w / kWindow) + 0.5;
            cairo_move_to (cr, x, g.y); cairo_line_to (cr, x, g.y + g.h);
        }
        cairo_stroke (cr);

        // traces: harmony voices first, then the lead, then your voice on top
        auto trace = [&] (int slot, C col, double width, double alpha)
        {
            rgb (cr, col, alpha); cairo_set_line_width (cr, width);
            cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
            bool pen = false; double lastM = 0;
            for (int i = 0; i < histCount; ++i)
            {
                const Sample& s = at (i);
                const double age = nowT - s.t;
                if (age > kWindow) break;
                const float m = slot < 0 ? s.in : s.v[slot];
                if (m <= 0.0f) { pen = false; continue; }
                // voices outside the visible range ride along the edge
                const double x = g.x + g.w - 6 - age * (g.w / kWindow), y = std::clamp (yOf (m), g.y + 4.0, g.y + g.h - 4.0);
                if (pen && std::abs (m - lastM) < 1.5) cairo_line_to (cr, x, y);
                else { cairo_move_to (cr, x + 0.01, y); cairo_line_to (cr, x, y); }
                pen = true; lastM = m;
            }
            cairo_stroke (cr);
        };
        for (int slot = kSlots - 1; slot >= 1; --slot) trace (slot, slotColour (slot), 2.4, 0.9);
        trace (Harmonizer::kLeadSlot, kText, 2.0, 0.85);
        trace (-1, kAmber, 1.6, 0.95);

        // legend
        struct L { const char* name; C col; };
        const L legend[] = { { "YOU", kAmber }, { "LEAD", kText }, { "AUTO", kAccent }, { "TONIC", kGreen }, { "MIDI", kCapPink } };
        double lx = g.x + g.w - 14;
        for (int i = 4; i >= 0; --i)
        {
            cairo_text_extents_t e; cairo_set_font_size (cr, 8); cairo_text_extents (cr, legend[i].name, &e);
            lx -= e.x_advance;
            rgb (cr, kPrint, 0.9); text (cr, legend[i].name, lx, g.y + 16, 8, -1);
            lx -= 22;
            rgb (cr, legend[i].col); cairo_set_line_width (cr, 2.4);
            cairo_move_to (cr, lx, g.y + 13); cairo_line_to (cr, lx + 16, g.y + 13); cairo_stroke (cr);
            lx -= 14;
        }
        cairo_restore (cr);
    }

    static void arrow (cairo_t* cr, Rect r, bool right)
    {
        const double cx = r.x + r.w / 2, cy = r.y + r.h / 2, s = 4.5;
        cairo_move_to (cr, cx + (right ? s : -s), cy);
        cairo_line_to (cr, cx + (right ? -s : s), cy - s);
        cairo_line_to (cr, cx + (right ? -s : s), cy + s);
        cairo_close_path (cr); cairo_fill (cr);
    }

    void drawVoicePanel (cairo_t* cr)
    {
        const Rect p = kPanel;
        window (cr, p);
        rgb (cr, kDim); text (cr, "AUTO VOICES", p.x + 12, p.y + 18, 9, -1);
        const bool aOn = autoOn();
        if (! aOn) { rgb (cr, kCapPink, 0.9); text (cr, "MIDI ONLY", p.x + p.w - 12, p.y + 18, 8, 1); }

        const KeyContext k = keyCtx();
        const float sm = sungMidi();
        const int count = (int) values[pAutoVoices];
        for (int r = 0; r < kRows; ++r)
        {
            const Rect b = rowRect (r);
            const bool tonic = r == 4;
            const bool on = tonic ? values[pTonic] > 0.5f : (aOn && r < count);
            const C col = tonic ? kGreen : kAccent;
            if (tonic)
            {
                cairo_set_source_rgba (cr, 1, 1, 1, 0.10); cairo_set_line_width (cr, 1);
                cairo_move_to (cr, p.x + 10, b.y - 4.5); cairo_line_to (cr, p.x + p.w - 10, b.y - 4.5); cairo_stroke (cr);
            }
            // on/off lamp
            const Rect t = rowToggle (r);
            lamp (cr, { t.x + 8, t.y + 4, 14, t.h - 8 }, col, on ? 1.0 : 0.0);

            // step selector
            rgb (cr, kDim, 0.9); arrow (cr, rowDown (r), false); arrow (cr, rowUp (r), true);
            char label[32], note[16] = "";
            int target = -1;
            if (tonic)
            {
                target = 12 * ((int) values[pTonicOctave] + 1) + k.key;
                std::snprintf (label, sizeof label, "tonic");
                noteName (target, note, sizeof note);
            }
            else
            {
                const int steps = (int) std::lround (values[pAuto1 + r]);
                stepLabel (steps, label, sizeof label, false);
                if (sm > 0.0f)
                {
                    target = k.shiftDegrees (k.nearest (sm), steps);
                    noteName (target, note, sizeof note);
                }
            }
            rgb (cr, on ? kText : kDim, on ? 1.0 : 0.6);
            text (cr, label, b.x + 98, b.y + b.h / 2 + 4, 11, 0);
            rgb (cr, on ? col : kDim, on ? 1.0 : 0.5);
            text (cr, note[0] ? note : "-", b.x + b.w - 8, b.y + b.h / 2 + 4, 12, 1);
        }

        // what you're singing
        const double y = p.y + p.h - 16;
        rgb (cr, kDim); text (cr, "YOU", p.x + 12, y, 9, -1);
        if (sm > 0.0f)
        {
            const int n = (int) std::lround (sm);
            char nb[16], cb[16];
            noteName (n, nb, sizeof nb);
            std::snprintf (cb, sizeof cb, "%+.0f ct", (sm - n) * 100.0);
            rgb (cr, kAmber); text (cr, nb, p.x + 104, y + 1, 15, 0);
            rgb (cr, kDim); text (cr, cb, p.x + p.w - 14, y, 9, 1);
        }
        else { rgb (cr, kDim, 0.6); text (cr, "-", p.x + 104, y, 12, 0); }
    }

    void drawMenu (cairo_t* cr)
    {
        const Rect m = menuRect();
        cairo_set_source_rgba (cr, 0, 0, 0, 0.45); rrect (cr, m.x + 3, m.y + 5, m.w, m.h, 8); cairo_fill (cr);
        window (cr, m, 7);
        cairo_set_source_rgba (cr, 1, 1, 1, 0.15); cairo_set_line_width (cr, 1); rrect (cr, m.x + .5, m.y + .5, m.w - 1, m.h - 1, 7); cairo_stroke (cr);
        for (int i = 0; i < kScaleCount; ++i)
        {
            const double y = m.y + 4 + i * 28;
            const bool sel = i == (int) values[pScale];
            if (hoverMenu == i) { cairo_set_source_rgba (cr, 1, 1, 1, 0.08); rrect (cr, m.x + 4, y, m.w - 8, 28, 5); cairo_fill (cr); }
            led (cr, m.x + 18, y + 14, 3.2, kGreen, sel ? 1.0 : 0.0);
            rgb (cr, sel ? kAmber : kText); text (cr, kScaleNames[i], m.x + 34, y + 19, 13, -1);
        }
    }

    void drawKnob(cairo_t* cr, int k)
    {
        const int p = kKnobParams[k];
        const double cx = knobX(k), cy = kKnobY, r = kKnobR;
        const double a0 = 0.75 * M_PI, a1 = 2.25 * M_PI;
        const double n = toNorm(p, values[p]);
        const double av = a0 + (a1 - a0) * n;
        const bool active = dragKnob == k || hoverKnob == k;
        const bool dim = false;
        if (dim) cairo_push_group(cr);
        cairo_new_path(cr);

        // printed scale ticks
        rgb(cr, kPrint, 0.85); cairo_set_line_width(cr, 1.2); cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        for (int i = 0; i <= 10; ++i) {
            const double a = a0 + (a1 - a0) * i / 10.0;
            const double r0 = r + 4, r1 = r + ((i % 5 == 0) ? 9.5 : 7);
            cairo_move_to(cr, cx + std::cos(a) * r0, cy + std::sin(a) * r0);
            cairo_line_to(cr, cx + std::cos(a) * r1, cy + std::sin(a) * r1);
        }
        cairo_stroke(cr);
        if (kSpecs[p].min < 0 && kSpecs[p].max > 0 && p == pFormant) { rgb(cr, kPrint); text(cr, "0", cx, cy - r - 11, 7, 0); }

        // shadow + dark skirt
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45); cairo_arc(cr, cx + 1.5, cy + 2.5, r + 2.5, 0, 2 * M_PI); cairo_fill(cr);
        cairo_pattern_t* sk = cairo_pattern_create_linear(cx, cy - r, cx, cy + r);
        cairo_pattern_add_color_stop_rgb(sk, 0, 0.30, 0.31, 0.33);
        cairo_pattern_add_color_stop_rgb(sk, 1, 0.07, 0.07, 0.08);
        cairo_set_source(cr, sk); cairo_arc(cr, cx, cy, r + 2, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(sk);

        // coloured cap with knurled rim
        const C cap = capColour(p);
        const C rim = mix(cap, C{0, 0, 0}, 0.28);
        rgb(cr, rim); cairo_arc(cr, cx, cy, r, 0, 2 * M_PI); cairo_fill(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18); cairo_set_line_width(cr, 1.0);
        for (int i = 0; i < 28; ++i) {
            const double a = i * 2 * M_PI / 28;
            cairo_move_to(cr, cx + std::cos(a) * (r - 3.2), cy + std::sin(a) * (r - 3.2));
            cairo_line_to(cr, cx + std::cos(a) * r, cy + std::sin(a) * r);
        }
        cairo_stroke(cr);
        cairo_pattern_t* g = cairo_pattern_create_radial(cx - r * 0.35, cy - r * 0.45, r * 0.1, cx, cy, r * 0.85);
        const C hi = mix(cap, C{1, 1, 1}, 0.38);
        cairo_pattern_add_color_stop_rgb(g, 0, hi.r, hi.g, hi.b);
        cairo_pattern_add_color_stop_rgb(g, 1, cap.r, cap.g, cap.b);
        cairo_set_source(cr, g); cairo_arc(cr, cx, cy, r - 3.2, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
        if (active) { cairo_set_source_rgba(cr, 1, 1, 1, 0.16); cairo_arc(cr, cx, cy, r - 3.2, 0, 2 * M_PI); cairo_fill(cr); }

        // pointer line (dark on light caps)
        const bool lightCap = cap.r + cap.g + cap.b > 2.2;
        if (lightCap) cairo_set_source_rgb(cr, 0.12, 0.12, 0.14); else cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
        cairo_set_line_width(cr, 2.6); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, cx + std::cos(av) * r * 0.25, cy + std::sin(av) * r * 0.25);
        cairo_line_to(cr, cx + std::cos(av) * (r - 1.5), cy + std::sin(av) * (r - 1.5));
        cairo_stroke(cr);

        // value + label (silk-screen)
        char buf[32]; formatValue(p, buf, sizeof buf);
        rgb(cr, active ? kAmber : kText); text(cr, buf, cx, cy + r + 26, 11, 0);
        rgb(cr, kPrint); text(cr, knobLabel(p), cx, cy + r + 41, 9, 0);
        if (dim) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, 0.33); }
    }

};

} // namespace voxbrew
