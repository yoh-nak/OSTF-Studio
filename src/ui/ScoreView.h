#pragma once
#include "ui/Notation.h"
#include "ui/AnalysisService.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ScoreView final : public juce::Component {
  public:
    ScoreView();
    ~ScoreView() override;
    void resized() override;
    void showScore(const ostfstudio::OstfpPreview&, bool lead, std::optional<std::size_t> track);
    void refresh(const ostfstudio::OstfpPreview&);
    void playbackPosition(juce::int64 tick);

  private:
    class Panel;
    std::unique_ptr<Panel> panel_;
};
using OpenImportedSong = std::function<void(std::unique_ptr<ostfstudio::OstfpPreview>)>;
class ImportResultRow final : public juce::TextButton {
  public:
    ImportResultRow(const juce::String& label, const ostfstudio::OstfpPreview& song, int sourceIndex)
        : TextButton(label), source(song), index(sourceIndex) { setViewportIgnoreDragFlag(true); }
    void paintButton(juce::Graphics &g, bool over, bool down) override {
        g.setColour(juce::Colour(over || down ? 0xff35556a : 0xff283d4e));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4.f);
        auto area = getLocalBounds().reduced(7, 2);
        const bool midi = index >= 0 && source.trackKinds[index] != ostfstudio::TrackKind::audio;
        const auto *events =
            midi ? &(source.trackKinds[index] == ostfstudio::TrackKind::drum ? source.drumNotes[index]
                                                                             : source.pitchedNotes[index])
                 : nullptr;
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.f));
        g.drawText(getButtonText() + (events ? "   " + juce::String(int(events->size())) + " " + TRANS("notes") : ""),
                   area.removeFromTop(20), juce::Justification::centredLeft);
        const int bars = std::max(1, source.bars.endBar - source.bars.firstBar);
        const int step = std::max(1, int((juce::int64(bars) * 28 + area.getWidth() - 1) / std::max(1, area.getWidth())));
        auto barX = [&](double bar) { return area.getX() + juce::roundToInt((bar - source.bars.firstBar) * area.getWidth() / bars); };
        g.setColour(juce::Colour(0xff435360));
        for (juce::int64 b = source.bars.firstBar; b < source.bars.endBar; b += step)
            g.drawVerticalLine(barX(b), float(area.getY()), float(area.getBottom()));
        if (index == -1) {
            for (const auto& lane : source.globalLanes) if (lane.kind == ostfstudio::TimelineLane::chord)
                for (const auto& region : lane.regions) {
                    const auto start = region.exactBars ? region.exactBars->start : double(region.bars.firstBar);
                    const auto end = region.exactBars ? region.exactBars->end : double(region.bars.endBar);
                    auto cell = area.withX(barX(start)).withWidth(std::max(1, barX(end) - barX(start) - 1));
                    g.setColour(juce::Colour(0xff504766));
                    g.fillRect(cell);
                    g.setColour(juce::Colours::white);
                    g.drawText(juce::String::fromUTF8(region.label), cell.reduced(3, 0), juce::Justification::centredLeft);
                }
        }
        if (events && !events->empty()) {
            auto [lo, hi] = std::minmax_element(events->begin(), events->end(),
                                                [](const auto &a, const auto &b) { return a.pitch < b.pitch; });
            const double scale = double(area.getWidth()) / source.playbackEndTick();
            g.setColour(juce::Colour(0xff7ad2d2));
            for (const auto &n : *events)
                g.fillRect(area.getX() + int(n.startTick * scale),
                           area.getY() +
                               (hi->pitch - n.pitch) * (area.getHeight() - 2) / std::max(1, hi->pitch - lo->pitch),
                           std::max(1, int((n.endTick - n.startTick) * scale)), 2);
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (e.getDistanceFromDragStart() < 4)
            return;
        if (auto* host = juce::DragAndDropContainer::findParentDragContainerFor(this);
            host && !host->isDragAndDropActive()) {
            juce::Image badge(juce::Image::ARGB, 200, 30, true);
            juce::Graphics g(badge);
            g.setColour(juce::Colour(0xf02c4957)); g.fillRoundedRectangle(badge.getBounds().toFloat(), 4);
            g.setColour(juce::Colours::white);
            g.drawFittedText(getButtonText(), badge.getBounds().reduced(8,0), juce::Justification::centredLeft,1);
            const juce::Point<int> offset(-12,-10);
            host->startDragging("ostf-import-result", this, juce::ScaledImage(badge), true, &offset, &e.source);
        }
    }
    const ostfstudio::OstfpPreview& source;
    const int index; // -1 is the inferred chord progression.
};
std::unique_ptr<juce::DocumentWindow> makeMediaImportWindow(OpenImportedSong open);
void setImportConnections(juce::DocumentWindow&, std::vector<ostfstudio::AnalysisConnection>);
