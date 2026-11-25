#pragma once
#include "ui/TimelinePan.h"

#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <limits>
#include <map>
#include <set>

class PianoNoteGrid final : public juce::Component, public juce::SettableTooltipClient {
    using Note = ostfstudio::PitchedNote;
    using Edit = ostfstudio::NoteEdit;
    enum Gesture { idle, marquee, draw, repeat, move, trimStart, trimEnd, velocity };
public:
    TimelinePan viewportPan;
    std::function<juce::Range<double>()> tickRange;
    std::function<void(const std::vector<Edit>&)> commit;
    std::function<void(const std::vector<Edit>&)> previewEdit;
    std::function<void(const Note&)> inspectNote;
    std::function<void(const Note&)> editLyrics;
    std::function<void(bool)> undoRedo;
    std::function<void()> selectionChanged, togglePencil;
    std::function<juce::String(juce::int64, const Note&)> theoryHint;
    std::vector<Note> notes;
    juce::StringArray selection;
    juce::Range<juce::int64> editSection;
    bool acceptsOnset(juce::int64 t) const { return editSection.isEmpty() || editSection.contains(t); }
    double grid = 1;
    juce::int64 endTick = 1, quarter = 1;
    int low = 52, high = 70;
    std::vector<int> kitPitches; // Ascending; rows are displayed highest first.
    int rowCount() const {
        return percussion ? int(kitPitches.size()) : high - low + 1;
    }
    int rowPitch(int row) const {
        return percussion ? kitPitches.at(size_t(rowCount() - 1 - row)) : high - row;
    }
    int pitchRow(int p) const {
        if (!percussion)
            return high - p;
        auto it = std::lower_bound(kitPitches.begin(), kitPitches.end(), p);
        return it == kitPitches.end() || *it != p ? -1 : int(kitPitches.end() - it) - 1;
    }
    int rowAt(int y) const {
        return std::clamp(int(std::floor(double(y) * rowCount() / std::max(1, getHeight()))), 0,
                          std::max(0, rowCount() - 1));
    }
    bool editable = false, absoluteSnap = false, pencil = false, percussion = false;
    const std::vector<Edit>& pendingEdits() const { return pending; }

    PianoNoteGrid() { setWantsKeyboardFocus(true); setComponentID("piano-note-grid"); }

    void quantizeSelection(int strength) {
        if (grid <= 1 || strength <= 0) return;
        editSelection([&](Note& note) {
            const auto nearest = snapTick(double(note.startTick), grid);
            const auto delta = std::clamp<juce::int64>(std::llround((nearest - note.startTick) * strength / 100.0),
                                                      -note.startTick, endTick - note.endTick);
            note.startTick += delta;
            note.endTick += delta;
        });
    }
    void shapeVelocity(int first, int last) {
        juce::int64 from = endTick, to = 0;
        for (const auto& note : notes) if (selection.contains(note.id)) {
            from = std::min(from, note.startTick);
            to = std::max(to, note.startTick);
        }
        editSelection([&](Note& note) {
            const double position = to > from ? double(note.startTick - from) / (to - from) : 0.0;
            note.velocity = std::clamp(juce::roundToInt(first + (last - first) * position), 1, 127);
        });
    }

    void splitIntoTriplets() {
        if (!editable || !commit) return;
        std::vector<Edit> changes;
        for (const auto& source : notes) {
            const auto length = source.endTick - source.startTick;
            if (!selection.contains(source.id) || length < 3) continue;
            // Round both boundaries against the original span so the last note ends exactly on time.
            const auto third = length / 3 + (length % 3 == 2 ? 1 : 0);
            const juce::int64 cuts[] {source.startTick, source.startTick + third, source.endTick - third, source.endTick};
            for (const int part : {1, 2, 0}) {
                auto note = source;
                note.startTick = cuts[part];
                note.endTick = cuts[part + 1];
                changes.push_back({part == 0 ? Edit::change : Edit::duplicate, note});
            }
        }
        if (!changes.empty()) commit(changes);
    }

