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
