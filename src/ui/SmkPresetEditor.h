#pragma once
#include "ui/Smk25Bluetooth.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SmkPresetEditor final : public juce::Component, public juce::FileDragAndDropTarget {
public:
    using Preset = ostfstudio::Smk25Preset;
    using PresetReadbacks = std::array<std::optional<Preset>, 8>;
    void showVerifiedSetup(const PresetReadbacks& slots, int displayed) {
        showingRecommended = false;
        verifiedSlots = slots;
        if (displayed >= 0 && displayed < int(slots.size()) && slots[displayed])
            displayVerifiedPreset(displayed);
    }
    void loadRecommended() {
        confirmReplacement([this] {
            for (int i = 0; i < 8; ++i) recommendedSlots[i] = Preset::studioPreset().forStudioSlot(i);
            showingRecommended = true;
            preset = recommendedSlots[deviceSlot.getSelectedId() - 1];
            source = {};
            clearDraft();
            status.setText(TRANS("Recommended mixer layout loaded for all eight presets."), juce::dontSendNotification);
            refresh();
        });
    }
#if JUCE_WINDOWS && defined(_MSC_VER)
    void beginRecommended() {
        if (bluetooth.getSelectedId() > 0) { confirmSetup(); return; }
        if (devices.size() > 1 && isEnabled()) {
            status.setText(TRANS("Choose a Bluetooth device"), juce::dontSendNotification);
            return;
        }
        setupAfterScan = true;
        if (isEnabled()) scanDevices();
    }
#endif
    void visibilityChanged() override {
#if JUCE_WINDOWS && defined(_MSC_VER)
        if (!scanned && isShowing()) { scanned = true; scanDevices(); }
#endif
    }
    explicit SmkPresetEditor(juce::PropertiesFile& settings) : prefs(settings) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&create, &open, &save, &filename, &status, &bank,
                  &mode, &channel, &number, &modeLabel, &channelLabel, &numberLabel, &colourLabel}) addAndMakeVisible(c);
        addAndMakeVisible(load);
        load.onClick = [this] { loadRecommended(); };
        addAndMakeVisible(legend);
        for (int i = 0; i < int(pages.size()); ++i) {
            auto& page = pages[i];
            const int first = (i % 8) * 8 + 1;
            page.setButtonText("P" + juce::String(i % 8 + 1) + (i < 8 ? " A" : " B"));
            page.setTooltip(TRANS("Tracks") + " " + juce::String(first) + "-" + juce::String(first + 7)
                            + "  /  " + (i < 8 ? TRANS("Volume") : TRANS("Pan")));
            addAndMakeVisible(page);
            page.onClick = [this, i] {
                const int next = i % 8;
                const auto choosePage = [this, next, i] {
                    if (next != deviceSlot.getSelectedId() - 1) {
                        source = {};
                        clearDraft();
                        preset = showingRecommended ? recommendedSlots[next] : verifiedSlots[next];
                        if (!preset) preset = Preset::studioPreset().forStudioSlot(next);
                    }
                    deviceSlot.setSelectedId(next + 1, juce::dontSendNotification);
                    bank.setSelectedId(i / 8 + 1, juce::dontSendNotification);
                    refresh();
                };
                if (next != deviceSlot.getSelectedId() - 1) confirmReplacement(choosePage);
                else choosePage();
            };
        }
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
        addAndMakeVisible(deviceSlot);
        deviceSlot.setTitle(TRANS("Device preset slot"));
        for (int i = 1; i <= 8; ++i) deviceSlot.addItem(TRANS("Preset") + " " + juce::String(i), i);
        deviceSlot.setSelectedId(1, juce::dontSendNotification);
        deviceSlot.onChange = [this] {
            const int index = deviceSlot.getSelectedId() - 1;
            if (index >= 0 && index < int(verifiedSlots.size()) && verifiedSlots[index] && !dirty)
                displayVerifiedPreset(index);
        };
        for (int i = 0; i < 24; ++i) {
            addAndMakeVisible(controls[i]);
            controls[i].onClick = [this, i] { slot = i; refresh(); };
        }
