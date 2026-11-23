#pragma once

#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <limits>

class PianoNoteGrid final : public juce::Component, public juce::SettableTooltipClient {
    using Note = ostfstudio::PitchedNote;
    using Edit = ostfstudio::NoteEdit;
    enum Gesture { idle, marquee, move, trimStart, trimEnd, velocity };
public:
    std::function<juce::Range<double>()> tickRange;
    std::function<void(const std::vector<Edit>&)> commit;
    std::function<void(bool)> undoRedo;
    std::vector<Note> notes;
    juce::StringArray selection;
    juce::int64 grid = 1, endTick = 1, quarter = 1;
    int low = 52, high = 70;
    bool editable = false;

    PianoNoteGrid() { setWantsKeyboardFocus(true); setComponentID("piano-note-grid"); }

    void resetRange(bool drum) {
        low = drum ? 33 : 52;
        high = drum ? 54 : 70;
        if (!notes.empty()) {
            auto limits = std::minmax_element(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.pitch < b.pitch; });
            low = std::max(0, limits.first->pitch - 3);
            high = std::min(127, limits.second->pitch + 3);
        }
        selection.clear();
        cancel();
    }
    void cancel() { gesture = idle; pending.clear(); repaint(); }

    float noteY(int pitch) const { return float(high - pitch) * getHeight() / (high - low + 1); }
    juce::Rectangle<float> box(const Note& n) const {
        const auto range = tickRange();
        const auto scale = getWidth() / std::max(1.0, range.getLength());
        return {float((n.startTick - range.getStart()) * scale), noteY(n.pitch),
                std::max(2.f, float((n.endTick - n.startTick) * scale)), std::max(1.f, noteY(0) - noteY(1) - 1)};
    }
    void paint(juce::Graphics& g) override {
        if (!editable || !tickRange) return;
        g.setColour(juce::Colour(0xffffc57e));
        for (const auto& n : notes) if (selection.contains(n.id)) g.drawRoundedRectangle(box(n).reduced(.5f), 2, 1.5f);
        for (const auto& e : pending) {
            g.setColour(juce::Colour(0xcaffc57e));
            g.fillRoundedRectangle(box(e.note), 2);
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
            : mode == velocity ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        cancel();
        if (!editable || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) return;
        if (isShowing()) grabKeyboardFocus();
        anchor = current = e.getPosition();
        auto* hit = hitNote(e.position);
        if (!hit) {
            if (!e.mods.isShiftDown()) selection.clear();
            originalSelection = selection;
            gesture = marquee;
        } else {
            if (e.mods.isShiftDown() && selection.contains(hit->id)) { selection.removeString(hit->id); repaint(); return; }
            if (!selection.contains(hit->id)) {
                if (!e.mods.isShiftDown()) selection.clear();
                selection.add(hit->id);
            }
            gesture = part(*hit, e);
            copy = e.mods.isAltDown() && gesture == move;
        }
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!editable || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu() || hitNote(e.position) || getHeight() < 1) return;
        cancel();
        const auto range = tickRange();
        const auto tick = range.getStart() + e.x * range.getLength() / std::max(1, getWidth());
        auto start = std::clamp(static_cast<juce::int64>(std::floor(tick / grid)) * grid, juce::int64(0), endTick - 1);
        const auto duration = grid > 1 ? grid : std::max<juce::int64>(1, quarter / 4);
        commit({{Edit::insert, {start, std::min(endTick, start + duration), pitchAt(e.y), 96, true, {}}}});
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (gesture == idle || !e.mouseWasDraggedSinceMouseDown()) return;
        current = e.getPosition();
        if (gesture == marquee) {
            selection = originalSelection;
            const auto rect = juce::Rectangle<int>(anchor, current).toFloat();
            for (const auto& n : notes) if (rect.intersects(box(n))) selection.addIfNotAlreadyThere(n.id);
        } else {
            const auto dx = (current.x - anchor.x) * tickRange().getLength() / std::max(1, getWidth());
            const auto dt = std::llround(dx / grid) * grid;
            makeChanges(dt, pitchAt(current.y) - pitchAt(anchor.y), (anchor.y - current.y) / 2);
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        auto changes = std::move(pending);
        cancel();
        if (!changes.empty()) commit(changes);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (w.deltaY == 0 || e.mods.isAnyModifierKeyDown()) { Component::mouseWheelMove(e, w); return; }
        const auto delta = std::clamp(w.deltaY > 0 ? 3 : -3, -low, 127 - high);
        low += delta; high += delta;
        if (auto* p = getParentComponent()) p->repaint();
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (!editable) return false;
        const auto m = k.getModifiers();
        if (m.isCommandDown()) {
            if (k.isKeyCode('Z') || k.isKeyCode('Y')) { cancel(); undoRedo(m.isShiftDown() || k.isKeyCode('Y')); return true; }
            if (k.isKeyCode('A')) { selection.clear(); for (const auto& n : notes) selection.add(n.id); repaint(); return true; }
            return false;
        }
        if (k.isKeyCode(juce::KeyPress::escapeKey)) { cancel(); selection.clear(); return true; }
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
        makeChanges(dx * grid, dy * (m.isShiftDown() ? 12 : 1), 0);
        auto changes = std::move(pending);
        cancel();
        if (!changes.empty()) commit(changes);
        return true;
    }
private:
    int pitchAt(int y) const { return std::clamp(high - int(std::floor(double(y) * (high - low + 1) / std::max(1, getHeight()))), 0, 127); }
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
        pending.clear();
        // One shared delta preserves chord intervals and timing at the song and MIDI limits.
        for (const auto& n : notes) if (selection.contains(n.id)) {
            if (gesture == move) {
                dt = std::clamp(dt, -n.startTick, endTick - n.endTick);
                dp = std::clamp(dp, -n.pitch, 127 - n.pitch);
            } else if (gesture == trimStart) dt = std::clamp(dt, -n.startTick, n.endTick - n.startTick - 1);
            else if (gesture == trimEnd) dt = std::clamp(dt, n.startTick + 1 - n.endTick, endTick - n.endTick);
        }
        for (const auto& original : notes) if (selection.contains(original.id)) {
            auto n = original;
            if (gesture == move || gesture == trimStart) n.startTick += dt;
            if (gesture == move || gesture == trimEnd) n.endTick += dt;
            if (gesture == move) n.pitch += dp;
            if (gesture == velocity && dv != 0) n.velocity = std::clamp(n.velocity.value_or(96) + dv, 1, 127);
            if (n.startTick != original.startTick || n.endTick != original.endTick || n.pitch != original.pitch || n.velocity != original.velocity)
                pending.push_back({copy && gesture == move ? Edit::duplicate : Edit::change, n});
        }
    }
    Gesture gesture = idle;
    juce::Point<int> anchor, current;
    juce::StringArray originalSelection;
    bool copy = false;
    std::vector<Edit> pending;
};

class PianoVelocityLane final : public juce::Component, public juce::SettableTooltipClient {
public:
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
        dragged.reset();
        if (!grid.editable || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) return;
        const ostfstudio::PitchedNote* nearest = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const auto& n : grid.notes) {
            const auto dx = std::abs(e.position.x - grid.box(n).getX());
            if (dx > 7) continue;
            const float distance = dx * dx + std::pow(e.position.y - yAt(n.velocity.value_or(96)), 2.f);
            if (distance < best || (distance == best && grid.selection.contains(n.id))) { nearest = &n; best = distance; }
        }
        if (!nearest) return;
        dragged = *nearest;
        candidate = nearest->velocity.value_or(96);
        grid.selection = juce::StringArray(nearest->id);
        if (grid.isShowing()) grid.grabKeyboardFocus();
        grid.repaint(); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragged) return;
        candidate = std::clamp(juce::roundToInt(127.f * (getHeight() - 1 - e.position.y) / std::max(1, getHeight() - 3)), 1, 127);
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
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
