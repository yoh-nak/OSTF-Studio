#pragma once
#include "ui/Notation.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ScoreWindow final : public juce::DocumentWindow {
  public:
    ScoreWindow();
    void showScore(const ostfstudio::OstfpPreview&, bool lead, std::optional<std::size_t> track);
    void refresh(const ostfstudio::OstfpPreview&);
    void playbackPosition(juce::int64 tick);
    void closeButtonPressed() override { setVisible(false); }

  private:
    class Panel;
    Panel* panel_;
};
std::unique_ptr<juce::DocumentWindow> makeMediaImportWindow(std::function<void(const juce::File&)> open);
