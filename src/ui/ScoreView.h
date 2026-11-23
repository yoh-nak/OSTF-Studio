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
std::unique_ptr<juce::DocumentWindow> makeMediaImportWindow(std::function<void(const juce::File&)> open);
