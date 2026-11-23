#pragma once
#include "ui/Notation.h"
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
