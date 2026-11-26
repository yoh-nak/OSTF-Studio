#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ostfstudio::scale {
// Native views of the Max device's chromatic matrix and next-chord resolution graph.
class FlowView final : public juce::Component {
public:
    void update(juce::var result, bool showConnections) {
        data = std::move(result);
        connections = showConnections;
        setTitle(connections ? "Connections" : "Scale Flow");
        setDescription(data["from"].toString()+" / "+data["to"].toString()+" / "+
                       juce::String(data["matrix"].size())+" chords");
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().reduced(6, 2);
        g.fillAll(juce::Colour(0xff192534));
        g.setColour(juce::Colour(0xffe5eef4));
        g.setFont(juce::FontOptions(13.f));
        const auto matrix = data["matrix"];
        if (!matrix.size()) { g.drawText("Select a chord and scale candidate",area,juce::Justification::centred); return; }
        if (connections) {
            auto title = area.removeFromTop(23);
            const auto to = data["to"].toString();
            const auto arrow="  "+juce::String::charToString(0x2192)+"  ";
            g.drawText(to.isEmpty() ? juce::String("End of progression") : data["from"].toString()+arrow+to,
                       title,juce::Justification::centredLeft);
            const auto motions = data["motions"];
            if (!motions.size()) { g.drawText("No suggested resolution",area,juce::Justification::centredLeft); return; }
            const int perColumn = (motions.size()+1)/2;
            const int width = area.getWidth()/2;
            const int height = std::min(24,area.getHeight()/perColumn);
            for (int i=0;i<motions.size();++i) {
                const auto m=motions[i];
                const int delta=m["delta"];
                auto cell=juce::Rectangle<int>(area.getX()+(i/perColumn)*width,area.getY()+(i%perColumn)*height,width,height);
                g.setColour(m["kind"].toString()=="guide" ? juce::Colour(0xff98debf) : juce::Colour(0xffe2bc84));
                g.drawText(m["from"].toString()+arrow+m["to"].toString()+"    "+(delta>0?"+":"")+juce::String(delta)
                           +"    "+m["kind"].toString(), cell.reduced(4,0),juce::Justification::centredLeft);
            }
            return;
        }
        const int labelWidth=std::min(125,area.getWidth()/4);
        const int cellWidth=(area.getWidth()-labelWidth)/12;
        auto head=area.removeFromTop(20);
        static constexpr const char* pitches[]{"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
        head.removeFromLeft(labelWidth);
        for(auto name:pitches) g.drawText(name,head.removeFromLeft(cellWidth),juce::Justification::centred);
        const int height=std::min(27,area.getHeight()/matrix.size());
        for(int i=0;i<matrix.size();++i) {
            auto row=area.removeFromTop(height);
            const auto entry=matrix[i];
            const bool selected=entry["role"].toString()=="SELECTED";
            g.setColour(selected ? juce::Colour(0xffa9ebcf) : juce::Colour(0xffb8c9d8));
            g.drawText(entry["label"].toString(),row.removeFromLeft(labelWidth),juce::Justification::centredLeft);
            const auto scale=entry["scale"], chord=entry["chord"];
            for(int pc=0;pc<12;++pc) {
                const auto tile=row.removeFromLeft(cellWidth).reduced(1,2);
                const bool inScale=scale.getArray() && scale.getArray()->contains(juce::var(pc));
                g.setColour(juce::Colour(inScale ? (selected ? 0xff488c77 : 0xff39586a) : 0xff253442));
                g.fillRect(tile);
                if(chord.getArray() && chord.getArray()->contains(juce::var(pc))) {
                    g.setColour(juce::Colour(0xffe8c591)); g.drawRect(tile);
                }
            }
        }
    }
private:
    juce::var data;
    bool connections = false;
};
}
