#pragma once
#include "ui/ControllerMap.h"
#include "ui/MidiActivityLamp.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ControllerSurface final : public juce::Component {
public:
    std::function<void(juce::MidiDeviceInfo)> selected;
    std::function<void(int)> learn;
    std::function<void(bool)> panMode;
    std::function<void(int)> audition;
    juce::StringArray tracks, slots;
    std::array<int, 16> padInputs{};
    std::array<int, 8> knobInputs{};
    int learningControl = -1;
    static juce::String binding(int key, bool note) {
        if (key < 0) return TRANS("Unassigned");
        return "Ch" + juce::String(key / 128 + 1) + (note ? " N" : " CC") + juce::String(key % 128);
    }
    std::array<double, 8> levels{}, pans{};
    std::array<juce::uint32, 16> flashes{};
    juce::ComboBox devices, layer;
    juce::TextButton hardware{TRANS("Open Midi Suite...")};
    MidiActivityLamp inputLamp;
    juce::Label inputLabel;
    void received(const juce::String& event) {
        inputLamp.pulse();
        inputLabel.setText("MIDI IN  " + event, juce::dontSendNotification);
    }
    juce::ToggleButton learning{TRANS("MIDI Learn")};
    juce::Label status;
    ostfstudio::SurfaceModel model = ostfstudio::SurfaceModel::none;
    explicit ControllerSurface() {
        for (juce::Component *c : std::array<juce::Component *, 4>{&devices, &layer, &learning, &status})
            addAndMakeVisible(c);
        addAndMakeVisible(hardware);
        addAndMakeVisible(inputLamp);
        addAndMakeVisible(inputLabel);
        inputLabel.setText("MIDI IN", juce::dontSendNotification);
        layer.addItem(TRANS("Volume"), 1);
        layer.addItem(TRANS("Pan"), 2);
        layer.setSelectedId(1, juce::dontSendNotification);
        layer.onChange = [this] {
            if (panMode)
                panMode(layer.getSelectedId() == 2);
            repaint();
        };
        learning.onClick = [this] {
            if (learn)
                learn(-1);
        };
        devices.onChange = [this] {
            const int i = devices.getSelectedId() - 2;
            if (selected)
                selected(i >= 0 && i < ports.size() ? ports[i] : juce::MidiDeviceInfo{});
        };
        setSize(700, 306);
    }
    void refreshPorts(const juce::String &active) {
        ports.clear();
        devices.clear(juce::dontSendNotification);
        devices.addItem(TRANS("Disconnected"), 1);
        int index = 1;
        for (auto port : juce::MidiInput::getAvailableDevices()) {
            ports.add(port);
            devices.addItem(port.name, ports.size() + 1);
            if (port.identifier == active)
                index = ports.size() + 1;
        }
        devices.setSelectedId(index, juce::dontSendNotification);
    }
    void resized() override {
        auto top = getLocalBounds().reduced(12).removeFromTop(30);
        hardware.setBounds(top.removeFromRight(135).reduced(2));
        devices.setBounds(top.removeFromLeft(245).reduced(2));
        layer.setBounds(top.removeFromLeft(132).reduced(2));
        learning.setBounds(top);
        auto footer = getLocalBounds().reduced(12).removeFromBottom(54);
        auto activity = footer.removeFromTop(22);
        inputLamp.setBounds(activity.removeFromLeft(18));
        inputLabel.setBounds(activity);
        status.setBounds(footer);
    }
    juce::Rectangle<int> cell(int col, int row) const {
        auto b = getLocalBounds().reduced(12).withTrimmedTop(42).withTrimmedBottom(64);
        return {b.getX() + col * b.getWidth() / 8, b.getY() + row * b.getHeight() / 3,
                (col + 1) * b.getWidth() / 8 - col * b.getWidth() / 8, b.getHeight() / 3};
    }
    void paint(juce::Graphics &g) override {
        g.fillAll(juce::Colour(0xff202a32));
        if (model == ostfstudio::SurfaceModel::none) {
            g.setColour(juce::Colours::white);
            g.drawFittedText(TRANS("Connect your controller and choose its MIDI input above."),
                             getLocalBounds().reduced(22), juce::Justification::centred, 2);
            return;
        }
        const bool mixer = model == ostfstudio::SurfaceModel::mixer;
        for (int i = 0; i < 8; ++i) {
            auto knob = cell(i, 0).reduced(2);
            g.setColour(juce::Colour(0xffb8c6cf));
            g.setFont(13.f);
            if (!mixer) g.drawFittedText(binding(knobInputs[i], false), knob.removeFromBottom(15), juce::Justification::centred, 1);
            g.drawFittedText(tracks[i], knob.removeFromBottom(16), juce::Justification::centred, 1);
            auto dial = knob.toFloat().withSizeKeepingCentre(30, 30);
            g.setColour(juce::Colour(learningControl == i + 16 ? 0xff9b6932 : 0xff445561));
            g.fillEllipse(dial);
            auto c = dial.getCentre();
            float a = float(((mixer || layer.getSelectedId() == 2 ? pans[i] : levels[i]) * 1.5 - .75) *
                            juce::MathConstants<double>::pi);
            g.setColour(juce::Colour(0xff8dd9d2));
            g.drawLine(c.x, c.y, c.x + 11 * std::sin(a), c.y - 11 * std::cos(a), 2.f);
            if (mixer) {
                auto rail = cell(i, 1).getUnion(cell(i, 2)).reduced(9, 7);
                g.setColour(juce::Colour(0xff101a20));
                g.fillRect(rail.withSizeKeepingCentre(5, rail.getHeight()));
                g.setColour(juce::Colour(0xff92c5c9));
                g.fillRoundedRectangle(float(rail.getCentreX() - 14),
                                       float(rail.getBottom() - 8 - levels[i] * (rail.getHeight() - 8)), 28, 8, 2);
            }
        }
        if (!mixer)
            for (int i = 0; i < 16; ++i) {
                auto pad = cell(i % 8, 1 + i / 8).reduced(2);
                const bool lit = int(juce::Time::getMillisecondCounter() - flashes[i]) < 200 && flashes[i] != 0;
                g.setColour(juce::Colour(lit ? 0xff417f82 : learningControl == i ? 0xff916229 : 0xff344a5b));
                g.fillRoundedRectangle(pad.toFloat(), 5);
                g.setColour(juce::Colours::white);
                g.drawFittedText(juce::String(i + 1) + "  " + slots[i] + "\n" + binding(padInputs[i], true), pad.reduced(3), juce::Justification::centred,
                                 2);
            }
    }
    void mouseDown(const juce::MouseEvent &e) override {
        if (model != ostfstudio::SurfaceModel::smk25ii && model != ostfstudio::SurfaceModel::generic)
            return;
        for (int i = 0; i < 24; ++i) {
            const auto hit = i < 16 ? cell(i % 8, 1 + i / 8) : cell(i - 16, 0);
            if (!hit.contains(e.getPosition()))
                continue;
            if (learning.getToggleState() && learn)
                learn(i);
            else if (i < 16 && audition)
                audition(i);
            break;
        }
    }

private:
    juce::Array<juce::MidiDeviceInfo> ports;
};
