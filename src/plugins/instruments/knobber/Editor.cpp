#include "Lead.h"
#include "../../StudioPluginEditor.h"

namespace knobber {
namespace {
using State = juce::AudioProcessorValueTreeState;
using namespace juce;

class Dial final : public Component {
  public:
    Dial(State &state, const Control &c) {
        name.setText(c.name, dontSendNotification);
        name.setFont(FontOptions(12.5f));
        name.setJustificationType(Justification::centred);
        addAndMakeVisible(name);
        if (*c.labels) {
            menu.addItemList(StringArray::fromTokens(c.labels, "|", ""), 1);
            menu.setTitle(c.name);
            addAndMakeVisible(menu);
            choice = std::make_unique<State::ComboBoxAttachment>(state, c.id, menu);
        } else {
            knob.setTitle(c.name);
            knob.setTooltip(c.name);
            knob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(Slider::TextBoxBelow, false, 70, 17);
            knob.setNumDecimalPlacesToDisplay(c.discrete ? 0 : 2);
            addAndMakeVisible(knob);
            slider = std::make_unique<State::SliderAttachment>(state, c.id, knob);
        }
    }
    void resized() override {
        auto area = getLocalBounds().reduced(2, 1);
        name.setBounds(area.removeFromTop(26));
        if (choice)
            menu.setBounds(area.withSizeKeepingCentre(area.getWidth(), 26));
        else
            knob.setBounds(area);
    }

  private:
    Label name;
    ComboBox menu;
    Slider knob;
    std::unique_ptr<State::ComboBoxAttachment> choice;
    std::unique_ptr<State::SliderAttachment> slider;
};

class Section final : public Component {
  public:
    Section(State &state, int index, const ostfstudio::console::Finish &theme)
        : title(sectionNames[index]), panel(theme.panel), accent(theme.accent) {
        for (auto &c : controls)
            if (c.section == index) {
                auto *dial = dials.add(new Dial(state, c));
                addAndMakeVisible(dial);
            }
    }
    int preferredHeight(int width) const {
        return 44 + rowCount(width) * rowHeight;
    }
    void resized() override {
        const int rows = rowCount(getWidth());
        const int columns = (dials.size() + rows - 1) / rows;
        const int cell = (getWidth() - 16) / columns;
        for (int i = 0; i < dials.size(); ++i)
            dials[i]->setBounds(8 + (i % columns) * cell, 36 + (i / columns) * rowHeight, cell, rowHeight - 4);
    }
    void paint(Graphics &g) override {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(panel.darker(.45f));
        g.fillRoundedRectangle(area, 7);
        g.setColour(panel.brighter(.18f));
        g.drawRoundedRectangle(area, 7, 1);
        g.setColour(accent);
        g.setFont(FontOptions(14, Font::bold));
        g.drawText(title, 13, 5, getWidth() - 26, 26, Justification::centredLeft);
    }

  private:
    static constexpr int rowHeight = 93;
    int rowCount(int width) const {
        const int capacity = std::max(1, (width - 16) / 97);
        return std::max(1, (dials.size() + capacity - 1) / capacity);
    }
    String title;
    Colour panel, accent;
    OwnedArray<Dial> dials;
};

class Sections final : public Component {
  public:
    Sections(State &state, const ostfstudio::console::Finish &face) {
        for (int i = 0; i < static_cast<int>(std::size(sectionNames)); ++i) {
            auto *section = cards.add(new Section(state, i, face));
            addChildComponent(section);
        }
    }
    void show(int page, int width) {
        constexpr int pages[] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4};
        const int columnWidth = (width - 12) / 2;
        int heights[2]{};
        for (int i = 0; i < cards.size(); ++i) {
            auto *card = cards[i];
            card->setVisible(pages[i] == page);
            if (pages[i] != page)
                continue;
            if ((i == 2 || i == 5) && heights[0] && heights[1]) {
                const int y = std::max(heights[0], heights[1]);
                const int height = card->preferredHeight(width);
                card->setBounds(0, y, width, height);
                heights[0] = heights[1] = y + height + 9;
                continue;
            }
            const int col = heights[0] <= heights[1] ? 0 : 1;
            const int height = card->preferredHeight(columnWidth);
            card->setBounds(col * (columnWidth + 12), heights[col], columnWidth, height);
            heights[col] += height + 9;
        }
        setSize(width, std::max(heights[0], heights[1]));
    }