    enum class Stroke { lowFirst, highFirst, alternate };
    void strumSelection(Stroke direction, int spreadPercent) {
        if (!editable || percussion || !commit || spreadPercent <= 0 || selection.size() < 2) return;
        std::map<juce::int64, std::vector<Note>> chords;
        for (const auto& note : notes) if (selection.contains(note.id)) chords[note.startTick].push_back(note);
        std::vector<Edit> changes;
        bool descending = direction == Stroke::highFirst;
        for (auto it = chords.begin(); it != chords.end(); ++it) {
            auto& chord = it->second;
            std::sort(chord.begin(), chord.end(), [descending](const Note& a, const Note& b) {
                return descending ? a.pitch > b.pitch : a.pitch < b.pitch;
            });
            if (chord.front().pitch == chord.back().pitch) continue;
            int intervals = 0;
            auto spread = juce::int64(std::llround(quarter * std::clamp(spreadPercent, 0, 100) / 100.0));
            for (size_t i = 0; i < chord.size(); ++i) {
                if (i && chord[i].pitch != chord[i - 1].pitch) ++intervals;
                spread = std::min(spread, chord[i].endTick - it->first - 1);
            }
            if (auto next = std::next(it); next != chords.end())
                spread = std::min(spread, next->first - it->first - 1);
            int rank = 0;
            for (size_t i = 1; i < chord.size(); ++i) {
                if (chord[i].pitch != chord[i - 1].pitch) ++rank;
                auto note = chord[i];
                note.startTick += std::llround(double(spread) * rank / intervals);
                if (note.startTick != it->first) changes.push_back({Edit::change, note});
            }
            if (direction == Stroke::alternate) descending = !descending;
        }
        if (!changes.empty()) {
            auto selected = selection;
            commit(changes);
            selection = selected;
            if (selectionChanged) selectionChanged();
            repaint();
        }
    }

    juce::String getTooltip() override {
        if (theoryHint && tickRange && getWidth() > 0) {
            const auto point = getMouseXYRelative().toFloat();
            if (const auto* note = hitNote(point)) {
                const auto range = tickRange();
                const auto tick = static_cast<juce::int64>(std::floor(range.getStart() + point.x * range.getLength() / getWidth()));
                if (auto hint = theoryHint(tick, *note); hint.isNotEmpty()) return hint;
            }
        }
        return juce::SettableTooltipClient::getTooltip();
    }

