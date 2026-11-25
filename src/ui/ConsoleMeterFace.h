#pragma once
#include "ui/AudioMeters.h"
#include <juce_graphics/juce_graphics.h>

namespace ostfstudio::console {
inline void drawVu(juce::Graphics& g, juce::Rectangle<float> bounds, float amplitude, const juce::String& legend) {
    juce::Graphics::ScopedSaveState state(g);
    const auto fit = juce::RectanglePlacement(juce::RectanglePlacement::centred)
        .getTransformToFit({0.f, 0.f, 152.f, 78.f}, bounds);
    g.addTransform(fit);
    g.setColour(juce::Colour(0xff101516));
    g.fillRoundedRectangle(0, 0, 152, 78, 5);
    g.setColour(juce::Colour(0xff657072));
    g.drawRoundedRectangle(.6f, .6f, 150.8f, 76.8f, 4, 1);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffaf956b), 76, 4,
                                            juce::Colour(0xfff6eac9), 76, 59, false));
    g.fillRoundedRectangle(4, 4, 144, 69, 2.5f);
    const juce::Colour ink(0xff332e25), red(0xffaa3021);
    const juce::Point<float> pivot{76.f, 75.f};
    auto point = [&](float gain, float radius) {
        const float angle = -.96f + 1.92f * std::clamp(gain / 1.41254f, 0.f, 1.f);
        return pivot.getPointOnCircumference(radius, angle);
    };
    for (int i = 1; i <= 96; ++i) {
        const auto gain = 1.41254f * i / 96;
        g.setColour(gain > 1 ? red : ink);
        g.drawLine({point(gain - 1.41254f / 96, 57), point(gain, 57)}, gain > 1 ? 2.6f : 1.1f);
    }
    for (int db = -20; db <= 3; ++db) {
        const bool numbered = db == -20 || db == -10 || db == -5 || db == -3 || db == 0 || db == 3;
        if (!numbered && db < -10) continue;
        const auto gain = juce::Decibels::decibelsToGain(float(db));
        g.setColour(db > 0 ? red : ink);
        g.drawLine({point(gain, 57), point(gain, numbered ? 62.f : 60.f)}, numbered ? 1.3f : .8f);
        if (numbered) {
            const auto pos = point(gain, 68.f);
            g.setFont(juce::FontOptions(9.f, juce::Font::bold));
            g.drawText(db == 3 ? "+3" : juce::String(db), juce::Rectangle<float>{21, 11}.withCentre(pos),
                       juce::Justification::centred);
        }
    }
    g.setFont(juce::FontOptions(12.f, juce::Font::bold));
    g.setColour(ink);
    g.drawText(legend, 4, 38, 144, 17, juce::Justification::centred);
    const auto tip = point(amplitude, 60);
    g.setColour(juce::Colours::black.withAlpha(.20f));
    g.drawLine({pivot.translated(1, 1), tip.translated(1.7f, 1.4f)}, 1.8f);
    g.setColour(juce::Colour(0xff292720));
    g.drawLine({pivot, tip}, 1.4f);
    g.setColour(juce::Colour(0xff232829));
    g.fillRoundedRectangle(59, 68, 34, 9, 4);
}

inline void drawChannelVu(juce::Graphics& g, juce::Rectangle<float> bounds, StereoPeak levels, int channels,
                          const juce::String& label) {
    if (channels == 1) {
        drawVu(g,bounds,std::max(levels[0],levels[1]),label);
        return;
    }
    auto left = bounds.removeFromLeft(bounds.getWidth() * .5f);
    drawVu(g,left.reduced(1,0),levels[0],"L   " + label);
    drawVu(g,bounds.reduced(1,0),levels[1],"R   " + label);
}

