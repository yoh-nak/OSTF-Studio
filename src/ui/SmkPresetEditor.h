#pragma once
#include "ui/Smk25Bluetooth.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SmkPresetEditor final : public juce::Component, public juce::FileDragAndDropTarget {
public:
    using Preset = ostfstudio::Smk25Preset;
    std::function<bool(const Preset::Bindings&, const juce::String&)> applyBindings;

    explicit SmkPresetEditor(juce::PropertiesFile& settings) : prefs(settings) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&create, &open, &save, &apply, &filename, &status, &bank,
                  &mode, &channel, &number, &modeLabel, &channelLabel, &numberLabel, &colourLabel}) addAndMakeVisible(c);
        bank.setTitle(TRANS("Preset bank"));
        bank.addItem(TRANS("Bank A"), 1);
        bank.addItem(TRANS("Bank B"), 2);
        bank.setSelectedId(1, juce::dontSendNotification);
        bank.onChange = [this] { refresh(); };
        modeLabel.setText(TRANS("Message type"), juce::dontSendNotification);
        channelLabel.setText(TRANS("MIDI channel"), juce::dontSendNotification);
        colourLabel.setText(TRANS("Pad colour"), juce::dontSendNotification);
        mode.setTitle(TRANS("Message type"));
        channel.setTitle(TRANS("MIDI channel"));
        for (int i = 1; i <= 16; ++i) channel.addItem(juce::String(i), i);
        mode.onChange = channel.onChange = [this] { change(); };
        configureSlider(number, 127);
        for (int i = 0; i < 3; ++i) {
            addAndMakeVisible(rgb[i]);
            configureSlider(rgb[i], 255);
            rgb[i].setTitle(juce::String::charToString("RGB"[i]));
            rgb[i].setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(i == 0 ? 214 : 80, i == 1 ? 195 : 80, i == 2 ? 225 : 80));
        }
        for (int i = 0; i < 24; ++i) {
            addAndMakeVisible(controls[i]);
            controls[i].onClick = [this, i] { slot = i; refresh(); };
        }
#if JUCE_WINDOWS && defined(_MSC_VER)
        for (juce::Component* c : std::initializer_list<juce::Component*>{&scan, &bluetooth, &deviceSlot, &readDevice, &writeDevice}) addAndMakeVisible(c);
        bluetooth.setTitle(TRANS("Bluetooth settings device"));
        bluetooth.setTextWhenNothingSelected(TRANS("Choose a Bluetooth device"));
        deviceSlot.setTitle(TRANS("Device preset slot"));
        for (int i = 1; i <= 8; ++i) deviceSlot.addItem(TRANS("Preset") + " " + juce::String(i), i);
        deviceSlot.setSelectedId(1, juce::dontSendNotification);
        bluetooth.onChange = [this] { refresh(); };
        scan.onClick = [this] { scanDevices(); };
        readDevice.onClick = [this] { confirmReplacement([this] { startTransfer(false); }); };
        writeDevice.onClick = [this] { confirmWrite(); };
#endif
        create.onClick = [this] { confirmReplacement([this] { newPreset(); }); };
        open.onClick = [this] { choose(false); };
        save.onClick = [this] { choose(true); };
        apply.onClick = [this] { confirmApply(); };
        juce::MemoryBlock draft;
        if (draft.fromBase64Encoding(prefs.getValue("smk25Draft"))) {
            preset = decode(draft);
            const auto path = prefs.getValue("smk25DraftSource");
            if (preset) {
                if (juce::File::isAbsolutePath(path)) source = juce::File(path);
                dirty = true;
            }
        }
        if (!dirty) newPreset();
        else status.setText(TRANS("Restored unsaved preset edits."), juce::dontSendNotification);
        refresh();
    }

    ~SmkPresetEditor() override {
#if JUCE_WINDOWS && defined(_MSC_VER)
        jobs.removeAllJobs(true, -1);
#endif
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        return isEnabled() && files.size() == 1 && files[0].endsWithIgnoreCase(".mkcII");
    }
    void filesDropped(const juce::StringArray& files, int, int) override {
        if (isInterestedInFileDrag(files))
            confirmReplacement([this, file = juce::File(files[0])] { loadFile(file); });
    }

    void setDestination(juce::String id, juce::String name) {
        destination = std::move(id);
        destinationName = std::move(name);
        apply.setEnabled(preset.has_value() && destination.isNotEmpty());
        apply.setTooltip(destination.isEmpty() ? TRANS("Select a controller MIDI input in Studio assignments.") : destinationName);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16, 12);
        auto top = area.removeFromTop(33);
        create.setBounds(top.removeFromLeft(80).reduced(1));
        open.setBounds(top.removeFromLeft(142).reduced(1));
        save.setBounds(top.removeFromLeft(148).reduced(1));
        bank.setBounds(top.removeFromRight(116).reduced(1));
        filename.setBounds(top.reduced(8, 0));
        area.removeFromTop(9);
        for (int row = 0; row < 3; ++row) {
            auto strip = area.removeFromTop(49);
            for (int col = 0; col < 8; ++col) {
                const int index = row == 0 ? 16 + col : (row - 1) * 8 + col;
                controls[index].setBounds(strip.removeFromLeft(strip.getWidth() / (8 - col)).reduced(2));
            }
        }
        area.removeFromTop(8);
        auto labels = area.removeFromTop(22);
        auto fields = area.removeFromTop(31);
        for (auto* c : {&modeLabel, &channelLabel, &numberLabel}) c->setBounds(labels.removeFromLeft(182));
        mode.setBounds(fields.removeFromLeft(182).reduced(2));
        channel.setBounds(fields.removeFromLeft(182).reduced(2));
        number.setBounds(fields.removeFromLeft(182).reduced(2));
        auto colours = area.removeFromTop(36);
        colourLabel.setBounds(colours.removeFromLeft(96));
        for (auto& slider : rgb) slider.setBounds(colours.removeFromLeft(156).reduced(3, 2));