    void setPitchRows(int count) {
        count = std::clamp(count, 1, 128);
        low = std::clamp((low + high + 1 - count) / 2, 0, 128 - count);
        high = low + count - 1;
    }
    void fitPitchRange() {
        const auto drum = percussion;
        low = drum ? 33 : 52;
        high = drum ? 54 : 70;
        if (!notes.empty()) {
            auto limits = std::minmax_element(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.pitch < b.pitch; });
            low = std::max(0, limits.first->pitch - 3);
            high = std::min(127, limits.second->pitch + 3);
        }
    }
    void resetRange(bool drum) {
        percussion = drum;
        fitPitchRange();
        selection.clear();
        cancel();
    }
    void cancel() {
        gesture = idle; pending.clear(); occupiedOnsets.clear(); lastPreview.reset();
        if (previewEdit) previewEdit(pending);
        repaint();
    }

    float noteY(int pitch) const {
        return float(pitchRow(pitch)) * getHeight() / std::max(1, rowCount());
    }
    juce::Rectangle<float> box(const Note& n) const {
        const auto range = tickRange();
        const auto scale = getWidth() / std::max(1.0, range.getLength());
        return {float((n.startTick - range.getStart()) * scale), noteY(n.pitch),
                std::max(2.f, float((n.endTick - n.startTick) * scale)),
                std::max(1.f, float(getHeight()) / std::max(1, rowCount()) - 1)};
    }
    void paint(juce::Graphics& g) override {
        if (!editable || !tickRange) return;
        if (!editSection.isEmpty()) {
            const auto range = tickRange();
            const auto x = [&](juce::int64 tick) {
                return juce::roundToInt(std::clamp((tick-range.getStart())/std::max(1.,range.getLength()),0.,1.)*getWidth());
            };
            const int left=x(editSection.getStart()), right=x(editSection.getEnd());
            g.setColour(juce::Colour(0x880a1319));
            g.fillRect(0,0,left,getHeight()); g.fillRect(right,0,getWidth()-right,getHeight());
            g.setColour(juce::Colour(0xff84d4c9));
            if (left>0 && left<getWidth()) g.drawVerticalLine(left,0.f,float(getHeight()));
            if (right>0 && right<getWidth()) g.drawVerticalLine(right,0.f,float(getHeight()));
        }
        g.setColour(juce::Colour(0xffffc57e));
        for (const auto& n : notes) if (selection.contains(n.id)) {
            const auto r = box(n);
            g.setColour(juce::Colour(0x66ffd771)); g.fillRect(r);
            g.setColour(juce::Colour(0xff18212a)); g.drawRect(r, 2.f);
            g.setColour(juce::Colour(0xffffe18a));
            if (r.getWidth() >= 5 && r.getHeight() >= 5) g.drawRect(r.reduced(1), 1.f);
            g.fillRect(r.withWidth(std::min(4.f, r.getWidth())));
        }
        for (const auto& e : pending) {
            g.setColour(juce::Colour(0xffffc57e));
            g.drawRoundedRectangle(box(e.note).reduced(.5f), 2, 1.5f);
        }
        if (gesture != velocity && !pending.empty() && getDescription().isNotEmpty()) {
            const juce::Font font(juce::FontOptions(13.f));
            g.setFont(font);
            const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, getDescription())) + 18;
            auto tag = juce::Rectangle<int>(current.x + 10, current.y - 27, std::min(getWidth(), w), 23).constrainedWithin(getLocalBounds());
            g.setColour(juce::Colour(0xff18212a)); g.fillRoundedRectangle(tag.toFloat(), 3);
            g.setColour(juce::Colours::white); g.drawText(getDescription(), tag.reduced(5, 0), juce::Justification::centred);
        }
        if (gesture == velocity && !pending.empty()) {
            const auto value = pending.front().note.velocity.value_or(96);
            const auto tag = juce::Rectangle<int>(current.x + 8, current.y - 25, 52, 22).constrainedWithin(getLocalBounds());
            g.setColour(juce::Colour(0xff18212a)); g.fillRoundedRectangle(tag.toFloat(), 3);
            g.setColour(juce::Colours::white); g.drawText(juce::String(value), tag, juce::Justification::centred);
        }
        if (gesture == marquee) {
            const auto bounds = juce::Rectangle<int>(anchor, current);
            g.setColour(juce::Colour(0x336ecbcb)); g.fillRect(bounds);
            g.setColour(juce::Colour(0xff87cccc)); g.drawRect(bounds);
        }
    }
    void mouseMove(const juce::MouseEvent& e) override {
        auto* hit = hitNote(e.position);
        const auto mode = hit ? part(*hit, e) : idle;
        setMouseCursor(mode == trimStart || mode == trimEnd ? juce::MouseCursor::LeftRightResizeCursor
            : mode == velocity ? juce::MouseCursor::UpDownResizeCursor : (!hit && pencil && editable ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor));
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (viewportPan.begin(*this, e, getLocalBounds())) return;
        cancel();
        if (!editable || rowCount() == 0 || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu())
            return;
        if (isShowing()) grabKeyboardFocus();
        anchor = current = e.getPosition();
        auto* hit = hitNote(e.position);
        if (!hit) {
            if (!acceptsOnset(newNoteAt(e).startTick)) return;
            if (!e.mods.isShiftDown()) selection.clear();
            originalSelection = selection;
            if (pencil && !e.mods.isShiftDown()) {
                gesture = e.mods.isAltDown() && grid > 1 ? repeat : draw;
                const auto note = newNoteAt(e);
                if (gesture == repeat) {
                    for (const auto& existing : notes) if (existing.pitch == note.pitch) occupiedOnsets.insert(existing.startTick);
                    updateRepeats(current.x);
                } else pending.push_back({Edit::insert, note});
                setDescription({});
            } else gesture = marquee;
        } else {
            if (!acceptsOnset(hit->startTick)) return;
            if (inspectNote) inspectNote(*hit);
            if (e.mods.isShiftDown() && selection.contains(hit->id)) { selection.removeString(hit->id); if (selectionChanged) selectionChanged(); repaint(); return; }
            if (!selection.contains(hit->id)) {
                if (!e.mods.isShiftDown()) selection.clear();
                selection.add(hit->id);
            }
            gesture = part(*hit, e);
            snapAnchor = gesture == trimEnd ? hit->endTick : hit->startTick;
            copy = e.mods.isAltDown() && gesture == move;
        }
        if (selectionChanged) selectionChanged();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (TimelinePan::requested(e.mods)) return;
        if (editable && !pencil && editLyrics && e.mods.isLeftButtonDown())
            if (const auto* n = hitNote(e.position)) { if (acceptsOnset(n->startTick)) editLyrics(*n); return; }
        if (pencil || !editable || rowCount() == 0 || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu() ||
            hitNote(e.position) || getHeight() < 1)
            return;
        cancel();
        const auto note = newNoteAt(e);
        if (acceptsOnset(note.startTick)) commit({{Edit::insert, note}});
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (viewportPan.move(e)) return;
        if (gesture == idle || !e.mouseWasDraggedSinceMouseDown()) return;
        current = e.getPosition();
        if (gesture == repeat) {
            updateRepeats(current.x);
        } else if (gesture == draw) {
            auto& note = pending.front().note;
            const auto step = e.mods.isShiftDown() ? juce::int64(1) : grid;
            const auto end = snapTick(tickAtX(e.x), step);
            const auto minimum = step > 1 ? boundary(std::llround(note.startTick / grid) + 1) : note.startTick + 1;
            note.endTick = std::clamp(end, std::min(minimum, endTick), endTick);
        } else if (gesture == marquee) {
            selection = originalSelection;
            const auto rect = juce::Rectangle<int>(anchor, current).toFloat();
            for (const auto& n : notes) if (acceptsOnset(n.startTick) && rect.intersects(box(n))) selection.addIfNotAlreadyThere(n.id);
            if (selectionChanged) selectionChanged();
        } else {
            const auto dx = (current.x - anchor.x) * tickRange().getLength() / std::max(1, getWidth());
            const auto step = e.mods.isShiftDown() ? juce::int64(1) : grid;
            const auto line = std::round(snapAnchor / step);
            const auto relativeDelta = std::llround((line + std::round(dx / step)) * step) - std::llround(line * step);
            const auto dt = current.x == anchor.x ? 0 : absoluteSnap ? snapTick(snapAnchor + dx, step) - snapAnchor : relativeDelta;
            makeChanges(dt, percussion ? rowAt(anchor.y) - rowAt(current.y) : pitchAt(current.y) - pitchAt(anchor.y),
                        (anchor.y - current.y) / 2);
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (viewportPan.end(*this)) return;
        auto changes = std::move(pending);
        cancel();
        if (!changes.empty()) commit(changes);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (w.deltaY == 0 || e.mods.isAnyModifierKeyDown()) { Component::mouseWheelMove(e, w); return; }
        if (percussion)
            return;
        const auto delta = std::clamp(w.deltaY > 0 ? 3 : -3, -low, 127 - high);
        low += delta; high += delta;
        if (auto* p = getParentComponent()) p->repaint();
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (!editable) return false;
        const auto m = k.getModifiers();
        if (m.isCommandDown()) {
            if (k.isKeyCode('Z') || k.isKeyCode('Y')) { cancel(); undoRedo(m.isShiftDown() || k.isKeyCode('Y')); return true; }
            if (k.isKeyCode('A')) { selection.clear(); for (const auto& n : notes) if (acceptsOnset(n.startTick)) selection.add(n.id); if (selectionChanged) selectionChanged(); repaint(); return true; }
            return false;
        }
        if (k.isKeyCode(juce::KeyPress::escapeKey)) { cancel(); selection.clear(); if (selectionChanged) selectionChanged(); return true; }
        if (k.isKeyCode('B') && !m.isAnyModifierKeyDown() && togglePencil) { cancel(); togglePencil(); return true; }
        if (selection.isEmpty()) return false;
        if (k.isKeyCode(juce::KeyPress::deleteKey) || k.isKeyCode(juce::KeyPress::backspaceKey)) {
            std::vector<Edit> changes;
            for (const auto& n : notes) if (selection.contains(n.id)) changes.push_back({Edit::erase, n});
            if (!changes.empty()) commit(changes);
            return true;
        }
        const int dy = int(k.isKeyCode(juce::KeyPress::upKey)) - int(k.isKeyCode(juce::KeyPress::downKey));
        const int dx = int(k.isKeyCode(juce::KeyPress::rightKey)) - int(k.isKeyCode(juce::KeyPress::leftKey));
        if (dx == 0 && dy == 0) return false;
        gesture = move; copy = false;
        auto ticks = juce::int64(0);
        if (dx) {
            auto first = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return selection.contains(n.id); });
            if (first != notes.end()) {
                const auto line = std::llround(first->startTick / grid);
                ticks = boundary(line + dx) - boundary(line);
            }
        }
        makeChanges(ticks, dy * (m.isShiftDown() ? 12 : 1), 0);
        auto changes = std::move(pending);
        cancel();
        if (!changes.empty()) commit(changes);
        return true;
    }