inline void drawMasterDetails(juce::Graphics& g, juce::Rectangle<float> bounds, const MasterReadout& data,
                              const std::array<float,31>& bands) {
    g.setColour(juce::Colour(0xff101b20));
    g.fillRoundedRectangle(bounds,4.f);
    auto area = bounds.reduced(7,5);
    const auto mint = juce::Colour(0xff94e5d3), dim = juce::Colour(0xffadbfca), amber = juce::Colour(0xffffc47a);
    auto number = [](double value) { return std::isfinite(value) ? juce::String(value,1) : juce::String::charToString(0x2014); };
    auto db = [&](float amplitude) { return number(amplitude > 0 ? 20.0*std::log10(amplitude) : -INFINITY); };
    auto cell = [&](juce::Rectangle<float> rect, juce::String title, juce::String value) {
        g.setFont(juce::FontOptions(10.f)); g.setColour(dim);
        g.drawText(title,rect.removeFromTop(13),juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(16.f,juce::Font::bold)); g.setColour(mint);
        g.drawText(value,rect,juce::Justification::centredLeft);
    };
    const auto& levels = data.levels;
    const auto width = area.getWidth()/3;
    auto row = area.removeFromTop(34);
    cell(row.removeFromLeft(width),"M  LUFS",number(levels.momentary));
    cell(row.removeFromLeft(width),"S  LUFS",number(levels.shortTerm));
    cell(row,"I  LUFS",number(levels.integrated));
    row = area.removeFromTop(34);
    cell(row.removeFromLeft(width),"RMS  dBFS",db(std::max(levels.rms[0],levels.rms[1])));
    cell(row.removeFromLeft(width),"PEAK  dBFS",db(std::max(levels.peak[0],levels.peak[1])));
    cell(row,"MAX  dBTP",db(std::max(levels.maxTruePeak[0],levels.maxTruePeak[1])));
    g.setFont(juce::FontOptions(10.f));
    auto bipolar = [&](juce::String label, double value, bool valid) {
        auto line = area.removeFromTop(17);
        g.setColour(dim); g.drawText(label,line.removeFromLeft(83),juce::Justification::centredLeft);
        g.drawText(valid ? juce::String(value,2) : juce::String::charToString(0x2014),
                   line.removeFromRight(37),juce::Justification::centredRight);
        auto bar = line.reduced(2,6);
        g.setColour(juce::Colour(0xff3f555c)); g.fillRect(bar);
        g.setColour(dim); g.drawVerticalLine(int(bar.getCentreX()),bar.getY()-2,bar.getBottom()+2);
        if (valid) { g.setColour(amber); g.fillRect(float(bar.getX()+(.5+.5*value)*bar.getWidth())-1.5f,bar.getY()-1,3.f,bar.getHeight()+2); }
    };
    const auto sum = levels.rms[0]+levels.rms[1];
    bipolar("BALANCE  L/R",sum > 0 ? (levels.rms[1]-levels.rms[0])/sum : 0.,sum > 0);
    bipolar("PHASE  -1/+1",levels.correlation,levels.hasCorrelation);
    g.setColour(dim); g.drawText("SHORT-TERM   /   60 s",area.removeFromTop(17),juce::Justification::centredLeft);
    auto plot = area.removeFromTop(area.getHeight() * .44f);
    auto y = [&](float level) { return plot.getBottom()-plot.getHeight()*std::clamp((level+60.f)/60.f,0.f,1.f); };
    for (int tick : {-12,-24,-36,-48}) {
        g.setColour(juce::Colour(0xff35454a)); g.drawHorizontalLine(int(y(float(tick))),plot.getX(),plot.getRight());
    }
    juce::Path history;
    bool connected = false;
    const auto size = unsigned(data.shortHistory.size());
    for (unsigned i=0;i<data.historyCount;++i) {
        const auto sample = data.shortHistory[(data.historyWrite+size-data.historyCount+i)%size];
        if (!std::isfinite(sample)) { connected=false; continue; }
        const float x = plot.getX()+plot.getWidth()*float(size-data.historyCount+i)/(size-1);
        if (connected) history.lineTo(x,y(sample)); else history.startNewSubPath(x,y(sample));
        connected=true;
    }
    g.setColour(mint); g.strokePath(history,juce::PathStrokeType(1.5f));
    g.setColour(dim); g.drawText("SPECTRUM   /   31 BANDS",area.removeFromTop(17),juce::Justification::centredLeft);
    auto scale=area.removeFromBottom(13);
    g.drawText("20 Hz",scale,juce::Justification::centredLeft);
    g.drawText("20 kHz",scale,juce::Justification::centredRight);
    const float step=area.getWidth()/bands.size();
    for (size_t i=0;i<bands.size();++i) {
        const auto h=area.getHeight()*std::clamp((juce::Decibels::gainToDecibels(bands[i],-72.f)+72.f)/72.f,0.f,1.f);
        g.setColour(mint); g.fillRect(area.getX()+i*step,area.getBottom()-h,std::max(1.f,step-1),h);
    }
}

inline void drawPhaseScope(juce::Graphics& g, juce::Rectangle<float> bounds, const MasterReadout& data) {
    if (bounds.isEmpty()) return;
    g.setColour(juce::Colour(0xff101b1b));
    g.fillRoundedRectangle(bounds, 5);
    g.setColour(juce::Colour(0xff647572));
    g.drawRoundedRectangle(bounds.reduced(.5f), 5, 1);
    auto screen = bounds.reduced(9, 4);
    g.setColour(juce::Colour(0xffbccdc4));
    g.setFont(juce::FontOptions(11.f, juce::Font::bold));
    g.drawText("PHASE", screen.removeFromTop(14), juce::Justification::centredLeft);
    const auto centre = screen.getCentre();
    const float r = std::min(screen.getWidth(), screen.getHeight()) * .46f;
    if (r < 2) return;
    auto project = [&](float x, float y) { return centre.translated(x*r, -y*r); };
    g.setColour(juce::Colour(0xff354b42));
    for (auto scale : {.5f, 1.f})
        g.drawEllipse(juce::Rectangle<float>{2*r*scale, 2*r*scale}.withCentre(centre), 1);
    for (const auto p : {juce::Point<float>{1, 0}, {0, 1}, {.707f, .707f}, {-.707f, .707f}})
        g.drawLine({project(-p.x,-p.y), project(p.x,p.y)});
    g.setColour(juce::Colour(0xffb3c5b8));
    g.setFont(juce::FontOptions(10.f));
    for (int side : {-1, 1})
        g.drawText(side < 0 ? "L" : "R", juce::Rectangle<float>{12,12}.withCentre(project(side*.96f,.74f)),
                   juce::Justification::centred);
    // A mono signal draws vertically. Walk the ring in time order so wrapping never joins unrelated samples.
    juce::Path trace;
    for (size_t n = 0; n < data.scope.size(); ++n) {
        const auto& lr = data.scope[(n + data.nextPoint) % data.scope.size()];
        const auto xy = project(std::clamp(lr[1]-lr[0], -1.f, 1.f), std::clamp(lr[0]+lr[1], -1.f, 1.f));
        if (n == 0) trace.startNewSubPath(xy); else trace.lineTo(xy);
    }
    g.setColour(juce::Colour(0x4650dc9b));
    g.strokePath(trace, juce::PathStrokeType(3));
    g.setColour(juce::Colour(0xff97f5b9));
    g.strokePath(trace, juce::PathStrokeType(1.1f));
}
} // namespace ostfstudio::console