#if JUCE_WINDOWS && defined(_MSC_VER)
        for (juce::Component* c : std::initializer_list<juce::Component*>{&scan, &bluetooth, &deviceSlot, &readDevice, &writeDevice, &setup}) addAndMakeVisible(c);
        bluetooth.setTitle(TRANS("Bluetooth settings device"));
        bluetooth.setTextWhenNothingSelected(TRANS("Choose a Bluetooth device"));
        bluetooth.onChange = [this] { verifiedSlots = {}; refresh(); };
        setup.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff17634f));
        setup.onClick = [this] { beginRecommended(); };
        scan.onClick = [this] { scanDevices(); };
        readDevice.onClick = [this] { confirmReplacement([this] { startTransfer(false); }); };
        writeDevice.onClick = [this] { confirmWrite(); };
#endif
        create.onClick = [this] { confirmReplacement([this] { newPreset(); }); };
        open.onClick = [this] { choose(false); };
        save.onClick = [this] { choose(true); };
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
        if (destination == id && destinationName == name) return;
        destination = std::move(id);
        destinationName = std::move(name);

        refresh();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16, 12);
        auto actions = area.removeFromTop(32);
        load.setBounds(actions.removeFromLeft(actions.getWidth() / 2).reduced(2));
#if JUCE_WINDOWS && defined(_MSC_VER)
        setup.setBounds(actions.reduced(2));
#endif
        for (int b = 0; b < 2; ++b) {
            auto row = area.removeFromTop(25);
            for (int col = 0; col < 8; ++col) pages[b * 8 + col].setBounds(row.removeFromLeft(row.getWidth() / (8 - col)).reduced(1));
        }
        legend.setBounds(area.removeFromTop(28));
        auto top = area.removeFromTop(33);
        create.setBounds(top.removeFromLeft(80).reduced(1));
        open.setBounds(top.removeFromLeft(142).reduced(1));
        save.setBounds(top.removeFromLeft(148).reduced(1));
        bank.setVisible(false);
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
        deviceSlot.setVisible(false);
        readDevice.setBounds(deviceRow.removeFromLeft(118).reduced(2));
        writeDevice.setBounds(deviceRow.removeFromLeft(140).reduced(2));
#else
        deviceSlot.setVisible(false);
#endif
        auto bottom = area.removeFromBottom(40);
        status.setBounds(bottom);
    }

