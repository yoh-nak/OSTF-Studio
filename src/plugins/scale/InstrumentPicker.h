#pragma once
#include "ui/PluginLibrary.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace ostfstudio::scale {
struct InstrumentChoice {
    juce::String name, vendor, path;
    std::optional<juce::PluginDescription> scanned;
    bool needsInspection=false;
};

inline std::vector<InstrumentChoice> availableInstruments(const std::optional<juce::PluginDescription>& current) {
    juce::KnownPluginList library;
    if (auto xml=juce::XmlDocument::parse(pluginDataDirectory().getChildFile("vst3-cache.xml")))
        library.recreateFromXml(*xml);
    auto module=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    while (!module.hasFileExtension("vst3") && module!=module.getParentDirectory()) module=module.getParentDirectory();
    const auto home=module.hasFileExtension("vst3") ? module.getParentDirectory()
        : juce::File::getSpecialLocation(juce::File::hostApplicationPath).getParentDirectory();
    auto scanned=library.getTypes();
    if (current) scanned.add(*current);
    std::vector<InstrumentChoice> result;
    juce::StringArray knownPaths;
    for (const auto& d:scanned) knownPaths.add(d.fileOrIdentifier);
    for (const auto& d:pluginCatalog(scanned,home))
        if (d.isInstrument && d.pluginFormatName=="VST3" && d.name!="OSTF Scale Editor" && juce::File(d.fileOrIdentifier).exists())
            result.push_back({d.name,d.manufacturerName,d.fileOrIdentifier,d});
    juce::VST3PluginFormat format;
    auto paths=format.searchPathsForPlugins(format.getDefaultLocationsToSearch(),true,false);
    for (const auto& parent:{home,home.getChildFile("OSTF Studio Effects")})
        for (const auto& f:parent.findChildFiles(juce::File::findFilesAndDirectories,false,"*.vst3")) paths.add(f.getFullPathName());
    paths.removeDuplicates(false);
    for (const auto& path:paths) {
        if (knownPaths.contains(path)) continue;
        const juce::File bundle(path);
        if (bundle.getFileName()=="OSTF Scale Editor.vst3") continue;
        const auto info=juce::JSON::parse(bundle.getChildFile("Contents/Resources/moduleinfo.json"));
        if (auto* classes=info["Classes"].getArray()) {
            for (const auto& cls:*classes) {
                if (cls["Category"].toString()!="Audio Module Class") continue;
                auto* roles=cls["Sub Categories"].getArray();
                if (!roles || !roles->contains(juce::var("Instrument"))) continue;
                juce::PluginDescription type;
                type.name=cls["Name"].toString();
                type.manufacturerName=cls["Vendor"].toString();
                type.pluginFormatName="VST3";
                if (type.name=="OSTF Scale Editor" || isOstfGenerator(type) || isOstfArranger(type)) continue;
                juce::var identity(new juce::DynamicObject());
                identity.getDynamicObject()->setProperty("format","vst3");
                identity.getDynamicObject()->setProperty("name",type.name);
                identity.getDynamicObject()->setProperty("vendor",type.manufacturerName);
                identity.getDynamicObject()->setProperty("pluginId",cls["CID"]);
                auto existing=std::find_if(result.begin(),result.end(),[&](const auto& choice) {
                    return choice.scanned && matchesVst3Instrument(identity,*choice.scanned);
                });
                if (existing==result.end()) result.push_back({type.name,type.manufacturerName,path,{}});
                else if (bundle.getParentDirectory()==home || bundle.getParentDirectory()==home.getChildFile("OSTF Studio Effects")) {
                    existing->path=path;
                    existing->scanned->fileOrIdentifier=path;
                }
            }
        } else result.push_back({bundle.getFileNameWithoutExtension(),{},path,{},true});
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b) {
        if (a.needsInspection!=b.needsInspection) return !a.needsInspection;
        const int vendor=a.vendor.compareIgnoreCase(b.vendor);
        return vendor ? vendor<0 : a.name.compareNatural(b.name)<0;
    });
    return result;
}

