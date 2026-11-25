#pragma once

#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class LiveTrackSelector final : public juce::Component {
public:
    std::function<void(std::size_t)> selectTrack;

    LiveTrackSelector() {
        for (auto* component : std::initializer_list<juce::Component*>{&all_, &favourites_, &editing_, &view_})
            addAndMakeVisible(component);
        all_.onClick = [this] { setFilter(false); };
        favourites_.onClick = [this] { setFilter(true); };
        all_.setTitle(TRANS("All tracks"));
        favourites_.setTitle(TRANS("Favourites"));
        for (auto* button : {&all_, &favourites_})
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff36616a));
        view_.setViewedComponent(&tiles_, false);
        view_.setScrollBarsShown(true, false);
        view_.setScrollBarThickness(9);
        tiles_.addChildComponent(empty_);
        empty_.setText(TRANS("Star a track in All tracks to show it here."), juce::dontSendNotification);
        empty_.setMinimumHorizontalScale(1.f);
    }

    void openDocument(const juce::String& id, juce::PropertySet& settings) {
        settings_ = &settings;
        settingsKey_ = id.isEmpty() ? juce::String{} : "liveTrackFilter" + juce::String::toHexString(id.hashCode64());
        starred_.clear();
        onlyStarred_ = false;
        if (settingsKey_.isNotEmpty())
            if (auto saved = settings.getXmlValue(settingsKey_)) {
                onlyStarred_ = saved->getBoolAttribute("onlyStarred");
                for (auto* item : saved->getChildWithTagNameIterator("TRACK"))
                    starred_.addIfNotAlreadyThere(item->getStringAttribute("id"));
            }
        view_.setViewPosition(0, 0);
        updateFilter();
    }

    void bind(const ostfstudio::OstfpPreview& song, std::optional<std::size_t> selected) {
        const auto count = song.trackLanes.size();
        while (buttons_.size() < count) {
            const auto index = buttons_.size();
            auto& tile = *buttons_.emplace_back(std::make_unique<Tile>());
            tiles_.addAndMakeVisible(tile);
            tile.name.onClick = [this, index] { if (selectTrack) selectTrack(index); };
            tile.star.onClick = [this, index] {
                const auto& id = buttons_[index]->id;
                if (starred_.contains(id)) starred_.removeString(id);
                else starred_.add(id);
                updateFilter();
                save();
            };
        }
        buttons_.resize(count);
        juce::String current;
        for (std::size_t i = 0; i < count; ++i) {
            auto& tile = *buttons_[i];
            tile.id = song.chordVoicingTrack == i ? "global:voicing" : "track:" + song.trackIds[int(i)];
            const auto name = juce::String::fromUTF8(song.trackLanes[i].label);
            tile.name.setButtonText(name);
            tile.name.setTitle(name);
            tile.name.setTooltip(name);
            tile.star.setTitle(TRANS("Favourite track") + " / " + name);
            tile.star.setTooltip(tile.star.getTitle());
            tile.accent = juce::Colour(song.trackColour(i));
            tile.name.setToggleState(selected == i, juce::dontSendNotification);
            if (selected == i) current = name;
        }
        editing_.setText(current.isEmpty() ? juce::String{} : TRANS("Editing track") + "  " + current, juce::dontSendNotification);
        updateFilter();
    }

    int preferredHeight(int width) const {
        const int rows = (int(buttons_.size()) + columns(width) - 1) / columns(width);
        return headerHeight(width) + 2 + rowHeight * std::clamp(rows, 1, 2);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff19242a));
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto header = bounds.removeFromTop(30);
        all_.setBounds(header.removeFromLeft(std::min(98, header.getWidth() / 2)).reduced(2, 2));
        favourites_.setBounds(header.removeFromLeft(std::min(138, header.getWidth())).reduced(2, 2));
        if (headerHeight(getWidth()) > 30) header = bounds.removeFromTop(22);
        editing_.setBounds(header.reduced(4, 0));
        view_.setBounds(bounds);
        layoutTiles();
    }