private:
    template<class Transform> void editSelection(Transform transform) {
        if (!editable || !commit || selection.isEmpty()) return;
        std::vector<Edit> changes;
        for (const auto& original : notes) if (selection.contains(original.id)) {
            auto updated = original;
            transform(updated);
            if (original.startTick != updated.startTick || original.velocity != updated.velocity)
                changes.push_back({Edit::change, updated});
        }
        if (!changes.empty()) {
            const auto selected = selection;
            commit(changes);
            selection = selected;
            if (selectionChanged) selectionChanged();
            repaint();
        }
    }
    double tickAtX(int x) const {
        const auto range = tickRange();
        return range.getStart() + x * range.getLength() / std::max(1, getWidth());
    }
    static juce::int64 snapTick(double tick, double step) { return std::llround(std::round(tick / step) * step); }
    juce::int64 boundary(juce::int64 line) const { return std::llround(line * grid); }
    juce::int64 lineAtX(int x) const {
        const auto tick = std::clamp(tickAtX(std::clamp(x, 0, getWidth())), 0.0, double(endTick - 1));
        auto line = juce::int64(std::floor(tick / grid));
        // A MIDI tick can represent the next fractional grid line after rounding.
        if (boundary(line + 1) <= tick) ++line;
        if (boundary(line) > tick && line > 0) --line;
        return line;
    }
    void updateRepeats(int x) {
        const auto first = lineAtX(anchor.x), last = lineAtX(x);
        pending.clear();
        for (auto i = std::min(first, last); i <= std::max(first, last); ++i) {
            const auto start = boundary(i);
            if (acceptsOnset(start) && !occupiedOnsets.contains(start))
                pending.push_back({Edit::insert, {start, std::min(endTick, boundary(i + 1)), pitchAt(anchor.y), 96, true, {}}});
        }
    }
    std::set<juce::int64> occupiedOnsets;
    Note newNoteAt(const juce::MouseEvent& e) const {
        const auto line = lineAtX(e.x);
        const auto start = boundary(line);
        const auto end = grid > 1 ? boundary(line + 1) : start + std::max<juce::int64>(1, quarter / 4);
        return {start, std::min(endTick, end), pitchAt(e.y), 96, true, {}};
    }
    juce::int64 snapAnchor = 0;
    int pitchAt(int y) const {
        if (percussion)
            return rowCount() ? rowPitch(rowAt(y)) : 0;
        return std::clamp(high - int(std::floor(double(y) * (high - low + 1) / std::max(1, getHeight()))), 0, 127);
    }
    const Note* hitNote(juce::Point<float> p) const {
        for (auto it = notes.rbegin(); it != notes.rend(); ++it)
            if (box(*it).contains(p)) return &*it;
        return nullptr;
    }
    Gesture part(const Note& n, const juce::MouseEvent& e) const {
        if (e.mods.isCommandDown()) return velocity;
        if (e.mods.isAltDown()) return move;
        const auto r = box(n);
        const auto handle = std::min(5.f, r.getWidth() * .22f);
        if (e.position.x <= r.getX() + handle) return trimStart;
        if (e.position.x >= r.getRight() - handle) return trimEnd;
        return move;
    }
    void makeChanges(juce::int64 dt, int dp, int dv) {
        const std::array<juce::int64, 3> delta {dt, dp, gesture == velocity ? dv : 0};
        if (lastPreview == delta) return;
        lastPreview = delta;
        pending.clear();
        // One shared delta preserves chord intervals and timing at the song and MIDI limits.
        for (const auto& n : notes) if (selection.contains(n.id)) {
            if (!editSection.isEmpty() && (gesture==move || gesture==trimStart))
                dt=std::clamp(dt, editSection.getStart()-n.startTick, editSection.getEnd()-1-n.startTick);
            if (gesture == move) {
                dt = std::clamp(dt, -n.startTick, endTick - n.endTick);
                dp = percussion ? std::clamp(dp, pitchRow(n.pitch) - rowCount() + 1, pitchRow(n.pitch))
                                : std::clamp(dp, -n.pitch, 127 - n.pitch);
            } else if (gesture == trimStart) dt = std::clamp(dt, -n.startTick, n.endTick - n.startTick - 1);
            else if (gesture == trimEnd) dt = std::clamp(dt, n.startTick + 1 - n.endTick, endTick - n.endTick);
        }
        for (const auto& original : notes) if (selection.contains(original.id)) {
            auto n = original;
            if (gesture == move || gesture == trimStart) n.startTick += dt;
            if (gesture == move || gesture == trimEnd) n.endTick += dt;
            if (gesture == move && dp)
                n.pitch = percussion ? rowPitch(pitchRow(n.pitch) - dp) : n.pitch + dp;
            if (gesture == velocity && dv != 0) n.velocity = std::clamp(n.velocity.value_or(96) + dv, 1, 127);
            if (n.startTick != original.startTick || n.endTick != original.endTick || n.pitch != original.pitch || n.velocity != original.velocity)
                pending.push_back({copy && gesture == move ? Edit::duplicate : Edit::change, n});
        }
        if (previewEdit && gesture != velocity) previewEdit(pending);
    }
    std::optional<std::array<juce::int64, 3>> lastPreview;
    Gesture gesture = idle;
    juce::Point<int> anchor, current;
    juce::StringArray originalSelection;
    bool copy = false;
    std::vector<Edit> pending;
};