#if JUCE_WINDOWS && defined(_MSC_VER)
        auto deviceRow = area.removeFromTop(32);
        scan.setBounds(deviceRow.removeFromLeft(118).reduced(2));
        bluetooth.setBounds(deviceRow.removeFromLeft(208).reduced(2));
        deviceSlot.setBounds(deviceRow.removeFromLeft(108).reduced(2));
        readDevice.setBounds(deviceRow.removeFromLeft(118).reduced(2));
        writeDevice.setBounds(deviceRow.removeFromLeft(140).reduced(2));
#endif
        auto bottom = area.removeFromBottom(40);
        apply.setBounds(bottom.removeFromRight(200).reduced(2));
        status.setBounds(bottom);
    }

private:
    juce::PropertiesFile& prefs;
    std::optional<Preset> preset;
    juce::File source;
    bool dirty = false;
    int slot = 0;
    juce::String destination, destinationName;
    juce::TextButton create{TRANS("New preset")}, open{TRANS("Open preset...")}, save{TRANS("Save preset as...")}, apply{TRANS("Use bank in Studio")};
    juce::Label filename, status, modeLabel, channelLabel, numberLabel, colourLabel;
    juce::ComboBox bank, mode, channel;
    juce::Slider number;
    std::array<juce::Slider, 3> rgb;
    std::array<juce::TextButton, 24> controls;
    std::unique_ptr<juce::FileChooser> chooser;

    static std::optional<Preset> decode(const juce::MemoryBlock& bytes) {
        return Preset::read({static_cast<const std::uint8_t*>(bytes.getData()), bytes.getSize()});
    }
    void configureSlider(juce::Slider& slider, int maximum) {
        slider.setRange(0, maximum, 1);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 24);
        slider.onValueChange = [this] { change(); };
    }
#if JUCE_WINDOWS && defined(_MSC_VER)
    juce::TextButton scan{TRANS("Scan Bluetooth")}, readDevice{TRANS("Read from device")}, writeDevice{TRANS("Write to device...")};
    juce::ComboBox bluetooth, deviceSlot;
    std::vector<ostfstudio::smkble::Device> devices;
    juce::ThreadPool jobs{1};

    void scanDevices() {
        setEnabled(false);
        status.setText(TRANS("Searching for SMK-25 II controllers..."), juce::dontSendNotification);
        jobs.addJob([safe = juce::Component::SafePointer<SmkPresetEditor>(this)] {
            auto result = ostfstudio::smkble::discover();
            juce::MessageManager::callAsync([safe, result = std::move(result)] {
                if (!safe) return;
                safe->devices = result.devices;
                safe->bluetooth.clear(juce::dontSendNotification);
                for (size_t i = 0; i < safe->devices.size(); ++i) {
                    auto& d = safe->devices[i];
                    safe->bluetooth.addItem(d.name + " · " + juce::String::toHexString(juce::int64(d.address)).getLastCharacters(4), int(i + 1));
                }
                if (safe->devices.size() == 1) safe->bluetooth.setSelectedId(1, juce::dontSendNotification);
                safe->status.setText(result.message.isNotEmpty() ? result.message : TRANS("Choose the device preset slot to read or write."), juce::dontSendNotification);
                safe->setEnabled(true);
                safe->refresh();
            });
        });
    }
    void confirmWrite() {
        auto msg = bluetooth.getText() + " / " + deviceSlot.getText() + "\n"
            + TRANS("Replace both banks in this device slot? Studio saves a backup before writing.");
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, writeDevice.getButtonText(),
            msg, TRANS("Write preset"), TRANS("Cancel"), this, juce::ModalCallbackFunction::create(
                [safe = juce::Component::SafePointer<SmkPresetEditor>(this)](int ok) { if (ok && safe) safe->startTransfer(true); }));
    }
    void startTransfer(bool writing) {
        const auto i = size_t(bluetooth.getSelectedId() - 1);
        if (i >= devices.size() || (writing && !preset)) return;
        const int index = deviceSlot.getSelectedId() - 1;
        setEnabled(false);
        status.setText(writing ? TRANS("Backing up and writing the device preset...") : TRANS("Reading the device preset..."), juce::dontSendNotification);
        jobs.addJob([safe = juce::Component::SafePointer<SmkPresetEditor>(this), device = devices[i], index, data = writing ? preset : std::nullopt] {
            auto result = ostfstudio::smkble::transfer(device, index, data);
            juce::MessageManager::callAsync([safe, result = std::move(result), writing = data.has_value()] {
                if (!safe) return;
                safe->setEnabled(true);
                auto message = result.message;
                if (message.isEmpty()) {
                    if (!writing && result.preset) {
                        safe->preset = result.preset;
                        safe->source = {};
                        safe->slot = 0;
                        safe->bank.setSelectedId(1, juce::dontSendNotification);
                        safe->storeDraft();
                    }
                    message = writing ? TRANS("Device preset verified.") : TRANS("Device preset loaded.");
                }
                if (result.backup.existsAsFile()) message += "  " + TRANS("Backup") + " " + result.backup.getFullPathName();
                safe->status.setText(message, juce::dontSendNotification);
                safe->status.setTooltip(message);
                safe->refresh();
            });
        });
    }
