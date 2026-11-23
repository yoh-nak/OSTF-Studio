#pragma once
#include "ui/PianoNoteGrid.h"

class DrumSteps final : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr int labelWidth = 120, headerHeight = 44;
    void adjustPattern(int index, double value, bool relative) {
        auto& box = index == 0 ? bars : repeats;
        if (box.getNumItems() > 0)
            box.setSelectedItemIndex(std::clamp(relative ? box.getSelectedItemIndex() + int(value) :
                juce::roundToInt(value * (box.getNumItems() - 1)), 0, box.getNumItems() - 1), juce::sendNotificationSync);
    }
    using Tick = juce::int64;
    using Edit = ostfstudio::NoteEdit;
    const ostfstudio::OstfpPreview* song = nullptr;
    size_t track = 0;
    Tick cursor = 0, start = 0;
    double grid = 1;
    std::function<void(std::vector<Edit>)> commit;
    std::function<void()> loopChanged;
    std::function<void(int)> rowChanged;
    void flash(int pitch) { flashes[pitch] = juce::Time::getMillisecondCounter() + 150; repaint(); }
    void selectRow(int row) {
        selected = row;
        const int visible = std::max(1, gridArea().getHeight() / rowHeight);
        const int visual = displayRow(row);
        first = std::clamp(first, std::max(0, visual - visible + 1), visual);
        repaint();
    }
    DrumSteps() {
        setComponentID("drum-steps");
        for (auto* b : {&back, &forward, &locate, &fill})
            addAndMakeVisible(b);
        back.setTooltip(TRANS("Previous range"));
        forward.setTooltip(TRANS("Next range"));
        back.setButtonText("<");
        forward.setButtonText(">");
        locate.setButtonText(TRANS("At cursor"));
        fill.setButtonText(TRANS("Fill every 2"));
        loop.setButtonText(TRANS("Loop"));
        addAndMakeVisible(loop);
        loop.onClick = [this] { notifyRange(); };
        for (int n : {1, 2, 4})
            bars.addItem(juce::String(n) + " " + TRANS("bars"), n);
        bars.setSelectedId(1);
        addAndMakeVisible(bars);
        for (int n : {1, 2, 3, 4})
            repeats.addItem(TRANS("Hits per step") + " " + juce::String(n), n);
        repeats.setSelectedId(1);
        addAndMakeVisible(repeats);
        bars.onChange = [this] { notifyRange(); };
        locate.onClick = [this] {
            if (song) {
                start = barStart(cursor);
                notifyRange();
            }
        };
        back.onClick = [this] {
            if (song) {
                start = std::max<Tick>(0, start - span());
                notifyRange();
            }
        };
        forward.onClick = [this] {
            if (song) {
                start = barStart(std::min(song->playbackEndTick() - 1, start + span()));
                notifyRange();
            }
        };
        fill.onClick = [this] { fillRow(); };
        setTooltip(TRANS("Click a pad row to select it. Click a step to toggle its hits. Shift-click for an accent. Scroll "
                         "for more slots."));
    }
    bool looping() const { return isVisible() && loop.getToggleState(); }
    juce::Range<Tick> range() const {
        return song ? juce::Range<Tick>{start, std::min(song->playbackEndTick(), start + span())} : juce::Range<Tick>{};
    }
    void bind(const ostfstudio::OstfpPreview* project, size_t index, Tick at, double step) {
        if (song != project || track != index) {
            start = 0;
            selected = first = 0;
            loop.setToggleState(false, juce::dontSendNotification);
        }
        song = project;
        track = index;
        cursor = at;
        if (song) {
            grid = step > 1 ? step : std::max(1., song->ppq / 4.);
            start = std::clamp<Tick>(start, 0, song->playbackEndTick() - 1);
        }
        repaint();
    }
    void resized() override {
        if (song && track < song->slotsByTrack.size()) {
            const int visible = std::max(1, gridArea().getHeight() / rowHeight);
            first = std::min(first, std::max(0, int(song->slotsByTrack[track].size()) - visible));
            selectRow(selected);
        }
        auto top = getLocalBounds().removeFromTop(25);
        for (auto* c : {static_cast<juce::Component*>(&back), static_cast<juce::Component*>(&forward)})
            c->setBounds(top.removeFromLeft(27).reduced(1));
        locate.setBounds(top.removeFromLeft(79).reduced(1));
        bars.setBounds(top.removeFromLeft(80).reduced(1));
        loop.setBounds(top.removeFromLeft(64));
        repeats.setBounds(top.removeFromLeft(112).reduced(1));
        fill.setBounds(top.removeFromLeft(104).reduced(1));
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff192735));
        if (!song || track >= song->slotsByTrack.size()) return;
        auto body = gridArea();
        const auto& slots = song->slotsByTrack[track];
        g.setFont(juce::FontOptions(13.f, juce::Font::bold));
        g.setColour(juce::Colours::white);
        g.drawText(TRANS("Bar") + " " + juce::String(1 + start / song->ticksPerBar), 6, 25, labelWidth - 6, 19,
                   juce::Justification::centredLeft);
        const int steps = count();
        if (steps <= 0 || body.isEmpty()) return;
        const float w = float(body.getWidth()) / steps;
        for (int n = 0; n < steps; ++n) {
            const int x = body.getX() + int(n * w);
            if (w >= 24 || n % 4 == 0)
                g.drawText(juce::String(n + 1), x, 25, std::max(16, int(w)), 19, juce::Justification::centredLeft);
        }
        for (int visual = first; visual < int(slots.size()); ++visual) {
            const int row = displayRow(visual);
            const int y = body.getY() + (visual - first) * rowHeight;
            if (y >= getHeight()) break;
            const auto it = flashes.find(slots[row].pitch);
            const bool active = it != flashes.end() && int(it->second - juce::Time::getMillisecondCounter()) > 0;
            g.setColour(juce::Colour(active ? 0xffd28434 : row == selected ? 0xff356585 : 0xff293c4c));
            g.fillRect(0, y, labelWidth - 2, rowHeight - 1);
            g.setColour(juce::Colours::white);
            g.drawText(slots[row].name, 5, y, labelWidth - 9, rowHeight, juce::Justification::centredLeft);
            for (int n = 0; n < steps; ++n) {
                g.setColour(juce::Colour((n / 4) % 2 ? 0xff35485a : 0xff283b4a));
                g.fillRect(body.getX() + int(n * w), y, std::max(1, int((n + 1) * w) - int(n * w) - 1), rowHeight - 2);
            }
        }
        for (const auto& hit : song->slotHits[track])
            if (range().contains(hit.startTick)) {
                const auto slot =
                    std::find_if(slots.begin(), slots.end(), [&](const auto& s) { return s.id == hit.slotId; });
                if (slot == slots.end()) continue;
                int row = displayRow(int(slot - slots.begin())) - first;
                if (row < 0 || body.getY() + row * rowHeight >= getHeight()) continue;
                const int n = stepAt(hit.startTick);
                const auto a = boundary(n), b = boundary(n + 1);
                const auto x = body.getX() + (n + double(hit.startTick - a) / std::max<Tick>(1, b - a)) * w;
                g.setColour(juce::Colour(hit.velocity.value_or(96) > 105 ? 0xffffc670 : 0xff61bade));
                g.fillRect(float(x), float(body.getY() + row * rowHeight + 3),
                           std::max(2.f, float(std::min(hit.endTick, b) - hit.startTick) / std::max<Tick>(1, b - a) * w - 1),
                           float(rowHeight - 8));
            }
        if (range().contains(cursor)) {
            g.setColour(juce::Colour(0xfff7c36a));
            g.drawVerticalLine(body.getX() + int((cursor - start) / grid * w), 25, float(getHeight()));
        }
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
        if (!song || track >= song->slotsByTrack.size()) return;
        first =
            std::clamp(first + (wheel.deltaY < 0 ? 1 : -1), 0,
                       std::max(0, int(song->slotsByTrack[track].size()) - std::max(1, gridArea().getHeight() / rowHeight)));
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!song || !e.mods.isLeftButtonDown() || e.y < headerHeight || track >= song->slotsByTrack.size()) return;
        const int visual = first + (e.y - headerHeight) / rowHeight;
        if (visual >= int(song->slotsByTrack[track].size())) return;
        const int row = displayRow(visual);
        selected = row;
        if (rowChanged)
            rowChanged(row);
        repaint();
        if (e.x < labelWidth || !commit || gridArea().getWidth() <= 0) return;
        const int n = std::clamp((e.x - labelWidth) * count() / gridArea().getWidth(), 0, count() - 1);
        std::vector<Edit> edits;
        const auto& slot = song->slotsByTrack[track][row];
        for (const auto& h : song->slotHits[track])
            if (h.slotId == slot.id && h.startTick >= boundary(n) && h.startTick < boundary(n + 1))
                edits.push_back({Edit::erase, h});
        if (edits.empty()) addHits(edits, n, e.mods.isShiftDown() ? 120 : 92);
        if (!edits.empty()) commit(std::move(edits));
    }

