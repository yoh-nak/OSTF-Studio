#include "ui/ChordReview.h"
#include "ui/GlobalTimelineEdit.h"
#include "ui/MainComponent.h"
#include <set>

namespace ostfstudio {
using Tick = juce::int64;

static ReviewRange chordRange(const juce::var& event) {
    const Tick start = event["start"];
    return {start, start + Tick(event["duration"])};
}

juce::StringArray rankChords(const std::vector<PitchedNote>& notes, ReviewRange range) {
    std::array<double, 12> weights{};
    int bass = 128;
    double total = 0;
    for (const auto& note : notes) {
        const auto duration = range.getIntersectionWith({note.startTick, note.endTick}).getLength();
        if (!note.enabled || note.fixedTrigger || duration <= 0 || note.pitch < 0 || note.pitch > 127)
            continue;
        weights[note.pitch % 12] += duration;
        bass = std::min(bass, note.pitch);
        total += duration;
    }
    if (total == 0)
        return {};
    struct Quality {
        const char* name;
        std::vector<int> intervals;
    };
    static const Quality qualities[] = {{"", {0, 4, 7}},         {"m", {0, 3, 7}},      {"7", {0, 4, 7, 10}},
                                        {"maj7", {0, 4, 7, 11}}, {"m7", {0, 3, 7, 10}}, {"dim", {0, 3, 6}},
                                        {"m7b5", {0, 3, 6, 10}}, {"aug", {0, 4, 8}},    {"sus4", {0, 5, 7}},
                                        {"sus2", {0, 2, 7}}};
    static const char* names[] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    struct Match {
        double fit;
        juce::String name;
    };
    std::vector<Match> matches;
    for (int root = 0; root < 12; ++root)
        for (const auto& quality : qualities) {
            double covered = 0;
            int absent = 0;
            for (auto interval : quality.intervals) {
                const auto weight = weights[(root + interval) % 12];
                covered += weight;
                if (weight == 0)
                    ++absent;
            }
            const double fit =
                covered / total - .13 * absent + .08 * weights[root] / total + (bass % 12 == root ? .04 : 0);
            matches.push_back({fit, juce::String(names[root]) + quality.name});
        }
    std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.fit > b.fit; });
    return {matches[0].name, matches[1].name, matches[2].name};
}

std::vector<ReviewRange> changedNoteRanges(const std::vector<PitchedNote>& oldNotes,
                                           const std::vector<NoteEdit>& edits) {
    std::vector<ReviewRange> ranges;
    for (const auto& edit : edits) {
        const auto& n = edit.note;
        if (n.fixedTrigger)
            continue;
        const auto old = std::find_if(oldNotes.begin(), oldNotes.end(), [&](const auto& v) { return v.id == n.id; });
        if (edit.action == NoteEdit::change && old != oldNotes.end() && old->pitch == n.pitch &&
            old->startTick == n.startTick && old->endTick == n.endTick && old->enabled == n.enabled)
            continue;
        if ((edit.action == NoteEdit::change || edit.action == NoteEdit::erase) && old != oldNotes.end())
            ranges.emplace_back(old->startTick, old->endTick);
        if (edit.action != NoteEdit::erase)
            ranges.emplace_back(n.startTick, n.endTick);
    }
    return ranges;
}

