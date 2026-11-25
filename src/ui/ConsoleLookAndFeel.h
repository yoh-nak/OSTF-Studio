#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ostfstudio::console {
enum class Theme { studioBlue, harmonic, graphite, aubergine, forest, indigo, copper, count };
struct Finish {
    const char* key;
    const char* name;
    juce::uint32 background, panel, accent;
};

inline constexpr Finish finishes[] {
    {"blue", "Studio Blue", 0xff111d29, 0xff354756, 0xff8dc9e7},
    {"harmonic", "Harmonic colours", 0xff15181d, 0xff364448, 0xff83d4c7},
    {"graphite", "Graphite", 0xff202122, 0xff424446, 0xffd7d7cf},
    {"aubergine", "Aubergine", 0xff211925, 0xff49394c, 0xffe2b1d1},
    {"forest", "Forest Console", 0xff101e1a, 0xff304a40, 0xffe5c782},
    {"indigo", "Midnight Indigo", 0xff151a31, 0xff354161, 0xffaeaaff},
    {"copper", "Copper Console", 0xff241b18, 0xff514039, 0xfff1ab7c}
};

static_assert(std::size(finishes) == static_cast<std::size_t>(Theme::count));
inline const Finish& finishFor(Theme theme) { return finishes[static_cast<std::size_t>(theme)]; }