class InstrumentPicker final : public juce::Component, private juce::ListBoxModel {
public:
    std::function<void(InstrumentChoice)> onChoose;
    std::function<void()> onBrowse,onReference;
    InstrumentPicker(const std::optional<juce::PluginDescription>& current,bool japanese) : choices(availableInstruments(current)), ja(japanese) {
        for (juce::Component* c:std::initializer_list<juce::Component*>{&search,&list,&hint,&load,&browse,&reference,&unlisted})
            addAndMakeVisible(*c);
        search.setTextToShowWhenEmpty(juce::String::fromUTF8(ja ? "音源名・メーカーで検索" : "Search instruments by name or manufacturer"),juce::Colours::lightgrey);
        if (ja) {
            load.setButtonText(juce::String::fromUTF8("読み込む")); browse.setButtonText(juce::String::fromUTF8("VST3を参照…"));
            reference.setButtonText(juce::String::fromUTF8("基準音")); unlisted.setButtonText(juce::String::fromUTF8("未スキャンも表示"));
        }
        search.setTitle("Search instruments");
        list.setComponentID("matching-instruments");
        list.setModel(this); list.setRowHeight(26);
        setSize(495,402);
        search.onTextChange=[this]{filter();};
        search.onReturnKey=[this]{choose();};
        load.onClick=[this]{choose();};
        unlisted.onClick=[this]{filter();};
        browse.onClick=[this]{if(onBrowse) onBrowse();};
        reference.onClick=[this]{if(onReference) onReference();};
        filter();
    }
    ~InstrumentPicker() override {list.setModel(nullptr);}
    void resized() override {
        auto r=getLocalBounds().reduced(8);
        search.setBounds(r.removeFromTop(30)); r.removeFromTop(4);
        auto buttons=r.removeFromBottom(29);
        reference.setBounds(buttons.removeFromLeft(124)); browse.setBounds(buttons.removeFromLeft(128));
        load.setBounds(buttons.removeFromRight(85));
        auto status=r.removeFromBottom(26);
        unlisted.setBounds(status.removeFromRight(165)); hint.setBounds(status);
        list.setBounds(r);
    }
private:
    void filter(){
        const auto words=juce::StringArray::fromTokens(search.getText().trim()," \t",{});
        visible.clear();
        for (size_t i=0;i<choices.size();++i) {
            const auto& c=choices[i];
            const auto text=c.name+" "+c.vendor;
            if ((!c.needsInspection || unlisted.getToggleState())
                && std::all_of(words.begin(),words.end(),[&](const auto& w){return text.containsIgnoreCase(w);})) visible.push_back(i);
        }
        list.deselectAllRows(); list.updateContent();
        if (!visible.empty()) list.selectRow(0);
        hint.setText(visible.empty() ? juce::String::fromUTF8(ja ? "該当する音源がありません" : "No matching instruments")
            : juce::String(int(visible.size()))+juce::String::fromUTF8(ja ? " 件" : " matches"),juce::dontSendNotification);
        load.setEnabled(!visible.empty()); list.repaint();
    }
    int getNumRows() override {return int(visible.size());}
    void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected) override {
        if (!juce::isPositiveAndBelow(row,getNumRows())) return;
        const auto& c=choices[visible[size_t(row)]];
        if(selected) g.fillAll(juce::Colour(0xff365a67));
        g.setColour(juce::Colours::white);
        g.drawFittedText(c.name,6,0,2*w/3-9,h,juce::Justification::centredLeft,1);
        g.setColour(juce::Colour(0xffb8ced6));
        g.drawFittedText(c.needsInspection ? juce::String::fromUTF8(ja ? "未スキャン" : "Unscanned") : c.vendor,2*w/3,0,w/3-6,h,juce::Justification::centredLeft,1);
    }
    void choose(){const int row=list.getSelectedRow(); if (onChoose && juce::isPositiveAndBelow(row,getNumRows())) onChoose(choices[visible[size_t(row)]]);}
    void listBoxItemDoubleClicked(int,const juce::MouseEvent&) override {choose();}
    void returnKeyPressed(int) override {choose();}
    std::vector<InstrumentChoice> choices;
    bool ja;
    std::vector<size_t> visible;
    juce::TextEditor search;
    juce::ListBox list;
    juce::Label hint;
    juce::TextButton load{"Load"},browse{"Browse for VST3..."},reference{"Reference tone"};
    juce::ToggleButton unlisted{"Show unscanned"};
};
}