  private:
    OwnedArray<Section> cards;
};

class Editor final : public ostfstudio::StudioPluginEditor, private Timer {
  public:
    explicit Editor(Lead &p)
        : StudioPluginEditor(p), lead(p), sections(p.state, face), mpeAttachment(p.state, "mpe", mpe) {
        addAndMakeVisible(ccButton); ccButton.onClick=[this]{lead.ccMap.open(ccButton);};
        addAndMakeVisible(genre);
        addAndMakeVisible(preset);
        addAndMakeVisible(quality);
        addAndMakeVisible(mpe);
        genre.setTitle("Preset genre");
        preset.setTitle("Factory preset");
        quality.setTitle("Voice quality");
        quality.addItemList({"Original rate", "2x oversampling"}, 1);
        qualityAttachment = std::make_unique<State::ComboBoxAttachment>(p.state, "quality", quality);
        mpe.setButtonText("MPE");
        for (auto &program : programs)
            categories.addIfNotAlreadyThere(program.genre);
        genre.addItemList(categories, 1);
        genre.onChange = [this] {
            fillPresets(genre.getText());
            if (preset.getNumItems() > 0)
                preset.setSelectedItemIndex(0, sendNotificationSync);
        };
        preset.onChange = [this] {
            if (preset.getSelectedId() > 0)
                lead.setCurrentProgram(preset.getSelectedId() - 1);
        };
        const StringArray titles{"Oscillators", "FM / Table / Filter", "Modulation", "Performance",
                                 "Effects"};
        for (int i = 0; i < titles.size(); ++i) {
            auto *tab = tabs.add(new TextButton(titles[i]));
            tab->onClick = [this, i] { selectPage(i); };
            addAndMakeVisible(tab);
        }
        addAndMakeVisible(view);
        view.setScrollBarsShown(true, false);
        view.setViewedComponent(&sections, false);
        setResizable(true, true);
        setResizeLimits(760, 460, 1600, 1200);
        setSize(1040, 690);
        selectPage(0);
        timerCallback();
        startTimerHz(8);
    }
    void paint(Graphics &g) override {
        StudioPluginEditor::paint(g);
        g.setColour(Colour(face.accent));
        g.setFont(FontOptions(23, Font::bold));
        g.drawFittedText("SUPER KNOBBER LEAD", 16, 10, std::min(265, getWidth() / 4) - 6, 36,
                         Justification::centredLeft, 1);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(14, 10);
        auto bar = area.removeFromTop(36);
        bar.removeFromLeft(std::min(265, getWidth() / 4));
        const int menuWidth = (bar.getWidth() - 186) / 2;
        genre.setBounds(bar.removeFromLeft(menuWidth).reduced(3, 3));
        preset.setBounds(bar.removeFromLeft(menuWidth).reduced(3, 3));
        quality.setBounds(bar.removeFromLeft(124).reduced(3, 3));
        mpe.setBounds(bar);
        area.removeFromTop(8);
        auto nav = area.removeFromTop(30);
        ccButton.setBounds(nav.removeFromRight(80).reduced(2,0));
        const int width = nav.getWidth() / tabs.size();
        for (auto *tab : tabs)
            tab->setBounds(nav.removeFromLeft(width).reduced(2, 0));
        area.removeFromTop(12);
        view.setBounds(area);
        sections.show(page, area.getWidth() - view.getScrollBarThickness() - 2);
    }

  private:
    juce::TextButton ccButton{"MIDI CC"};
    void selectPage(int next) {
        page = next;
        for (int i = 0; i < tabs.size(); ++i)
            tabs[i]->setToggleState(i == page, dontSendNotification);
        resized();
        view.setViewPosition(0, 0);
    }
    void fillPresets(const String &category) {
        preset.clear(dontSendNotification);
        for (int i = 0; i < lead.getNumPrograms(); ++i)
            if (category == programs[i].genre)
                preset.addItem(programs[i].name, i + 1);
    }
    void timerCallback() override {
        const int current = lead.getCurrentProgram();
        if (preset.getSelectedId() == current + 1)
            return;
        const String category = programs[current].genre;
        genre.setSelectedItemIndex(categories.indexOf(category), dontSendNotification);
        fillPresets(category);
        preset.setSelectedId(current + 1, dontSendNotification);
    }
    Lead &lead;
    Sections sections;
    Viewport view;
    ComboBox genre, preset, quality;
    ToggleButton mpe;
    StringArray categories;
    std::unique_ptr<State::ComboBoxAttachment> qualityAttachment;
    State::ButtonAttachment mpeAttachment;
    OwnedArray<TextButton> tabs;
    int page = 0;
};
} // namespace
juce::AudioProcessorEditor *Lead::createEditor() { return new Editor(*this); }
} // namespace knobber