class LookAndFeel final : public juce::LookAndFeel_V4 {
public:
    enum { panelColourId = 0x2f74100, accentColourId };
    explicit LookAndFeel(const Finish& finish = finishes[0]) { setFinish(finish); }
    void setFinish(const Finish& finish) {
        setColour(panelColourId, juce::Colour(finish.panel));
        setColour(accentColourId, juce::Colour(finish.accent));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(finish.background));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(finish.panel).brighter(.22f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                          float start, float end, juce::Slider& slider) override {
        const float r = .48f * static_cast<float>(std::min(w, h));
        if (r < 2.f) return;
        const juce::Point<float> c(x + .5f * w, y + .5f * h);
        const auto at = [&](float distance, float angle) { return c.getPointOnCircumference(distance, angle); };
        auto circle = [&](float radius) { return juce::Rectangle<float>(2*radius, 2*radius).withCentre(c); };
        const auto accent = findColour(accentColourId);
        const float angle = start + position * (end - start);
        if (!slider.isEnabled()) g.beginTransparencyLayer(.48f);
        const int intervals = r < 20.f ? 6 : 12;
        for (int i = 0; i <= intervals; ++i) {
            const float a = start + (end - start) * i / intervals;
            g.setColour(i % 3 == 0 ? juce::Colour(0xffd1d3cc) : juce::Colour(0xff828787));
            g.drawLine({at(r * (i % 3 == 0 ? .85f : .90f), a), at(r, a)}, 1.f);
        }
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, .80f*r, .80f*r, 0, start, end, true);
        g.setColour(juce::Colour(0xff101519));
        g.strokePath(arc, juce::PathStrokeType(std::max(1.4f, .052f*r)));
        arc.clear();
        arc.addCentredArc(c.x, c.y, .80f*r, .80f*r, 0, start, angle, true);
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(std::max(1.4f, .052f*r)));
        auto rim = circle(.72f*r);
        g.setColour(juce::Colours::black.withAlpha(.58f));
        g.fillEllipse(rim.translated(0.f, std::max(1.f, .06f*r)).expanded(.5f));
        const bool active = slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(true);
        verticalGradient(g, rim, active ? accent.getARGB() : 0xff8b9291, 0xff15191c);
        g.fillEllipse(rim);
        const auto skirt = circle(.65f*r);
        verticalGradient(g, skirt, 0xff41494b, 0xff22272b);
        g.fillEllipse(skirt);
        if (r >= 20.f) {
            g.setColour(juce::Colours::black.withAlpha(.46f));
            for (int i = 0; i < 28; ++i) {
                const float a = angle + juce::MathConstants<float>::twoPi * i / 28.f;
                g.drawLine({at(.54f*r, a), at(.64f*r, a)}, 1.f);
            }
        }
        const auto top = circle(.53f*r);
        if (slider.getProperties()["consoleHardware"]) {
            juce::ColourGradient face(juce::Colour(0xff89918f), c.x - .29f*r, c.y - .36f*r,
                                      juce::Colour(0xff282e32), c.x + .50f*r, c.y + .48f*r, true);
            face.addColour(.48, juce::Colour(0xff545d60));
            g.setGradientFill(face);
        } else verticalGradient(g, top, 0xff646d6d, 0xff32383b);
        g.fillEllipse(top);
        g.setColour(juce::Colour(0xff111718));
        g.drawLine({at(.20f*r, angle), at(.65f*r, angle)}, std::max(3.f, .13f*r));
        g.setColour(juce::Colour(0xfffff4dc));
        g.drawLine({at(.21f*r, angle), at(.63f*r, angle)}, std::max(1.6f, .078f*r));
        if (!slider.isEnabled()) g.endTransparencyLayer();
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos,
                          float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override {
        const bool vertical = style == juce::Slider::LinearVertical;
        if (!vertical && style != juce::Slider::LinearHorizontal) {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, slider);
            return;
        }
        if (w <= 0 || h <= 0) return;
        juce::Graphics::ScopedSaveState save(g);
        if (!slider.isEnabled()) g.beginTransparencyLayer(.48f);
        if (!vertical) {
            // Draw both orientations in rail coordinates, with increasing travel down the rail.
            g.addTransform(juce::AffineTransform(0, 1, (float)x, -1, 0, (float)(y+h)));
            pos -= x;
            std::swap(w,h);
            x = y = 0;
        }
        const float centre = x + w*.5f;
        const bool hardware = vertical && static_cast<bool>(slider.getProperties()["consoleHardware"]);
        const float width = std::min(vertical ? (hardware ? 35.f : 29.f) : 16.f, w * .84f);
        const float halfHeight = std::min(vertical ? (hardware ? 12.f : 10.f) : 5.f, h*.20f);
        const auto rail = juce::Rectangle<float>(centre - 2.2f, (float)y, 4.4f, (float)h);
        g.setColour(juce::Colour(0xff687070));
        g.fillRoundedRectangle(rail.expanded(1.f), 2.6f);
        g.setColour(juce::Colour(0xff0d1114));
        g.fillRoundedRectangle(rail, 2.f);
        if (vertical && w >= 50 && h >= 74 && slider.getTextValueSuffix() == " dB"
            && slider.getMinimum() == -60.0 && slider.getMaximum() == 6.0) {
            g.setFont(juce::FontOptions(9.f));
            float previous = -20.f;
            for (int db : {6, 0, -6, -12, -18, -24, -36, -48, -60}) {
                const float tick = y + h * (1.f - static_cast<float>(slider.valueToProportionOfLength(db)));
                g.setColour(db == 0 ? findColour(accentColourId) : juce::Colour(0xffabb1ad));
                g.drawHorizontalLine(juce::roundToInt(tick), centre + 5.f, centre + (db == 0 ? 15.f : 10.f));
                if (tick - previous >= 11.f) {
                    g.drawText(juce::String(db), x, juce::roundToInt(tick)-5, w/2-7, 10, juce::Justification::centredRight);
                    previous = tick;
                }
            }
        } else if (w >= 32 && h >= 52) {
            const int divisions = std::clamp(h / 13, 4, 24);
            g.setColour(juce::Colour(0xffacb1ac));
            for (int i = 0; i <= divisions; ++i) {
                const float tick = y + h * (float)i/divisions;
                const float length = i % 2 == 0 ? 8.f : 4.f;
                g.drawHorizontalLine(juce::roundToInt(tick), centre + 5.f, centre + 5.f + length);
            }
        }
        auto cap = juce::Rectangle<float>(width, 2*halfHeight).withCentre({centre, pos});
        g.setColour(juce::Colours::black.withAlpha(.65f));
        g.fillRoundedRectangle(cap.translated(.5f, 1.5f).expanded(.8f), 2.8f);
        juce::ColourGradient metal(juce::Colour(0xfff2f0e7), centre, cap.getY(),
                                   juce::Colour(0xff656e70), centre, cap.getBottom(), false);
        metal.addColour(.22, juce::Colour(0xffbbc0bd));
        metal.addColour(.46, juce::Colour(0xff89908f));
        metal.addColour(.76, juce::Colour(0xffe1e0d7));
        g.setGradientFill(metal);
        g.fillRoundedRectangle(cap, 2.f);
        if (hardware && width >= 26.f && halfHeight >= 9.f) {
            for (const float offset : {-7.f, -4.f, 4.f, 7.f}) {
                g.setColour(juce::Colour(0x65707978));
                g.drawHorizontalLine(juce::roundToInt(pos + offset), cap.getX()+3.f, cap.getRight()-3.f);
                g.setColour(juce::Colour(0x66ffffff));
                g.drawHorizontalLine(juce::roundToInt(pos + offset + 1.f), cap.getX()+3.f, cap.getRight()-3.f);
            }
        }
        g.setColour(juce::Colour(0xff20282b));
        g.fillRect(cap.getX()+1, pos-1.45f, width-2.f, 2.9f);
        g.setColour(slider.isEnabled() ? findColour(accentColourId) : juce::Colour(0xff939796));
        g.fillRect(cap.getX()+2, pos-.55f, std::max(1.f,width-4.f), 1.1f);
        if (!slider.isEnabled()) g.endTransparencyLayer();
    }

    static void verticalGradient(juce::Graphics& g, juce::Rectangle<float> rect,
                                 juce::uint32 light, juce::uint32 dark) {
        g.setGradientFill(juce::ColourGradient(juce::Colour(light), rect.getCentreX(), rect.getY(),
                                               juce::Colour(dark), rect.getCentreX(), rect.getBottom(), false));
    }
};
}
