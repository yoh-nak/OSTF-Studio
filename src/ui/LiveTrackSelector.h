#pragma once
#include "ui/OstfpPreview.h"
#include <juce_gui_basics/juce_gui_basics.h>

class LiveTrackSelector final : public juce::Component {
public:
    std::function<void()> filterChanged;

    LiveTrackSelector() {
        addAndMakeVisible(choose_);
        addAndMakeVisible(editing_);
        choose_.setTitle(TRANS("Visible tracks"));
        choose_.onClick = [this] { showMenu(); };
    }
    void openDocument(const juce::String& id, juce::PropertySet& settings) {
        settings_ = &settings;
        key_ = id.isEmpty() ? juce::String{} : "liveTrackFilter" + juce::String::toHexString(id.hashCode64());
        visible_.clear();
        all_ = true;
        if (key_.isNotEmpty()) if (auto saved = settings.getXmlValue(key_)) {
            all_ = !saved->getBoolAttribute("onlyStarred");
            for (auto* item : saved->getChildWithTagNameIterator("TRACK")) visible_.add(item->getStringAttribute("id"));
        }
    }
    void bind(const ostfstudio::OstfpPreview& song, std::optional<std::size_t> selected, const juce::String& section) {
        ids_.clear(); names_.clear();
        int count = 0;
        for (size_t i = 0; i < song.sourceTrackCount(); ++i) {
            ids_.add("track:" + song.trackIds[int(i)]);
            names_.add(juce::String::fromUTF8(song.trackLanes[i].label));
            if (includesTrack(i)) ++count;
        }
        choose_.setButtonText(TRANS("Visible tracks") + "  " + juce::String(count) + "/" + juce::String(ids_.size()));
        const auto name = selected && *selected < song.trackLanes.size() ? juce::String::fromUTF8(song.trackLanes[*selected].label) : juce::String{};
        editing_.setText(name + "  /  " + section, juce::dontSendNotification);
    }
    bool includesTrack(size_t i) const { return int(i)<ids_.size() && (all_ || visible_.contains(ids_[int(i)])); }
    void toggleTrack(int i) {
        if (!juce::isPositiveAndBelow(i,ids_.size())) return;
        if (all_) { visible_ = ids_; all_ = false; }
        if (visible_.contains(ids_[i])) visible_.removeString(ids_[i]); else visible_.add(ids_[i]);
        const bool complete = std::all_of(ids_.begin(),ids_.end(),[this](const auto& id){return visible_.contains(id);});
        if (complete) all_ = true;
        save();
    }
    void resized() override {
        auto row=getLocalBounds();
        choose_.setBounds(row.removeFromLeft(std::min(156,row.getWidth())).reduced(2,1));
        editing_.setBounds(row.reduced(4,0));
    }
private:
    void save() {
        if (settings_ && key_.isNotEmpty()) {
            juce::XmlElement state("LIVE_TRACKS");
            state.setAttribute("onlyStarred",!all_);
            for (const auto& id : visible_) state.createNewChildElement("TRACK")->setAttribute("id",id);
            settings_->setValue(key_,&state);
        }
        if (filterChanged) filterChanged();
    }
    void showMenu() {
        juce::PopupMenu menu;
        menu.addItem(1,TRANS("All tracks"),true,all_);
        menu.addSeparator();
        for (int i=0;i<names_.size();++i) menu.addItem(i+2,names_[i],true,includesTrack(size_t(i)));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&choose_),[safe=juce::Component::SafePointer<LiveTrackSelector>(this),ids=ids_,key=key_](int item){
            if (!safe || safe->ids_ != ids || safe->key_ != key || !item) return;
            if (item==1) { safe->all_=true; safe->save(); } else safe->toggleTrack(item-2);
        });
    }
    bool all_=true;
    juce::StringArray ids_,names_,visible_;
    juce::String key_;
    juce::PropertySet* settings_=nullptr;
    juce::TextButton choose_;
    juce::Label editing_;
};