ChordDecisions analyseChordRanges(const OstfpPreview& project, std::optional<size_t> track,
                                  const std::vector<ReviewRange>& affected, const juce::Array<juce::var>& imported,
                                  bool crop, Tick end) {
    const Tick limit = std::max(project.durationTicks, end);
    ChordDecisions result;
    if (affected.empty())
        return result;
    const auto score = juce::JSON::parse(project.sourceScore.toString());
    const auto chords = score["harmony"]["chords"];
    std::set<Tick> edges{0, limit};
    if (auto* list = chords.getArray())
        for (const auto& chord : *list) {
            const auto range = chordRange(chord);
            edges.insert(range.getStart());
            edges.insert(range.getEnd());
        }
    for (const auto& chord : imported) {
        const auto range = chordRange(chord);
        edges.insert(range.getStart());
        edges.insert(range.getEnd());
    }
    for (auto range : affected) {
        range = range.getIntersectionWith({0, limit});
        if (range.isEmpty())
            continue;
        if (crop) {
            edges.insert(range.getStart());
            edges.insert(range.getEnd());
        }
        const auto bar = std::max<Tick>(1, project.ticksPerBar);
        for (Tick tick = (range.getStart() / bar) * bar; tick <= std::min(limit, range.getEnd() + bar); tick += bar) {
            auto chord = globalEventAt(score, TimelineLane::chord, tick);
            if (!chord.isObject())
                edges.insert(tick);
        }
    }
    for (auto i = edges.begin(), j = std::next(i); j != edges.end(); ++i, ++j) {
        const ReviewRange interval(*i, *j);
        if (*i < 0 || *j > limit ||
            !std::any_of(affected.begin(), affected.end(), [&](auto r) { return r.intersects(interval); }))
            continue;
        const auto old = globalEventAt(score, TimelineLane::chord, *i);
        ChordDecision item{interval, old["label"].toString()};
        if (track && *track < project.pitchedNotes.size())
            item.candidates = rankChords(project.pitchedNotes[*track], interval);
        for (const auto& source : imported)
            if (chordRange(source).contains(*i)) {
                item.sourceChord = source;
                break;
            }
        item.choice = item.sourceChord.isObject() ? 4 : item.candidates.isEmpty() ? 0 : 1;
        result.push_back(std::move(item));
    }
    return result;
}

juce::Result chordDecisionEdits(const juce::var& score, const ChordDecisions& decisions, juce::Array<juce::var>& remove,
                                juce::Array<juce::var>& insert) {
    auto put = [&](juce::var event, ReviewRange range, juce::String id) {
        event = event.clone();
        auto* o = event.getDynamicObject();
        o->setProperty("id", id.isEmpty() ? "chord-" + juce::Uuid().toString() : id);
        o->setProperty("start", range.getStart());
        o->setProperty("duration", range.getLength());
        insert.add(event);
    };
    std::vector<ReviewRange> replaced;
    std::set<juce::String> reused;
    for (const auto& row : decisions) {
        if (!row.choice)
            continue;
        const auto old = globalEventAt(score, TimelineLane::chord, row.range.getStart());
        if (old["label"].toString() != row.current || (old.isObject() && chordRange(old).getEnd() < row.range.getEnd()))
            return juce::Result::fail("The source event changed. Select it again.");
        juce::var chord;
        if (row.choice == 4 && row.sourceChord.isObject()) {
            chord = row.sourceChord.clone();
            auto sameValue = [](const juce::var& a, const juce::var& b) {
                for (const auto* key : {"label", "root", "intervals", "bass", "voicing"})
                    if (juce::JSON::toString(a[key], true) != juce::JSON::toString(b[key], true))
                        return false;
                return true;
            };
            if (old.isObject() && sameValue(old, chord))
                continue;
        } else if (row.choice >= 1 && row.choice <= row.candidates.size()) {
            const auto label = row.candidates[row.choice - 1];
            if (old.isObject() && label == old["label"].toString())
                continue;
            chord = juce::var(new juce::DynamicObject);
            if (auto r = editChordSymbol(chord, label); r.failed())
                return r;
        } else
            return juce::Result::fail("Invalid chord selection.");
        juce::String id;
        if (old.isObject() && Tick(old["start"]) == row.range.getStart()) {
            id = old["id"].toString();
            reused.insert(id);
        }
        put(chord, row.range, id);
        replaced.push_back(row.range);
    }
    std::sort(replaced.begin(), replaced.end(), [](auto a, auto b) { return a.getStart() < b.getStart(); });
    const auto chords = score["harmony"]["chords"];
    if (auto* list = chords.getArray())
        for (const auto& old : *list) {
            const auto range = chordRange(old);
            Tick cursor = range.getStart();
            auto preserve = [&](Tick end) {
                if (end > cursor) {
                    const auto id = old["id"].toString();
                    put(old, {cursor, end}, reused.insert(id).second ? id : juce::String{});
                }
            };
            for (const auto& cut : replaced)
                if (cut.intersects(range)) {
                    preserve(cut.getStart());
                    cursor = std::min(range.getEnd(), cut.getEnd());
                }
            if (cursor != range.getStart()) {
                remove.add(old);
                preserve(range.getEnd());
            }
        }
    return juce::Result::ok();
}
} // namespace ostfstudio

