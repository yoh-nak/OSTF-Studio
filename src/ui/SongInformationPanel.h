#pragma once
#include "ui/SongMusicFields.h"
#include <juce_gui_basics/juce_gui_basics.h>

class SongInformationPanel final : public juce::Component {
    struct Row final : juce::Component {
        ostfstudio::SongCredit original;
        juce::ComboBox role;
        juce::TextEditor name;
        juce::TextButton remove{TRANS("Remove")};
        explicit Row(const ostfstudio::SongCredit& credit) : original(credit) {
            role.setEditableText(true);
            role.addItemList({"composer", "lyricist", "arranger", "performer", "producer"}, 1);
            role.setText(credit.role, juce::dontSendNotification);
            name.setText(credit.name);
            role.setTitle(TRANS("Role"));
            name.setTitle(TRANS("Contributor"));
            for (juce::Component* child : std::array<juce::Component*, 3>{&role, &name, &remove}) addAndMakeVisible(child);
        }
        void resized() override {
            auto r = getLocalBounds().reduced(0, 2);
            role.setBounds(r.removeFromLeft(148).withTrimmedRight(5));
            remove.setBounds(r.removeFromRight(70).withTrimmedLeft(5));
            name.setBounds(r);
        }
    };
public:
    using Apply = std::function<juce::Result(const ostfstudio::SongInformation&, const ostfstudio::SongMusicEdit&)>;
    SongInformationPanel(const ostfstudio::OstfpPreview& project, Apply apply) : music_(project) {
        const auto info = project.songInformation();
        for (auto* tag : {&genres_, &moods_}) {
            metadata_.addAndMakeVisible(*tag);
            tag->setMultiLine(true, true);
            tag->setTooltip(TRANS("One tag per line"));
        }
        if (info.genres)
            genres_.setText(info.genres->joinIntoString("\n"));
        if (info.moods)
            moods_.setText(info.moods->joinIntoString("\n"));
        metadata_.addAndMakeVisible(genresLabel_);
        metadata_.addAndMakeVisible(moodsLabel_);
        genresLabel_.attachToComponent(&genres_, true);
        moodsLabel_.attachToComponent(&moods_, true);
        title_.setText(info.title);
        subtitle_.setText(info.subtitle);
        description_.setMultiLine(true, true);
        description_.setText(info.description);
        for (auto* c : std::array<juce::Component*, 6>{&title_, &subtitle_, &description_, &credits_, &add_, &hint_}) metadata_.addAndMakeVisible(*c);
        for (auto* label : {&titleLabel_, &subtitleLabel_, &descriptionLabel_, &creditsLabel_, &roleLabel_, &nameLabel_})
            metadata_.addAndMakeVisible(*label);
        titleLabel_.attachToComponent(&title_, true);
        subtitleLabel_.attachToComponent(&subtitle_, true);
        descriptionLabel_.attachToComponent(&description_, true);
        hint_.setText(TRANS("Use a role such as composer, lyricist or arranger."), juce::dontSendNotification);
        error_.setColour(juce::Label::textColourId, juce::Colour(0xffffaa8c));
        credits_.setViewedComponent(&list_, false);
        credits_.setScrollBarsShown(true, false);
        for (const auto& row : info.credits) addRow(row);
        add_.onClick = [this] {
            addRow({{}, {}, "composer", {}});
            resized();
            credits_.setViewPosition(0, list_.getHeight());
            rows_.back()->name.grabKeyboardFocus();
        };
        cancel_.onClick = [this] { close(); };
        apply_.onClick = [this, apply = std::move(apply)] {
            ostfstudio::SongInformation info{title_.getText(), subtitle_.getText(), description_.getText(), {}};
            auto tags = [](const juce::TextEditor& editor) {
                auto names = juce::StringArray::fromLines(editor.getText());
                names.removeEmptyStrings(true);
                return names;
            };
            info.genres = tags(genres_);
            info.moods = tags(moods_);
            for (const auto& row : rows_) {
                auto credit = row->original;
                credit.name = row->name.getText().trim();
                credit.role = row->role.getText().trim();
                info.credits.push_back(std::move(credit));
            }
            ostfstudio::SongMusicEdit music;
            auto result = music_.changes(music);
            if (result.failed()) tabs_.setCurrentTabIndex(1);
            else result = apply(info, music);
            if (result.wasOk()) close();
            else error_.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
        };
        addAndMakeVisible(apply_);
        addAndMakeVisible(cancel_);
        addAndMakeVisible(error_);
        tabs_.addTab(TRANS("Song information and credits"), juce::Colour(0xff26333d), &metadata_, false);
        tabs_.addTab(TRANS("Musical settings"), juce::Colour(0xff26333d), &music_, false);
        addAndMakeVisible(tabs_);
        tabs_.setTabBarDepth(32);
        setSize(650, 530);
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff242a31)); }
    void resized() override {
        auto outer = getLocalBounds().reduced(10);
        auto footer = outer.removeFromBottom(36);
        apply_.setBounds(footer.removeFromRight(90).reduced(2));
        cancel_.setBounds(footer.removeFromRight(90).reduced(2));
        error_.setBounds(footer.reduced(3));
        tabs_.setBounds(outer);
        auto r = tabs_.getLocalBounds().withTrimmedTop(tabs_.getTabBarDepth()).withPosition(0, 0).reduced(12);
        for (auto* e : {&title_, &subtitle_, &description_})
            e->setBounds(r.removeFromTop(e == &description_ ? 60 : 32).withTrimmedLeft(94).reduced(0, 2));
        for (auto* tag : {&genres_, &moods_})
            tag->setBounds(r.removeFromTop(45).withTrimmedLeft(94).reduced(0, 2));
        auto heading = r.removeFromTop(34);
        add_.setBounds(heading.removeFromRight(110).reduced(0, 3));
        creditsLabel_.setBounds(heading);
        hint_.setBounds(r.removeFromTop(23));
        auto headers = r.removeFromTop(22);
        roleLabel_.setBounds(headers.removeFromLeft(148));
        nameLabel_.setBounds(headers);
        credits_.setBounds(r);
        list_.setSize(std::max(0, r.getWidth() - credits_.getScrollBarThickness()), static_cast<int>(rows_.size()) * 32);
        auto list = list_.getLocalBounds();
        for (auto& row : rows_) row->setBounds(list.removeFromTop(32));
    }
