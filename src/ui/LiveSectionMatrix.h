#pragma once

#include "ui/LiveSections.h"
#include "ui/LiveTrackSelector.h"
#include "ui/TheoryGuide.h"
#include "ui/FitColour.h"

// The row controls stay fixed when the player scrolls through track columns.
class LiveSectionMatrix final : public juce::Component, private juce::ListBoxModel,
                                private juce::ScrollBar::Listener {
public:
    std::function<void(std::size_t, int)> editCell;
    std::function<void(std::size_t, int, juce::Component*)> trackAction;
    std::function<void(int)> launchRow;
    std::function<bool(int)> isQueued;
    std::function<void(int, int)> changeRepeats;
    std::function<void(size_t,int,juce::Component*)> takeMenu;
    std::function<juce::String(size_t,int)> takeCaption;
    std::function<bool(size_t,int)> hasAudition;
    std::function<void(size_t,int)> toggleAudition;
    std::function<void(size_t,int)> toggleCellLoop;
    std::function<bool(size_t,int)> cellLoops;

    LiveSectionMatrix() {
        addAndMakeVisible(list_);
        addAndMakeVisible(header_);
        header_.setInterceptsMouseClicks(false,true);
        addChildComponent(horizontal_);
        horizontal_.addListener(this);
        horizontal_.setSingleStepSize(100.);
        list_.setModel(this);
        list_.setRowHeight(rowHeight);
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1c292f));
    }
    ~LiveSectionMatrix() override {
        horizontal_.removeListener(this);
        list_.setModel(nullptr);
    }

    void bind(const ostfstudio::OstfpPreview& song, const LiveSections& session,
              const LiveTrackSelector& filter, std::optional<std::size_t> selectedTrack, bool recording,
              const ostfstudio::TheoryGuide* theory, const std::vector<std::vector<ostfstudio::TheoryGuide::TrackCell>>& fits,
              bool showChord, bool showScale, const juce::String& armed, const std::set<juce::String>& audioArms = {}) {
        session_ = &session;
        selectedTrack_ = selectedTrack;
        recording_ = recording;
        chordFit_ = showChord;
        scaleFit_ = showScale;
        columns_.clear();
        for (std::size_t i = 0; i < song.sourceTrackCount(); ++i) {
            if (!filter.includesTrack(i)) continue;
            Column column{i, juce::String::fromUTF8(song.trackLanes[i].label), juce::Colour(song.trackColour(i)),
                          std::vector<int>(session.sections.size()), {}};
            column.progressions.resize(session.sections.size());
            if (song.trackKinds[i] == ostfstudio::TrackKind::audio) {
                column.audio=true;
                column.clips.resize(session.sections.size());
                for (size_t j=0;j<session.sections.size();++j)
                    for (const auto& region:song.audio)
                        if (region.track==i && session.sections[j].ticks.intersects({region.startTick,region.startTick+region.durationTicks}))
                            column.clips[j].add(region.path.replaceCharacter('\\','/').fromLastOccurrenceOf("/",false,false));
                columns_.push_back(std::move(column)); continue;
            }
            if (theory) for (std::size_t s = 0; s < session.sections.size(); ++s) {
                const auto ticks = session.sections[s].ticks;
                auto& segments = column.progressions[s];
                for (auto c = theory->firstCell(double(ticks.getStart())); c < theory->cells.size() && theory->cells[c].start < ticks.getEnd(); ++c) {
                    const auto& cell = theory->cells[c];
                    if (segments.empty() || cell.chord != segments.back().chord) {
                        const auto name = cell.chord >= 0 ? theory->chords[std::size_t(cell.chord)].name : juce::String{};
                        segments.push_back({cell.chord, name, 0., {}});
                    }
                    auto& segment = segments.back();
                    segment.end = double(std::min(cell.end, ticks.getEnd()) - ticks.getStart()) / double(ticks.getLength());
                    if (i < fits.size() && c < fits[i].size()) {
                        const auto& local = fits[i][c];
                        segment.fit.chord.inside += local.localChord.inside;
                        segment.fit.chord.total += local.localChord.total;
                        segment.fit.scale.inside += local.scale.inside;
                        segment.fit.scale.total += local.scale.total;
                    }
                }
            }
            // Use the same onset rule as the section piano roll, including gaps between sections.
            const auto count = [&](const auto& notes) {
                for (const auto& note : notes) {
                    auto next = std::upper_bound(session.sections.begin(), session.sections.end(), note.startTick,
                        [](auto tick, const auto& section) { return tick < section.ticks.getStart(); });
                    if (next == session.sections.begin()) continue;
                    const auto section = std::prev(next);
                    if (section->ticks.contains(note.startTick))
                        ++column.notes[std::size_t(section - session.sections.begin())];
                }
            };
            count(song.pitchedNotes[i]);
            count(song.drumNotes[i]);
            columns_.push_back(std::move(column));
        }
        while (settings_.size() < columns_.size()) {
            const auto col = settings_.size();
            auto& group = settings_.emplace_back(std::make_unique<TrackSettings>());
            header_.addAndMakeVisible(*group);
            for (int action=0; action<int(group->buttons.size()); ++action) {
                auto& b = group->buttons[size_t(action)];
                b.onClick = [this,col,action] {
                    if (trackAction && col < columns_.size())
                        trackAction(columns_[col].track,action,&settings_[col]->buttons[size_t(action)]);
                };
            }
        }
        for (size_t i=0;i<settings_.size();++i) {
            auto& group = *settings_[i];
            group.setVisible(i<columns_.size());
            if (i>=columns_.size()) continue;
            for (auto& button : group.buttons) button.setEnabled(!recording);
            group.buttons[4].setEnabled(true);
            group.buttons[5].setColour(juce::TextButton::buttonColourId,columns_[i].colour.darker(.25f));
            group.buttons[2].setEnabled(!recording && !columns_[i].audio);
            const auto id=song.trackIds[int(columns_[i].track)];
            group.buttons[1].setToggleState(id==armed || audioArms.contains(id),juce::dontSendNotification);
        }
        resized();
        list_.updateContent();
        repaint();
    }

    void setTrackMute(size_t track, bool muted) {
        for(size_t i=0;i<columns_.size();++i) if(columns_[i].track==track)
            settings_[i]->buttons[4].setToggleState(muted,juce::dontSendNotification);
    }
    int preferredHeight() const {
        return headerHeight + rowHeight * std::max(1,getNumRowsForLayout()) + 12;
    }

    void refreshVisibleRows() {
        const int first = std::max(0, list_.getRowContainingPosition(1, 1));
        const int end = std::min(getNumRowsForLayout(), first + list_.getNumRowsOnScreen() + 2);
        for (int row = first; row < end; ++row)
            if (auto* controls = dynamic_cast<Row*>(list_.getComponentForRowNumber(row)))
                controls->updateStatus();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto head = bounds.removeFromTop(headerHeight);
        header_.setBounds(head.withTrimmedLeft(sidebarWidth()).withTrimmedTop(23));
        const int available = std::max(1, getWidth() - sidebarWidth() - list_.getViewport()->getScrollBarThickness());
        const int total = int(columns_.size()) * columnWidth();
        horizontal_.setVisible(total > available);
        if (horizontal_.isVisible()) horizontal_.setBounds(bounds.removeFromBottom(12).withTrimmedLeft(sidebarWidth()));
        horizontal_.setRangeLimits(0., double(std::max(total, available)), juce::dontSendNotification);
        horizontal_.setCurrentRange(horizontal_.getCurrentRangeStart(), double(available), juce::dontSendNotification);
        list_.setBounds(bounds);
        relayoutRows();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1c292f));
        g.setFont(juce::FontOptions(12.f, juce::Font::bold));
        g.setColour(juce::Colour(0xffb5c9ce));
        auto head = getLocalBounds().removeFromTop(headerHeight);
        auto left = head.removeFromLeft(sidebarWidth());
        g.drawText(TRANS("Section"), left.removeFromLeft(left.getWidth() - 88).reduced(5,0), juce::Justification::centredLeft);
        g.drawText(TRANS("Loop count"), left, juce::Justification::centred);
        g.reduceClipRegion(head);
        if (columns_.empty()) g.drawText(TRANS("No tracks in this filter"), head.reduced(5,0), juce::Justification::centredLeft);
        for (std::size_t col = 0; col < columns_.size(); ++col) {
            auto r = head.withX(head.getX() + columnX(col)).withWidth(columnWidth()).reduced(2, 1);
            g.setColour(columns_[col].colour);
            g.fillRect(r.removeFromBottom(2));
            g.setColour(juce::Colour(0xffe0e9ec));
            g.drawText(columns_[col].name, r.removeFromTop(20).reduced(5, 0), juce::Justification::centredLeft);
        }
    }

