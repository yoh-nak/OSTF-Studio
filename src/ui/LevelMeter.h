#pragma once

#include "ui/AudioMeters.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

class LevelMeter final : public juce::Component {
public:
    explicit LevelMeter(bool vertical = false) : vertical_(vertical) { setInterceptsMouseClicks(false, false); }

    void update(ostfstudio::StereoPeak peak, double now) {
        const auto fall = static_cast<float>(24.0 * std::max(0.0, now - lastUpdate_));
        const auto elapsedFrom = lastUpdate_;
        lastUpdate_ = now;
        bool changed = false;
        for (std::size_t c = 0; c < peak.size(); ++c) {
            const float db = juce::Decibels::gainToDecibels(peak[c], floorDb);
            const float next = std::max(db, levels_[c] - fall);
            const auto previous = held_[c];
            const auto decay = 18.f * float(std::max(0.,now - std::max(elapsedFrom,holdUntil_[c])));
            held_[c] = std::max(floorDb,previous - decay);
            if (db > floorDb && db >= held_[c]) {
                held_[c] = db;
                holdUntil_[c] = now + 1.5;
            }
            changed |= previous != held_[c];
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
        held_.fill(floorDb);
        holdUntil_.fill(0.);
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
            band(floorDb, -12, juce::Colour(0xff49ce75));
            band(-12, -3, juce::Colour(0xffffd14b));
            band(-3, 0, juce::Colour(0xfff65055));
            if (held_[c] > floorDb) {
                const auto pos = std::clamp((held_[c]-floorDb)/-floorDb,0.f,1.f);
                g.setColour(juce::Colour(0xfff6ecc5));
                if (vertical_) g.fillRect(bar.getX(),std::clamp(bar.getBottom()-pos*bar.getHeight(),bar.getY(),std::max(bar.getY(),bar.getBottom()-2)),bar.getWidth(),2.f);
                else g.fillRect(std::min(bar.getRight()-2,bar.getX()+pos*bar.getWidth()),bar.getY(),2.f,bar.getHeight());
            }
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
    ostfstudio::StereoPeak held_{floorDb,floorDb};
    std::array<double,2> holdUntil_{},clipUntil_{};
    std::array<bool, 2> clipped_{};
    double lastUpdate_ = 0;
};
