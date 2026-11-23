#pragma once

#include "ui/AudioMeters.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

class LevelMeter final : public juce::Component {
public:
    explicit LevelMeter(bool vertical = false) : vertical_(vertical) { setInterceptsMouseClicks(false, false); }

    void update(ostfstudio::StereoPeak peak, double now) {
        const auto fall = static_cast<float>(24.0 * std::max(0.0, now - lastUpdate_));
        lastUpdate_ = now;
        bool changed = false;
        for (std::size_t c = 0; c < peak.size(); ++c) {
            const float db = juce::Decibels::gainToDecibels(peak[c], floorDb);
            const float next = std::max(db, levels_[c] - fall);
            if (db >= 0.0f) clipUntil_[c] = now + 1.0;
            const bool clipped = now < clipUntil_[c];
            changed |= levels_[c] != next || clipped_[c] != clipped;
            levels_[c] = next;
            clipped_[c] = clipped;
        }
        if (changed) repaint();
    }
    void clear() {
        levels_.fill(floorDb);
        clipped_.fill(false);
        clipUntil_.fill(0.0);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto bars = getLocalBounds().toFloat();
        const bool scale = vertical_ && bars.getWidth() >= 34 && bars.getHeight() >= 65;
        auto legend = scale ? bars.removeFromLeft(20.0f) : juce::Rectangle<float>{};
        if (scale) {
            bars.reduce(0, 5);
            g.setColour(juce::Colour(0xffbccbd1));
            g.setFont(juce::FontOptions(9.0f));
            for (int db : {0, -12, -24, -48}) {
                g.drawText(juce::String(db), legend.withY(bars.getY() - db / 60.0f * bars.getHeight() - 5)
                           .withHeight(10), juce::Justification::centredLeft);
            }
        }
        if (!vertical_ && bars.getWidth() >= 140 && bars.getHeight() >= 22) {
            auto axis = bars.removeFromBottom(12);
            g.setColour(juce::Colour(0xffbccbd1));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("-60", axis, juce::Justification::centredLeft);
            const auto tick = axis.getX() + 0.6f * axis.getWidth();
            g.drawText("-24", axis.withX(tick - 12).withWidth(24), juce::Justification::centred);
            g.drawText("0 dBFS", axis, juce::Justification::centredRight);
        }
        auto left = vertical_ ? bars.removeFromLeft(bars.getWidth() * 0.5f) : bars.removeFromTop(bars.getHeight() * 0.5f);
        for (std::size_t c = 0; c < levels_.size(); ++c) {
            const auto bar = (c == 0 ? left : bars).reduced(vertical_ ? 1.0f : 0.0f, vertical_ ? 0.0f : 0.5f);
            g.setColour(juce::Colour(0xff152026));
            g.fillRect(bar);
            auto band = [&](float low, float high, juce::Colour colour) {
                const auto top = std::min(levels_[c], high);
                if (top <= low) return;
                const auto start = (low - floorDb) / -floorDb, end = (top - floorDb) / -floorDb;
                g.setColour(colour);
                if (vertical_) g.fillRect(bar.getX(), bar.getBottom() - end * bar.getHeight(),
                                          bar.getWidth(), (end - start) * bar.getHeight());
                else g.fillRect(bar.getX() + start * bar.getWidth(), bar.getY(),
                                (end - start) * bar.getWidth(), bar.getHeight());
            };
            band(floorDb, -12, juce::Colour(0xff56ceb0));
            band(-12, -3, juce::Colour(0xffefbb65));
            band(-3, 0, juce::Colour(0xffed8961));
            if (clipped_[c]) {
                g.setColour(juce::Colour(0xffff5654));
                g.fillRect(vertical_ ? bar.withHeight(2.0f) : bar.withLeft(bar.getRight() - 3.0f));
            }
        }
    }
private:
    static constexpr float floorDb = -60.0f;
    const bool vertical_;
    ostfstudio::StereoPeak levels_{floorDb, floorDb};
    std::array<double, 2> clipUntil_{};
    std::array<bool, 2> clipped_{};
    double lastUpdate_ = 0;
};
