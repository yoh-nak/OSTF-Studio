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

ChordAnalysis::ChordAnalysis(const OstfpPreview& song, std::optional<size_t> track)
    : project(song), score(juce::JSON::parse(song.sourceScore.toString())), editedTrack(track) {
    const auto tracks = score["tracks"];
    for (size_t i = 0; i < song.sourceTrackCount(); ++i) {
        if (song.trackKinds[i] != TrackKind::pitched)
            continue;
        std::set<juce::String> disabledClips;
        const auto clips = i < size_t(tracks.size()) ? tracks[int(i)]["clips"] : juce::var{};
        if (const auto* list = clips.getArray())
            for (const auto& clip : *list)
                if (!bool(clip.getProperty("enabled", true)))
                    disabledClips.insert(clip["id"].toString());
        for (const auto& n : song.pitchedNotes[i])
            if (n.enabled && !n.fixedTrigger && n.pitch >= 0 && n.pitch <= 127 &&
                n.startTick < n.endTick && !disabledClips.contains(n.clipId))
                notes.push_back({n.startTick, n.endTick, n.endTick, n.pitch, i});
    }
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    // The prefix maximum preserves sustained notes when seeking to a later chord.
    for (size_t i = 1; i < notes.size(); ++i)
        notes[i].latestEnd = std::max(notes[i].end, notes[i - 1].latestEnd);
}

juce::StringArray ChordAnalysis::rank(ReviewRange range) const {
    ChordDecision row{range};
    updateCandidates(row);
    return row.candidates;
}

void ChordAnalysis::updateCandidates(ChordDecision& row) const {
    const auto range = row.range;
    row.candidates.clear();
    row.trackFit.fill(-1);
    std::array<double, 12> weights{}, partWeights{};
    int bass = 128;
    double total = 0;
    const auto first = std::upper_bound(notes.begin(), notes.end(), range.getStart(),
                                        [](Tick tick, const Note& n) { return tick < n.latestEnd; });
    for (auto it = first; it != notes.end() && it->start < range.getEnd(); ++it) {
        const auto& note = *it;
        const auto duration = range.getIntersectionWith({note.start, note.end}).getLength();
        if (duration <= 0)
            continue;
        weights[note.pitch % 12] += duration;
        if (editedTrack == note.track) partWeights[note.pitch % 12] += duration;
        bass = std::min(bass, note.pitch);
        total += duration;
    }
    if (total == 0)
        return;
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
        int mask;
    };
    std::vector<Match> matches;
    for (int root = 0; root < 12; ++root)
        for (const auto& quality : qualities) {
            double covered = 0;
            int absent = 0;
            int mask = 0;
            for (auto interval : quality.intervals) {
                mask |= 1 << ((root + interval) % 12);
                const auto weight = weights[(root + interval) % 12];
                covered += weight;
                if (weight == 0)
                    ++absent;
            }
            const double fit =
                covered / total - .13 * absent + .08 * weights[root] / total + (bass % 12 == root ? .04 : 0);
            matches.push_back({fit, juce::String(names[root]) + quality.name, mask});
        }
    std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.fit > b.fit; });
    auto fit = [&](int mask) {
        TheoryGuide::Fit result;
        if (mask) for (int pitch = 0; pitch < 12; ++pitch)
            result.add(TheoryGuide::contains(mask, pitch), static_cast<Tick>(partWeights[pitch]));
        return result.percent();
    };
    row.trackFit[0] = fit(TheoryGuide::chordToneMask(globalEventAt(score, TimelineLane::chord, range.getStart())));
    for (int i = 0; i < 3; ++i) {
        row.candidates.add(matches[i].name);
        row.trackFit[i + 1] = fit(matches[i].mask);
    }
    row.trackFit[4] = fit(TheoryGuide::chordToneMask(row.sourceChord));
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

