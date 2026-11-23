#pragma once
#include "ui/Notation.h"
#include "ui/AudioClipWaveforms.h"
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
        : TextButton(label), source(song), index(sourceIndex) {}
    AudioClipWaveforms *waveforms = nullptr;
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
        if (waveforms)
            for (const auto &clip : source.audio) {
                if (int(clip.track) == index)
                    waveforms->draw(g, area, clip, source.clock, 0, source.playbackEndTick());
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
            host && !host->isDragAndDropActive())
            host->startDragging("ostf-import-result", this, juce::ScaledImage{}, true);
    }
    const ostfstudio::OstfpPreview& source;
    const int index; // -1 is the inferred chord progression.
};
std::unique_ptr<juce::DocumentWindow> makeMediaImportWindow(OpenImportedSong open);