class PianoVelocityLane final : public juce::Component, public juce::SettableTooltipClient {
public:
    TimelinePan viewportPan;
    explicit PianoVelocityLane(PianoNoteGrid& editor) : grid(editor) {
        setComponentID("piano-velocity-lane");
        setTooltip(TRANS("Drag a velocity handle up or down."));
    }
    void paint(juce::Graphics& g) override {
        for (const auto& n : grid.notes) {
            const auto x = grid.box(n).getX();
            if (x < 0 || x >= getWidth()) continue;
            const bool active = dragged && dragged->id == n.id;
            const auto v = active ? candidate : n.velocity.value_or(96);
            const float y = yAt(v);
            g.setColour(juce::Colour(grid.selection.contains(n.id) ? 0xffffc57e : 0xff58c9bd));
            if (n.velocity || active) g.fillEllipse(x - 3, y - 3, 6, 6);
            else g.drawEllipse(x - 3, y - 3, 6, 6, 1);
            if (active) {
                g.drawLine(x, y, x, float(getHeight()), 2);
                g.drawText(juce::String(v), std::clamp(int(x) + 5, 0, std::max(0, getWidth() - 40)), 0, 40, getHeight(), juce::Justification::centredLeft);
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (viewportPan.begin(*this, e, getLocalBounds())) return;
        dragged.reset();
        if (!grid.editable || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) return;
        const ostfstudio::PitchedNote* nearest = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const auto& n : grid.notes) {
            const auto dx = std::abs(e.position.x - grid.box(n).getX());
            if (dx > 7 || !grid.acceptsOnset(n.startTick)) continue;
            const float distance = dx * dx + std::pow(e.position.y - yAt(n.velocity.value_or(96)), 2.f);
            if (distance < best || (distance == best && grid.selection.contains(n.id))) { nearest = &n; best = distance; }
        }
        if (!nearest) return;
        dragged = *nearest;
        candidate = nearest->velocity.value_or(96);
        grid.selection = juce::StringArray(nearest->id);
        if (grid.selectionChanged) grid.selectionChanged();
        if (grid.isShowing()) grid.grabKeyboardFocus();
        grid.repaint(); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (viewportPan.move(e)) return;
        if (!dragged) return;
        candidate = std::clamp(juce::roundToInt(127.f * (getHeight() - 1 - e.position.y) / std::max(1, getHeight() - 3)), 1, 127);
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (viewportPan.end(*this)) return;
        if (!dragged) return;
        auto n = *dragged;
        dragged.reset();
        if (e.mouseWasDraggedSinceMouseDown() && n.velocity != candidate) {
            n.velocity = candidate;
            grid.commit({{ostfstudio::NoteEdit::change, n}});
        }
        repaint();
    }
    void cancel() { dragged.reset(); }
private:
    float yAt(int velocity) const { return getHeight() - 1.f - velocity / 127.f * (getHeight() - 3); }
    PianoNoteGrid& grid;
    std::optional<ostfstudio::PitchedNote> dragged;
    int candidate = 96;
};