void rememberReviewedNotes(ChordDecisions& rows, const juce::MemoryBlock& before, std::size_t track,
                           const std::vector<NoteEdit>& edits, const juce::StringArray& resultIds,
                           const std::vector<PitchedNote>& after) {
    struct BeforeNote { ReviewNoteOrigin origin; ReviewRange range; };
    std::map<juce::String, BeforeNote> old;
    std::set<juce::String> existingClips;
    const auto score = juce::JSON::parse(before.toString());
    const auto clips = score["tracks"][int(track)]["clips"];
    if (auto* list = clips.getArray()) for (const auto& clip : *list) {
        const auto owner = clip["id"].toString();
        existingClips.insert(owner);
        const auto notes = clip["notes"];
        if (auto* events = notes.getArray()) for (int i = 0; i < events->size(); ++i) {
            const auto& event = events->getReference(i);
            const Tick start = Tick(clip["start"]) + Tick(event["start"]);
            old.emplace(event["id"].toString(), BeforeNote{{owner, event, i}, {start, start + Tick(event["duration"])}});
        }
    }
    ReviewNoteOrigins earliest;
    std::set<juce::String> created;
    for (const auto& row : rows) for (const auto& [id, origin] : row.noteOrigins) {
        earliest.emplace(id, origin);
        created.insert(origin.createdClips.begin(), origin.createdClips.end());
    }
    std::map<juce::String, const PitchedNote*> current;
    for (const auto& note : after) current.emplace(note.id, &note);
    int output = 0;
    for (const auto& edit : edits) {
        const auto id = edit.action == NoteEdit::erase ? edit.note.id : resultIds[output++];
        if (edit.note.fixedTrigger) continue;
        const auto original = old.find(id);
        const auto now = current.find(id);
        const auto from = original == old.end() ? ReviewRange{} : original->second.range;
        const auto to = now == current.end() ? ReviewRange{} : ReviewRange(now->second->startTick, now->second->endTick);
        auto origin = original == old.end() ? ReviewNoteOrigin{} : original->second.origin;
        if (auto it = earliest.find(id); it != earliest.end()) origin = it->second;
        if (now != current.end() && (!existingClips.contains(now->second->clipId) || created.contains(now->second->clipId)))
            origin.createdClips.addIfNotAlreadyThere(now->second->clipId);
        for (auto& row : rows)
            if (row.noteOrigins.contains(id) || (!from.isEmpty() && row.range.intersects(from)) || (!to.isEmpty() && row.range.intersects(to)))
                row.noteOrigins[id] = origin;
    }
}

ChordDecisions ChordAnalysis::review(const std::vector<ReviewRange>& affected,
                                     const juce::Array<juce::var>& imported, bool crop, Tick end) const {
    const Tick limit = std::max(project.durationTicks, end);
    ChordDecisions result;
    if (affected.empty())
        return result;
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
        if (*i < 0 || *j > limit)
            continue;
        if (std::none_of(affected.begin(), affected.end(), [&](auto r) { return r.intersects(interval); }))
            continue;
        const auto old = globalEventAt(score, TimelineLane::chord, *i);
        ChordDecision item{interval, old["label"].toString()};
        for (const auto& source : imported)
            if (chordRange(source).contains(*i)) {
                item.sourceChord = source;
                break;
            }
        updateCandidates(item);
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

namespace {
const juce::Colour reviewBase(0xff16232d), reviewSurface(0xff253841), reviewText(0xfff1f6f6),
                   reviewMuted(0xffa4b8c1), reviewAccent(0xff8ce0d0), confirmGold(0xffffc36a);
const auto noCandidate = juce::String::charToString(0x2014);

juce::Rectangle<int> candidateArea(juce::Rectangle<int> box) {
    return box.reduced(4, 3).withTrimmedTop(box.getHeight() / 2 - 2);
}
juce::Rectangle<int> candidateCell(juce::Rectangle<int> area, int index, int count) {
    const int left = area.getX() + area.getWidth() * index / count;
    return area.withLeft(left).withRight(area.getX() + area.getWidth() * (index + 1) / count).reduced(1, 0);
}
bool fitsChoices(juce::Rectangle<int> box, int count) {
    return count > 0 && candidateArea(box).getWidth() >= count * 84 && box.getHeight() >= 36;
}
juce::Rectangle<int> actionBounds(juce::Rectangle<int> box) {
    auto header = box.removeFromTop(box.getHeight() / 2).reduced(std::min(2, box.getWidth() / 8), 3);
    const int width = header.getWidth() < 75 ? 22 : std::min(92, header.getWidth() / 2);
    return header.removeFromRight(std::min(width, header.getWidth()));
}
} // namespace

ChordReviewLane::ChordReviewLane() {
    setComponentID("chord-review");
    setTitle(TRANS("Chord review"));
    using Action = ostfstudio::ChordReviewAction;
    auto batch = [this](juce::TextButton& button, const char* id, juce::String label, juce::String tip, Action action) {
        addAndMakeVisible(button);
        button.setComponentID(id);
        button.setButtonText(label);
        button.setTooltip(tip);
        button.setEnabled(false);
        button.onClick = [this, action] { if (decide && !rows_.empty()) decide(true, action); };
    };
    batch(confirmAll_, "chord-confirm-all", TRANS("Confirm all"), TRANS("Apply all selected chords"), Action::apply);
    batch(keepAll_, "chord-keep-all", TRANS("Keep all"), TRANS("Keep all current chords"), Action::keep);
    batch(cancelAll_, "chord-cancel-all", TRANS("Cancel all"), TRANS("Undo note edits for all pending intervals"), Action::cancel);
    confirmAll_.setColour(juce::TextButton::buttonColourId, confirmGold);
    confirmAll_.setColour(juce::TextButton::textColourOffId, reviewBase);
}

juce::PopupMenu ChordReviewLane::decisionMenu(int index) {
    juce::PopupMenu menu;
    if (!juce::isPositiveAndBelow(index, int(rows_.size()))) return menu;
    using Action = ostfstudio::ChordReviewAction;
    auto action = [&](Action choice) {
        return [safe = juce::Component::SafePointer<ChordReviewLane>(this), revision = revision_, index, choice] {
            if (safe && safe->isVisible() && safe->isEnabled() && safe->revision_ == revision && safe->decide) {
                safe->index_ = index;
                safe->decide(false, choice);
            }
        };
    };
    menu.addSectionHeader(names_[index]);
    menu.addItem(TRANS("Confirm"), action(Action::apply));
    menu.addItem(TRANS("Keep"), action(Action::keep));
    menu.addItem(TRANS("Cancel chord"), action(Action::cancel));
    return menu;
}

void ChordReviewLane::display(const ostfstudio::ChordDecisions& rows, const ostfstudio::OstfpPreview& song) {
    const auto oldRange = rows_.empty() ? ostfstudio::ReviewRange{} : rows_[size_t(index_)].range;
    rows_ = rows;
    ++revision_;
    const auto selected = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.range == oldRange; });
    index_ = selected == rows.end() ? std::clamp(index_, 0, std::max(0, int(rows.size()) - 1))
                                    : int(selected - rows.begin());
    names_.clear();
    auto location = [&](juce::int64 tick) {
        return TransportPanel::musicalPosition(song, tick, ostfstudio::activeMeter(song.meters, double(tick),
                                              {0, song.meterBeats, song.beatUnit}));
    };
    for (const auto& row : rows)
        names_.add(location(row.range.getStart()) + " - " + location(row.range.getEnd()) + "   " + row.current);

    for (auto* button : {&confirmAll_, &keepAll_, &cancelAll_}) button->setEnabled(!rows_.empty());
    resized();
    repaint();
}

