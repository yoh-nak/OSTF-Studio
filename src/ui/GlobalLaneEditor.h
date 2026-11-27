#pragma once
#include "ui/GlobalTimelineEdit.h"
#include "ui/OstfpPreview.h"
#include "ui/SongBasicsPanel.h"

class GlobalLaneEditor final : public juce::Component, private juce::ListBoxModel {
    using Lane = ostfstudio::TimelineLane;
    using Tick = juce::int64;
public:
    std::function<void(Tick)> navigate;
    std::function<void(juce::Array<juce::var>, juce::Array<juce::var>)> commit;

    static bool supports(Lane lane) {
        return lane == Lane::scale || lane == Lane::chord || lane == Lane::key || lane == Lane::meter || lane == Lane::section;
    }
    GlobalLaneEditor() {
        setComponentID("global-lane-editor");
        for (juce::Component* c : std::initializer_list<juce::Component*>{&title,&sections,&chords,&root,&value,&preset,&steps,&apply,&message})
            addAndMakeVisible(c);
        apply.setButtonText(TRANS("Apply")); apply.setTitle(TRANS("Apply"));
        addAndMakeVisible(progression);
        progression.setTitle(TRANS("Chord progression"));
        progression.setRowHeight(26);
        progression.setColour(juce::ListBox::backgroundColourId,juce::Colour(0xff17232e));
        addChildComponent(chordRoot); addChildComponent(chordQuality); addChildComponent(chordBass);
        chordRoot.setTitle(TRANS("Root")); chordQuality.setTitle(TRANS("Chord")); chordBass.setTitle(TRANS("Bass"));
        chordBass.addItem(TRANS("Root position"),1);
        for (auto note : {"C","C#","Db","D","D#","Eb","E","F","F#","Gb","G","G#","Ab","A","A#","Bb","B","Cb","B#","E#","Fb"}) {
            chordRoot.addItem(note,chordRoot.getNumItems()+1);
            chordBass.addItem(note,chordBass.getNumItems()+1);
        }
        for (auto quality : {"major","m","5","dim","aug","sus2","sus4","6","m6","7","maj7","m7","m7b5","dim7","9","maj9","m9","add9","11","13","7(b9)"})
            chordQuality.addItem(quality,chordQuality.getNumItems()+1);
        chordRoot.onChange=chordQuality.onChange=chordBass.onChange=[this]{
            if (chordRoot.getSelectedId() && chordQuality.getSelectedId())
                value.setText(chordRoot.getText()+(chordQuality.getSelectedId()==1?juce::String{}:chordQuality.getText())
                              +(chordBass.getSelectedId()>1?"/"+chordBass.getText():juce::String{}),false);
        };
        addAndMakeVisible(chordOnly);
        chordOnly.setButtonText(TRANS("This chord only"));
        sections.setTitle(TRANS("Section")); chords.setTitle(TRANS("Chord"));
        root.setTitle(TRANS("Tonic")); steps.setTitle(TRANS("Intervals in semitones"));
        preset.setTitle(TRANS("Preset")); preset.setTextWhenNothingSelected(TRANS("Preset"));
        for (auto* mode : SongBasicsPanel::scaleNames) preset.addItem(mode,preset.getNumItems()+1);
        preset.onChange = [this] {
            const int i=preset.getSelectedItemIndex();
            if (i<0) return;
            juce::StringArray pattern;
            for (int n : SongBasicsPanel::scaleSteps[i]) pattern.add(juce::String(n));
            steps.setText(pattern.joinIntoString(" "),false);
            presetLabel=root.getText().trim()+" "+preset.getText();
            value.setText(presetLabel,false);
        };
        root.onTextChange=[this]{
            if(preset.getSelectedId() && value.getText()==presetLabel) {
                presetLabel=root.getText().trim()+" "+preset.getText();
                value.setText(presetLabel,false);
            }
        };
        sections.onChange=[this]{ choose(sections,sectionTicks); };
        chords.onChange=[this]{ chordOnly.setToggleState(true,juce::dontSendNotification); choose(chords,chordTicks); };
        apply.onClick=[this]{ save(); };
        value.onReturnKey=apply.onClick;
        root.setTextToShowWhenEmpty(TRANS("Tonic"),juce::Colours::lightgrey);
        steps.setTextToShowWhenEmpty(TRANS("Intervals in semitones"),juce::Colours::lightgrey);
    }
    void bind(const juce::var& score,Lane selection,Tick tick,const ostfstudio::OstfpPreview& project,bool writable) {
        using namespace ostfstudio;
        const auto songEnd = project.durationTicks;
        lane=selection;
        const auto section=globalEventAt(score,Lane::section,tick), chord=globalEventAt(score,Lane::chord,tick);
        const auto span=[](const juce::var& item){ return juce::Range<Tick>::withStartAndLength(Tick(item["start"]),Tick(item["duration"])); };
        auto location=[&](Tick t){return juce::String(project.measureNumber(t))+"  ·  ";};
        auto fill=[&](juce::ComboBox& box,std::vector<Tick>& ticks,juce::var entries,const juce::var& current,bool filter) {
            box.clear(juce::dontSendNotification); ticks.clear();
            if (auto* list=entries.getArray()) for (const auto& item:*list) {
                const Tick start=item["start"];
                if (filter && section.isObject() && !span(section).intersects(span(item))) continue;
                ticks.push_back(filter && section.isObject()?std::max(start,span(section).getStart()):start);
                box.addItem(location(start)+item[filter?"label":"name"].toString(),int(ticks.size()));
                if (item["id"]==current["id"]) box.setSelectedId(int(ticks.size()),juce::dontSendNotification);
            }
            box.setTextWhenNothingSelected(box.getTitle());
        };
        fill(sections,sectionTicks,score["timeline"]["sections"],section,false);
        fill(chords,chordTicks,score["harmony"]["chords"],chord,true);
        rowTicks=chordTicks;
        rowNames.clear();
        for (int i=0;i<chords.getNumItems();++i) rowNames.add(chords.getItemText(i));
        int currentRow=chords.getSelectedItemIndex();
        if (lane==Lane::scale) {
            rowNames.clear(); rowTicks.clear(); currentRow=-1;
            const auto whole=section.isObject()?span(section):juce::Range<Tick>{0,songEnd};
            std::vector<Tick> edges{whole.getStart(),whole.getEnd()};
            const auto scales=score["harmony"]["scales"];
            if (const auto* list=scales.getArray()) for (const auto& s:*list) {
                if(s.hasProperty("targets")) continue;
                const auto region=whole.getIntersectionWith(span(s));
                if (!region.isEmpty()) { edges.push_back(region.getStart()); edges.push_back(region.getEnd()); }
            }
            std::sort(edges.begin(),edges.end());
            for (size_t i=1;i<edges.size();++i) {
                const auto at=edges[i-1];
                if (edges[i]==at) continue;
                const auto s=globalEventAt(score,Lane::scale,at);
                const auto scale=s.isObject()?s:score["song"]["scale"];
                if(at<=tick && tick<edges[i]) currentRow=int(rowTicks.size());
                rowTicks.push_back(at);
                rowNames.add(location(at)+(scale.isObject()?scale.getProperty("label",scale["id"]).toString():TRANS("No scale assigned")));
            }
        }
        {
            juce::ScopedValueSetter<bool> updating(binding,true);
            progression.updateContent();
            progression.selectRow(currentRow);
        }
        original=globalEventAt(score,lane,tick);
        draft=original.clone();
        const auto number = project.measureNumber(tick);
        auto range = juce::Range<Tick>(project.measureStart(number), project.measureStart(number + 1)).getIntersectionWith({0,songEnd});
        if (section.isObject()) range=range.getIntersectionWith(span(section));
        if (lane==Lane::scale && chord.isObject()) range=span(chord).getIntersectionWith({0,songEnd});
        if (original.isVoid()) {
            draft=new juce::DynamicObject();
            if (lane==Lane::key) {
                draft.getDynamicObject()->setProperty("key",score["song"]["key"].isObject()?score["song"]["key"].clone():juce::var(new juce::DynamicObject()));
                draft.getDynamicObject()->setProperty("id","key-"+juce::Uuid().toString());
                draft.getDynamicObject()->setProperty("at",range.getStart());
            } else if (globalSource(lane).span) {
                if (lane==Lane::scale) {
                    range=sharedScaleGap(score,tick,range);
                    if (score["song"]["scale"].isObject()) draft=score["song"]["scale"].clone();
                } else {
                    const auto src=globalSource(lane);
                    auto entries=score[src.group][src.events];
                    if (auto* list=entries.getArray()) for(const auto& item:*list) {
                        const auto other=span(item);
                        if(other.getEnd()<=tick) range.setStart(std::max(range.getStart(),other.getEnd()));
                        if(other.getStart()>tick) range.setEnd(std::min(range.getEnd(),other.getStart()));
                    }
                }
                draft.getDynamicObject()->setProperty("id","event-"+juce::Uuid().toString());
                draft.getDynamicObject()->setProperty("start",range.getStart());
                draft.getDynamicObject()->setProperty("duration",range.getLength());
            }
        }
        const bool scale=lane==Lane::scale, key=lane==Lane::key;
        chordRange=chord.isObject() && original.isObject()?span(chord).getIntersectionWith(span(original)):juce::Range<Tick>{};
        chordOnly.setVisible(scale && !chordRange.isEmpty() && chordRange!=span(original));
        const auto name=juce::translate(scale?"Scale":key?"Key":lane==Lane::meter?"Meter":lane==Lane::section?"Section":"Chord");
        const auto start=Tick(draft[globalSource(lane).span?"start":"at"]);
        auto barLabel=juce::String(project.measureNumber(start));
        if (globalSource(lane).span && Tick(draft["duration"])>0) {
            const auto last=project.measureNumber(start+Tick(draft["duration"])-1);
            if(last>project.measureNumber(start))barLabel+=" - "+juce::String(last);
        }
        title.setText(name+"    "+TRANS("Bar")+" "+barLabel,juce::dontSendNotification);
        value.setTitle(name); value.setTextToShowWhenEmpty(name,juce::Colours::lightgrey);
        auto keyEvent=globalEventAt(score,Lane::key,tick);
        auto activeKey=keyEvent.isObject()?keyEvent["key"]:score["song"]["key"];
        auto tonic=key?draft["key"]["tonic"]:draft["tonic"];
        if (!tonic.isObject()) tonic=activeKey["tonic"];
        root.setText(tonic.isObject()?pitchText(tonic):"C",false);
        root.setVisible(scale||key); preset.setVisible(scale); steps.setVisible(scale);
        preset.setSelectedId(0,juce::dontSendNotification);
        value.setText(key?draft["key"]["mode"].toString():lane==Lane::meter?
            draft["numerator"].toString()+"/"+draft["denominator"].toString():draft[lane==Lane::section?"name":"label"].toString(),false);
        for (auto* box : {&chordRoot,&chordQuality,&chordBass}) box->setVisible(lane==Lane::chord);
        if (lane==Lane::chord) {
            const auto rootName=pitchText(draft["root"]);
            auto label=value.getText().upToFirstOccurrenceOf("/",false,false);
            const auto quality=label.substring(rootName.length());
            chordRoot.setText(rootName,juce::dontSendNotification);
            chordQuality.setText(quality.isEmpty() || quality=="maj" ? "major" : quality=="min" ? "m" : quality,juce::dontSendNotification);
            if (draft["bass"].isObject()) chordBass.setText(pitchText(draft["bass"]),juce::dontSendNotification);
            else chordBass.setSelectedId(1,juce::dontSendNotification);
        }
        steps.setText(intervalsText(draft),false); message.setText({},juce::dontSendNotification);
        apply.setEnabled(writable && tick>=0 && tick<songEnd && (original.isObject() || key || (!range.isEmpty() && globalSource(lane).span)));
        resized();
    }
    void resized() override {
        auto area=getLocalBounds().reduced(10,4);
        auto nav=area.removeFromTop(30); title.setBounds(nav.removeFromLeft(std::min(150,nav.getWidth()/3)));
        sections.setBounds(nav.removeFromLeft(std::min(260,nav.getWidth()/2)).reduced(2)); chords.setBounds(nav.removeFromLeft(std::min(260,nav.getWidth())).reduced(2));
        auto row=area.removeFromTop(34); apply.setBounds(row.removeFromRight(std::min(100,row.getWidth()/6)).reduced(2));
        if(chordOnly.isVisible()) chordOnly.setBounds(row.removeFromRight(std::min(146,row.getWidth()/4)).reduced(2));
        if(chordRoot.isVisible()) for (auto* box : {&chordRoot,&chordQuality,&chordBass})
            box->setBounds(row.removeFromLeft(std::min(112,row.getWidth()/3)).reduced(2));
        if(root.isVisible()) root.setBounds(row.removeFromLeft(std::min(72,row.getWidth()/5)).reduced(2));
        if(preset.isVisible()) preset.setBounds(row.removeFromLeft(std::min(140,row.getWidth()/4)).reduced(2));
        value.setBounds(row.removeFromLeft(std::min(270,steps.isVisible()?row.getWidth()/2:row.getWidth())).reduced(2));
        if(steps.isVisible()) steps.setBounds(row.removeFromLeft(std::min(240,row.getWidth())).reduced(2));
        message.setBounds(area.removeFromBottom(24));
        progression.setBounds(area.reduced(2));
    }
private:
    int getNumRows() override { return rowNames.size(); }
    void paintListBoxItem(int index,juce::Graphics& g,int w,int h,bool selected) override {
        if(!juce::isPositiveAndBelow(index,getNumRows())) return;
        g.fillAll(selected?juce::Colour(0xff365867):juce::Colour(index%2?0xff202d37:0xff1a2833));
        g.setColour(juce::Colours::white);
        g.drawText(rowNames[index],10,0,w-20,h,juce::Justification::centredLeft,true);
    }
    void selectedRowsChanged(int row) override {
        if(binding || !juce::isPositiveAndBelow(row,int(rowTicks.size()))) return;
        chordOnly.setToggleState(false,juce::dontSendNotification);
        if(navigate) navigate(rowTicks[size_t(row)]);
    }
    void choose(juce::ComboBox& box,const std::vector<Tick>& ticks) {
        const int i=box.getSelectedItemIndex();
        if(navigate && juce::isPositiveAndBelow(i,int(ticks.size()))) navigate(ticks[size_t(i)]);
    }
    void save() {
        using namespace ostfstudio;
        if(!apply.isEnabled() || !commit) return;
        auto copy=draft.clone();
        auto* obj=copy.getDynamicObject();
        const auto text=value.getText().trim();
        const auto invalid=[this]{message.setText(TRANS("Enter a valid musical value."),juce::dontSendNotification);};
        if(lane==Lane::section) obj->setProperty("name",text);
        if(lane==Lane::chord) {
            if(auto result=editChordSymbol(copy,text);result.failed()){message.setText(juce::translate(result.getErrorMessage()),juce::dontSendNotification);return;}
        }
        if(lane==Lane::meter) {
            auto parts=juce::StringArray::fromTokens(text,"/","");
            if(parts.size()!=2 || parts[0].isEmpty() || parts[1].isEmpty() || !parts[0].containsOnly("0123456789") || !parts[1].containsOnly("0123456789")){invalid();return;}
            obj->setProperty("numerator",parts[0].getIntValue());obj->setProperty("denominator",parts[1].getIntValue());
        }
        if(lane==Lane::key || lane==Lane::scale) {
            const auto tonic=parseSpelling(root.getText());
            if(tonic.isVoid() || text.isEmpty()){invalid();return;}
            if(lane==Lane::key) {
                auto* key=copy["key"].getDynamicObject();
                key->removeProperty("label");key->setProperty("state","tonal");key->setProperty("tonic",tonic);key->setProperty("mode",text);
            } else {
                juce::Array<juce::var> intervals;
                for(const auto& part:juce::StringArray::fromTokens(steps.getText()," ,","")) {
                    if(part.isEmpty())continue;
                    if(!part.containsOnly("0123456789")){invalid();return;}
                    const int pitch=part.getIntValue();
                    if(pitch<0 || pitch>11 || (intervals.isEmpty()?pitch!=0:pitch<=int(intervals.getLast()))){invalid();return;}
                    intervals.add(pitch);
                }
                if(intervals.isEmpty()){invalid();return;}
                obj->setProperty("tonic",tonic);obj->setProperty("label",text);obj->setProperty("intervals",intervals);
            }
        }
        juce::Array<juce::var> remove;
        if(original.isObject()) remove.add(original);
        juce::Array<juce::var> insert;
        if(lane==Lane::scale && chordOnly.isVisible() && chordOnly.getToggleState()) {
            const Tick start=original["start"], end=start+Tick(original["duration"]);
            auto append=[&](const juce::var& source,Tick a,Tick b) {
                if(b<=a)return;
                auto piece=source.clone();
                auto* fields=piece.getDynamicObject();
                if(!insert.isEmpty())fields->setProperty("id","scale-"+juce::Uuid().toString());
                fields->setProperty("start",a);fields->setProperty("duration",b-a);insert.add(piece);
            };
            append(original,start,chordRange.getStart());
            append(copy,chordRange.getStart(),chordRange.getEnd());
            append(original,chordRange.getEnd(),end);
        } else insert.add(copy);
        commit(std::move(remove),std::move(insert));
    }
    Lane lane=Lane::none;
    juce::var original,draft;
    std::vector<Tick> sectionTicks,chordTicks;
    juce::Range<Tick> chordRange;
    std::vector<Tick> rowTicks;
    juce::StringArray rowNames;
    juce::ComboBox chordRoot,chordQuality,chordBass;
    juce::String presetLabel;
    bool binding=false;
    juce::Label title,message;
    juce::ComboBox sections,chords,preset;
    juce::TextEditor root,value,steps;
    juce::TextButton apply;
    juce::ToggleButton chordOnly;
    juce::ListBox progression{"",this};
};