private:
    int displayRow(int index) const {
        return song && track < song->slotsByTrack.size() ? std::max(0, int(song->slotsByTrack[track].size()) - 1 - index) : 0;
    }
    std::map<int, juce::uint32> flashes;
    static constexpr int rowHeight = 24;
    juce::TextButton back, forward, locate, fill;
    juce::ToggleButton loop;
    juce::ComboBox bars, repeats;
    int selected = 0, first = 0;
    Tick barStart(Tick t) const { return t / song->ticksPerBar * song->ticksPerBar; }
    Tick span() const { return song->ticksPerBar * std::max(1, bars.getSelectedId()); }
    int count() const { return std::max(1, int(std::ceil(range().getLength() / grid))); }
    Tick boundary(int i) const { return std::min(range().getEnd(), start + std::llround(i * grid)); }
    int stepAt(Tick t) const {
        auto i = int((t - start) / grid);
        return i + 1 < count() && boundary(i + 1) <= t ? i + 1 : i;
    }
    juce::Rectangle<int> gridArea() const { return getLocalBounds().withTrimmedTop(headerHeight).withTrimmedLeft(labelWidth); }
    void notifyRange() {
        repaint();
        if (loopChanged) loopChanged();
    }
    void addHits(std::vector<Edit>& edits, int n, int velocity) {
        const auto& slot = song->slotsByTrack[track][selected];
        const auto a = boundary(n), len = boundary(n + 1) - a;
        const int hits = int(std::min<Tick>(len, std::max(1, repeats.getSelectedId())));
        for (int i = 0; i < hits; ++i)
            edits.push_back(
                {Edit::insert,
                 {a + len * i / hits, a + len * (i + 1) / hits, slot.pitch, velocity, true, {}, false, {}, slot.id}});
    }
    void fillRow() {
        if (!song || !commit || track >= song->slotsByTrack.size() || selected >= int(song->slotsByTrack[track].size()))
            return;
        const auto& slot = song->slotsByTrack[track][selected];
        std::set<int> occupied;
        for (const auto& h : song->slotHits[track])
            if (h.slotId == slot.id && range().contains(h.startTick)) occupied.insert(stepAt(h.startTick));
        std::vector<Edit> edits;
        for (int n = 0; n < count(); n += 2)
            if (!occupied.contains(n)) addHits(edits, n, 92);
        if (!edits.empty()) commit(std::move(edits));
    }
};
