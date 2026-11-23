#pragma once
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TempoEditor final : public juce::Component {
public:
    TempoEditor() {
        setComponentID("tempo-editor");
        heading.setText(TRANS("Tempo editor"), juce::dontSendNotification);
        addAndMakeVisible(heading);
        addAndMakeVisible(hint);
        hint.setText(TRANS("Click to set BPM. Drag to draw a tempo ramp."), juce::dontSendNotification);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xffb6c8d4));
    }
    std::function<juce::Range<double>()> visibleBars;
    std::function<void(juce::int64, double, juce::int64, double)> edit;
    void bind(const ostfstudio::OstfpPreview* project) {
        song = project;
        dragging = false;
        if (song) {
            low = 40; high = 200;
            const auto events = juce::JSON::parse(song->sourceScore.toString())["timeline"]["tempoChanges"];
            if (auto* points = events.getArray()) for (const auto& p : *points) {
                const auto bpm = double(p["bpm"]);
                low = std::min(low, std::max(0., std::floor((bpm - 10.) / 10.) * 10.));
                high = std::max(high, std::min(1000., std::ceil((bpm + 10.) / 10.) * 10.));
            }
        }
        repaint();
    }
    juce::Range<int> timelineColumns() const { return graph().getHorizontalRange(); }
    void setTimelineColumns(int left, int right) {
        graphLeft = left;
        graphRight = right;
        repaint();
    }
    void resized() override {
        auto row = getLocalBounds().removeFromTop(26);
        heading.setBounds(row.removeFromLeft(135));
        hint.setBounds(row);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1a2934));
        const auto area = graph();
        if (!song || area.isEmpty()) return;
        const auto bars = visibleBars();
        if (bars.isEmpty()) return;
        g.setFont(juce::FontOptions(13.f));
        for (int i = 0; i <= 4; ++i) {
            const double bpm = low + (high - low) * i / 4.;
            const int y = juce::roundToInt(yFor(bpm));
            g.setColour(juce::Colour(0xff3b505b));
            g.drawHorizontalLine(y, float(area.getX()), float(area.getRight()));
            g.setColour(juce::Colour(0xffc7d7dd));
            g.drawText(juce::String(bpm, 0), 0, y - 8, area.getX() - 6, 16, juce::Justification::centredRight);
        }
        const int stride = std::max(1, int(std::ceil(bars.getLength() * 44. / area.getWidth())));
        for (int bar = int(std::ceil(bars.getStart())); bar < bars.getEnd(); bar += stride) {
            const int x = area.getX() + juce::roundToInt((bar - bars.getStart()) / bars.getLength() * area.getWidth());
            g.setColour(juce::Colour(0xff354954)); g.drawVerticalLine(x, float(area.getY()), float(area.getBottom()));
            g.setColour(juce::Colour(0xffb6c8d4)); g.drawText(juce::String(bar), x + 2, 25, 40, 16, juce::Justification::centredLeft);
        }
        auto tick = tickFor(area.getX());
        const auto last = tickFor(area.getRight());
        const auto xFor = [&](juce::int64 t) { return float(area.getX() + (double(t) / song->ticksPerBar + 1. - bars.getStart()) * area.getWidth() / bars.getLength()); };
        juce::Path path;
        path.startNewSubPath(float(area.getX()), yFor(song->clock.bpmAt(tick)));
        while (const auto next = song->clock.nextChangeAfter(tick)) {
            if (*next > last) break;
            const auto x = xFor(*next);
            path.lineTo(x, yFor(song->clock.bpmAt(tick)));
            path.lineTo(x, yFor(song->clock.bpmAt(*next)));
            tick = *next;
        }
        path.lineTo(float(area.getRight()), yFor(song->clock.bpmAt(tick)));
        g.setColour(juce::Colour(0xff69d7c2));
        g.strokePath(path, juce::PathStrokeType(2.f));
        if (dragging) {
            g.setColour(juce::Colour(0xffffbd69));
            g.drawLine(xFor(first), yFor(firstBpm), xFor(lastTick), yFor(lastBpm), 2.f);
            g.fillEllipse(xFor(lastTick) - 3.f, yFor(lastBpm) - 3.f, 6.f, 6.f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!song || !graph().contains(e.getPosition()) || !e.mods.isLeftButtonDown()) return;
        first = lastTick = tickFor(e.x); firstBpm = lastBpm = bpmFor(e.y);
        dragging = true; showGesture();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragging) return;
        lastTick = tickFor(e.x); lastBpm = bpmFor(e.y); showGesture();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!dragging) return;
        dragging = false;
        hint.setText(TRANS("Click to set BPM. Drag to draw a tempo ramp."), juce::dontSendNotification);
        if (edit) edit(first, firstBpm, tickFor(e.x), bpmFor(e.y));
        repaint();
    }
private:
    int graphLeft = 47, graphRight = -1;
    juce::Rectangle<int> graph() const {
        auto bounds = getLocalBounds().withTrimmedTop(43).withTrimmedBottom(12);
        return bounds.withLeft(graphLeft).withRight(graphRight >= 0 ? graphRight : bounds.getRight());
    }
    float yFor(double bpm) const { const auto r = graph(); return float(r.getBottom() - (bpm - low) / (high - low) * r.getHeight()); }
    double bpmFor(int y) const {
        auto r = graph();
        return std::clamp(std::round(high - std::clamp(double(y-r.getY()) / std::max(1,r.getHeight()), 0., 1.) * (high-low)), 1., 1000.);
    }
    juce::int64 tickFor(int x) const {
        const auto r = graph(); const auto bars = visibleBars();
        const auto fraction = std::clamp(double(x - r.getX()) / std::max(1,r.getWidth()), 0., 1.);
        return std::clamp<juce::int64>(std::llround((bars.getStart() - 1. + fraction * bars.getLength()) * song->ticksPerBar), 0, song->durationTicks - 1);
    }
    void showGesture() {
        hint.setText(juce::String(firstBpm, 0) + " " + juce::String::charToString(0x2192) + " " + juce::String(lastBpm, 0) + " BPM", juce::dontSendNotification);
        repaint();
    }
    const ostfstudio::OstfpPreview* song = nullptr;
    juce::Label heading, hint;
    double low = 40, high = 200, firstBpm = 120, lastBpm = 120;
    juce::int64 first = 0, lastTick = 0;
    bool dragging = false;
};
