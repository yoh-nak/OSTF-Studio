#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class LiveChordGuide final : public juce::Component {
public:
    void display(juce::String section,juce::String current,juce::String next,juce::String nextPlace,juce::String visits) {
        const std::array<juce::String,5> text{section,current,next,nextPlace,visits};
        if(text==labels) return;
        labels=text; setDescription(labels[0]+" / "+labels[1]+" / "+labels[2]+" / "+labels[3]+" / "+labels[4]);repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff19272c));
        auto area=getLocalBounds().reduced(12,3);
        const bool big=getHeight()>150;
        auto itinerary=big || area.getWidth()<900 ? juce::Rectangle<int>{} : area.removeFromRight(area.getWidth()/4);
        auto section=area.removeFromLeft(std::min(area.getWidth()/4,210));
        auto now=area.removeFromLeft(area.getWidth()/2);
        const auto draw=[&](juce::Rectangle<int> r,const juce::String& title,const juce::String& value,juce::Colour colour,int height){
            g.setFont(juce::FontOptions(big?18.f:12.f)); g.setColour(juce::Colour(0xffbbcccb));
            g.drawText(title,r.removeFromTop(big?32:17),juce::Justification::centredLeft);
            g.setColour(colour);g.setFont(juce::FontOptions(float(height),juce::Font::bold));
            g.drawFittedText(value.isEmpty()?juce::String::charToString(0x2014):value,r,juce::Justification::centredLeft,1,.62f);
        };
        if(big) {const auto h=std::min(220,section.getHeight());section=section.withSizeKeepingCentre(section.getWidth(),h);now=now.withSizeKeepingCentre(now.getWidth(),h);area=area.withSizeKeepingCentre(area.getWidth(),h);}
        draw(section,TRANS("Section"),labels[0],juce::Colours::white,big?31:18);
        draw(now,TRANS("Current chord"),labels[1],juce::Colour(0xffffcf83),big?90:28);
        draw(area,TRANS("Next chord")+"   "+labels[3],labels[2],juce::Colour(0xffa2e5cd),big?80:26);
        if(!itinerary.isEmpty()) draw(itinerary,TRANS("Playing order"),labels[4],juce::Colours::white,14);
        if(big){g.setFont(juce::FontOptions(18.f));g.drawFittedText(labels[4],getLocalBounds().removeFromBottom(52).reduced(12,2),juce::Justification::centredLeft,2);}
    }
private:
    std::array<juce::String,5> labels;
};