private:
    class Star final : public juce::TextButton {
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
            if (highlighted || down || hasKeyboardFocus(false)) {
                g.setColour(juce::Colours::white.withAlpha(.12f));
                g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 3.f);
            }
            juce::Path star;
            star.addStar(getLocalBounds().toFloat().getCentre(), 5, 4.f, 9.f);
            g.setColour(juce::Colour(getToggleState() ? 0xffffce79 : 0xff98adb4));
            if (getToggleState()) g.fillPath(star);
            else g.strokePath(star, juce::PathStrokeType(1.4f));
        }
    };
    struct Tile final : juce::Component {
        Tile() {
            addAndMakeVisible(name);
            addAndMakeVisible(star);
            name.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff36616a));
        }
        void resized() override {
            auto r = getLocalBounds().reduced(1);
            r.removeFromLeft(5);
            star.setBounds(r.removeFromRight(28));
            name.setBounds(r);
        }
        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colour(0xff26353b));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.f);
            g.setColour(accent);
            g.fillRoundedRectangle(1.f, 4.f, 3.f, float(getHeight()-8), 1.5f);
        }
        juce::String id;
        juce::Colour accent;
        juce::TextButton name;
        Star star;
    };

    void setFilter(bool favourites) {
        onlyStarred_ = favourites;
        view_.setViewPosition(0, 0);
        updateFilter();
        save();
    }
    void save() const {
        if (!settings_ || settingsKey_.isEmpty()) return;
        juce::XmlElement state("LIVE_TRACKS");
        state.setAttribute("onlyStarred", onlyStarred_);
        for (const auto& id : starred_)
            state.createNewChildElement("TRACK")->setAttribute("id", id);
        settings_->setValue(settingsKey_, &state);
    }
    void updateFilter() {
        int count = 0;
        for (auto& tile : buttons_) {
            const bool pinned = starred_.contains(tile->id);
            tile->star.setToggleState(pinned, juce::dontSendNotification);
            tile->setVisible(!onlyStarred_ || pinned);
            tile->repaint();
            if (pinned) ++count;
        }
        all_.setToggleState(!onlyStarred_, juce::dontSendNotification);
        favourites_.setToggleState(onlyStarred_, juce::dontSendNotification);
        favourites_.setButtonText(TRANS("Favourites") + "  " + juce::String(count));
        empty_.setVisible(onlyStarred_ && count == 0);
        layoutTiles();
    }
    static int columns(int width) { return std::max(1, (width-10) / 164); }
    static int headerHeight(int width) { return width < 425 ? 52 : 30; }
    void layoutTiles() {
        const int cols = columns(view_.getWidth());
        const int count = int(std::count_if(buttons_.begin(), buttons_.end(), [](const auto& tile) { return tile->isVisible(); }));
        const int height = std::max(1, (count + cols - 1) / cols) * rowHeight;
        const int width = std::max(0, view_.getWidth() - (height > view_.getHeight() ? view_.getScrollBarThickness() : 0));
        const int tileWidth = std::min(210, width / cols);
        tiles_.setSize(width, height);
        empty_.setBounds(tiles_.getLocalBounds());
        int visible = 0;
        for (auto& tile : buttons_) {
            if (!tile->isVisible()) continue;
            tile->setBounds(juce::Rectangle<int>((visible % cols)*tileWidth, (visible / cols)*rowHeight, tileWidth, rowHeight).reduced(2));
            ++visible;
        }
    }

    static constexpr int rowHeight = 33;
    juce::TextButton all_{TRANS("All tracks")}, favourites_;
    juce::Label editing_, empty_;
    juce::Component tiles_;
    juce::Viewport view_;
    std::vector<std::unique_ptr<Tile>> buttons_;
    juce::PropertySet* settings_ = nullptr;
    juce::String settingsKey_;
    juce::StringArray starred_;
    bool onlyStarred_ = false;
};