juce::String ChordReviewLane::option(const ostfstudio::ChordDecision& row, int choice) const {
    auto label = choice == 0 ? TRANS("Keep") + " " + (row.current.isEmpty() ? noCandidate : row.current)
               : choice == 4 ? TRANS("Source") + " " + row.sourceChord["label"].toString() : row.candidates[choice - 1];
    const int fit = row.trackFit[size_t(choice)];
    return label + "  " + (fit < 0 ? noCandidate : juce::String(fit) + "%");
}

void ChordReviewLane::showMenu(int index) {
    const auto& row = rows_[size_t(index)];
    juce::PopupMenu menu;
    menu.addSectionHeader(names_[index]);
    for (int c = 0; c <= row.candidates.size(); ++c)
        menu.addItem(c + 1, option(row, c), true, row.choice == c);
    if (row.sourceChord.isObject()) menu.addItem(5, option(row, 4), true, row.choice == 4);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe = juce::Component::SafePointer<ChordReviewLane>(this), revision = revision_, index](int id) {
            if (!safe || !safe->isVisible() || !safe->isEnabled() || id <= 0 || safe->revision_ != revision) return;
            safe->index_ = index;
            if (safe->choose) safe->choose(id - 1);
        });
}

juce::Rectangle<int> ChordReviewLane::boundsFor(ostfstudio::ReviewRange range) const {
    if (!timeline) return {};
    const auto [plot, ticks] = timeline();
    if (ticks.isEmpty() || plot.isEmpty() || range.getEnd() <= ticks.getStart() || range.getStart() >= ticks.getEnd()) return {};
    auto x = [&](double tick) {
        return plot.getX() + juce::roundToInt(std::clamp((tick - ticks.getStart()) / ticks.getLength(), 0., 1.) * plot.getWidth());
    };
    const int start = std::min(x(double(range.getStart())), plot.getRight() - 1);
    return {start, 1, std::max(1, x(double(range.getEnd())) - start), std::max(0, getHeight() - 2)};
}
juce::Rectangle<int> ChordReviewLane::intervalBounds(int i) const {
    return juce::isPositiveAndBelow(i, int(rows_.size())) ? boundsFor(rows_[size_t(i)].range) : juce::Rectangle<int>{};
}

