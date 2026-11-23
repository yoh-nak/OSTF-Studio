#pragma once
#include "ui/PianoNoteGrid.h"

// Editable MIDI values are discrete; the production pan envelope can also interpolate.
class PerformanceEditor final : public juce::Component, public juce::SettableTooltipClient {
public:
    using Point = ostfstudio::PerformanceCurve::Point;
    explicit PerformanceEditor(PianoNoteGrid& editor) : grid(editor) {
        setComponentID("performance-editor");
        setTooltip(TRANS("Click to add, drag to move, right-click a point to remove. Shift bypasses snap."));
    }
    std::vector<Point> points;
    juce::int64 cursor = 0;
    int maximum = 127;
    bool linear = false, bipolar = false;
    std::function<void(juce::String, juce::int64, std::optional<double>)> edit;
    void cancel() {
        active = false;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1a2937));
        if (bipolar) {
            g.setColour(juce::Colour(0xff536371));
            g.drawHorizontalLine(yAt(.5), 0, float(getWidth()));
        }
        g.setColour(juce::Colour(0xff83c6ef));
        const auto range = grid.tickRange();
        const Point* last = nullptr;
        for (const auto& p : points) {
            if (last && p.tick >= range.getStart() && last->tick < range.getEnd()) {
                const auto a = position(*last), b = position(p);
                if (linear)
                    g.drawLine({a, b}, 1.5f);
                else {
                    g.drawLine(a.x, a.y, b.x, a.y);
                    g.drawLine(b.x, a.y, b.x, b.y);
                }
            }
            if (range.contains(double(p.tick))) g.fillEllipse(position(p).x - 3, float(yAt(p.value) - 3), 6, 6);
            last = &p;
            if (p.tick >= range.getEnd()) break;
        }
        if (last && last->tick < range.getEnd()) {
            const auto a = position(*last);
            g.drawLine(a.x, a.y, float(getWidth()), a.y);
        }
        if (range.contains(double(cursor))) {
            g.setColour(juce::Colour(0xffffbf66));
            g.drawVerticalLine(juce::roundToInt((cursor - range.getStart()) * getWidth() / range.getLength()), 0,
                               float(getHeight()));
        }
        if (active) {
            auto p = position(candidate);
            g.setColour(juce::Colour(0xffffd678));
            g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
            const auto label =
                maximum == 0 ? juce::String(juce::roundToInt(candidate.value * 200 - 100)) + "%"
                             : juce::String(juce::roundToInt(candidate.value * maximum) - (maximum == 16383 ? 8192 : 0));
            g.drawText(label, std::clamp(int(p.x) + 6, 0, std::max(0, getWidth() - 52)), 0, 52, 18,
                       juce::Justification::centredLeft);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        active = false;
        const Point* nearest = nullptr;
        float distance = 9;
        for (const auto& p : points) {
            const auto d = position(p).getDistanceFrom(e.position);
            if (d < distance) {
                nearest = &p;
                distance = d;
            }
        }
        if (e.mods.isPopupMenu()) {
            if (nearest && edit) edit(nearest->id, nearest->tick, {});
            return;
        }
        if (!e.mods.isLeftButtonDown()) return;
        active = true;
        moved = false;
        existed = nearest != nullptr;
        candidate = nearest ? *nearest : Point{};
        if (!nearest) update(e);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (active && e.mouseWasDraggedSinceMouseDown()) {
            moved = true;
            update(e);
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (!active) return;
        active = false;
        repaint();
        if ((!existed || moved) && edit) edit(candidate.id, candidate.tick, candidate.value);
    }

private:
    PianoNoteGrid& grid;
    Point candidate{};
    bool active = false, moved = false, existed = false;
    int yAt(double v) const { return juce::roundToInt(3 + (1 - v) * std::max(1, getHeight() - 7)); }
    juce::Point<float> position(const Point& p) const {
        const auto range = grid.tickRange();
        return {float((p.tick - range.getStart()) * getWidth() / range.getLength()), float(yAt(p.value))};
    }
    void update(const juce::MouseEvent& e) {
        const auto r = grid.tickRange();
        const double step = e.mods.isShiftDown() ? 1 : grid.grid;
        const auto t = r.getStart() + e.x * r.getLength() / std::max(1, getWidth());
        candidate.tick = std::clamp<juce::int64>(std::llround(std::round(t / step) * step), 0, grid.endTick - 1);
        candidate.value = std::clamp(1. - (e.y - 3.) / std::max(1, getHeight() - 7), 0., 1.);
    }
};