private:
    void addRow(const ostfstudio::SongCredit& credit) {
        auto& r = rows_.emplace_back(std::make_unique<Row>(credit));
        auto* row = r.get();
        row->remove.onClick = [this, target = juce::Component::SafePointer<Row>(row)] {
            juce::MessageManager::callAsync([self = juce::Component::SafePointer<SongInformationPanel>(this), target] {
                if (!self || !target) return;
                std::erase_if(self->rows_, [&](const auto& p) { return p.get() == target.getComponent(); });
                self->resized();
            });
        };
        row->name.onTextChange = [this, row] {
            if (row->original.contributorId.isEmpty()) return;
            for (auto& other : rows_)
                if (other.get() != row && other->original.contributorId == row->original.contributorId)
                    other->name.setText(row->name.getText(), false);
        };
        list_.addAndMakeVisible(*row);
    }
    void close() {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) dialog->exitModalState(0);
    }
    juce::TextEditor title_, subtitle_, description_, genres_, moods_;
    juce::Label genresLabel_{{}, TRANS("Genres")}, moodsLabel_{{}, TRANS("Moods")};
    juce::Label titleLabel_{{}, TRANS("Song title")}, subtitleLabel_{{}, TRANS("Subtitle")},
                descriptionLabel_{{}, TRANS("Description")}, creditsLabel_{{}, TRANS("Credits")},
                roleLabel_{{}, TRANS("Role")}, nameLabel_{{}, TRANS("Contributor")}, error_, hint_;
    juce::TextButton add_{TRANS("Add credit")}, apply_{TRANS("Apply")}, cancel_{TRANS("Cancel")};
    std::vector<std::unique_ptr<Row>> rows_;
    juce::Component list_;
    juce::Viewport credits_;
    juce::Component metadata_;
    SongMusicFields music_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
};