void ChordReviewLane::paint(juce::Graphics& g) {
    g.fillAll(reviewBase);
    g.setColour(reviewSurface.brighter(.25f));
    g.drawHorizontalLine(0, 0.f, float(getWidth()));
    g.setColour(reviewAccent);
    g.fillRect(0, 1, 3, std::max(0, getHeight() - 2));
    g.setFont(juce::FontOptions(14.f, juce::Font::bold));
    auto title = titleBounds_;
    if (title.getWidth() >= 250) {
        auto fitLabel = title.removeFromRight(145);
        g.setColour(reviewMuted);
        g.setFont(juce::FontOptions(12.f));
        g.drawText(TRANS("Selected track fit"), fitLabel, juce::Justification::centredRight);
    }
    g.setColour(reviewAccent);
    g.setFont(juce::FontOptions(14.f, juce::Font::bold));
    g.drawText(TRANS("Chord review") + "   " + juce::String(int(rows_.size())),
               title, juce::Justification::centredLeft);
    auto heading = [](juce::Rectangle<int> box) { return box.removeFromTop(box.getHeight() / 2).reduced(5, 1); };
    for (int i = 0; i < int(rows_.size()); ++i) {
        auto box = intervalBounds(i);
        if (box.isEmpty()) continue;
        const auto& row = rows_[size_t(i)];
        const bool selected = i == index_;
        g.setColour(reviewBase); g.fillRect(box);
        g.setColour(selected ? juce::Colour(0xff334854) : reviewSurface); g.fillRect(box.withTrimmedRight(1));
        g.setColour(selected ? confirmGold : reviewAccent);
        g.fillRect(box.withHeight(2));
        g.setColour(reviewText);
        const auto label = row.choice == 0 ? option(row, 0)
            : row.current + "  " + juce::String::charToString(0x2192) + "  " + option(row, row.choice);
        g.setFont(juce::FontOptions(13.f, juce::Font::bold));
        g.drawText(label, heading(box).withRight(actionBounds(box).getX()), juce::Justification::centredLeft);
        const auto area = candidateArea(box);
        const int count = row.candidates.size();
        if (!fitsChoices(box, count)) {
            g.setColour(reviewAccent);
            g.drawText(TRANS("Candidates") + " " + juce::String::charToString(0x25be), area, juce::Justification::centredLeft);
            continue;
        }
        for (int c = 0; c < count; ++c) {
            const auto chip = candidateCell(area, c, count);
            g.setColour(row.choice == c + 1 ? reviewAccent : reviewBase);
            g.fillRoundedRectangle(chip.toFloat(), 3.f);
            g.setColour(row.choice == c + 1 ? reviewBase : reviewText);
            g.drawText(option(row, c + 1), chip, juce::Justification::centred);
        }
    }
}

void ChordReviewLane::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < int(rows_.size()); ++i) {
        const auto box = intervalBounds(i);
        if (!box.contains(e.getPosition())) continue;
        index_ = i;
        repaint();
        const int count = rows_[size_t(i)].candidates.size();
        if (!e.mods.isPopupMenu() && fitsChoices(box, count)) {
            for (int c = 0; c < count; ++c) {
                if (candidateCell(candidateArea(box), c, count).contains(e.getPosition())) {
                    if (choose) choose(c + 1);
                    return;
                }
            }
        }
        showMenu(i);
        return;
    }
}

void ChordReviewLane::resized() {
    const int gutter = timeline ? timeline().first.getX() : 320;
    auto controls = getLocalBounds().withWidth(std::clamp(gutter, 0, getWidth())).reduced(9, 3);
    titleBounds_ = controls.removeFromTop(std::min(22, controls.getHeight() / 2));
    const int width = controls.getWidth() / 3;
    for (auto* button : {&confirmAll_, &keepAll_, &cancelAll_})
        button->setBounds(controls.removeFromLeft(width).reduced(2, 1));
    int visible = 0;
    auto place = [&](juce::Rectangle<int> box, int index, const juce::String& name) {
        if (box.isEmpty()) return;
        const auto bounds = actionBounds(box);
        if (bounds.isEmpty()) return;
        if (visible == decisions_.size())
            addChildComponent(decisions_.add(new juce::TextButton()));
        auto* button = decisions_[visible++];
        button->setBounds(bounds);
        button->setVisible(true);
        button->setComponentID("chord-decision-" + juce::String(index));
        button->setTitle(name + "  " + TRANS("Decide chord"));
        button->setColour(juce::TextButton::buttonColourId, confirmGold);
        button->setColour(juce::TextButton::textColourOffId, reviewBase);
        button->setTooltip(TRANS("Apply a chord, undo note edits, or keep the existing chord"));
        const auto caret = juce::String::charToString(0x25be);
        button->setButtonText(bounds.getWidth() < 55 ? caret : TRANS("Decide chord") + " " + caret);
        button->onClick = [this, index, button] {
            index_ = index;
            repaint();
            decisionMenu(index).showMenuAsync(juce::PopupMenu::Options().withTargetComponent(button));
        };
    };
    for (int i = 0; i < int(rows_.size()); ++i) place(intervalBounds(i), i, names_[i]);
    decisions_.removeLast(decisions_.size() - visible);
}