private:
    struct TrackSettings : juce::Component {
        std::array<juce::TextButton,6> buttons;
        TrackSettings() {
            const juce::StringArray labels{TRANS("Input"),"R",TRANS("Instrument"),"FX","M",TRANS("Colour")};
            for (int i=0;i<int(buttons.size());++i) {
                auto& b=buttons[size_t(i)];
                b.setButtonText(i==5?juce::String{}:labels[i]); b.setTitle(i==1 ? TRANS("Arm") : labels[i]);
                if(i==4) { b.setTitle(TRANS("Mute")); b.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffd09b34)); }
                b.setTooltip(b.getTitle()); addAndMakeVisible(b);
            }
        }
        void resized() override {
            auto row=getLocalBounds().reduced(2,1);
            auto upper=row.removeFromTop(row.getHeight()/2);
            buttons[4].setBounds(upper.removeFromLeft(22).reduced(1,0));
            buttons[1].setBounds(upper.removeFromLeft(22).reduced(1,0));
            buttons[0].setBounds(upper.reduced(1,0));
            buttons[5].setBounds(row.removeFromRight(25).reduced(1,0));
            buttons[3].setBounds(row.removeFromRight(25).reduced(1,0));
            buttons[2].setBounds(row.reduced(1,0));
        }
    };
    struct Segment {
        int chord;
        juce::String name;
        double end;
        ostfstudio::TheoryGuide::ClipFit fit;
    };
    struct Column {
        std::size_t track;
        juce::String name;
        juce::Colour colour;
        std::vector<int> notes;
        std::vector<std::vector<Segment>> progressions;
        bool audio=false;
        std::vector<juce::StringArray> clips;
    };
    class Slot final : public juce::TextButton {
    public:
        class AuditionButton final : public juce::TextButton {
            void paintButton(juce::Graphics& g, bool hover, bool down) override {
                auto face=findColour(getToggleState() ? buttonOnColourId : buttonColourId);
                getLookAndFeel().drawButtonBackground(g,*this,face,hover,down);
                const auto box=getLocalBounds().toFloat().withSizeKeepingCentre(9.f,10.f);
                g.setColour(findColour(textColourOffId).withMultipliedAlpha(isEnabled() ? 1.f : .4f));
                if (getToggleState()) { g.fillRect(box.withSizeKeepingCentre(8.f,8.f)); return; }
                juce::Path play;
                play.addTriangle(box.getX(),box.getY(),box.getRight(),box.getCentreY(),box.getX(),box.getBottom());
                g.fillPath(play);
            }
        };
        juce::TextButton take;
        AuditionButton audition;
        class LoopButton final : public juce::TextButton {
            void paintButton(juce::Graphics& g,bool over,bool down) override {
                getLookAndFeel().drawButtonBackground(g,*this,findColour(getToggleState()?buttonOnColourId:buttonColourId),over,down);
                const auto centre=getLocalBounds().toFloat().getCentre();
                g.setOrigin(juce::roundToInt(centre.x),juce::roundToInt(centre.y));
                g.setColour(getToggleState()?juce::Colour(0xff91ffe0):juce::Colour(0xffc6d3d9));
                juce::Path arrows;
                arrows.startNewSubPath(-6,1);arrows.lineTo(-6,-3);arrows.lineTo(5,-3);
                arrows.lineTo(2,-6);arrows.startNewSubPath(6,-1);arrows.lineTo(6,3);arrows.lineTo(-5,3);arrows.lineTo(-2,6);
                g.strokePath(arrows,juce::PathStrokeType(1.5f));
            }
        } loop;
        Slot() { addChildComponent(take); addAndMakeVisible(audition); addAndMakeVisible(loop); }
        int takeWidth() const {return std::min(take.isVisible()?84:52,getWidth()*3/5);}
        void resized() override {
            auto controls=getLocalBounds().removeFromLeft(takeWidth());
            audition.setBounds(controls.removeFromLeft(std::min(23,controls.getWidth()/2)).reduced(1));
            loop.setBounds(controls.removeFromLeft(std::min(23,controls.getWidth())).reduced(1));
            take.setBounds(controls.reduced(1));
        }
        std::vector<Segment> segments;
        bool chord = true, scale = true;
        void paintButton(juce::Graphics& g, bool hover, bool down) override {
            if (segments.empty()) {
                auto face=findColour(getToggleState()?buttonOnColourId:buttonColourId);
                getLookAndFeel().drawButtonBackground(g,*this,face,hover,down);
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(12.f));
                g.drawText(getButtonText(),getLocalBounds().withTrimmedLeft(takeWidth()).reduced(2,0),juce::Justification::centredLeft,true);
                return;
            }
            const auto face = findColour(getToggleState() ? buttonOnColourId : buttonColourId);
            g.fillAll(hover || down ? face.brighter(.15f) : face);
            juce::Graphics::ScopedSaveState save(g);
            const int width = getWidth()-takeWidth();
            g.setOrigin(takeWidth(),0);
            int left = 0;
            for (const auto& seg : segments) {
                const int right = juce::roundToInt(width * seg.end);
                auto r = juce::Rectangle<int>(left, 2, std::max(0,right-left-1), getHeight()-4);
                const auto strip = [&](int percent) {
                    auto band = r.removeFromBottom(6);
                    g.setColour(percent<0 ? juce::Colour(0xff303f49) : ostfstudio::fitColour(percent));
                    g.fillRect(band);
                    r.removeFromBottom(1);
                };
                if (scale) strip(seg.fit.scale.percent());
                if (chord) strip(seg.fit.chord.percent());
                left = right;
            }
            g.setColour(juce::Colour(0xffecf0f0));
            g.setFont(juce::FontOptions(12.f));
            g.drawFittedText(getButtonText(),0,0,width,std::max(12,getHeight()-17),juce::Justification::centredLeft,1);
            if (getToggleState() || hasKeyboardFocus(false)) { g.setColour(juce::Colour(0xffa7eee0)); g.drawRect(0,0,width,getHeight()); }
        }
        juce::String getTooltip() override {
            auto text = juce::TextButton::getTooltip();
            if (segments.empty()) return text;
            const double x = double(getMouseXYRelative().x-takeWidth()) / std::max(1,getWidth()-takeWidth());
            auto seg = std::find_if(segments.begin(),segments.end(),[x](const auto& s){return x<s.end;});
            if (seg==segments.end()) return text;
            if (seg->name.isNotEmpty()) text += "  /  " + seg->name;
            const auto add = [&](const juce::String& title, int value) {
                text += "\n" + title + "   " + (value<0 ? juce::String::charToString(0x2014) : juce::String(value)+"%");
            };
            if (chord) add(TRANS("Chord fit"),seg->fit.chord.percent());
            if (scale) add(TRANS("Scale fit"),seg->fit.scale.percent());
            return text;
        }
    };
    class Row final : public juce::Component {
    public:
        explicit Row(LiveSectionMatrix& matrix) : owner(matrix) {
            for (juce::Component* c : std::initializer_list<juce::Component*>{&launch, &name, &bars, &less, &count, &more, &cells})
                addAndMakeVisible(c);
            bars.setFont(juce::FontOptions(10.f)); bars.setColour(juce::Label::textColourId,juce::Colour(0xffbdd3d6));
            count.setJustificationType(juce::Justification::centred);
            launch.setButtonText(juce::String::charToString(0x25b6));
            launch.onClick = [this] { if (owner.launchRow) owner.launchRow(index); };
            less.onClick = [this] { step(-1); };
            more.onClick = [this] { step(1); };
        }
        void bind(int row) {
            index = row;
            const auto& section = owner.session_->sections[std::size_t(row)];
            name.setText(section.name, juce::dontSendNotification);
            bars.setText(owner.session_->barDetails[row],juce::dontSendNotification);
            name.setTooltip(section.name + "  /  " + owner.session_->barDetails[row]);
            launch.setTitle(TRANS("Add to queue") + " / " + section.name);
            launch.setTooltip(launch.getTitle());
            launch.setEnabled(!owner.recording_);
            for (auto* button : {&less, &more}) {
                button->setTitle(TRANS("Loop count") + " " + button->getButtonText() + " / " + section.name);
                button->setTooltip(button->getTitle());
            }
            while (buttons.size() < owner.columns_.size()) {
                auto col = buttons.size();
                auto& b = *buttons.emplace_back(std::make_unique<Slot>());
                cells.addAndMakeVisible(b);
                b.onClick = [this, col] {
                    if (owner.editCell) owner.editCell(owner.columns_[col].track, index);
                };
                b.audition.onClick=[this,col] {
                    if(owner.toggleAudition) owner.toggleAudition(owner.columns_[col].track,index);
                };
                b.take.onClick=[this,col] {
                    if(owner.takeMenu) owner.takeMenu(owner.columns_[col].track,index,&buttons[col]->take);
                };
                b.loop.onClick=[this,col] {
                    if(owner.toggleCellLoop) owner.toggleCellLoop(owner.columns_[col].track,index);
                };
            }
            buttons.resize(owner.columns_.size());
            for (std::size_t col = 0; col < buttons.size(); ++col) {
                auto& button = *buttons[col];
                const auto& column = owner.columns_[col];
                const auto n = column.notes[std::size_t(row)];
                button.segments = column.progressions[std::size_t(row)];
                button.chord = owner.chordFit_; button.scale = owner.scaleFit_;
                button.setButtonText(n ? juce::String(n) + " " + TRANS("notes") : "+");
                button.setTitle(section.name + " / " + column.name);
                button.take.setVisible(!column.audio);
                button.audition.setVisible(true);
                button.loop.setTitle(TRANS("Loop cell audition")+" / "+button.getTitle());
                button.loop.setTooltip(button.loop.getTitle());
                button.audition.setTitle(TRANS("Audition take")+" / "+button.getTitle());
                button.take.setButtonText(owner.takeCaption ? owner.takeCaption(column.track,row) : "T1");
                button.take.setTitle(TRANS("Takes")+" / "+button.getTitle());
                button.take.setTooltip(button.take.getTitle());
                button.setTooltip(TRANS("Edit in piano roll") + " / " + button.getTitle());
                if (column.audio) {
                    const auto& clips=column.clips[std::size_t(row)];
                    button.setButtonText(clips.isEmpty() ? juce::String::charToString(0x2014) : clips.joinIntoString(" / "));
                    button.setTooltip(button.getTitle()+"  /  "+TRANS("Audio"));
                }
                button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff223039).interpolatedWith(column.colour, n ? .29f : .04f));
                button.setColour(juce::TextButton::buttonOnColourId, column.colour.darker(.45f));
                button.setEnabled(!owner.recording_);
                button.setToggleState(owner.selectedTrack_ == column.track && owner.session_->selected == row,
                                      juce::dontSendNotification);
            }
            updateStatus();
            resized();
        }
        void updateStatus() {
            const auto& session = *owner.session_;
            const int n = session.repeats(std::size_t(index));
            count.setText(juce::String(n), juce::dontSendNotification);
            less.setEnabled(n > 0 && !owner.recording_);
            more.setEnabled(n < 99 && !owner.recording_);
            const bool queued=owner.isQueued && owner.isQueued(index);
            launch.setButtonText("+");
            launch.setTooltip(queued ? TRANS("Queued section")+" / "+session.sections[size_t(index)].name : launch.getTitle());
            launch.setToggleState(index == session.playing || queued, juce::dontSendNotification);
            launch.setEnabled(!owner.recording_ && n>0);
            for(size_t col=0;col<buttons.size();++col) {
                const auto track=owner.columns_[col].track;
                if(owner.takeCaption) buttons[col]->take.setButtonText(owner.takeCaption(track,index));
                auto& play=buttons[col]->audition;
                const bool pending=owner.hasAudition && owner.hasAudition(track,index);
                play.setToggleState(pending,juce::dontSendNotification);
                buttons[col]->loop.setToggleState(owner.cellLoops && owner.cellLoops(track,index),juce::dontSendNotification);
                buttons[col]->resized();
                play.setTooltip(pending ? TRANS("Stop audition") : TRANS("Audition take"));
                play.setEnabled(!owner.recording_);
            }
            launch.setColour(juce::TextButton::buttonOnColourId, juce::Colour(queued ? 0xff816534 : 0xff34756d));
        }
        void resized() override {
            auto bounds = getLocalBounds();
            auto side = bounds.removeFromLeft(owner.sidebarWidth());
            launch.setBounds(side.removeFromLeft(29).reduced(1, 3));
            more.setBounds(side.removeFromRight(29).reduced(1, 3));
            count.setBounds(side.removeFromRight(30));
            less.setBounds(side.removeFromRight(29).reduced(1, 3));
            name.setBounds(side.removeFromTop(side.getHeight()/2+2));
            bars.setBounds(side);
            cells.setBounds(bounds);
            for (std::size_t col = 0; col < buttons.size(); ++col)
                buttons[col]->setBounds(juce::Rectangle<int>(owner.columnX(col), 0, owner.columnWidth(), getHeight()).reduced(2, 3));
        }
    private:
        void step(int delta) { if (owner.changeRepeats) owner.changeRepeats(index, delta); }
        LiveSectionMatrix& owner;
        int index = 0;
        juce::TextButton launch, less{"-"}, more{"+"};
        juce::Label name, bars, count;
        juce::Component cells;
        std::vector<std::unique_ptr<Slot>> buttons;
    };

    int sidebarWidth() const { return std::clamp(getWidth()/3, 178, 244); }
    int columnWidth() const { return std::clamp((getWidth()-sidebarWidth()-list_.getViewport()->getScrollBarThickness()) / std::max(1,int(columns_.size())),110,270); }
    int columnX(std::size_t col) const { return int(col)*columnWidth() - int(horizontal_.getCurrentRangeStart()); }
    int getNumRowsForLayout() const { return session_ ? int(session_->sections.size()) : 0; }
    int getNumRows() override { return getNumRowsForLayout(); }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow(int row, bool, juce::Component* existing) override {
        std::unique_ptr<Row> result(static_cast<Row*>(existing));
        if (row < 0 || row >= getNumRows()) return nullptr;
        if (!result) result = std::make_unique<Row>(*this);
        result->bind(row);
        return result.release();
    }
    void relayoutRows() {
        for (size_t i=0;i<settings_.size();++i)
            settings_[i]->setBounds(columnX(i),0,columnWidth(),header_.getHeight()-2);
        const int first = std::max(0, list_.getRowContainingPosition(1, 1));
        const int end = std::min(getNumRows(), first + list_.getNumRowsOnScreen() + 2);
        for (int row = first; row < end; ++row)
            if (auto* c = list_.getComponentForRowNumber(row)) c->resized();
    }
    void scrollBarMoved(juce::ScrollBar*, double) override { relayoutRows(); repaint(); }

    static constexpr int headerHeight = 65, rowHeight = 44;
    juce::Component header_;
    std::vector<std::unique_ptr<TrackSettings>> settings_;
    const LiveSections* session_ = nullptr;
    std::optional<std::size_t> selectedTrack_;
    bool recording_ = false;
    bool chordFit_ = true, scaleFit_ = true;
    std::vector<Column> columns_;
    juce::ListBox list_;
    juce::ScrollBar horizontal_{false};
};