ChordReviewLane::ChordReviewLane() {
  setComponentID("chord-review");
  addAndMakeVisible(intervals_);
  addAndMakeVisible(all_);
  intervals_.onClick = [this] { showMenu(-1); };
  all_.setButtonText(TRANS("Confirm all..."));
  all_.onClick = [this] {
    juce::PopupMenu menu;
    menu.addItem(1, TRANS("Apply all selected chords"));
    menu.addItem(2, TRANS("Keep all current chords"));
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&all_),
        [safe = juce::Component::SafePointer<ChordReviewLane>(this),
         revision = revision_](int id) {
          if (safe && revision == safe->revision_ && id && safe->confirm)
            safe->confirm(true, id == 2);
        });
  };
}
void ChordReviewLane::display(const ostfstudio::ChordDecisions &rows,
                              const ostfstudio::OstfpPreview &song) {
  if (rows_.empty() && !rows.empty()) {
    auto edited = std::find_if(rows.begin(), rows.end(),
                               [](const auto &r) { return r.choice != 0; });
    index_ = edited == rows.end() ? 0 : int(edited - rows.begin());
  }
  rows_ = rows;
  ++revision_;
  index_ = std::clamp(index_, 0, std::max(0, int(rows.size()) - 1));
  names_.clear();
  auto location = [&](juce::int64 tick) {
    return TransportPanel::musicalPosition(
        song, tick,
        ostfstudio::activeMeter(song.meters, double(tick),
                                {0, song.meterBeats, song.beatUnit}));
  };
  for (const auto &row : rows)
    names_.add(location(row.range.getStart()) + " - " +
               location(row.range.getEnd()) + "   " + row.current);
  intervals_.setButtonText(TRANS("Chord review") + " (" +
                           juce::String(int(rows.size())) + ")");
  repaint();
}
juce::String ChordReviewLane::option(const ostfstudio::ChordDecision &row,
                                     int choice) const {
  if (choice == 0)
    return TRANS("Keep") + " " +
           (row.current.isEmpty() ? juce::String("-") : row.current);
  if (choice == 4)
    return TRANS("Source") + " " + row.sourceChord["label"].toString();
  return row.candidates[choice - 1];
}
void ChordReviewLane::showMenu(int index) {
  juce::PopupMenu menu;
  auto options = [&](int i) {
    juce::PopupMenu sub;
    const auto &row = rows_[size_t(i)];
    for (int c = 0; c < 5; ++c) {
      if ((c > 0 && c < 4 && c > row.candidates.size()) ||
          (c == 4 && !row.sourceChord.isObject()))
        continue;
      sub.addItem(1 + i * 6 + c, option(row, c), true, row.choice == c);
    }
    sub.addSeparator();
    sub.addItem(6 + i * 6, TRANS("Confirm interval"));
    return sub;
  };
  if (index >= 0)
    menu = options(index);
  else
    for (int i = 0; i < int(rows_.size()); ++i)
      menu.addSubMenu(names_[i], options(i));
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
      [safe = juce::Component::SafePointer<ChordReviewLane>(this),
       rev = revision_](int id) {
        if (!safe || id <= 0 || safe->revision_ != rev)
          return;
        safe->index_ = (id - 1) / 6;
        const auto c = (id - 1) % 6;
        if (c == 5) {
          if (safe->confirm)
            safe->confirm(false, false);
        } else if (safe->choose)
          safe->choose(c);
      });
}
juce::Rectangle<int> ChordReviewLane::intervalBounds(int i) const {
  if (!timeline || !juce::isPositiveAndBelow(i, int(rows_.size())))
    return {};
  const auto [plot, ticks] = timeline();
  if (ticks.isEmpty() || plot.isEmpty())
    return {};
  const auto &range = rows_[size_t(i)].range;
  if (range.getEnd() <= ticks.getStart() || range.getStart() >= ticks.getEnd())
    return {};
  auto x = [&](double t) {
    return plot.getX() + juce::roundToInt(std::clamp((t - ticks.getStart()) /
                                                         ticks.getLength(),
                                                     0.0, 1.0) *
                                          plot.getWidth());
  };
  const auto start = std::min(x(double(range.getStart())), plot.getRight() - 1);
  return {start, 1, std::max(1, x(double(range.getEnd())) - start),
          std::max(0, getHeight() - 2)};
}
void ChordReviewLane::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff1b2730));
  g.setFont(juce::FontOptions(13.f));
  for (int i = 0; i < int(rows_.size()); ++i) {
    auto box = intervalBounds(i);
    if (box.isEmpty())
      continue;
    const auto &row = rows_[size_t(i)];
    g.setColour(juce::Colour(0xff425d64));
    g.fillRect(box);
    g.setColour(juce::Colour(0xff7ccac6));
    g.fillRect(box.removeFromLeft(1));
    const bool wide = box.getWidth() >= 180;
    const auto header = wide ? box.removeFromTop(box.getHeight() / 2) : box;
    g.setColour(juce::Colours::white);
    const auto current =
        row.current.isEmpty() ? juce::String("-") : row.current;
    const auto title =
        row.choice == 0 ? option(row, 0)
                        : current + "  " + juce::String::charToString(0x2192) +
                              "  " + option(row, row.choice);
    g.drawText(wide ? title : current, header.reduced(3, 0),
               juce::Justification::centredLeft);
    if (!wide)
      continue;
    const int cellWidth = box.getWidth() / (row.candidates.size() + 1);
    for (int c = 1; c <= row.candidates.size(); ++c) {
      auto cell = box.removeFromLeft(cellWidth).reduced(1);
      g.setColour(juce::Colour(row.choice == c ? 0xff267a72 : 0xff2d3e49));
      g.fillRect(cell);
      g.setColour(juce::Colours::white);
      g.drawText(row.candidates[c - 1], cell, juce::Justification::centred);
    }
    g.setColour(juce::Colours::white);
    g.drawText(TRANS("Confirm"), box, juce::Justification::centred);
  }
}
void ChordReviewLane::mouseDown(const juce::MouseEvent &e) {
  for (int i = 0; i < int(rows_.size()); ++i) {
    const auto box = intervalBounds(i);
    if (!box.contains(e.getPosition()))
      continue;
    index_ = i;
    if (box.getWidth() < 181 || e.y < box.getY() + box.getHeight() / 2 ||
        e.mods.isPopupMenu()) {
      showMenu(i);
      return;
    }
    const auto count = rows_[size_t(i)].candidates.size();
    const auto c = (e.x - box.getX() - 1) /
                   std::max(1, (box.getWidth() - 1) / (count + 1));
    if (c >= count) {
      if (confirm)
        confirm(false, false);
    } else if (choose)
      choose(c + 1);
    return;
  }
}
void ChordReviewLane::resized() {
  int gutter = timeline ? timeline().first.getX() : 180;
  auto area =
      getLocalBounds().withWidth(std::clamp(gutter, 0, getWidth())).reduced(2);
  if (area.getWidth() < 150) {
    intervals_.setBounds(area.removeFromTop(area.getHeight() / 2));
    all_.setBounds(area);
  } else {
    all_.setBounds(
        area.removeFromRight(std::min(100, area.getWidth() / 2)).reduced(1));
    intervals_.setBounds(area.reduced(1));
  }
}
void MainComponent::refreshChordReview() {
  chordReview_.setVisible(project_ && !pendingReview_.empty());
  if (project_)
    chordReview_.display(pendingReview_, *project_);
  resized();
  repaint();
}
bool MainComponent::chordReviewBlocksSwitch() {
    if (pendingReview_.empty())
        return false;
    statusLabel_.setText(TRANS("Confirm or keep the pending chords before switching tracks."),
                         juce::dontSendNotification);
    return true;
}

