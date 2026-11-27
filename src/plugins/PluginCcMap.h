#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace ostfstudio {
// Bindings live in the plug-in instance state. Zero is an unused CC slot.
class PluginCcMap {
public:
    explicit PluginCcMap(juce::AudioProcessor& owner) : processor(owner) {}
    void learn(int parameter) { learning.store(parameter); }
    void forget(int parameter) {
        for (auto& slot : slots) {
            int expected=parameter+1;
            slot.compare_exchange_strong(expected,0);
        }
    }
    void process(const juce::MidiBuffer& midi) {
        const auto& params=processor.getParameters();
        for (const auto e : midi) {
            if (e.numBytes!=3 || (e.data[0]&0xf0)!=0xb0 || e.data[1]>=120) continue;
            const int slot=(e.data[0]&15)*120+e.data[1];
            const int armed=learning.exchange(-1);
            if (juce::isPositiveAndBelow(armed,params.size())) {
                forget(armed);
                slots[size_t(slot)].store(armed+1);
            }
            const int index=slots[size_t(slot)].load()-1;
            if (juce::isPositiveAndBelow(index,params.size())) params[index]->setValueNotifyingHost(e.data[2]/127.f);
        }
    }
    void save(juce::ValueTree& state) const {
        state.removeChild(state.getChildWithName("MidiCcMap"),nullptr);
        juce::ValueTree map("MidiCcMap");
        const auto& params=processor.getParameters();
        for (size_t i=0;i<slots.size();++i) {
            const int index=slots[i].load()-1;
            if (!juce::isPositiveAndBelow(index,params.size())) continue;
            if (auto* p=dynamic_cast<juce::AudioProcessorParameterWithID*>(params[index]))
                map.appendChild(juce::ValueTree("Binding",{{"slot",int(i)},{"parameter",p->paramID}}),nullptr);
        }
        if (map.getNumChildren()) state.appendChild(map,nullptr);
    }
    void restore(const juce::ValueTree& state) {
        learning.store(-1);
        for (auto& slot : slots) slot.store(0);
        for (const auto binding : state.getChildWithName("MidiCcMap")) {
            const int slot=binding.getProperty("slot",-1);
            if (!juce::isPositiveAndBelow(slot,int(slots.size()))) continue;
            for (auto* param : processor.getParameters())
                if (auto* p=dynamic_cast<juce::AudioProcessorParameterWithID*>(param); p && binding["parameter"].toString()==p->paramID)
                    slots[size_t(slot)].store(p->getParameterIndex()+1);
        }
    }
    void save(juce::XmlElement& xml) const {
        juce::ValueTree root("Root");
        save(root);
        if(auto map=root.getChildWithName("MidiCcMap");map.isValid()) xml.addChildElement(map.createXml().release());
    }
    void restore(const juce::XmlElement& xml) {
        juce::ValueTree root("Root");
        if(auto* map=xml.getChildByName("MidiCcMap")) root.appendChild(juce::ValueTree::fromXml(*map),nullptr);
        restore(root);
    }
    void open(juce::TextButton& button) {
        juce::PopupMenu menu;
        menu.addSectionHeader(TRANS("Select a parameter, then turn a MIDI controller"));
        const int armed=learning.load();
        if(armed>=0) menu.addItem(1,TRANS("Cancel MIDI learn"));
        for (auto* p : processor.getParameters()) {
            if (!p->isAutomatable() || !dynamic_cast<juce::AudioProcessorParameterWithID*>(p)) continue;
            const auto index=p->getParameterIndex();
            juce::String label=p->getName(70);
            bool mapped=false;
            for(size_t i=0;i<slots.size();++i) if(slots[i].load()==index+1) {
                label+="  ·  Ch "+juce::String(int(i/120+1))+" / CC "+juce::String(int(i%120));
                mapped=true; break;
            }
            if(mapped) {
                juce::PopupMenu actions;
                actions.addItem(2+2*index,TRANS("Learn MIDI CC"));
                actions.addItem(3+2*index,TRANS("Remove mapping"));
                menu.addSubMenu(label,actions);
            } else menu.addItem(2+2*index,label,true,armed==index);
        }
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&button),
            [this, safe=juce::Component::SafePointer<juce::TextButton>(&button)](int result) {
                if (!safe || !result) return;
                if (result==1) learn(-1);
                else if (result%2==0) learn((result-2)/2);
                else forget((result-3)/2);
            });
    }
private:
    juce::AudioProcessor& processor;
    std::atomic<int> learning{-1};
    std::array<std::atomic<int>,16*120> slots{};
};

class PluginCcButton : public juce::TextButton {
public:
    explicit PluginCcButton(PluginCcMap& map) : juce::TextButton("MIDI CC") {
        setTooltip(TRANS("Select a parameter, then turn a MIDI controller"));
        onClick=[this,&map]{map.open(*this);};
    }
};
}
