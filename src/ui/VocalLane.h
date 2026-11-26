#pragma once
#include "ui/PianoNoteGrid.h"
#include "core/VocalDelivery.h"

class VocalLane final : public juce::Component {
public:
    explicit VocalLane(PianoNoteGrid& notes) : grid(notes) {
        addAndMakeVisible(entrance);
        addAndMakeVisible(voice);
        addAndMakeVisible(overlay);
        overlay.setButtonText(TRANS("Overlay vocal parts"));
        overlay.setClickingTogglesState(true);
        setComponentID("piano-vocal-lanes");
        entrance.setButtonText(TRANS("First lyric"));
        voice.setButtonText(TRANS("Sing this track"));
    }
    juce::TextButton entrance, voice, overlay;
    std::function<const ostfstudio::VocalNote*(const juce::String&)> lookup;
    std::function<void(const ostfstudio::PitchedNote&)> edit;
    std::function<double(juce::int64)> secondsAt;
    std::vector<ostfstudio::OstfpPreview::DisplayPhrase> phrases;
    int gridX = 0;
    void drawContour(juce::Graphics& g, const ostfstudio::VocalNote& data, const ostfstudio::PitchedNote& note,
                     float x, float width, float centre, float pixelsPerSemitone) const {
        const double duration = secondsAt(note.endTick)-secondsAt(note.startTick);
        if (!(duration > 0) || width < 1) return;
        const int segments = std::clamp(juce::roundToInt(width),2,512);
        juce::Path path;
        for (int i=0; i<=segments; ++i) {
            const float t = float(i)/segments;
            const auto cents = data.technique=="scoop" ? ostfstudio::singing::scoopCents(t*duration,data.intensity)
                : ostfstudio::singing::vibratoCents(t*duration,data.intensity);
            const float y = centre - float(cents*.01)*pixelsPerSemitone;
            if (!i) path.startNewSubPath(x,y);
            else path.lineTo(x+width*t,y);
        }
        g.strokePath(path,juce::PathStrokeType(1.5f));
    }
    void resized() override {
        const int w = juce::jlimit(0, 125, gridX - 106);
        entrance.setBounds(2, 0, w, getHeight()/2);
        voice.setBounds(2, getHeight()/2, w, getHeight()-getHeight()/2);
        overlay.setBounds(w+5, getHeight()/2, std::min(124,std::max(0,gridX-w-108)),getHeight()-getHeight()/2);
    }
    void paint(juce::Graphics& g) override {
        const int row = getHeight()/3;
        if (row < 1 || !lookup) return;
        g.fillAll(juce::Colour(0xff192930));
        const char* labels[] = {phrases.empty() ? "Note lyrics" : "Display lyrics", "Pronunciation", "Vocal expression"};
        for (int i = 0; i < 3; ++i) {
            g.setColour(juce::Colour(0xff30474f));
            g.drawHorizontalLine(row*i, 0, float(getWidth()));
            g.setColour(juce::Colour(0xffb5dcd4));
            g.setFont(juce::FontOptions(12.f));
            g.drawFittedText(juce::translate(labels[i]), entrance.getRight()+3, i*row,
                             std::max(0,gridX-entrance.getRight()-7), row, juce::Justification::centredRight, 1);
        }
        const auto range = grid.tickRange();
        if (range.isEmpty() || getWidth() <= gridX) return;
        g.reduceClipRegion(gridX, 0, getWidth()-gridX, getHeight());
        auto x = [&](double tick) { return gridX + juce::roundToInt((tick-range.getStart())/range.getLength()*(getWidth()-gridX)); };
        for (const auto& note : grid.notes) {
            if (note.startTick >= range.getEnd()) break;
            if (note.endTick <= range.getStart()) continue;
            const auto* data = lookup(note.id);
            if (!data) continue;
            const int left = x(note.startTick), w = std::max(1,x(note.endTick)-left-1);
            g.setColour(juce::Colour(0xff2a4248));
            const int top = phrases.empty() ? 1 : row;
            g.fillRect(left,top,w,getHeight()-top);
            g.setColour(juce::Colours::white);
            if (phrases.empty()) g.drawText(data->text, left+2,0,w-2,row,juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff9ce4d8));
            g.drawText(data->pronunciation,left+2,row,w-2,row,juce::Justification::centredLeft);
            if (data->technique.isEmpty()) continue;
            g.setColour(juce::Colour(0xffffc871));
            if (data->technique == "vibrato" || data->technique == "scoop") {
                drawContour(g,*data,note,float(left),float(w),2.45f*row,.36f*row);
            } else g.drawText(data->technique,left+2,row*2,w-2,row,juce::Justification::centredLeft);
        }
        for (const auto& phrase : phrases) {
            if (phrase.start >= range.getEnd()) break;
            if (phrase.end <= range.getStart()) continue;
            const int left = std::max(gridX,x(phrase.start)), w = x(phrase.end)-left-1;
            g.setColour(juce::Colour(0xff285750));
            g.fillRect(left,1,w,row-1);
            g.setColour(juce::Colours::white);
            g.drawText(phrase.text,left+3,0,std::max(0,w-5),row,juce::Justification::centredLeft);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!edit || e.x < gridX || getWidth() <= gridX) return;
        if (!phrases.empty() && e.y < getHeight()/3) return;
        const auto range = grid.tickRange();
        const double tick = range.getStart()+(e.x-gridX)*range.getLength()/(getWidth()-gridX);
        for (const auto& note : grid.notes) {
            if (note.startTick > tick) break;
            if (tick < note.endTick) { edit(note); return; }
        }
    }
private:
    PianoNoteGrid& grid;
};
