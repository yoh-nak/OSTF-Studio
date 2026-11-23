#pragma once
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class DrumPads final : public juce::Component {
  public:
    std::function<void(int, std::optional<int>)> edit;
    std::function<void(int, bool)> trigger;
    std::function<void(int)> selectionChanged;
    DrumPads() {
        setComponentID("drum-pad-bank");
        for (auto *c : {&slot, &pitch, &size})
            addAndMakeVisible(c);
        for (auto *b : {&add, &remove, &assign, &previous, &next})
            addAndMakeVisible(b);
        for (int i = 0; i < 128; ++i)
            pitch.addItem(juce::String(i) + "  " + juce::MidiMessage::getMidiNoteName(i, true, true, 4), i + 1);
        size.addItem(TRANS("16 pads"), 4);
        size.addItem(TRANS("64 pads"), 8);
        size.setSelectedId(4, juce::dontSendNotification);
        size.onChange = [this] { select(selected); };
        slot.onChange = [this] { select(slot.getSelectedId() - 1); };
        pitch.setTooltip(TRANS("Assigned MIDI note"));
        add.onClick = [this] {
            int p = std::max(0, pitch.getSelectedId() - 1);
            const auto count = pads.size();
            for (int i = 0; i < 128; ++i) {
                if (std::none_of(pads.begin(), pads.end(), [&](const auto &s) { return s.pitch == p; })) {
                    if (edit)
                        edit(-1, p);
                    if (pads.size() > count)
                        select(int(pads.size()) - 1);
                    return;
                }
                p = (p + 1) % 128;
            }
        };
        remove.onClick = [this] {
            if (edit)
                edit(selected, std::nullopt);
        };
        assign.onClick = [this] {
            if (edit && pitch.getSelectedId())
                edit(selected, pitch.getSelectedId() - 1);
        };
        previous.onClick = [this] { select(std::max(0, (page - 1) * capacity())); };
        next.onClick = [this] { select(std::min(int(pads.size()) - 1, (page + 1) * capacity())); };
    }
    void bind(const std::vector<ostfstudio::DrumSlot> &kit, bool surface, bool changeTrack, bool editing) {
        pads = kit;
        showSurface = surface;
        slot.clear(juce::dontSendNotification);
        for (int i = 0; i < int(pads.size()); ++i)
            slot.addItem(juce::String(i + 1) + "   " + pads[i].name, i + 1);
        select(changeTrack ? 0 : selected);
        assign.setEnabled(editing && !pads.empty());
        remove.setEnabled(editing && !pads.empty());
        add.setEnabled(editing && pads.size() < 128);
        size.setVisible(surface);
        previous.setVisible(surface);
        next.setVisible(surface);
        resized();
    }
    void select(int index) {
        selected = std::clamp(index, 0, std::max(0, int(pads.size()) - 1));
        slot.setSelectedId(pads.empty() ? 0 : selected + 1, juce::dontSendNotification);
        pitch.setSelectedId(pads.empty() ? 37 : pads[selected].pitch + 1, juce::dontSendNotification);
        page = selected / capacity();
        previous.setEnabled(page > 0);
        next.setEnabled((page + 1) * capacity() < int(pads.size()));
        if (selectionChanged)
            selectionChanged(selected);
        repaint();
    }
    void flash(int midiPitch) {
        flashPitch = midiPitch;
        flashUntil = juce::Time::getMillisecondCounter() + 130;
        repaint();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(2);
        auto first = r.removeFromTop(26);
        slot.setBounds(first.removeFromLeft(first.getWidth() / 2).reduced(1));
        pitch.setBounds(first.reduced(1));
        auto second = r.removeFromTop(26);
        int w = second.getWidth() / 3;
        assign.setBounds(second.removeFromLeft(w).reduced(1));
        add.setBounds(second.removeFromLeft(w).reduced(1));
        remove.setBounds(second.reduced(1));
        auto bar = r.removeFromTop(24);
        next.setBounds(bar.removeFromRight(30).reduced(1));
        previous.setBounds(bar.removeFromRight(30).reduced(1));
        size.setBounds(bar.reduced(1));
        const int side = std::max(0, std::min(r.getWidth(), r.getHeight()));
        surfaceArea = r.withSizeKeepingCentre(side, side);
    }
    void paint(juce::Graphics &g) override {
        if (!showSurface || surfaceArea.isEmpty())
            return;
        const int dim = size.getSelectedId();
        g.setFont(juce::FontOptions(dim == 4 ? 13.f : 11.f, juce::Font::bold));
        for (int i = 0; i < capacity(); ++i) {
            const int x = i % dim, y = dim - 1 - i / dim, n = page * capacity() + i;
            auto cell =
                juce::Rectangle<int>::leftTopRightBottom(surfaceArea.getX() + x * surfaceArea.getWidth() / dim,
                                                         surfaceArea.getY() + y * surfaceArea.getHeight() / dim,
                                                         surfaceArea.getX() + (x + 1) * surfaceArea.getWidth() / dim,
                                                         surfaceArea.getY() + (y + 1) * surfaceArea.getHeight() / dim)
                    .reduced(1);
            bool active = n < int(pads.size());
            bool lit = active &&
                       ((pads[n].pitch == flashPitch && juce::Time::getMillisecondCounter() < flashUntil) || held == n);
            g.setColour(juce::Colour(lit                       ? 0xffe5ae60
                                     : n == selected && active ? 0xff367999
                                     : active                  ? 0xff314f66
                                                               : 0xff24323d));
            g.fillRoundedRectangle(cell.toFloat(), 3.f);
            if (!active)
                continue;
            g.setColour(juce::Colours::white);
            g.drawFittedText(cell.getHeight() > 30 && cell.getWidth() > 44 ? juce::String(n + 1) + "\n" + pads[n].name
                                                                           : juce::String(n + 1),
                             cell.reduced(1), juce::Justification::centred, 2);
        }
    }
    void mouseDown(const juce::MouseEvent &e) override {
        if (!showSurface || !surfaceArea.contains(e.getPosition()))
            return;
        const int dim = size.getSelectedId(), x = (e.x - surfaceArea.getX()) * dim / surfaceArea.getWidth();
        const int y = (e.y - surfaceArea.getY()) * dim / surfaceArea.getHeight();
        const int n = page * capacity() + (dim - 1 - y) * dim + x;
        if (n >= int(pads.size()))
            return;
        select(n);
        held = n;
        if (trigger)
            trigger(n, true);
    }
    void mouseUp(const juce::MouseEvent &) override {
        if (held >= 0 && trigger)
            trigger(held, false);
        held = -1;
        repaint();
    }

  private:
    int capacity() const {
        int dim = size.getSelectedId();
        return dim * dim;
    }
    std::vector<ostfstudio::DrumSlot> pads;
    juce::ComboBox slot, pitch, size;
    juce::TextButton add{TRANS("Add pad")}, remove{TRANS("Remove pad")}, assign{TRANS("Assign note")}, previous{"<"},
        next{">"};
    juce::Rectangle<int> surfaceArea;
    int selected = 0, page = 0, held = -1, flashPitch = -1;
    juce::uint32 flashUntil = 0;
    bool showSurface = false;
};
