#include "DistrhoUI.hpp"
#include "VoxBrewPlugin.hpp"
#include "../../src/VoxView.h"

#include <chrono>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::CairoGraphicsContext;

class VoxBrewUI : public UI, public voxbrew::VoxView::Host
{
public:
    VoxBrewUI()
        : UI ((uint) voxbrew::VoxView::kW, (uint) voxbrew::VoxView::kH, true),
          view (this),
          t0 (std::chrono::steady_clock::now())
    {
        // the editor reads live voice pitches straight from the plugin
        if (auto* const p = static_cast<VoxBrewPlugin*> (static_cast<Plugin*> (getPluginInstancePointer())))
            view.setEngine (p->enginePtr());
    }

protected:
    // ---- host -> editor ----
    void parameterChanged (uint32_t index, float value) override
    {
        view.setValue ((int) index, value);
    }

    void uiIdle() override
    {
        // redraw only when something on screen actually changed
        if (view.tick (now())) repaint();
    }

    // ---- editor -> host ----
    void setParam (int index, float value) override { setParameterValue ((uint32_t) index, value); }
    void beginEdit (int index) override { editParameter ((uint32_t) index, true); }
    void endEdit (int index) override { editParameter ((uint32_t) index, false); }

    // ---- drawing & input ----
    void onCairoDisplay (const CairoGraphicsContext& context) override
    {
        view.draw (context.handle, getWidth(), getHeight());
    }

    // mouse positions arrive in window pixels; the view works in its own 912 x 540 space
    double zoom() const { return std::min (getWidth() / voxbrew::VoxView::kW, getHeight() / voxbrew::VoxView::kH); }
    double sx (double x) const { return x / zoom(); }
    double sy (double y) const { return y / zoom(); }

    bool onMouse (const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const bool fine = (ev.mod & DGL_NAMESPACE::kModifierShift) != 0;
        const bool used = ev.press ? view.mouseDown (sx (ev.pos.getX()), sy (ev.pos.getY()), fine, ev.time / 1000.0)
                                   : view.mouseUp (sx (ev.pos.getX()), sy (ev.pos.getY()));
        if (used) repaint();
        return used;
    }

    bool onMotion (const MotionEvent& ev) override
    {
        const bool fine = (ev.mod & DGL_NAMESPACE::kModifierShift) != 0;
        view.mouseMove (sx (ev.pos.getX()), sy (ev.pos.getY()), fine);
        return true;
    }

    bool onScroll (const ScrollEvent& ev) override
    {
        const bool used = view.scroll (sx (ev.pos.getX()), sy (ev.pos.getY()), ev.delta.getY());
        if (used) repaint();
        return used;
    }

private:
    double now() const
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    }

    voxbrew::VoxView view;
    std::chrono::steady_clock::time_point t0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxBrewUI)
};

UI* createUI()
{
    return new VoxBrewUI();
}

END_NAMESPACE_DISTRHO
