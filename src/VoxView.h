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
    static constexpr double kFaceW = 860, kFaceH = 700;
    static constexpr double kW = kFaceW + 2 * kCheek, kH = kFaceH;

    struct Host
    {
        virtual void setParam (int index, float value) = 0;
        virtual void beginEdit (int index) = 0;
        virtual void endEdit (int index) = 0;
        virtual void notesChanged (const NoteSet&) {}   // suggested-note edits
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

    // Notes written on the timeline (from the saved session, or the engine at start-up).
    void setNotes (const NoteSet& n) { if (gdrag == dNone) { notes = n; if (sel >= notes.n) sel = -1; dirty = true; } }

    // The tape deck drawer (the eject key toggles it). `instant` skips the slide.
    void setDeck (bool open, bool instant = false) { deckOpen = open; if (instant) deckPos = open ? 1.0 : 0.0; dirty = true; }

    float getValue (int i) const { return i >= 0 && i < pCount ? values[i] : 0.0f; }

    // Call regularly with a running clock in seconds; true means "repaint".
    bool tick (double now)
    {
        const double dt = std::clamp (now - nowT, 0.0, 0.1);
        nowT = now;
        const double want = deckOpen ? 1.0 : 0.0;
        if (deckPos != want)
        {
            // the deck slides out over about a third of a second
            deckPos = want > deckPos ? std::min (1.0, deckPos + dt / 0.38) : std::max (0.0, deckPos - dt / 0.30);
            dirty = true;
        }
        if (deckPos > 0.0 && values[pEcho] > 0.5f)
        {
            // reels turn while the echo is on: quicker for shorter times
            reel = std::fmod (reel + dt * 2.2 * std::sqrt (380.0 / values[pEchoTime]), 2.0 * M_PI * 30.0);
            dirty = true;
        }
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
        if (notesMode() && eng != nullptr)
        {
            const double pos = eng->meterPos.load (std::memory_order_relaxed);
            const bool playing = eng->meterPlaying.load (std::memory_order_relaxed);
            const uint32_t ver = eng->scan.version.load (std::memory_order_relaxed);
            if (playing && gdrag == dNone && (pos < viewStart || pos > viewStart + kViewLen * 0.9))
                viewStart = std::max (0.0, pos - kViewLen * 0.1);
            if (playing || pos != lastPos || ver != lastScanVer) dirty = true;
            lastPos = pos; lastScanVer = ver;
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
        if (notesMode()) drawNoteEditor (cr); else drawGraph (cr);
        drawVoicePanel (cr);
        pushButton (cr, kLowLatBtn, "LOW LATENCY", values[pLowLatency] > 0.5f, kAmber, 9.5, 12);
        drawMangle (cr);
        for (int k = 0; k < kDeckKnob0; ++k) drawKnob (cr, k);
        drawEject (cr);
        drawDeck (cr);
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
        if (inRect (x, y, { kEjectBtn.x - 4, kEjectBtn.y - 4, kEjectBtn.w + 8, kEjectBtn.h + 8 })) { deckOpen = ! deckOpen; return true; }
        if (deckPos > 0.0 && inRect (x, y, kDeck))
        {
            // the deck covers the display: only its own knobs answer
            pressKnob (knobAt (x, y), y, fine, timeSec);
            return true;
        }

        if (notesMode())
        {
            const bool wasSuggest = confirmSuggest, wasClear = confirmClear;
            confirmSuggest = confirmClear = false;
            if (inRect (x, y, kSuggestBtn))
            {
                if (notes.n > 0 && ! wasSuggest) confirmSuggest = true;   // second press replaces your notes
                else suggest();
                return true;
            }
            if (inRect (x, y, kClearBtn))
            {
                if (notes.n > 0 && ! wasClear) confirmClear = true;
                else if (notes.n > 0) { notes.n = 0; sel = -1; notify(); }
                return true;
            }
            if (inRect (x, y, kGraph)) { editorDown (x, y, timeSec); return true; }
        }

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

        return pressKnob (knobAt (x, y), y, fine, timeSec);
    }

    bool pressKnob (int k, double y, bool fine, double timeSec)
    {
        if (k < 0) return false;
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

    bool mouseUp (double, double)
    {
        if (gdrag != dNone) { editorUp(); return true; }
        if (dragKnob >= 0) { host->endEdit (kKnobParams[dragKnob]); dragKnob = -1; dirty = true; return true; }
        return false;
    }

    bool mouseMove (double x, double y, bool fine)
    {
        x -= kCheek;
        const int hk = knobAt (x, y), hc = keyCellAt (x, y), hm = menuOpen ? menuItemAt (x, y) : -1;
        if (hk != hoverKnob || hc != hoverCell || hm != hoverMenu) dirty = true;
        hoverKnob = hk; hoverCell = hc; hoverMenu = hm;
        if (gdrag != dNone) { editorDrag (x, y, fine); return true; }
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
        if (deckPos > 0.0 && inRect (x, y, kDeck))
        {
            const int k = knobAt (x, y);
            if (k >= 0)
            {
                const int p = kKnobParams[k];
                commit (p, fromNorm (p, std::clamp (toNorm (p, values[p]) + (dy > 0 ? 0.02 : -0.02), 0.0, 1.0)));
            }
            return true;
        }
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
        if (notesMode() && inRect (x, y, kGraph))
        {
            const int hit = noteAt (x, y);
            if (hit >= 0)
            {
                // over a note: step it through the scale
                sel = hit;
                const KeyContext kc = keyCtx();
                Note& n = notes.r[hit];
                n.note = std::clamp (kc.inScale (n.note) ? kc.shiftDegrees (n.note, dy > 0 ? 1 : -1) : n.note + (dy > 0 ? 1 : -1), 24, 108);
                notify();
            }
            else
                viewStart = std::max (0.0, viewStart + (dy > 0 ? -1.0 : 1.0));
            return true;
        }
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

    // suggested notes editor
    enum Drag { dNone, dMove, dLeft, dRight, dPan };
    NoteSet notes;
    int sel = -1;
    Drag gdrag = dNone;
    double viewStart = 0.0, dragOffT = 0.0, panX = 0.0, panStart = 0.0;
    double edCenter = 60.0, edSpan = 26.0;
    double lastPos = -2.0, lastNoteClickT = -1.0;
    int lastNoteClick = -3;
    uint32_t lastScanVer = 0;
    bool confirmSuggest = false, confirmClear = false, edited = false;
    double lastFitStart = -1.0;
    uint32_t lastFitVer = 0xFFFFFFFFu;
    const Harmonizer* eng = nullptr;
    float values[pCount];
    double nowT = 0, lastSample = -1, lastActive = -100, center = 0;
    bool dirty = true, menuOpen = false;
    bool deckOpen = false;
    double deckPos = 0.0, reel = 0.0;   // 0 = away, 1 = fully out
    int hoverKnob = -1, dragKnob = -1, hoverCell = -1, hoverMenu = -1;
    int lastClickKnob = -1;
    double lastClickTime = -1, dragY = 0, dragNorm = 0;
    bool dragFine = false;
    cairo_surface_t* bg = nullptr;
    int bgW = 0, bgH = 0;

    // ---------------- layout (faceplate coords, 860 x 700) ----------------
    struct Rect { double x, y, w, h; };
    static constexpr Rect kScaleBox  = { 254, 14, 220, 34 };
    static constexpr Rect kKeyStrip  = { 92, 68, 744, 44 };
    static constexpr Rect kGraph     = { 24, 124, 588, 240 };
    static constexpr Rect kPanel     = { 624, 124, 212, 240 };
    static constexpr double kModTop = 380, kSeamX = 506;
    static constexpr double kMod2Top = 540;                    // the Mangle module, full width
    static constexpr Rect kMangleScr = { 476, 556, 360, 110 };
    static constexpr Rect kLowLatBtn = { 716, 386, 120, 22 };
    static constexpr Rect kSuggestBtn = { 634, 328, 100, 26 };
    static constexpr Rect kClearBtn   = { 742, 328, 84, 26 };
    static constexpr double kViewLen = 10.0;   // seconds shown in the note editor
    static constexpr int kRows = 5;            // 4 auto voices + tonic
    static constexpr double kRowTop = 150, kRowH = 33;
    static constexpr Rect kDeck = { 12, 118, 836, 258 };       // the tape deck, when it is out
    static constexpr Rect kEjectBtn = { 46, 514, 34, 16 };
    static constexpr int kNumKnobs = 22;
    static constexpr int kDeckKnob0 = 18;      // knobs 18.. sit on the tape deck
    static constexpr int kMixKnobs = 12;       // knobs 12.. belong to the Mangle module
    static constexpr int kVoiceKnobs = 7;
    static constexpr int kKnobParams[kNumKnobs] = { pFormant, pNatural, pLoose, pGlide, pAttack, pRelease, pConsonants,
                                                    pDry, pLead, pHarmony, pSpread, pOutput,
                                                    pMangle, pScatter, pWobble, pCrush, pHiss, pCrackle,
                                                    pEcho, pEchoAge, pEchoTime, pEchoRepeats };
    static constexpr double kKnobY = 452, kKnobR = 18;
    static constexpr double kWindow = 6.0;   // seconds shown in the graph
    static constexpr double kSpan = 26.0;    // semitones shown in the graph

    static Rect modeBtn (int m)
    {
        static const Rect r[kModeCount] = { { 488, 14, 80, 34 }, { 574, 14, 80, 34 }, { 660, 14, 80, 34 }, { 746, 14, 84, 34 } };
        return r[m];
    }
    static Rect rowRect (int r)   { return { kPanel.x + 6, kRowTop + r * kRowH + (r == 4 ? 8 : 0), kPanel.w - 12, kRowH - 3 }; }
    static Rect rowToggle (int r) { const Rect b = rowRect (r); return { b.x + 2, b.y + 3, 30, b.h - 6 }; }
    static Rect rowDown (int r)   { const Rect b = rowRect (r); return { b.x + 38, b.y + 3, 20, b.h - 6 }; }
    static Rect rowUp (int r)     { const Rect b = rowRect (r); return { b.x + 138, b.y + 3, 20, b.h - 6 }; }

    static double knobX (int k)
    {
        if (k < kVoiceKnobs) return 44 + k * 68.0;
        if (k < kMixKnobs) return kSeamX + 44 + (k - kVoiceKnobs) * 68.0;
        if (k < kDeckKnob0) return k == kMixKnobs ? 66.0 : 160 + (k - kMixKnobs - 1) * 66.0;
        static const double dx[4] = { 76, 164, kDeck.w - 164, kDeck.w - 76 };
        return kDeck.x + dx[k - kDeckKnob0];
    }
    // the Mangle amount knob is the big one
    static double knobR (int k) { return k == kMixKnobs ? 24.0 : k >= kDeckKnob0 ? 21.0 : kKnobR; }
    static double knobY (int k) { return k < kMixKnobs ? kKnobY : k >= kDeckKnob0 ? kDeck.y + 112 : kMod2Top + 90 - knobR (k); }
    int knobAt (double x, double y) const
    {
        const int count = deckPos > 0.98 ? kNumKnobs : kDeckKnob0;   // deck knobs only once it is out
        for (int k = 0; k < count; ++k)
            if (std::hypot (x - knobX (k), y - knobY (k)) <= knobR (k) + 9) return k;
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
            case pEchoTime:                           return 2.0;
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
            case pMangle: case pEcho:
                if (v < 0.5f) std::snprintf (buf, n, "off");
                else std::snprintf (buf, n, "%.0f%%", v);
                break;
            case pDry: case pLead: case pHarmony:
                if (v <= -59.9f) std::snprintf (buf, n, "off");
                else std::snprintf (buf, n, "%+.1f dB", v);
                break;
            case pOutput:  std::snprintf (buf, n, "%+.1f dB", v); break;
            case pFormant: std::snprintf (buf, n, "%+.1f st", v); break;
            case pLoose:   std::snprintf (buf, n, "%.0f ct", v); break;
            case pGlide: case pAttack: case pRelease: case pEchoTime: std::snprintf (buf, n, "%.0f ms", v); break;
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
            case pMangle: return "AMOUNT";     case pScatter: return "SCATTER";
            case pWobble: return "WOBBLE";     case pCrush: return "CRUSH";
            case pHiss: return "HISS";         case pCrackle: return "CRACKLE";
            case pEcho: return "ECHO";         case pEchoAge: return "AGE";
            case pEchoTime: return "TIME";     case pEchoRepeats: return "REPEATS";
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
            case pMangle:     return kCapPink;
            case pScatter:    return kCapGreen;
            case pWobble:     return kCapBlue;
            case pCrush:      return kCapBlack;
            case pHiss:       return kCapGrey;
            case pCrackle:    return kCapGrey;
            case pEcho:       return kCapPink;
            case pEchoAge:    return kCapBlack;
            case pEchoTime:   return kCapBlue;
            case pEchoRepeats: return kCapGreen;
        }
        return kCapGrey;
    }

    KeyContext keyCtx() const
    {
        return { (int) values[pKey], scaleMask ((int) values[pScale]) };
    }
    bool autoOn() const { return (int) values[pMode] == kModeAuto || (int) values[pMode] == kModeAutoAndMidi; }
    bool notesMode() const { return (int) values[pMode] == kModeNotes; }
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
        plate (4, kModTop, kSeamX - 6, kMod2Top - kModTop - 4);
        plate (kSeamX + 2, kModTop, kFaceW - kSeamX - 6, kMod2Top - kModTop - 4);
        plate (4, kMod2Top, kFaceW - 8, kFaceH - kMod2Top);
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
        const double b1 = kMod2Top - 4;
        text (cr, "VB 1", 16, b1 - 10, 9, -1); text (cr, "CHARACTER / MOVEMENT", kSeamX - 14, b1 - 10, 9, 1);
        text (cr, "VB 2", kSeamX + 14, b1 - 10, 9, -1); text (cr, "LEVELS / WIDTH", kFaceW - 16, b1 - 10, 9, 1);
        screw (cr, kSeamX / 2, b1 - 13, 0.3); screw (cr, (kSeamX + kFaceW) / 2, b1 - 13, 1.2);

        // the Mangle module
        screw (cr, 16, kMod2Top + 12, 1.3); screw (cr, kFaceW - 16, kFaceH - 13, 0.6);
        rgb (cr, kText); text (cr, "MANGLE", 34, kMod2Top + 16, 11, -1);
        cairo_set_source_rgba (cr, kPrint.r, kPrint.g, kPrint.b, 0.35); cairo_set_line_width (cr, 1);
        cairo_move_to (cr, 112, kMod2Top + 12.5); cairo_line_to (cr, kMangleScr.x - 20, kMod2Top + 12.5); cairo_stroke (cr);
        cairo_move_to (cr, 113.5, kMod2Top + 40); cairo_line_to (cr, 113.5, kFaceH - 30); cairo_stroke (cr);
        rgb (cr, kPrint);
        text (cr, "VB 3", 16, kFaceH - 10, 9, -1); text (cr, "GRAIN / DAMAGE", kMangleScr.x - 20, kFaceH - 10, 9, 1);
    }

    // A cheap repeatable random number for the picture (not for sound).
    static double pic (int i, int salt)
    {
        uint32_t h = (uint32_t) i * 2654435761u + (uint32_t) salt * 40503u + 12345u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
        return (h & 0xFFFFFF) / 16777216.0;
    }

    // The Mangle screen: a clean wave in the background and, over it, what the
    // knobs are doing to it. It is a picture of the settings, not a meter.
    void drawMangle (cairo_t* cr)
    {
        const Rect r = kMangleScr;
        const double amt = values[pMangle] * 0.01;
        const double sc = amt * values[pScatter] * 0.01, wb = amt * values[pWobble] * 0.01,
                     cu = amt * values[pCrush] * 0.01, hs = amt * values[pHiss] * 0.01, ck = amt * values[pCrackle] * 0.01;

        led (cr, 98, kMod2Top + 12, 3.6, kAmber, amt > 0.004 ? 0.35 + 0.65 * amt : 0.0);

        window (cr, r);
        cairo_save (cr);
        rrect (cr, r.x + 2, r.y + 2, r.w - 4, r.h - 4, 5); cairo_clip (cr);
        const double cy = r.y + r.h / 2 + 5, amp = r.h * 0.36;
        cairo_set_source_rgba (cr, 1, 1, 1, 0.06); cairo_set_line_width (cr, 1);
        cairo_move_to (cr, r.x, std::floor (cy) + 0.5); cairo_line_to (cr, r.x + r.w, std::floor (cy) + 0.5); cairo_stroke (cr);

        auto voice = [] (double t)
        {
            const double swell = 0.55 + 0.45 * std::sin (2.0 * M_PI * (1.5 * t - 0.2));
            return swell * (0.7 * std::sin (2.0 * M_PI * 9.0 * t) + 0.3 * std::sin (2.0 * M_PI * 18.0 * t + 0.7));
        };
        const int N = (int) r.w * 2;
        auto trace = [&] (bool mangled)
        {
            for (int i = 0; i <= N; ++i)
            {
                double t = (double) i / N, y;
                if (! mangled)
                    y = voice (t);
                else
                {
                    if (cu > 0.02)                       // crush: coarse steps in time
                    {
                        const double cells = N / (1.0 + cu * cu * 30.0);
                        t = std::floor (t * cells) / cells;
                    }
                    const int seg = (int) (t * 30.0);    // scatter: lost, repeated and uneven pieces
                    double gain = 1.0, back = 0.0;
                    if (pic (seg, 1) < sc * sc * 0.55) gain = 0.0;
                    else
                    {
                        if (pic (seg, 2) < sc * 0.6) back = (1.0 + (int) (pic (seg, 3) * 3.0)) / 30.0;
                        gain = 1.0 - sc * 0.6 * pic (seg, 4);
                    }
                    const double tw = t - back + wb * 0.03 * std::sin (2.0 * M_PI * 2.2 * t);   // wobble: stretch and squeeze
                    y = voice (tw) * gain;
                    if (cu > 0.02)                       // crush: coarse steps in level
                    {
                        const double levels = 2.0 + 30.0 * (1.0 - cu) * (1.0 - cu);
                        y = std::round (y * levels) / levels;
                    }
                    y += hs * 0.22 * (pic (i, 5) * 2.0 - 1.0);                                  // hiss: fuzz on the line
                    const int cell = i / 12;                                                    // crackle: spikes
                    if (i % 12 == (int) (pic (cell, 6) * 12.0) && pic (cell, 7) < ck * ck * 0.8)
                        y += (pic (cell, 8) < 0.5 ? -1.0 : 1.0) * (0.35 + 0.9 * pic (cell, 9)) * (0.4 + 0.6 * ck);
                }
                const double px = r.x + 6 + (r.w - 12) * i / N, py = cy - y * amp;
                if (i == 0) cairo_move_to (cr, px, py); else cairo_line_to (cr, px, py);
            }
        };
        cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
        rgb (cr, kDim, amt > 0.004 ? 0.30 : 0.9); cairo_set_line_width (cr, 1.2); trace (false); cairo_stroke (cr);
        if (amt > 0.004)
        {
            cairo_set_source_rgba (cr, kAmber.r, kAmber.g, kAmber.b, 0.16); cairo_set_line_width (cr, 4.5); trace (true); cairo_stroke (cr);
            rgb (cr, kAmber); cairo_set_line_width (cr, 1.3); trace (true); cairo_stroke (cr);
        }
        cairo_restore (cr);

        const char* word = amt <= 0.004 ? "CLEAN" : amt < 0.2 ? "SUBTLE" : amt < 0.4 ? "WORN" : amt < 0.6 ? "ROUGH"
                         : amt < 0.8 ? "BROKEN" : "WRECKED";
        rgb (cr, kDim, 0.9); text (cr, "DAMAGE", r.x + 12, r.y + 17, 8, -1);
        rgb (cr, amt > 0.004 ? kAmber : kDim); text (cr, word, r.x + r.w - 12, r.y + 17, 9, 1);
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
        static const char* const names[kModeCount] = { "AUTO", "MIDI", "BOTH", "NOTES" };
        static const C cols[kModeCount] = { kAccent, kCapPink, kAmber, kGreen };
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
        const bool nm = notesMode();
        rgb (cr, kDim); text (cr, nm ? "VOICES TO SUGGEST" : "AUTO VOICES", p.x + 12, p.y + 18, 9, -1);
        const bool aOn = autoOn() || nm;   // in Notes mode the rows are the recipe for SUGGEST
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

        if (nm)
        {
            const bool ready = eng != nullptr && eng->scan.len.load (std::memory_order_relaxed) > 0;
            pushButton (cr, kSuggestBtn, confirmSuggest ? "REPLACE?" : "SUGGEST", confirmSuggest || (ready && notes.n == 0),
                        confirmSuggest ? kRed : kAmber, 9.5, 12, ! ready);
            pushButton (cr, kClearBtn, confirmClear ? "SURE?" : "CLEAR", confirmClear, kRed, 9.5, 12, notes.n == 0);
            return;
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

    // ---------------- suggested notes editor ----------------
    double edX (double t) const { return kGraph.x + (t - viewStart) / kViewLen * kGraph.w; }
    double edT (double x) const { return viewStart + (x - kGraph.x) / kGraph.w * kViewLen; }
    double edRow() const { return kGraph.h / edSpan; }
    double edY (double midi) const { return kGraph.y + kGraph.h / 2 - (midi - edCenter) * edRow(); }
    double edMidi (double y) const { return edCenter + (kGraph.y + kGraph.h / 2 - y) / edRow(); }

    Rect noteRect (const Note& n) const
    {
        const double h = std::max (7.0, edRow() - 1.0);
        return { edX (n.start), edY (n.note) - h / 2, std::max (3.0, edX (n.end) - edX (n.start)), h };
    }
    int noteAt (double x, double y) const
    {
        for (int i = notes.n - 1; i >= 0; --i)
        {
            const Rect r = noteRect (notes.r[i]);
            if (x >= r.x - 2 && x < r.x + r.w + 2 && y >= r.y - 2 && y < r.y + r.h + 2) return i;
        }
        return -1;
    }

    void notify()
    {
        // keep the selection on the same note after sorting
        const Note keep = sel >= 0 && sel < notes.n ? notes.r[sel] : Note { -1, -1, -1 };
        notes.sort();
        sel = -1;
        for (int i = 0; i < notes.n && keep.note >= 0; ++i)
            if (notes.r[i].start == keep.start && notes.r[i].end == keep.end && notes.r[i].note == keep.note) { sel = i; break; }
        host->notesChanged (notes);
        dirty = true;
    }

    // Fill the timeline with harmony notes for the melody heard so far.
    void suggest()
    {
        if (eng == nullptr) return;
        int steps[4], n = 0;
        for (int r = 0; r < std::min (4, (int) values[pAutoVoices]); ++r)
            steps[n++] = (int) std::lround (values[pAuto1 + r]);
        if (n == 0) { steps[0] = 2; n = 1; }   // no voices switched on: offer a third above
        suggestNotes (eng->scan, keyCtx(), steps, n, notes);
        sel = -1;
        if (notes.n > 0 && (notes.r[0].start < viewStart || notes.r[0].start > viewStart + kViewLen))
            viewStart = std::max (0.0, notes.r[0].start - 1.0);
        fitRange();
        host->notesChanged (notes);
        dirty = true;
    }

    // Centre the editor on what is visible (melody and notes).
    void fitRange()
    {
        double lo = 1e9, hi = -1e9;
        if (eng != nullptr)
        {
            const int a = std::max (0, (int) (viewStart / MelodyScan::kHop));
            const int b = std::min (eng->scan.len.load (std::memory_order_relaxed), (int) ((viewStart + kViewLen) / MelodyScan::kHop));
            for (int i = a; i < b; i += 2)
            {
                const float m = eng->scan.at (i);
                if (m > 0.0f) { lo = std::min (lo, (double) m); hi = std::max (hi, (double) m); }
            }
        }
        for (int i = 0; i < notes.n; ++i)
            if (notes.r[i].end > viewStart && notes.r[i].start < viewStart + kViewLen)
            { lo = std::min (lo, (double) notes.r[i].note); hi = std::max (hi, (double) notes.r[i].note); }
        if (hi < lo) return;
        edSpan = std::clamp (hi - lo + 8.0, 24.0, 48.0);
        edCenter = std::round ((lo + hi) / 2);
    }

    void editorDown (double x, double y, double timeSec)
    {
        const int hit = noteAt (x, y);
        const bool dbl = timeSec - lastNoteClickT < 0.35 && lastNoteClick == (hit >= 0 ? hit : -2);
        lastNoteClickT = timeSec;
        lastNoteClick = hit >= 0 ? hit : -2;

        if (hit >= 0)
        {
            if (dbl) { notes.remove (hit); sel = -1; lastNoteClick = -3; notify(); return; }   // double-click a note: delete it
            sel = hit;
            const Rect r = noteRect (notes.r[hit]);
            const double edge = r.w > 18 ? 6 : 0;
            gdrag = x < r.x + edge ? dLeft : x > r.x + r.w - edge ? dRight : dMove;
            dragOffT = edT (x) - notes.r[hit].start;
            edited = false;
            return;
        }
        if (dbl)
        {
            // double-click empty space: a new half-second note on the nearest note of the key
            const double t = std::max (0.0, edT (x) - 0.05);
            if (notes.add (t, t + 0.5, std::clamp (keyCtx().nearest (edMidi (y)), 24, 108)))
            {
                sel = notes.n - 1;
                notify();
            }
            lastNoteClick = -3;
            return;
        }
        sel = -1;
        gdrag = dPan; panX = x; panStart = viewStart;
    }

    void editorDrag (double x, double y, bool fine)
    {
        dirty = true;
        if (gdrag == dPan)
        {
            viewStart = std::max (0.0, panStart - (x - panX) / kGraph.w * kViewLen);
            return;
        }
        if (sel < 0 || sel >= notes.n) return;
        Note& n = notes.r[sel];
        const double t = edT (x);
        edited = true;
        if (gdrag == dMove)
        {
            const double len = n.end - n.start;
            n.start = std::max (0.0, t - dragOffT);
            n.end = n.start + len;
            // snaps to the key; hold Shift for any semitone
            const double m = edMidi (y);
            n.note = std::clamp (fine ? (int) std::lround (m) : keyCtx().nearest (m), 24, 108);
        }
        else if (gdrag == dLeft)  n.start = std::clamp (t, 0.0, n.end - 0.05);
        else if (gdrag == dRight) n.end = std::max (n.start + 0.05, t);
    }

    void editorUp()
    {
        const bool changed = gdrag != dPan && edited;
        const bool panned = gdrag == dPan;
        gdrag = dNone;
        if (changed) notify();
        if (panned) fitRange();
        dirty = true;
    }

    static void fmtTime (double sec, char* buf, size_t n)
    {
        const int s = (int) sec;
        std::snprintf (buf, n, "%d:%02d", s / 60, s % 60);
    }

    void drawNoteEditor (cairo_t* cr)
    {
        const Rect g = kGraph;
        if (gdrag == dNone && (lastFitStart != viewStart || lastFitVer != lastScanVer))
        {
            fitRange();
            lastFitStart = viewStart; lastFitVer = lastScanVer;
        }
        window (cr, g);
        cairo_save (cr);
        rrect (cr, g.x, g.y, g.w, g.h, 6); cairo_clip (cr);

        const KeyContext k = keyCtx();
        const double rowH = edRow();
        const int lo = (int) std::floor (edCenter - edSpan / 2) - 1, hi = (int) std::ceil (edCenter + edSpan / 2) + 1;

        // note lanes, as in the live view
        for (int n = lo; n <= hi; ++n)
        {
            const double y = edY (n);
            const int deg = ((n - k.key) % 12 + 12) % 12;
            const bool in = k.inScale (n), home = deg == 0;
            if (in)
            {
                cairo_set_source_rgba (cr, 1, 1, 1, home ? 0.075 : 0.035);
                cairo_rectangle (cr, g.x, y - rowH / 2, g.w, rowH - 0.5); cairo_fill (cr);
            }
            if (in && (home || deg == 7 || deg == 4 || deg == 3))
            {
                char nb[16]; noteName (n, nb, sizeof nb);
                rgb (cr, home ? kAmber : kDim, home ? 0.95 : 0.7);
                text (cr, nb, g.x + 10, y + 3, 8, -1);
            }
        }
        // seconds
        cairo_set_line_width (cr, 1);
        for (int sec = (int) std::ceil (viewStart); sec < viewStart + kViewLen; ++sec)
        {
            const double x = std::floor (edX (sec)) + 0.5;
            cairo_set_source_rgba (cr, 1, 1, 1, sec % 5 == 0 ? 0.10 : 0.05);
            cairo_move_to (cr, x, g.y); cairo_line_to (cr, x, g.y + g.h); cairo_stroke (cr);
            if (sec % 2 == 0)
            {
                char tb[16]; fmtTime (sec, tb, sizeof tb);
                rgb (cr, kDim, 0.8); text (cr, tb, x + 4, g.y + g.h - 6, 8, -1);
            }
        }

        // your melody, as heard while the song played
        int scanLen = 0;
        if (eng != nullptr)
        {
            scanLen = eng->scan.len.load (std::memory_order_relaxed);
            rgb (cr, kAmber, 0.9); cairo_set_line_width (cr, 1.6);
            cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
            const int a = std::max (0, (int) (viewStart / MelodyScan::kHop));
            const int b = std::min (scanLen, (int) ((viewStart + kViewLen) / MelodyScan::kHop) + 1);
            bool pen = false; float last = 0;
            for (int i = a; i < b; ++i)
            {
                const float m = eng->scan.at (i);
                if (m <= 0.0f) { pen = false; continue; }
                const double x = edX (i * MelodyScan::kHop), y = edY (m);
                if (pen && std::abs (m - last) < 1.5f) cairo_line_to (cr, x, y);
                else { cairo_move_to (cr, x + 0.01, y); cairo_line_to (cr, x, y); }
                pen = true; last = m;
            }
            cairo_stroke (cr);
        }

        // the harmony notes
        for (int i = 0; i < notes.n; ++i)
        {
            const Note& n = notes.r[i];
            if (n.end < viewStart || n.start > viewStart + kViewLen) continue;
            const Rect r = noteRect (n);
            const bool s = i == sel;
            cairo_set_source_rgba (cr, 0, 0, 0, 0.35); rrect (cr, r.x + 1, r.y + 1.5, r.w, r.h, 2.5); cairo_fill (cr);
            cairo_pattern_t* gr = cairo_pattern_create_linear (0, r.y, 0, r.y + r.h);
            const C top = mix (kAccent, C { 1, 1, 1 }, s ? 0.45 : 0.15), bot = mix (kAccent, C { 0, 0, 0 }, s ? 0.05 : 0.30);
            cairo_pattern_add_color_stop_rgb (gr, 0, top.r, top.g, top.b);
            cairo_pattern_add_color_stop_rgb (gr, 1, bot.r, bot.g, bot.b);
            cairo_set_source (cr, gr); rrect (cr, r.x, r.y, r.w, r.h, 2.5); cairo_fill (cr); cairo_pattern_destroy (gr);
            if (s) { rgb (cr, kText); cairo_set_line_width (cr, 1.4); rrect (cr, r.x - 0.5, r.y - 0.5, r.w + 1, r.h + 1, 3); cairo_stroke (cr); }
            if (r.w > 26 && r.h >= 8)
            {
                char nb[16]; noteName (n.note, nb, sizeof nb);
                cairo_set_source_rgba (cr, 0.03, 0.10, 0.12, 0.95);
                text (cr, nb, r.x + 5, r.y + r.h / 2 + 3, std::min (9.0, r.h), -1);
            }
        }

        // playhead
        const double pos = eng != nullptr ? eng->meterPos.load (std::memory_order_relaxed) : -1.0;
        if (pos >= viewStart && pos <= viewStart + kViewLen)
        {
            const double x = std::floor (edX (pos)) + 0.5;
            rgb (cr, kText, 0.9); cairo_set_line_width (cr, 1.2);
            cairo_move_to (cr, x, g.y); cairo_line_to (cr, x, g.y + g.h); cairo_stroke (cr);
        }

        // legend and help
        rgb (cr, kAmber); cairo_set_line_width (cr, 2.4);
        cairo_move_to (cr, g.x + g.w - 122, g.y + 13); cairo_line_to (cr, g.x + g.w - 106, g.y + 13); cairo_stroke (cr);
        rgb (cr, kPrint, 0.9); text (cr, "YOU", g.x + g.w - 100, g.y + 16, 8, -1);
        rgb (cr, kAccent); rrect (cr, g.x + g.w - 68, g.y + 9, 16, 8, 2); cairo_fill (cr);
        rgb (cr, kPrint, 0.9); text (cr, "NOTES", g.x + g.w - 46, g.y + 16, 8, -1);

        const char* msg = nullptr;
        if (pos < 0.0)            msg = "This host gives no song position, so Notes can't follow the song here.";
        else if (notes.n > 0)     msg = nullptr;
        else if (scanLen == 0)    msg = "Play the song once so VoxBrew can learn your melody.";
        else                      msg = "Press SUGGEST for harmony notes, or double-click to add one.";
        if (msg != nullptr)
        {
            cairo_text_extents_t e; cairo_set_font_size (cr, 12); cairo_text_extents (cr, msg, &e);
            const double cx = g.x + g.w / 2, cy = g.y + g.h / 2;
            cairo_set_source_rgba (cr, 0.02, 0.03, 0.05, 0.8); rrect (cr, cx - e.width / 2 - 14, cy - 16, e.width + 28, 30, 6); cairo_fill (cr);
            rgb (cr, kText); text (cr, msg, cx, cy + 4, 12, 0);
        }
        else
        {
            rgb (cr, kDim, 0.85);
            text (cr, "drag: move   edges: length   double-click: add or delete   scroll: step", g.x + 48, g.y + 16, 8, -1);
        }
        cairo_restore (cr);
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


    // ---------------- the tape deck ----------------
    // An eject key, cassette-player style, under the Formant knob.
    void drawEject (cairo_t* cr)
    {
        const Rect b = kEjectBtn;
        const double dn = deckOpen ? 1.0 : 0.0;   // the key stays down while the deck is out
        cairo_set_source_rgb (cr, 0.03, 0.04, 0.06); rrect (cr, b.x - 2.5, b.y - 2.5, b.w + 5, b.h + 5, 4); cairo_fill (cr);
        cairo_pattern_t* g = cairo_pattern_create_linear (0, b.y, 0, b.y + b.h);
        cairo_pattern_add_color_stop_rgb (g, 0.0, 0.93 - 0.20 * dn, 0.94 - 0.20 * dn, 0.95 - 0.20 * dn);
        cairo_pattern_add_color_stop_rgb (g, 0.5, 0.74 - 0.12 * dn, 0.76 - 0.12 * dn, 0.78 - 0.12 * dn);
        cairo_pattern_add_color_stop_rgb (g, 1.0, 0.52, 0.54, 0.57);
        cairo_set_source (cr, g); rrect (cr, b.x, b.y + dn, b.w, b.h - dn, 2.5); cairo_fill (cr); cairo_pattern_destroy (g);
        cairo_set_source_rgba (cr, 1, 1, 1, 0.75 - 0.5 * dn); cairo_set_line_width (cr, 1);
        cairo_move_to (cr, b.x + 2.5, b.y + 1.2 + dn); cairo_line_to (cr, b.x + b.w - 2.5, b.y + 1.2 + dn); cairo_stroke (cr);
        // the eject sign: a triangle over a bar
        const double cx = b.x + b.w / 2, cy = b.y + b.h / 2 + dn * 0.6;
        cairo_set_source_rgb (cr, 0.10, 0.12, 0.16);
        cairo_move_to (cr, cx, cy - 5); cairo_line_to (cr, cx + 6.5, cy + 0.8); cairo_line_to (cr, cx - 6.5, cy + 0.8); cairo_close_path (cr); cairo_fill (cr);
        cairo_rectangle (cr, cx - 6.5, cy + 2.4, 13, 2.2); cairo_fill (cr);
        led (cr, b.x + b.w + 11, b.y + b.h / 2, 3.0, kAmber, values[pEcho] > 0.5f ? 1.0 : 0.0);   // echo is on
    }

    static void brushed (cairo_t* cr, Rect r, double rad, double light)
    {
        cairo_save (cr);
        rrect (cr, r.x, r.y, r.w, r.h, rad); cairo_clip (cr);
        cairo_pattern_t* g = cairo_pattern_create_linear (0, r.y, 0, r.y + r.h);
        cairo_pattern_add_color_stop_rgb (g, 0.00, 0.90 * light, 0.91 * light, 0.93 * light);
        cairo_pattern_add_color_stop_rgb (g, 0.45, 0.78 * light, 0.80 * light, 0.83 * light);
        cairo_pattern_add_color_stop_rgb (g, 1.00, 0.63 * light, 0.65 * light, 0.69 * light);
        cairo_set_source (cr, g); cairo_paint (cr); cairo_pattern_destroy (g);
        cairo_set_line_width (cr, 1);
        for (int i = 0; i < (int) r.h; ++i)          // the brushing: fine streaks along the plate
        {
            const double a = pic (i, 21);
            if (a < 0.5) cairo_set_source_rgba (cr, 1, 1, 1, 0.16 * a); else cairo_set_source_rgba (cr, 0, 0, 0, 0.10 * (a - 0.5));
            const double x0 = r.x + pic (i, 22) * r.w * 0.3, x1 = r.x + r.w - pic (i, 23) * r.w * 0.3;
            cairo_move_to (cr, x0, r.y + i + 0.5); cairo_line_to (cr, x1, r.y + i + 0.5); cairo_stroke (cr);
        }
        cairo_restore (cr);
        cairo_set_source_rgba (cr, 1, 1, 1, 0.7); cairo_set_line_width (cr, 1);
        cairo_move_to (cr, r.x + rad, r.y + 0.5); cairo_line_to (cr, r.x + r.w - rad, r.y + 0.5); cairo_stroke (cr);
        cairo_set_source_rgba (cr, 0, 0, 0, 0.35); rrect (cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, rad); cairo_stroke (cr);
    }

    static void engraved (cairo_t* cr, const char* s, double x, double y, double size, int align)
    {
        cairo_set_source_rgba (cr, 1, 1, 1, 0.65); text (cr, s, x, y + 1, size, align);
        cairo_set_source_rgb (cr, 0.13, 0.17, 0.24); text (cr, s, x, y, size, align);
    }

    void drawReel (cairo_t* cr, double cx, double cy, double pack, double ang)
    {
        // the wound tape
        cairo_pattern_t* g = cairo_pattern_create_radial (cx, cy, 16, cx, cy, pack);
        cairo_pattern_add_color_stop_rgb (g, 0, 0.21, 0.13, 0.09);
        cairo_pattern_add_color_stop_rgb (g, 1, 0.12, 0.075, 0.055);
        cairo_set_source (cr, g); cairo_arc (cr, cx, cy, pack, 0, 2 * M_PI); cairo_fill (cr); cairo_pattern_destroy (g);
        cairo_set_source_rgba (cr, 1, 1, 1, 0.07); cairo_set_line_width (cr, 1);
        for (double rr = 22; rr < pack - 2; rr += 5) { cairo_arc (cr, cx, cy, rr, ang * 0.2, ang * 0.2 + 1.3); cairo_stroke (cr); }
        // the white hub with its six teeth
        cairo_set_source_rgb (cr, 0.94, 0.95, 0.93); cairo_arc (cr, cx, cy, 17, 0, 2 * M_PI); cairo_fill (cr);
        cairo_set_source_rgba (cr, 0, 0, 0, 0.25); cairo_set_line_width (cr, 1); cairo_arc (cr, cx, cy, 16.5, 0, 2 * M_PI); cairo_stroke (cr);
        cairo_set_source_rgb (cr, 0.05, 0.06, 0.09); cairo_arc (cr, cx, cy, 9.5, 0, 2 * M_PI); cairo_fill (cr);
        cairo_set_source_rgb (cr, 0.94, 0.95, 0.93); cairo_set_line_width (cr, 3.2); cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
        for (int i = 0; i < 6; ++i)
        {
            const double a = ang + i * M_PI / 3;
            cairo_move_to (cr, cx + std::cos (a) * 5.5, cy + std::sin (a) * 5.5);
            cairo_line_to (cr, cx + std::cos (a) * 10.5, cy + std::sin (a) * 10.5);
        }
        cairo_stroke (cr);
    }

    // The deck itself: a brushed-metal unit that rises out of the slot above
    // the knob rows and covers the display while it is out.
    void drawDeck (cairo_t* cr)
    {
        if (deckPos <= 0.0) return;
        const Rect d = kDeck;
        const double e = 1.0 - std::pow (1.0 - deckPos, 3.0);
        cairo_save (cr);
        cairo_rectangle (cr, d.x - 4, d.y - 12, d.w + 8, d.h + 12); cairo_clip (cr);
        cairo_translate (cr, 0, (1.0 - e) * (d.h + 12));

        // shadow thrown on the panel above
        cairo_pattern_t* sh = cairo_pattern_create_linear (0, d.y - 12, 0, d.y + 2);
        cairo_pattern_add_color_stop_rgba (sh, 0, 0, 0, 0, 0); cairo_pattern_add_color_stop_rgba (sh, 1, 0, 0, 0, 0.55);
        cairo_set_source (cr, sh); cairo_rectangle (cr, d.x, d.y - 12, d.w, 14); cairo_fill (cr); cairo_pattern_destroy (sh);

        brushed (cr, d, 5, 1.0);
        screw (cr, d.x + 13, d.y + 13, 0.5); screw (cr, d.x + d.w - 13, d.y + 13, 1.2);

        // navy foot rail, like the faceplate
        const Rect rail = { d.x + 1, d.y + d.h - 34, d.w - 2, 33 };
        cairo_pattern_t* g = cairo_pattern_create_linear (0, rail.y, 0, rail.y + rail.h);
        cairo_pattern_add_color_stop_rgb (g, 0, kNavyHi.r, kNavyHi.g, kNavyHi.b);
        cairo_pattern_add_color_stop_rgb (g, 1, kNavyLo.r, kNavyLo.g, kNavyLo.b);
        cairo_set_source (cr, g); cairo_rectangle (cr, rail.x, rail.y, rail.w, rail.h); cairo_fill (cr); cairo_pattern_destroy (g);
        cairo_set_source_rgba (cr, 0, 0, 0, 0.5); cairo_rectangle (cr, rail.x, rail.y, rail.w, 1); cairo_fill (cr);
        cairo_set_source_rgba (cr, 1, 1, 1, 0.12); cairo_rectangle (cr, rail.x, rail.y + 1, rail.w, 1); cairo_fill (cr);
        screw (cr, d.x + 13, rail.y + 17, 0.9); screw (cr, d.x + d.w - 13, rail.y + 17, 0.1);
        rgb (cr, kPrint); text (cr, "VB 4", d.x + 30, rail.y + 21, 9, -1); text (cr, "TAPE / ECHO", d.x + d.w - 30, rail.y + 21, 9, 1);
        const bool on = values[pEcho] > 0.5f;
        led (cr, d.x + d.w / 2 - 44, rail.y + 17, 3.6, kAmber, on ? 1.0 : 0.0);
        rgb (cr, kText); text (cr, "TAPE ECHO", d.x + d.w / 2 + 6, rail.y + 21.5, 12, 0);

        // the cassette bay: a dark well with a clear plastic lid
        const Rect bay = { d.x + 240, d.y + 16, d.w - 480, 190 };
        window (cr, bay, 8);
        const Rect cs = { bay.x + 14, bay.y + 12, bay.w - 28, bay.h - 24 };
        cairo_set_source_rgba (cr, 0.62, 0.80, 0.95, 0.10); rrect (cr, cs.x, cs.y, cs.w, cs.h, 7); cairo_fill (cr);
        cairo_set_source_rgba (cr, 0.80, 0.92, 1.0, 0.45); cairo_set_line_width (cr, 1.2); rrect (cr, cs.x + .5, cs.y + .5, cs.w - 1, cs.h - 1, 7); cairo_stroke (cr);
        // paper label
        const Rect lb = { cs.x + 26, cs.y + 9, cs.w - 52, 20 };
        cairo_set_source_rgb (cr, 0.93, 0.90, 0.80); rrect (cr, lb.x, lb.y, lb.w, lb.h, 2); cairo_fill (cr);
        rgb (cr, kAmber); cairo_rectangle (cr, lb.x, lb.y + lb.h - 4, lb.w, 4); cairo_fill (cr);
        cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_source_rgb (cr, 0.14, 0.18, 0.28); text (cr, "VoxBrew echo loop", lb.x + 8, lb.y + 12, 9, -1);
        cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        char tb[24]; std::snprintf (tb, sizeof tb, "%.0f ms", values[pEchoTime]);
        text (cr, tb, lb.x + lb.w - 8, lb.y + 12, 9, 1);

        // reels: the more Repeats, the fuller the left one
        const double ry = cs.y + 84, lx = cs.x + cs.w * 0.27, rx = cs.x + cs.w * 0.73;
        const double fill = values[pEchoRepeats] / 95.0;
        const double packL = 30 + 18 * fill, packR = 48 - 18 * fill;
        drawReel (cr, lx, ry, packL, reel); drawReel (cr, rx, ry, packR, reel * 1.3 + 0.4);
        // tape path past the head
        const double ty = cs.y + cs.h - 14;
        cairo_set_source_rgb (cr, 0.27, 0.17, 0.12); cairo_set_line_width (cr, 1.6);
        cairo_move_to (cr, lx - packL, ry); cairo_line_to (cr, cs.x + 16, ty); cairo_line_to (cr, cs.x + cs.w - 16, ty); cairo_line_to (cr, rx + packR, ry); cairo_stroke (cr);
        for (double gx : { cs.x + 16, cs.x + cs.w - 16 })
        {
            cairo_set_source_rgb (cr, 0.80, 0.82, 0.85); cairo_arc (cr, gx, ty, 4, 0, 2 * M_PI); cairo_fill (cr);
            cairo_set_source_rgb (cr, 0.25, 0.27, 0.30); cairo_arc (cr, gx, ty, 1.3, 0, 2 * M_PI); cairo_fill (cr);
        }
        brushed (cr, { cs.x + cs.w / 2 - 20, ty - 9, 40, 16 }, 2, 0.95);          // the head
        cairo_set_source_rgb (cr, 0.1, 0.1, 0.12); cairo_rectangle (cr, cs.x + cs.w / 2 - 0.6, ty - 6, 1.2, 10); cairo_fill (cr);
        // gloss on the lid
        cairo_pattern_t* gl = cairo_pattern_create_linear (cs.x, cs.y, cs.x + cs.w * 0.5, cs.y + cs.h);
        cairo_pattern_add_color_stop_rgba (gl, 0.00, 1, 1, 1, 0.20); cairo_pattern_add_color_stop_rgba (gl, 0.35, 1, 1, 1, 0.05);
        cairo_pattern_add_color_stop_rgba (gl, 0.36, 1, 1, 1, 0.00); cairo_pattern_add_color_stop_rgba (gl, 1.00, 1, 1, 1, 0.00);
        cairo_set_source (cr, gl); rrect (cr, cs.x, cs.y, cs.w, cs.h, 7); cairo_fill (cr); cairo_pattern_destroy (gl);

        // engraved panel lines round each pair of knobs
        cairo_set_line_width (cr, 1);
        for (double px : { d.x + 30.0, d.x + d.w - 210.0 })
        {
            cairo_set_source_rgba (cr, 0, 0, 0, 0.30); rrect (cr, px + .5, d.y + 28.5, 180, 178, 6); cairo_stroke (cr);
            cairo_set_source_rgba (cr, 1, 1, 1, 0.55); rrect (cr, px + .5, d.y + 29.5, 180, 178, 6); cairo_stroke (cr);
        }
        engraved (cr, "LOOP", d.x + 120, d.y + 48, 9, 0);
        engraved (cr, "MOTOR", d.x + d.w - 120, d.y + 48, 9, 0);

        for (int k = kDeckKnob0; k < kNumKnobs; ++k) drawKnob (cr, k);
        cairo_restore (cr);

        // the slot it comes out of
        cairo_set_source_rgba (cr, 0, 0, 0, 0.6 * std::min (1.0, deckPos * 4)); cairo_rectangle (cr, d.x - 4, d.y + d.h - 1.5, d.w + 8, 2.5); cairo_fill (cr);
    }

    void drawKnob(cairo_t* cr, int k)
    {
        const int p = kKnobParams[k];
        const double cx = knobX(k), cy = knobY(k), r = knobR(k);
        const double a0 = 0.75 * M_PI, a1 = 2.25 * M_PI;
        const double n = toNorm(p, values[p]);
        const double av = a0 + (a1 - a0) * n;
        const bool active = dragKnob == k || hoverKnob == k;
        const bool dim = false;
        const bool metal = k >= kDeckKnob0;   // on the tape deck: dark printing on bright metal
        if (dim) cairo_push_group(cr);
        cairo_new_path(cr);

        // printed scale ticks
        if (metal) cairo_set_source_rgba(cr, 0.13, 0.17, 0.24, 0.9); else rgb(cr, kPrint, 0.85);
        cairo_set_line_width(cr, 1.2); cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
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
        if (metal)
        {
            if (active) cairo_set_source_rgb(cr, 0.62, 0.30, 0.02); else cairo_set_source_rgb(cr, 0.10, 0.13, 0.19);
            text(cr, buf, cx, cy + r + 26, 11, 0);
            engraved(cr, knobLabel(p), cx, cy + r + 41, 9, 0);
        }
        else
        {
            rgb(cr, active ? kAmber : kText); text(cr, buf, cx, cy + r + 26, 11, 0);
            rgb(cr, kPrint); text(cr, knobLabel(p), cx, cy + r + 41, 9, 0);
        }
        if (dim) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, 0.33); }
    }

};

} // namespace voxbrew