#endif

    void refresh() {
        const int b = bank.getSelectedId() - 1;
        const auto silent = juce::dontSendNotification;
        auto c = preset ? preset->control(b, slot) : Preset::Control{};
        const bool editable = c.mode != Preset::Mode::unsupported;
        const bool pad = slot < 16;
#if JUCE_WINDOWS && defined(_MSC_VER)
        const bool deviceReady = size_t(bluetooth.getSelectedId() - 1) < devices.size();
        readDevice.setEnabled(deviceReady);
        writeDevice.setEnabled(deviceReady && preset.has_value());
#endif
        bank.setEnabled(preset.has_value());
        filename.setText((source == juce::File{} ? TRANS("Studio preset") : source.getFileName()) + (dirty ? " *" : ""), silent);
        save.setEnabled(preset.has_value());
        apply.setEnabled(preset.has_value() && destination.isNotEmpty());
        for (int i = 0; i < 24; ++i) {
            const auto desc = preset ? preset->control(b, i) : Preset::Control{};
            const auto title = (i < 16 ? TRANS("Pad") : TRANS("Knob")) + " " + juce::String(i < 16 ? i + 1 : i - 15);
            juce::String suffix;
            if (desc.mode != Preset::Mode::unsupported)
                suffix = juce::String(desc.mode == Preset::Mode::cc ? "CC " : desc.mode == Preset::Mode::program ? "PC " : "N ") + juce::String(desc.number);
            else if (preset) suffix = TRANS("Other");
            auto& button = controls[i];
            button.setButtonText(title + (suffix.isEmpty() ? juce::String{} : "\n" + suffix));
            button.setEnabled(preset.has_value());
            button.setToggleState(i == slot, silent);
            if (i < 16 && desc.mode != Preset::Mode::unsupported) {
                const auto colour = juce::Colour::fromRGB(juce::uint8(desc.rgb[0]), juce::uint8(desc.rgb[1]), juce::uint8(desc.rgb[2]));
                button.setColour(juce::TextButton::buttonColourId, colour.darker(0.9f));
            } else button.removeColour(juce::TextButton::buttonColourId);
        }
        mode.clear(silent);
        if (pad) {
            mode.addItem(TRANS("Note"), int(Preset::Mode::note));
            mode.addItem(TRANS("Program change"), int(Preset::Mode::program));
        } else mode.addItem("CC", int(Preset::Mode::cc));
        mode.setSelectedId(int(c.mode), silent);
        mode.setTextWhenNothingSelected(preset ? TRANS("Other message type") : juce::String{});
        mode.setEnabled(editable && pad);
        channel.setSelectedId(editable ? c.channel : 0, silent);
        channel.setEnabled(editable);
        numberLabel.setText(c.mode == Preset::Mode::note ? TRANS("Note number") : c.mode == Preset::Mode::program ? TRANS("Program number") : pad ? TRANS("Number") : TRANS("CC number"), silent);
        number.setTitle(numberLabel.getText());
        number.setValue(c.number, silent);
        number.setEnabled(editable);
        colourLabel.setVisible(pad && editable);
        for (int i = 0; i < 3; ++i) {
            rgb[i].setVisible(colourLabel.isVisible());
            rgb[i].setValue(c.rgb[i], silent);
        }
    }

    void change() {
        if (!preset) return;
        const int b = bank.getSelectedId() - 1;
        auto c = preset->control(b, slot);
        c.mode = static_cast<Preset::Mode>(mode.getSelectedId());
        c.channel = channel.getSelectedId();
        c.number = int(number.getValue());
        for (int i = 0; i < 3; ++i) c.rgb[i] = int(rgb[i].getValue());
        if (!preset->edit(b, slot, c)) return;
        storeDraft();
        refresh();
    }
    void storeDraft() {
        dirty = true;
        const auto& bytes = preset->bytes();
        prefs.setValue("smk25Draft", juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding());
        prefs.setValue("smk25DraftSource", source.getFullPathName());
    }
    void clearDraft() {
        dirty = false;
        prefs.removeValue("smk25Draft");
        prefs.removeValue("smk25DraftSource");
    }
    void newPreset() {
        preset = Preset::studioPreset();
        source = {};
        clearDraft();
        bank.setSelectedId(1, juce::dontSendNotification);
        slot = 0;
        status.setText(TRANS("Select a pad or knob to edit its message."), juce::dontSendNotification);
        refresh();
    }
    void confirmReplacement(std::function<void()> action) {
        if (!dirty) { action(); return; }
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, TRANS("Discard edits"),
            TRANS("Discard the unsaved preset edits?"), TRANS("Discard edits"), TRANS("Cancel"), this,
            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<SmkPresetEditor>(this), action = std::move(action)](int ok) {
                if (ok && safe) action();
            }));
    }
    void loadFile(const juce::File& file) {
        juce::MemoryBlock bytes;
        if (file.getFileExtension().equalsIgnoreCase(".mkcII") && file.getSize() == Preset::fileSize && file.loadFileAsData(bytes)) {
            if (auto parsed = decode(bytes)) {
                preset = std::move(parsed);
                source = file;
                clearDraft();
                slot = 0;
                bank.setSelectedId(1, juce::dontSendNotification);
                status.setText(TRANS("Select a pad or knob to edit its message."), juce::dontSendNotification);
                refresh();
                return;
            }
        }
        status.setText(TRANS("Could not read this SMK-25 II preset."), juce::dontSendNotification);
    }
    void choose(bool writing) {
        auto start = source;
        if (writing) start = source == juce::File{}
            ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Studio.mkcII")
            : source.getSiblingFile(source.getFileNameWithoutExtension() + "-Studio.mkcII");
        chooser = std::make_unique<juce::FileChooser>(writing ? save.getButtonText() : open.getButtonText(), start, "*.mkcII");
        using Browser = juce::FileBrowserComponent;
        const auto flags = writing ? Browser::saveMode | Browser::warnAboutOverwriting : Browser::openMode;
        chooser->launchAsync(flags | Browser::canSelectFiles, [safe = juce::Component::SafePointer<SmkPresetEditor>(this), writing](const auto& fc) {
            if (!safe) return;
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            if (writing && safe->preset) {
                auto& bytes = safe->preset->bytes();
                if (!file.getFileExtension().equalsIgnoreCase(".mkcII")) {
                    safe->status.setText(TRANS("Use the .mkcII file extension."), juce::dontSendNotification);
                    return;
                }
                if (file.replaceWithData(bytes.data(), bytes.size())) {
                    safe->source = file;
                    safe->clearDraft();
                    safe->status.setText(TRANS("Preset file saved."), juce::dontSendNotification);
                    safe->refresh();
                } else safe->status.setText(TRANS("Could not save the preset file."), juce::dontSendNotification);
            } else safe->confirmReplacement([safe, file] { if (safe) safe->loadFile(file); });
        });
    }
    void confirmApply() {
        if (!preset || destination.isEmpty()) return;
        const auto bindings = preset->studioBindings(bank.getSelectedId() - 1);
        if (!bindings) {
            status.setText(TRANS("Choose a unique channel and number for each note or CC in this bank."), juce::dontSendNotification);
            return;
        }
        const auto msg = destinationName + "\n" + TRANS("Replace this input's Studio assignments with the notes and CCs in this bank? Other pad modes will be unassigned.");
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, apply.getButtonText(), msg,
            TRANS("Apply"), TRANS("Cancel"), this, juce::ModalCallbackFunction::create(
                [safe = juce::Component::SafePointer<SmkPresetEditor>(this), id = destination, map = *bindings](int ok) {
                    if (!ok || !safe) return;
                    bool applied = safe->applyBindings && safe->applyBindings(map, id);
                    safe->status.setText(applied ? TRANS("Studio input assignments saved.") : TRANS("The selected MIDI input is unavailable."), juce::dontSendNotification);
                }));
    }
};