void MainComponent::updateChordReviewLane() {
    const bool materialReview = !pendingReview_.empty() && pendingReview_.front().fromMaterial;
    const bool visible = !pendingReview_.empty() && workspaceMode_ == WorkspaceMode::arrangement && project_ && !mixerSelected_ &&
        (materialReview || (pianoGrid_.isVisible() && pianoGrid_.editable && pitchedSelection()));
    if (visible || (project_ && chordReview_.isVisible() && pendingReview_.empty()))
        chordReview_.display(pendingReview_, *project_);
    chordReview_.setVisible(visible);
    chordReview_.setEnabled(!recordTrack_);
}

void MainComponent::refreshChordReview() {
    updateChordReviewLane();
    resized();
    repaint();
}
bool MainComponent::chordReviewBlocksSwitch() {
    if (pendingReview_.empty())
        return false;
    statusLabel_.setText(TRANS("Resolve the pending chord proposals before switching tracks."),
                         juce::dontSendNotification);
    return true;
}

void MainComponent::resolveChordReview(bool all, ostfstudio::ChordReviewAction action) {
    using Action = ostfstudio::ChordReviewAction;
    const int chosen = chordReview_.selected();
    if (!project_ || recordTrack_ || !juce::isPositiveAndBelow(chosen, int(pendingReview_.size())))
        return;
    ostfstudio::ChordDecisions rows = all ? pendingReview_ : ostfstudio::ChordDecisions{pendingReview_[size_t(chosen)]};
    const auto previous = pendingReview_;
    juce::Array<juce::var> remove, insert;
    ostfstudio::ReviewNoteOrigins undoNotes;
    auto result = juce::Result::ok();
    if (action == Action::apply)
        result = ostfstudio::chordDecisionEdits(juce::JSON::parse(project_->sourceScore.toString()), rows, remove, insert);
    else if (action == Action::cancel)
        for (const auto& row : rows) undoNotes.insert(row.noteOrigins.begin(), row.noteOrigins.end());
    juce::MemoryBlock before;
    const bool changed = !undoNotes.empty() || !insert.isEmpty() || !remove.isEmpty();
    const bool playing = changed && transportActive_;
    if (playing)
        stopTransport();
    if (!undoNotes.empty() && selectedTrack_)
        result = project_->restoreReviewedNotes(*selectedTrack_, undoNotes, before);
    else if (result.wasOk() && changed) {
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
    if (!undoNotes.empty()) {
        const ostfstudio::ChordAnalysis analysis(*project_, selectedTrack_);
        std::erase_if(pendingReview_, [&](auto& row) {
            const auto was = row.noteOrigins.size();
            for (const auto& [id, origin] : undoNotes) row.noteOrigins.erase(id);
            if (row.noteOrigins.size() == was) return false;
            if (row.noteOrigins.empty() && !row.fromMaterial) return true;
            analysis.updateCandidates(row);
            if (row.choice != 4) row.choice = row.candidates.isEmpty() ? 0 : 1;
            return false;
        });
        inspectedNote_.reset();
        pianoGrid_.selection.clear();
    }
    rememberScoreEdit(changed ? std::move(before) : project_->sourceScore,
                      action == Action::cancel ? "Cancel chord edit" : action == Action::keep ? "Keep chords" : "Apply chord choices");
    auto& revision = noteHistory_.back();
    revision.reviewBefore = previous;
    revision.reviewAfter = pendingReview_;
    revision.reviewTrack = selectedTrack_;
    if (changed) {
        theory_.reset();
        refreshTheory();
        finishNoteEdit(playing);
    }
    refreshChordReview();
}

void MainComponent::reviewDroppedChords(const ostfstudio::OstfpPreview& source, int index, juce::int64 tick) {
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
    pendingReview_ = ostfstudio::ChordAnalysis(*project_, selectedTrack_).review(spans, imported, true,
                                                                spans.empty() ? 0 : spans.back().getEnd());
    for (auto& row : pendingReview_) row.fromMaterial = true;
    if (pendingReview_.empty())
        statusLabel_.setText(TRANS("No chord regions to place."), juce::dontSendNotification);
    refreshChordReview();
}
