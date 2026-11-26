#pragma once
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class SongMusicFields final : public juce::Component {
public:
    explicit SongMusicFields(const ostfstudio::OstfpPreview& project) {
        using namespace ostfstudio;
        auto song = juce::JSON::parse(project.sourceScore.toString())["song"];
        key = song["key"]; scale = song["scale"];
        keyMode = key["mode"].toString();
        keyPc = pitchClassOf(key["tonic"]); scalePc = pitchClassOf(scale["tonic"]);
        bpm = project.clock.bpmAt(0);
        metre = juce::String(project.meterBeats) + "/" + juce::String(project.beatUnit);
        juce::StringArray notes;
        if (const auto* items = scale["intervals"].getArray()) for (auto& n : *items) notes.add(n.toString());
        intervals = notes.joinIntoString(" ");
        scaleLabel = scale["label"].toString();
        auto tonics = chromaticNames();
        tonics.insert(0, TRANS("Unspecified"));
        auto keys = tonics; keys.add(TRANS("Atonal"));
        combo("key", keys, key["state"] == "atonal" ? 13 : keyPc + 1, TRANS("Key tonic"));
        addText("mode", keyMode, TRANS("Key mode (major, minor, dorian...)"));
        combo("scale", tonics, scalePc + 1, TRANS("Scale tonic"));
        const juce::StringArray patterns {"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian",
                                           "Major pentatonic", "Minor pentatonic", "Harmonic minor", "Melodic minor"};
        const juce::StringArray degrees {"0 2 4 5 7 9 11", "0 2 3 5 7 9 10", "0 1 3 5 7 8 10", "0 2 4 6 7 9 11",
                                         "0 2 4 5 7 9 10", "0 2 3 5 7 8 10", "0 1 3 5 6 8 10", "0 2 4 7 9", "0 3 5 7 10",
                                         "0 2 3 5 7 8 11", "0 2 3 5 7 9 11"};
        auto options = patterns; options.insert(0, TRANS("Custom"));
        combo("pattern", options, degrees.indexOf(intervals) + 1, TRANS("Scale pattern"));
        addText("intervals", intervals, TRANS("Scale intervals (0-11, separated by spaces)"));
        addText("label", scaleLabel, TRANS("Scale name"));
        auto* w = this;
        auto fillPattern = [w, patterns, degrees] {
            const auto index = w->getComboBoxComponent("pattern")->getSelectedItemIndex() - 1;
            if (index < 0) return;
            auto* tonic = w->getComboBoxComponent("scale");
            if (tonic->getSelectedItemIndex() == 0)
                tonic->setSelectedItemIndex(std::clamp(w->getComboBoxComponent("key")->getSelectedItemIndex(), 1, 12), juce::dontSendNotification);
            w->getTextEditor("intervals")->setText(degrees[index], false);
            w->getTextEditor("label")->setText(tonic->getText() + " " + patterns[index], false);
        };
        w->getComboBoxComponent("pattern")->onChange = fillPattern;
        w->getComboBoxComponent("scale")->onChange = [w, fillPattern] {
            if (w->getComboBoxComponent("scale")->getSelectedItemIndex() > 0) fillPattern();
        };
        w->getTextEditor("intervals")->onTextChange = [w, degrees] {
            const auto pos = degrees.indexOf(w->getTextEditorContents("intervals").trim());
            w->getComboBoxComponent("pattern")->setSelectedItemIndex(pos + 1, juce::dontSendNotification);
        };
        addText("bpm", juce::String(bpm, 6).trimCharactersAtEnd("0").trimCharactersAtEnd("."), TRANS("Initial tempo (BPM)"));
        addText("meter", metre, TRANS("Time signature"));

    }
    juce::Result changes(ostfstudio::SongMusicEdit& edit) const {
        using namespace ostfstudio;
        auto text = [this](const char* id) { return getTextEditorContents(id).trim(); };
        int k = getComboBoxComponent("key")->getSelectedItemIndex();
        if (k != (key["state"] == "atonal" ? 13 : keyPc + 1) || (k >= 1 && k <= 12 && text("mode") != keyMode)) {
            edit.key = juce::var(new juce::DynamicObject());
            auto* o = edit.key.getDynamicObject();
            o->setProperty("state", k == 0 ? "unspecified" : k == 13 ? "atonal" : "tonal");
            if (k >= 1 && k <= 12) {
                if (text("mode").isEmpty()) { return juce::Result::fail(TRANS("Enter a tonal mode, for example major or minor.")); }
                o->setProperty("tonic", spelledClass(k - 1)); o->setProperty("mode", text("mode"));
            }
        }
        const int s = getComboBoxComponent("scale")->getSelectedItemIndex() - 1;
        if (s != scalePc || text("intervals") != intervals || text("label") != scaleLabel) {
            edit.removeScale = s < 0;
            if (s >= 0) {
                juce::Array<juce::var> steps;
                for (const auto& token : juce::StringArray::fromTokens(text("intervals"), " ,", "")) {
                    if (token.isEmpty()) continue;
                    if (!token.containsOnly("0123456789") || token.length() > 2 || token.getIntValue() > 11 || steps.contains(token.getIntValue())) {
                        return juce::Result::fail(TRANS("Enter distinct scale intervals from 0 to 11, including 0."));
                    }
                    steps.add(token.getIntValue());
                }
                if (!steps.contains(0)) { return juce::Result::fail(TRANS("Enter distinct scale intervals from 0 to 11, including 0.")); }
                edit.scale = juce::var(new juce::DynamicObject()); auto* o = edit.scale.getDynamicObject();
                o->setProperty("id", scale.hasProperty("id") ? scale["id"].toString() : "scale-" + juce::Uuid().toString());
                o->setProperty("tonic", spelledClass(s)); o->setProperty("intervals", steps);
                o->setProperty("label", text("label").isEmpty() ? chromaticNames()[s] : text("label"));
            }
        }
        double value;
        if (!readNumber(text("bpm"), value) || value <= 0 || value > 1000) { return juce::Result::fail(TRANS("Enter a tempo above zero and at most 1000 BPM.")); }
        if (std::abs(value - bpm) > 0.0000005) edit.bpm = value;
        if (text("meter") != metre) {
            const auto parts = juce::StringArray::fromTokens(text("meter"), "/", "");
            if (parts.size() != 2 || !parts[0].trim().containsOnly("0123456789") || !parts[1].trim().containsOnly("0123456789")
                || (edit.numerator = parts[0].getIntValue()) <= 0 || (edit.denominator = parts[1].getIntValue()) <= 0) {
                return juce::Result::fail(TRANS("Enter a time signature such as 4/4 or 6/8."));
            }
        }

        return juce::Result::ok();
    }
    static bool readNumber(const juce::String& input, double& value) {
        const auto str = input.toStdString();
        char* end = nullptr;
        value = std::strtod(str.c_str(), &end);
        return end != str.c_str() && *end == 0 && std::isfinite(value);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
        for (int i = 0; i < fields.size(); ++i) {
            auto row = r.removeFromTop(39);
            labels[i]->setBounds(row.removeFromLeft(std::min(212, row.getWidth() / 2)));
            fields[i]->setBounds(row.reduced(3));
        }
    }
private:
    juce::ComboBox* getComboBoxComponent(const char* id) const { return static_cast<juce::ComboBox*>(findChildWithID(id)); }
    juce::TextEditor* getTextEditor(const char* id) const { return static_cast<juce::TextEditor*>(findChildWithID(id)); }
    juce::String getTextEditorContents(const char* id) const { return getTextEditor(id)->getText(); }
    void addField(juce::Component* c, const char* id, const juce::String& caption) {
        fields.add(c);
        c->setComponentID(id); c->setTitle(caption);
        auto* label = labels.add(new juce::Label({}, caption));
        addAndMakeVisible(label); addAndMakeVisible(c);
    }
    void addText(const char* id, const juce::String& initial, const juce::String& caption) {
        auto* field = new juce::TextEditor();
        field->setText(initial, false);
        addField(field, id, caption);
    }
    void combo(const char* id, const juce::StringArray& options, int selected, const juce::String& caption) {
        auto* field = new juce::ComboBox();
        field->addItemList(options, 1);
        field->setSelectedItemIndex(selected, juce::dontSendNotification);
        addField(field, id, caption);
    }
    juce::OwnedArray<juce::Component> fields;
    juce::OwnedArray<juce::Label> labels;
    juce::var key, scale;
    juce::String keyMode, metre, intervals, scaleLabel;
    int keyPc = -1, scalePc = -1;
    double bpm = 120;
};
