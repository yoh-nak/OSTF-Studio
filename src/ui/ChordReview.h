#pragma once
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ostfstudio {
using ReviewRange = juce::Range<juce::int64>;
struct ChordDecision {
    ReviewRange range;
    juce::String current;
    juce::StringArray candidates;
    juce::var sourceChord;
    // 0 keeps the score, 1..3 choose a ranked chord, 4 uses the dropped chord.
    int choice = 0;
};
using ChordDecisions = std::vector<ChordDecision>;
juce::StringArray rankChords(const std::vector<PitchedNote>& notes, ReviewRange range);
std::vector<ReviewRange> changedNoteRanges(const std::vector<PitchedNote>& oldNotes,
                                           const std::vector<NoteEdit>& edits);
ChordDecisions analyseChordRanges(const OstfpPreview&, std::optional<size_t> track,
                                  const std::vector<ReviewRange>& ranges, const juce::Array<juce::var>& imported = {},
                                  bool crop = false, juce::int64 end = 0);
juce::Result chordDecisionEdits(const juce::var& score, const ChordDecisions&, juce::Array<juce::var>& remove,
                                juce::Array<juce::var>& insert);
} // namespace ostfstudio

class ChordReviewLane : public juce::Component {
  public:
    ChordReviewLane();
    void display(const ostfstudio::ChordDecisions&, const ostfstudio::OstfpPreview&);
    int selected() const { return index_; }
    juce::Rectangle<int> intervalBounds(int index) const;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void resized() override;
    int preferredHeight(int) const { return 44; }
    std::function<std::pair<juce::Rectangle<int>,juce::Range<double>>()> timeline;
    std::function<void(int)> choose;
    std::function<void(bool all, bool keep)> confirm;

  private:
    int index_ = 0;
    unsigned revision_ = 0;
    ostfstudio::ChordDecisions rows_;
    juce::StringArray names_;
    juce::TextButton intervals_, all_;
    void showMenu(int index);
    juce::String option(const ostfstudio::ChordDecision&,int) const;
};