private:
    PresetReadbacks verifiedSlots{}, recommendedSlots{};
    bool showingRecommended = false;
    juce::TextButton load{TRANS("Load recommended mixer layout")};
    std::array<juce::TextButton,16> pages;
    juce::Label legend;
    void displayVerifiedPreset(int index) {
        preset = verifiedSlots[index];
        source = {};
        clearDraft();
        refresh();
    }
    juce::PropertiesFile& prefs;
    std::optional<Preset> preset;
    juce::File source;
    bool dirty = false;
    int slot = 0;
    juce::String destination, destinationName;
    juce::TextButton create{TRANS("New preset")}, open{TRANS("Open preset...")}, save{TRANS("Save preset as...")};
    juce::Label filename, status, modeLabel, channelLabel, numberLabel, colourLabel;
    juce::ComboBox bank, mode, channel, deviceSlot;
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
    juce::ComboBox bluetooth;
    std::vector<ostfstudio::smkble::Device> devices;
    juce::ThreadPool jobs{1};

    juce::TextButton setup{TRANS("Send Studio recommended setup")};
    bool setupAfterScan = false;
    bool scanned = false;
    void confirmSetup() {
        const int deviceIndex = bluetooth.getSelectedId() - 1;
        if (deviceIndex < 0 || deviceIndex >= int(devices.size())) return;
        const auto device = devices[size_t(deviceIndex)];
        const auto port = destination;
        const auto displayed = deviceSlot.getSelectedId() - 1;
        auto explanation = bluetooth.getText() + " / " + destinationName + "\n" +
            TRANS("Replace the pads and knobs in presets 1-8, banks A and B? Each changed slot is backed up and read back. Keyboard settings are preserved.");
        if (dirty) explanation += "\n" + TRANS("The verified preset will replace the unsaved edits in this editor.");
        auto complete = juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<SmkPresetEditor>(this), device, port, displayed](int choice) {
                if (choice == 0 || !safe || !safe->isEnabled() || safe->destination != port) return;
                safe->setEnabled(false);
                safe->jobs.addJob([safe, device, port, displayed] {
                    PresetReadbacks readbacks{};
                    int verified = 0;
                    juce::String failure;
                    for (int i = 0; i < 8; ++i) {
                        juce::MessageManager::callAsync([safe,i] {
                            if (safe) safe->status.setText(TRANS("Setting up preset") + " " + juce::String(i + 1) + "/8", juce::dontSendNotification);
                        });
                        const auto result = ostfstudio::smkble::transfer(device, i, {}, true);
                        if (result.message.isNotEmpty() || !result.preset) {
                            failure = result.message.isNotEmpty() ? result.message : TRANS("No verified preset was returned.");
                            break;
                        }
                        readbacks[i] = result.preset;
                        ++verified;
                    }
                    juce::MessageManager::callAsync([safe, verified, failure, port, readbacks = std::move(readbacks), displayed] {
                        if (!safe) return;
                        safe->setEnabled(true);
                        safe->showVerifiedSetup(readbacks, displayed);
                        auto message = TRANS("Verified presets") + " " + juce::String(verified) + "/8";
                        if (failure.isNotEmpty()) message += "  #" + juce::String(verified + 1) + " " + failure;
                        else message += "  " + TRANS("SMK-25 II mixer shortcuts");
                        safe->status.setText(message, juce::dontSendNotification);
                        safe->status.setTooltip(message);
                        safe->refresh();
                    });
                });
            });
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, setup.getButtonText(), explanation,
            TRANS("Send"), TRANS("Cancel"), this, complete);
    }
    void scanDevices() {
        verifiedSlots = {};
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
                if (std::exchange(safe->setupAfterScan, false) && safe->bluetooth.getSelectedId() > 0)
                    safe->confirmSetup();
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
        const auto port = destination;
        verifiedSlots = {};
        setEnabled(false);
        status.setText(writing ? TRANS("Backing up and writing the device preset...") : TRANS("Reading the device preset..."), juce::dontSendNotification);
        jobs.addJob([safe = juce::Component::SafePointer<SmkPresetEditor>(this), device = devices[i], index, port, data = writing ? preset : std::nullopt] {
            auto result = ostfstudio::smkble::transfer(device, index, data);
            juce::MessageManager::callAsync([safe, result = std::move(result), index, port, writing = data.has_value()] {
                if (!safe) return;
                safe->setEnabled(true);
                auto message = result.message;
                if (message.isEmpty()) {
                    if (!writing && result.preset) {
                        safe->showingRecommended = false;
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
        setup.setTooltip(TRANS("Set up all 8 presets..."));
        readDevice.setEnabled(deviceReady);
        writeDevice.setEnabled(deviceReady && preset.has_value());
#endif
        const int page = deviceSlot.getSelectedId() - 1;
        for (int i = 0; i < int(pages.size()); ++i) pages[i].setToggleState(i == b * 8 + page, silent);
        const auto range = TRANS("Tracks") + " " + juce::String(page * 8 + 1) + "-" + juce::String(page * 8 + 8);
        const auto mapping = b == 0 ? TRANS("Knobs: volume / Pads: mute, solo") : TRANS("Knobs: pan / Pads: select track, transport and cue");
        legend.setText(range + "   /   " + TRANS("Recommended") + "   " + mapping, silent);
        bank.setEnabled(preset.has_value());
        filename.setText((source == juce::File{} ? TRANS("Studio preset") : source.getFileName()) + (dirty ? " *" : ""), silent);
        save.setEnabled(preset.has_value());
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
        showingRecommended = false;
        verifiedSlots[deviceSlot.getSelectedId() - 1].reset();
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
        showingRecommended = false;
        verifiedSlots = {};
        preset = Preset::studioPreset().forStudioSlot(deviceSlot.getSelectedId() - 1);
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
                verifiedSlots = {};
                showingRecommended = false;
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

};