void MainComponent::resolveChordReview(bool all, bool keep) {
    const int chosen = chordReview_.selected();
    if (!project_ || recordTrack_ || !juce::isPositiveAndBelow(chosen, int(pendingReview_.size())))
        return;
    ostfstudio::ChordDecisions rows = all ? pendingReview_ : ostfstudio::ChordDecisions{pendingReview_[size_t(chosen)]};
    if (keep)
        for (auto& row : rows)
            row.choice = 0;
    const auto previous = pendingReview_;
    juce::Array<juce::var> remove, insert;
    const auto score = juce::JSON::parse(project_->sourceScore.toString());
    auto result = ostfstudio::chordDecisionEdits(score, rows, remove, insert);
    juce::MemoryBlock before;
    const bool changed = !insert.isEmpty() || !remove.isEmpty();
    const bool playing = changed && transportActive_;
    if (playing)
        stopTransport();
    if (result.wasOk() && changed) {
        auto end = project_->durationTicks;
        for (const auto& item : insert)
            end = std::max(end, juce::int64(item["start"]) + juce::int64(item["duration"]));
        if (end > project_->durationTicks) {
            std::unique_ptr<ostfstudio::OstfpPreview> extended;
            juce::MemoryBlock ignored;
            result = project_->extendedForImport(end, extended, before);
            if (result.wasOk())
                result = extended->changeGlobalEvents(ostfstudio::TimelineLane::chord, remove, insert, ignored);
            if (result.wasOk())
                *project_ = std::move(*extended);
        } else
            result = project_->changeGlobalEvents(ostfstudio::TimelineLane::chord, remove, insert, before);
    }
    if (result.failed()) {
        statusLabel_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        if (playing)
            startTransport(false);
        return;
    }
    if (all)
        pendingReview_.clear();
    else
        pendingReview_.erase(pendingReview_.begin() + chosen);
    if (changed) {
        rememberScoreEdit(std::move(before));
        auto& revision = noteHistory_.back();
        revision.reviewBefore = previous;
        revision.reviewAfter = pendingReview_;
        revision.reviewTrack = selectedTrack_;
        theory_.reset();
        refreshTheory();
        finishNoteEdit(playing);
    }
    refreshChordReview();
}

