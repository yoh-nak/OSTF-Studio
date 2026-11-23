#pragma once
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class SongBasicsPanel final : public juce::Component {
public:
    enum Field { title, genre, mood, key, scale, tempo, meter, count };
    std::function<void(Field, juce::String, int)> commit;
    std::function<void()> moveDock, advanced;
    static constexpr const char* scaleNames[]{"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian"};
    static constexpr int scaleSteps[][7]{{0,2,4,5,7,9,11}, {0,2,3,5,7,9,10}, {0,1,3,5,7,8,10},
        {0,2,4,6,7,9,11}, {0,2,4,5,7,9,10}, {0,2,3,5,7,8,10}, {0,1,3,5,6,8,10}};
    SongBasicsPanel() {
        addAndMakeVisible(heading);
        heading.setText(TRANS("Song defaults"), juce::dontSendNotification);
        heading.setFont(juce::FontOptions(15.f, juce::Font::bold));
        addAndMakeVisible(move);
        addAndMakeVisible(more);
        move.onClick = [this] { moveDock(); };
        more.onClick = [this] { advanced(); };
        for (int i = 0; i < count; ++i) {
            labels[i].setText(juce::translate(names[i]), juce::dontSendNotification);
            labels[i].setFont(juce::FontOptions(13.f));
            labels[i].setColour(juce::Label::textColourId, juce::Colour(0xffb6c8d4));
            addAndMakeVisible(labels[i]);
            if (i == key || i == scale) {
                auto& box = i == key ? keys : scales;
                box.setTitle(juce::translate(names[i]));
                box.setComponentID(i == key ? "song-base-key" : "song-base-scale");
                box.addItem(TRANS("Unspecified"), 1);
                if (i == key) box.addItem(TRANS("Atonal"), 2);
                for (int root = 0; root < 12; ++root) {
                    juce::PopupMenu modes;
                    for (int m = 0; m < (i == key ? 2 : 7); ++m) {
                        auto name = ostfstudio::chromaticNames()[root] + " " + (i == key ? (m == 0 ? "major" : "minor") : scaleNames[m]);
                        modes.addItem(10 + 8 * root + m, name);
                    }
                    box.getRootMenu()->addSubMenu(ostfstudio::chromaticNames()[root], modes);
                }
                box.onChange = [this, i] {
                    auto& selected = i == key ? keys : scales;
                    if (commit) commit(Field(i), selected.getText(), selected.getSelectedId());
                };
                addAndMakeVisible(box);
            } else {
                auto& value = values[i];
                value.setTitle(juce::translate(names[i]));
                value.setEditable(true, true);
                value.setColour(juce::Label::backgroundColourId, juce::Colour(0xff182932));
                value.setFont(juce::FontOptions(15.f));
                value.onTextChange = [this, i] { if (commit) commit(Field(i), values[i].getText().trim(), 0); };
                addAndMakeVisible(value);
            }
        }
    }
    void sync(const ostfstudio::OstfpPreview& song) {
        const auto metadata = song.songInformation();
        auto set = [&](Field f, juce::String value) { values[f].setText(value, juce::dontSendNotification); };
        set(title, metadata.title);
        set(genre, metadata.genres.value_or(juce::StringArray{}).joinIntoString(", "));
        set(mood, metadata.moods.value_or(juce::StringArray{}).joinIntoString(", "));
        set(tempo, juce::String(song.clock.bpmAt(0), 6).trimCharactersAtEnd("0").trimCharactersAtEnd("."));
        set(meter, juce::String(song.meterBeats) + "/" + juce::String(song.beatUnit));
        const auto base = juce::JSON::parse(song.sourceScore.toString())["song"];
        auto keyData = base["key"], scaleData = base["scale"];
        const int root = ostfstudio::pitchClassOf(keyData["tonic"]);
        keys.setText(root >= 0 ? ostfstudio::chromaticNames()[root] + " " + keyData["mode"].toString()
                               : keyData["state"] == "atonal" ? TRANS("Atonal") : TRANS("Unspecified"), juce::dontSendNotification);
        auto scaleName = scaleData["label"].toString();
        if (scaleName.isEmpty() && ostfstudio::pitchClassOf(scaleData["tonic"]) >= 0) {
            scaleName = ostfstudio::chromaticNames()[ostfstudio::pitchClassOf(scaleData["tonic"])] + " " + TRANS("Custom");
        }
        scales.setText(scaleName.isEmpty() ? TRANS("Unspecified") : scaleName, juce::dontSendNotification);
    }
    static int naturalHeight(int width) { return width >= 690 ? 72 : width >= 570 ? 118 : 206; }
    void resized() override {
        auto bounds = getLocalBounds().reduced(5, 2);
        auto top = bounds.removeFromTop(24);
        move.setBounds(top.removeFromRight(64).reduced(1));
        more.setBounds(top.removeFromRight(60).reduced(1));
        heading.setBounds(top);
        const int cols = getWidth() >= 690 ? count : getWidth() >= 570 ? 4 : 2;
        const int w = bounds.getWidth() / cols;
        for (int i = 0; i < count; ++i) {
            auto cell = juce::Rectangle<int>(bounds.getX() + i % cols * w, bounds.getY() + i / cols * 44, w, 44).reduced(2, 1);
            labels[i].setBounds(cell.removeFromTop(16));
            if (i == key) keys.setBounds(cell);
            else if (i == scale) scales.setBounds(cell);
            else values[i].setBounds(cell);
        }
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff21323e));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
    }
private:
    static constexpr const char* names[]{"Title", "Genres", "Moods", "Key", "Scale", "Tempo (BPM)", "Time signature"};
    juce::Label heading;
    juce::TextButton move{TRANS("Move panel")}, more{TRANS("Details")};
    std::array<juce::Label, count> labels, values;
    juce::ComboBox keys, scales;
};
