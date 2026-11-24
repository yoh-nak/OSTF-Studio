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
    addAndMakeVisible(ranges_);
    ranges_.onChange = [this] {
        if (navigate)
            navigate();
    };
    for (int i = 0; i < 5; ++i) {
        auto& b = choices_[i];
        addAndMakeVisible(b);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff236861));
        b.onClick = [this, i] {
            if (choose)
                choose(i);
        };
    }
    addAndMakeVisible(accept_);
    addAndMakeVisible(all_);
    accept_.setButtonText(TRANS("Confirm interval"));
    all_.setButtonText(TRANS("Confirm all..."));
    accept_.onClick = [this] {
        if (confirm)
            confirm(false, false);
    };
    all_.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, TRANS("Apply all selected chords"));
        menu.addItem(2, TRANS("Keep all current chords"));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&all_),
                           [safe = juce::Component::SafePointer<ChordReviewLane>(this)](int id) {
                               if (safe && id && safe->confirm)
                                   safe->confirm(true, id == 2);
                           });
    };
}
void ChordReviewLane::display(const ostfstudio::ChordDecisions& rows, const ostfstudio::OstfpPreview& song) {
    const int previous = selected();
    ranges_.clear(juce::dontSendNotification);
    if (rows.empty())
        return;
    auto location = [&](juce::int64 t) {
        const auto meter = ostfstudio::activeMeter(song.meters, double(t), {0,song.meterBeats,song.beatUnit});
        return TransportPanel::musicalPosition(song,t,meter);
    };
    int id = 0;
    for (const auto& row : rows)
        ranges_.addItem(
            TRANS("Chord review") + "  " + location(row.range.getStart()) + " - " + location(row.range.getEnd()), ++id);
    const int index = juce::jlimit(0, int(rows.size()) - 1, previous);
    ranges_.setSelectedId(index + 1, juce::dontSendNotification);
    const auto& row = rows[size_t(index)];
    choices_[0].setButtonText(TRANS("Keep") + " " + (row.current.isEmpty() ? juce::String("-") : row.current));
    for (int i = 1; i <= 3; ++i) {
        choices_[i].setVisible(i <= row.candidates.size());
        choices_[i].setButtonText(row.candidates[i - 1]);
    }
    choices_[4].setVisible(row.sourceChord.isObject());
    choices_[4].setButtonText(TRANS("Source") + " " + row.sourceChord["label"].toString());
    for (int i = 0; i < 5; ++i)
        choices_[i].setToggleState(row.choice == i, juce::dontSendNotification);
    resized();
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

void ChordReviewLane::resized() {
    auto area = getLocalBounds().reduced(3);
    const bool wrap = area.getHeight() > 40;
    auto header = wrap ? area.removeFromTop(area.getHeight() / 2) : area;
    all_.setBounds(header.removeFromRight(94).reduced(1));
    accept_.setBounds(header.removeFromRight(96).reduced(1));
    ranges_.setBounds(
        header.removeFromLeft(wrap ? header.getWidth() : std::min(255, header.getWidth() / 3)).reduced(1));
    if (!wrap)
        area = header;
    int count = 0;
    for (auto& b : choices_)
        if (b.isVisible())
            ++count;
    for (auto& b : choices_)
        if (b.isVisible())
            b.setBounds(area.removeFromLeft(area.getWidth() / count--).reduced(1));
}
