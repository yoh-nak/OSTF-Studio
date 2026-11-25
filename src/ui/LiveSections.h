#pragma once
#include "ui/PreviewTransport.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The selection is the section being edited. The playhead can be in another section.
class LiveSections final : public juce::Component, private juce::ListBoxModel {
public:
    std::vector<PreviewTransport::LiveSection> sections;
    juce::StringArray barDetails;
    std::function<void()> selectionChanged;
    std::function<void(int)> launchSection;
    std::function<void(size_t,int)> repeatsChanged;
    std::function<int(size_t)> remaining;
    std::function<void(int)> inputAction;
    int selected=0, playing=-1;
    juce::ListBox list;
    juce::Label heading{"",TRANS("Live session")}, count;
    juce::TextButton fewer{"-"}, more{"+"}, launch{TRANS("Play from section")};
    juce::TextButton input{TRANS("Input")},arm{TRANS("Arm")},instrument{TRANS("Instrument")},fx{"FX"};
    LiveSections() {
        for(auto* c:std::initializer_list<juce::Component*>{&heading,&list,&count,&fewer,&more,&launch,&input,&arm,&instrument,&fx}) addAndMakeVisible(c);
        heading.setFont(juce::FontOptions(20.f,juce::Font::bold));
        list.setModel(this); list.setRowHeight(45); list.setColour(juce::ListBox::backgroundColourId,juce::Colour(0xff202e38));
        fewer.onClick=[this]{changeRepeats(selected,-1);}; more.onClick=[this]{changeRepeats(selected,1);};
        launch.onClick=[this]{if(launchSection)launchSection(selected);};
        int i=0; for(auto* b:{&input,&arm,&instrument,&fx}) b->onClick=[this,index=i++]{if(inputAction)inputAction(index);};
    }
    ~LiveSections() override {list.setModel(nullptr);}
    juce::Range<juce::int64> range() const {return selected>=0 && selected<int(sections.size())?sections[size_t(selected)].ticks:juce::Range<juce::int64>{};}
    void bind(const ostfstudio::OstfpPreview& song, bool sameSong) {
        std::map<juce::String,int> previous;
        juce::String selectedId;
        if (sameSong && !sections.empty()) {
            selectedId = sections[size_t(selected)].id;
            for (const auto& section : sections) previous[section.id] = section.repeats;
        }
        sections.clear(); playing=-1;
        const auto score=juce::JSON::parse(song.sourceScore.toString());
        if(auto* items=score["timeline"]["sections"].getArray())
            for(const auto& s:*items){
                const juce::int64 start=s["start"], duration=s["duration"];
                if(start<0 || start>=song.playbackEndTick() || duration<=0) continue;
                sections.push_back({s["id"].toString(),s.getProperty("name",s["id"]).toString(),
                    {start,start+std::min(duration,song.playbackEndTick()-start)},1});
            }
        std::stable_sort(sections.begin(),sections.end(),[](const auto& a,const auto& b){return a.ticks.getStart()<b.ticks.getStart();});
        // Overlapping/nested score sections share a boundary. Give each distinct
        // section start one launchable span, ending when the next section starts.
        for(size_t i=1;i<sections.size();++i) sections[i-1].ticks.setEnd(std::min(sections[i-1].ticks.getEnd(),sections[i].ticks.getStart()));
        std::erase_if(sections,[](const auto& s){return s.ticks.isEmpty();});
        if(sections.empty()) sections.push_back({{},song.title,{0,song.playbackEndTick()},1});
        barDetails.clear();
        const auto bar = std::max<juce::int64>(1, song.ticksPerBar);
        for (const auto& section : sections) {
            const auto ticks = section.ticks;
            const auto first = song.bars.firstBar + ticks.getStart()/bar;
            const auto last = song.bars.firstBar + std::max(ticks.getStart(), ticks.getEnd()-1)/bar;
            const auto length = ticks.getLength() % bar == 0 ? juce::String(ticks.getLength()/bar)
                : juce::String(double(ticks.getLength())/double(bar), 2);
            barDetails.add(TRANS("BARS") + " " + juce::String(first) + (last==first ? juce::String{} : juce::String::charToString(0x2013)+juce::String(last))
                           + "   /   " + length + " " + TRANS("bars"));
        }
        selected=0;
        for (size_t i=0; i<sections.size(); ++i) {
            auto& section = sections[i];
            if (auto it=previous.find(section.id); it!=previous.end()) section.repeats=it->second;
            if (sameSong && section.id==selectedId) selected=int(i);
        }
        list.updateContent(); list.selectRow(selected); updatePosition(-1);
    }
    void updatePosition(juce::int64 tick) {
        playing=-1;
        for(size_t i=0;i<sections.size();++i) if(sections[i].ticks.contains(tick)) {playing=int(i);break;}
        if(!sections.empty()) {
            const int n=repeats(size_t(selected)); count.setText(TRANS("Repeats remaining")+"   "+juce::String(n),juce::dontSendNotification);
            fewer.setEnabled(n>0); more.setEnabled(n<99);
        }
        list.repaint();
    }
    void resized() override {
        auto a=getLocalBounds().reduced(8); heading.setBounds(a.removeFromTop(37));
        auto tools=a.removeFromBottom(32);
        for(auto* b:{&input,&arm,&instrument,&fx}) b->setBounds(tools.removeFromLeft((getWidth()-16)/4).reduced(1));
        launch.setBounds(a.removeFromBottom(33).reduced(1));
        auto controls=a.removeFromBottom(32);
        more.setBounds(controls.removeFromRight(34).reduced(1)); fewer.setBounds(controls.removeFromRight(34).reduced(1));count.setBounds(controls);
        list.setBounds(a.reduced(0,3));
    }
    int repeats(size_t i) const {return remaining ? remaining(i) : sections[i].repeats;}
    void changeRepeats(int row, int delta) {
        if(row<0 || row>=int(sections.size())) return;
        const auto i=size_t(row); sections[i].repeats=std::clamp(repeats(i)+delta,0,99);
        if(repeatsChanged)repeatsChanged(i,sections[i].repeats);
        if(row==selected) {
            count.setText(TRANS("Repeats remaining")+"   "+juce::String(sections[i].repeats),juce::dontSendNotification);
            fewer.setEnabled(sections[i].repeats>0); more.setEnabled(sections[i].repeats<99);
        }
        list.repaint();
    }
private:
    int getNumRows() override {return int(sections.size());}
    void selectedRowsChanged(int i) override {if(i>=0 && i<getNumRows()){selected=i;if(selectionChanged)selectionChanged();}}
    void paintListBoxItem(int i,juce::Graphics& g,int w,int h,bool chosen) override {
        if(i<0 || i>=getNumRows()) return;
        g.fillAll(juce::Colour(chosen?0xff35505d:0xff23313a));
        if(i==playing){g.setColour(juce::Colour(0xff70dcc0));g.fillRect(0,2,4,h-4);}
        g.setFont(juce::FontOptions(15.f,juce::Font::bold));g.setColour(juce::Colours::white);
        g.drawText(sections[size_t(i)].name,10,1,w-68,23,juce::Justification::centredLeft,true);
        g.setFont(juce::FontOptions(12.f)); g.setColour(juce::Colour(0xffb4c9d0));
        g.drawText(barDetails[i],10,23,w-62,h-24,juce::Justification::centredLeft,true);
        g.setColour(juce::Colour(0xff97d8c9));g.drawText("+"+juce::String(repeats(size_t(i))),w-55,0,45,h,juce::Justification::centredRight);
    }
};
