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
    std::function<void(int, int)> changeRepeats;

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
              bool showChord, bool showScale, const juce::String& armed) {
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
            group.buttons[1].setToggleState(song.trackIds[int(columns_[i].track)]==armed,juce::dontSendNotification);
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
        g.drawText(TRANS("Extra passes"), left, juce::Justification::centred);
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
                b.setButtonText(labels[i]); b.setTitle(i==1 ? TRANS("Arm") : labels[i]);
                if(i==4) { b.setTitle(TRANS("Mute")); b.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffd09b34)); }
                b.setTooltip(b.getTitle()); addAndMakeVisible(b);
            }
        }
        void resized() override {
            auto row=getLocalBounds().reduced(2,1);
            for (int i : {4,1,0,2,3,5}) {
                const int compact=std::min(getWidth()/8,30);
                const int width = i==4 || i==1 ? compact : i==0 ? row.getWidth()/4 : i==2 ? row.getWidth()/2 : i==3 ? row.getWidth()/2 : row.getWidth();
                buttons[size_t(i)].setBounds(row.removeFromLeft(width).reduced(1,0));
            }
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
        std::vector<Segment> segments;
        bool chord = true, scale = true;
        void paintButton(juce::Graphics& g, bool hover, bool down) override {
            if (segments.empty()) { juce::TextButton::paintButton(g, hover, down); return; }
            const auto face = findColour(getToggleState() ? buttonOnColourId : buttonColourId);
            g.fillAll(hover || down ? face.brighter(.15f) : face);
            const int width = getWidth();
            int left = 0;
            for (const auto& seg : segments) {
                const int right = juce::roundToInt(width * seg.end);
                auto r = juce::Rectangle<int>(left, 2, std::max(0,right-left-1), getHeight()-4);
                const auto strip = [&](int percent, const char* label) {
                    auto band = r.removeFromBottom(10);
                    g.setColour(percent<0 ? juce::Colour(0xff303f49) : ostfstudio::fitColour(percent));
                    g.fillRect(band);
                    if (band.getWidth()>=39) {
                        const auto value = percent<0 ? juce::String::charToString(0x2014) : juce::String(percent)+"%";
                        g.setColour(percent<0 ? juce::Colour(0xffb6c3cb) : juce::Colour(0xff112026));
                        g.setFont(juce::FontOptions(11.f,juce::Font::bold));
                        g.drawFittedText(juce::String(label)+value,band,juce::Justification::centred,1,.84f);
                    }
                    r.removeFromBottom(1);
                };
                if (scale) strip(seg.fit.scale.percent(),"S ");
                if (chord) strip(seg.fit.chord.percent(),"C ");
                g.setColour(juce::Colour(0xffecf0f0));
                g.setFont(juce::FontOptions(12.f));
                if (left == 0 && getButtonText() == "+" && r.getWidth() >= 35) {
                    g.drawText("+",r.removeFromLeft(12),juce::Justification::centred);
                }
                if (r.getWidth()>=10) g.drawText(seg.name,r.reduced(2,0),juce::Justification::centredLeft,false);
                if (right<width) { g.setColour(juce::Colour(0xff162329)); g.drawVerticalLine(right-1,1.f,float(getHeight()-1)); }
                left = right;
            }
            if (getToggleState() || hasKeyboardFocus(false)) { g.setColour(juce::Colour(0xffa7eee0)); g.drawRect(getLocalBounds()); }
        }
        juce::String getTooltip() override {
            auto text = juce::TextButton::getTooltip();
            if (segments.empty()) return text;
            const double x = double(getMouseXYRelative().x) / std::max(1,getWidth());
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
            launch.setTitle(TRANS("Play section") + " / " + section.name);
            launch.setTooltip(launch.getTitle());
            launch.setEnabled(!owner.recording_);
            for (auto* button : {&less, &more}) {
                button->setTitle(TRANS("Extra passes") + " " + button->getButtonText() + " / " + section.name);
                button->setTooltip(button->getTitle());
            }
            while (buttons.size() < owner.columns_.size()) {
                auto col = buttons.size();
                auto& b = *buttons.emplace_back(std::make_unique<Slot>());
                cells.addAndMakeVisible(b);
                b.onClick = [this, col] {
                    if (owner.editCell) owner.editCell(owner.columns_[col].track, index);
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
            launch.setToggleState(index == session.playing, juce::dontSendNotification);
            launch.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff34756d));
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
    int columnWidth() const { return std::clamp((getWidth()-sidebarWidth()-list_.getViewport()->getScrollBarThickness()) / std::max(1,int(columns_.size())),208,460); }
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

    static constexpr int headerHeight = 50, rowHeight = 50;
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
