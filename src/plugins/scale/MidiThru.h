#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace ostfstudio::scale {
// Merge the host's keyboard input with audition notes in sample order. A
// played key takes priority if the audition uses the same channel and pitch.
class MidiThru {
public:
    static constexpr int auditionChannel=1;

    void write(juce::MidiBuffer& out,const juce::MidiBuffer& keys,const juce::MidiBuffer& preview,bool releaseKeys) {
        out.clear();
        if (releaseKeys) release(out);
        auto key=keys.begin(), audition=preview.begin();
        while (key!=keys.end() || audition!=preview.end()) {
            if (key!=keys.end() && (audition==preview.end() || (*key).samplePosition<=(*audition).samplePosition)) {
                writeInput(out,*key++);
            } else {
                const auto event=*audition++;
                const auto msg=event.getMessage(); // The sequencer emits note messages only.
                const auto pitch=size_t(msg.getNoteNumber());
                if (msg.isNoteOn()) {
                    if (held[auditionChannel-1][pitch]) continue;
                    previewGate[pitch]=true;
                } else {
                    if (!previewGate[pitch]) continue;
                    previewGate[pitch]=false;
                }
                out.addEvent(event.data,event.numBytes,event.samplePosition);
            }
        }
    }
private:
    static constexpr std::array<int,3> pedals{64,66,69};
    std::array<std::array<unsigned char,128>,16> held{};
    std::array<std::array<bool,pedals.size()>,16> pedalDown{};
    std::array<bool,128> previewGate{};

    void writeInput(juce::MidiBuffer& out,const juce::MidiMessageMetadata& event) {
        if (event.numBytes==3 && event.data[0]<0xf0) {
            const auto msg=event.getMessage();
            const int ch=msg.getChannel();
            auto& channel=held[size_t(ch-1)];
            if (msg.isNoteOnOrOff()) {
                const auto pitch=size_t(msg.getNoteNumber());
                auto& count=channel[pitch];
                if (msg.isNoteOn()) {
                    if (ch==auditionChannel && previewGate[pitch]) {
                        out.addEvent(juce::MidiMessage::noteOff(ch,int(pitch)),event.samplePosition);
                        previewGate[pitch]=false;
                    }
                    if (count<255) ++count;
                } else if (count) {
                    --count;
                } else if (ch==auditionChannel && previewGate[pitch]) {
                    return;
                }
            } else if (msg.isController()) {
                const int cc=msg.getControllerNumber();
                auto& down=pedalDown[size_t(ch-1)];
                for (size_t i=0;i<pedals.size();++i)
                    if (cc==pedals[i]) down[i]=msg.getControllerValue()>=64;
                if (cc==121) down={};
                if (cc==120 || cc==123 || cc>=124) {
                    channel={};
                    if (ch==auditionChannel) previewGate={};
                }
            }
        }
        out.addEvent(event.data,event.numBytes,event.samplePosition);
    }
    void release(juce::MidiBuffer& out) {
        for (size_t ch=0;ch<held.size();++ch) {
            for (size_t i=0;i<pedals.size();++i)
                if (pedalDown[ch][i]) out.addEvent(juce::MidiMessage::controllerEvent(int(ch)+1,pedals[i],0),0);
            for (size_t key=0;key<held[ch].size();++key)
                for (int n=0;n<held[ch][key];++n)
                    out.addEvent(juce::MidiMessage::noteOff(int(ch)+1,int(key)),0);
        }
        held={}; pedalDown={};
    }
};
}