void MainComponent::reviewDroppedChords(const ostfstudio::OstfpPreview& source, int index, std::optional<size_t> track,
                                        juce::int64 tick) {
    using Range = ostfstudio::ReviewRange;
    using Tick = juce::int64;
    auto convert = [&](Tick at) -> Tick {
        return std::llround(project_->clock.tickAt(project_->clock.secondsAt(tick) + source.clock.secondsAt(at)));
    };
    const auto score = juce::JSON::parse(source.sourceScore.toString());
    juce::Array<juce::var> imported;
    std::vector<Range> affected;
    if (index >= 0) {
        const auto& notes = source.trackKinds[size_t(index)] == ostfstudio::TrackKind::drum
                                ? source.drumNotes[size_t(index)]
                                : source.pitchedNotes[size_t(index)];
        for (const auto& n : notes)
            affected.emplace_back(convert(n.startTick), convert(n.endTick));
        for (const auto& c : source.audio)
            if (c.track == size_t(index))
                affected.emplace_back(convert(c.startTick), convert(c.startTick + c.durationTicks));
        const auto clips = score["tracks"][index]["clips"];
        if (const auto* list = clips.getArray())
            for (const auto& c : *list)
                if (bool(c.getProperty("enabled", true))) {
                    const Tick start = c["start"], duration = c["duration"];
                    if (duration > 0)
                        affected.emplace_back(convert(start), convert(start + duration));
                }
    }
    if (const auto* list = score["harmony"]["chords"].getArray())
        for (const auto& c : *list) {
            const Tick start = c["start"];
            const Range range(convert(start), convert(start + Tick(c["duration"])));
            if (range.isEmpty())
                continue;
            auto item = c.clone();
            item.getDynamicObject()->setProperty("start", range.getStart());
            item.getDynamicObject()->setProperty("duration", range.getLength());
            imported.add(item);
            if (index < 0)
                affected.push_back(range);
        }
    if (affected.empty() && index >= 0)
        affected.emplace_back(tick, tick + 1);
    std::sort(affected.begin(), affected.end(), [](auto a, auto b) { return a.getStart() < b.getStart(); });
    std::vector<Range> spans;
    for (auto r : affected) {
        if (index >= 0)
            r = r.getIntersectionWith({0, project_->durationTicks});
        if (r.isEmpty())
            continue;
        if (!spans.empty() && r.getStart() <= spans.back().getEnd())
            spans.back() = spans.back().getUnionWith(r);
        else
            spans.push_back(r);
    }
    pendingReview_ = ostfstudio::analyseChordRanges(*project_, track, spans, imported, true,
                                                    spans.empty() ? 0 : spans.back().getEnd());
    if (pendingReview_.empty())
        statusLabel_.setText(TRANS("No chord regions to place."), juce::dontSendNotification);
    refreshChordReview();
}
