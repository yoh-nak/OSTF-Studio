#pragma once
#include "ui/ControllerMap.h"
#include "core/Smk25Preset.h"
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
    int learningControl = -1, rigPage = 0, rigBank = 0;
    bool rig = false;
    std::array<juce::Colour,16> rigColours;
    static juce::String binding(int key, bool note) {
        if (key < 0) return TRANS("Unassigned");
        return "Ch" + juce::String(key / 128 + 1) + (note ? " N" : " CC") + juce::String(key % 128);
    }
    std::array<double, 8> levels{}, pans{};
    std::array<juce::uint32, 16> flashes{};
    juce::ComboBox devices, layer;
    juce::ComboBox target, inputChannel;
    juce::Slider inputNumber;
    juce::TextButton assign{TRANS("Assign input")}, unassign{TRANS("Unassign")};
    std::function<void(int, int)> manualBinding;
    void showBinding() {
        const int i = target.getSelectedId() - 1;
        if (i < 0 || i >= 24) return;
        const int key = i < 16 ? padInputs[i] : knobInputs[i - 16];
        inputChannel.setSelectedId(key < 0 ? 1 : key / 128 + 1, juce::dontSendNotification);
        inputNumber.setValue(key < 0 ? 0 : key % 128, juce::dontSendNotification);
        inputNumber.setTitle(i < 16 ? TRANS("Note number") : TRANS("CC number"));
    }
    void showManualControls(bool visible, bool enabled) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&target, &inputChannel, &inputNumber, &assign, &unassign}) {
            c->setVisible(visible);
            c->setEnabled(enabled);
        }
    }
    MidiActivityLamp inputLamp;
    juce::Label inputLabel;
    void received(const juce::String& event) {
        inputLamp.pulse();
        inputLabel.setText("MIDI IN  " + event, juce::dontSendNotification);
    }
    juce::TextButton learning{TRANS("MIDI Learn")}, recommended{TRANS("Load recommended mixer layout")};
    juce::Label status;
    ostfstudio::SurfaceModel model = ostfstudio::SurfaceModel::none;
    explicit ControllerSurface() {
        for (juce::Component *c : std::array<juce::Component *, 4>{&devices, &layer, &learning, &status})
            addAndMakeVisible(c);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&target, &inputChannel, &inputNumber, &assign, &unassign}) addChildComponent(c);
        for (int i = 0; i < 24; ++i)
            target.addItem((i < 16 ? TRANS("Pad") : TRANS("Knob")) + " " + juce::String(i < 16 ? i + 1 : i - 15), i + 1);
        target.setSelectedId(1, juce::dontSendNotification);
        target.setTitle(TRANS("Control to assign"));
        target.onChange = [this] { showBinding(); };
        for (int i = 1; i <= 16; ++i) inputChannel.addItem("Ch " + juce::String(i), i);
        inputChannel.setTitle(TRANS("MIDI channel"));
        inputNumber.setRange(0, 127, 1);
        inputNumber.setSliderStyle(juce::Slider::LinearHorizontal);
        inputNumber.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 24);
        assign.onClick = [this] {
            if (manualBinding) manualBinding(target.getSelectedId() - 1, (inputChannel.getSelectedId() - 1) * 128 + int(inputNumber.getValue()));
        };
        unassign.onClick = [this] { if (manualBinding) manualBinding(target.getSelectedId() - 1, -1); };
        learning.setClickingTogglesState(true);
        learning.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff956126));
#if JUCE_WINDOWS && defined(_MSC_VER)
        addAndMakeVisible(recommended);
        recommended.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff17634f));
#endif
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
    void selectOnlySmkInput() {
        int match = -1;
        for (int i = 0; i < ports.size(); ++i) {
            if (ostfstudio::surfaceModel(ports[i].name) != ostfstudio::SurfaceModel::smk25ii) continue;
            if (match >= 0) return;
            match = i;
        }
        if (match >= 0) devices.setSelectedId(match + 2, juce::sendNotificationSync);
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

        devices.setBounds(top.removeFromLeft(245).reduced(2));
        layer.setBounds(top.removeFromLeft(132).reduced(2));
        recommended.setBounds(top.removeFromRight(207).reduced(2));
        learning.setBounds(top.reduced(2));
        auto footer = getLocalBounds().reduced(12).removeFromBottom(88);
        auto form = footer.removeFromTop(32);
        target.setBounds(form.removeFromLeft(110).reduced(2));
        inputChannel.setBounds(form.removeFromLeft(96).reduced(2));
        inputNumber.setBounds(form.removeFromLeft(160).reduced(2));
        assign.setBounds(form.removeFromLeft(125).reduced(2));
        unassign.setBounds(form.removeFromLeft(110).reduced(2));
        auto activity = footer.removeFromTop(22);
        inputLamp.setBounds(activity.removeFromLeft(18));
        inputLabel.setBounds(activity);
        status.setBounds(footer);
    }
    juce::Rectangle<int> cell(int col, int row) const {
        auto b = getLocalBounds().reduced(12).withTrimmedTop(42).withTrimmedBottom(98);
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
            float a = float((((rig ? rigBank == 1 : mixer || layer.getSelectedId() == 2) ? pans[i] : levels[i]) * 1.5 - .75) *
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
                if (rig && !lit) g.setColour(rigColours[i].darker(.62f));
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
            target.setSelectedId(i + 1, juce::sendNotificationSync);
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
