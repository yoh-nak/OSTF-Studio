#pragma once
#include "ui/AudioClipWaveforms.h"
#include "ui/ArrangementRuler.h"

// Clip view shows one complete source cycle so that markers remain reachable after trimming or looping.
class AudioClipEditor final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit AudioClipEditor(AudioClipWaveforms& source) : waves(source) {
        setComponentID("audio-clip-editor");
        for (auto* button : {&loop, &warp, &melodyne, &apply, &splice}) addAndMakeVisible(button);
        addAndMakeVisible(beats);
        addAndMakeVisible(recordRuler);
        addAndMakeVisible(channels);
        channels.addItem(TRANS("Mono input"), 1);
        channels.addItem(TRANS("Stereo input"), 2);
        channels.setSelectedId(1, juce::dontSendNotification);
        splice.onClick = [this] { if (onSplice) onSplice(); };
        loop.setClickingTogglesState(true);
        warp.setClickingTogglesState(true);
        beats.setEditableText(true);
        for (int value : {1, 2, 3, 4, 6, 8, 12, 16, 32}) beats.addItem(juce::String(value), value);
        beats.setTooltip(TRANS("Length of one cycle in quarter notes"));
        loop.setTooltip(TRANS("Enable loop, then drag the clip's right edge in the arrangement"));
        warp.setTooltip(TRANS("Fit the sample to beats. Pitch follows playback speed."));
        setTooltip(TRANS("Double-click the waveform to add or remove a warp marker. Drag a marker to move it."));
        loop.onClick = [this] { auto edit = timing; edit.loop = loop.getToggleState(); commit(edit); };
        warp.onClick = [this] {
            auto edit = timing;
            edit.cycleTicks = warp.getToggleState() ? std::max<juce::int64>(1, std::llround(nativeLength)) : 0;
            edit.markers.clear();
            commit(edit);
        };
        beats.onChange = [this] {
            const auto value = beats.getText().trim().getDoubleValue();
            if (timing.cycleTicks && std::isfinite(value) && value > 0 && value <= 1024) {
                auto edit = timing;
                edit.cycleTicks = std::max<juce::int64>(1, std::llround(value * ppq));
                commit(edit);
            } else sync();
        };
        apply.onClick = [this] { if (onApply) onApply(); };
        melodyne.onClick = [this] { if (onMelodyne) onMelodyne(); };
    }
    std::function<const ostfstudio::ProjectAudio*()> sourceClip;
    std::function<const ostfstudio::TempoMap&()> clock;
    std::function<void(const ostfstudio::ClipTiming&)> onEdit;
    std::function<void()> onMelodyne, onApply, onSplice;
    std::function<bool()> hasWorkingCopy;
    ArrangementRuler recordRuler;
    juce::ComboBox channels;
    void bind(juce::int64 resolution, bool editable) {
        ppq = std::max<juce::int64>(1, resolution);
        auto* clip = sourceClip();
        drag = -1;
        timing = clip ? clip->timing : ostfstudio::ClipTiming{};
        nativeLength = clip ? waves.nativeTicks(*clip, clock()) : 0;
        const bool available = editable && clip && nativeLength > 0;
        loop.setEnabled(available); warp.setEnabled(available); melodyne.setEnabled(available);
        apply.setVisible(available && hasWorkingCopy && hasWorkingCopy());
        resized();
        channels.setEnabled(editable);
        splice.setEnabled(editable);
        sync();
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8, 4);
        auto row = area.removeFromTop(26);
        recordRuler.setBounds(area.removeFromTop(23));
        channels.setBounds(row.removeFromLeft(110).reduced(2, 0));
        loop.setBounds(row.removeFromLeft(76).reduced(2, 0));
        warp.setBounds(row.removeFromLeft(123).reduced(2, 0));
        beats.setBounds(row.removeFromLeft(78).reduced(2, 0));
        row.removeFromLeft(70);
        melodyne.setBounds(row.removeFromLeft(105).reduced(2, 0));
        if (apply.isVisible()) apply.setBounds(row.removeFromLeft(134).reduced(2, 0));
        splice.setBounds(row.removeFromLeft(90).reduced(2, 0));
    }
    void paint(juce::Graphics& g) override {
        auto area = canvas();
        g.setColour(juce::Colour(0xff1b303d));
        g.fillRect(area);
        g.setColour(juce::Colours::white);
        g.drawText(TRANS("beats"), beats.getBounds().translated(beats.getWidth() + 3, 0), juce::Justification::centredLeft);
        auto* clip = sourceClip();
        if (!clip || nativeLength <= 0) {
            g.drawFittedText(clip ? TRANS("Missing WAV") : TRANS("Drop a WAV from Splice or a folder onto this audio track"),
                             area, juce::Justification::centred, 2);
            return;
        }
        const double duration = timing.cycleTicks ? double(timing.cycleTicks) : nativeLength;
        const auto ruler = area.removeFromTop(20);
        const double step = std::max(1.0, std::ceil(duration / ppq * 46 / std::max(1, area.getWidth())));
        for (double beat = 0; beat * double(ppq) < duration; beat += step) {
            const int x = area.getX() + juce::roundToInt(beat * double(ppq) / duration * area.getWidth());
            g.setColour(juce::Colour(0xff365467)); g.drawVerticalLine(x, float(ruler.getY()), float(area.getBottom()));
            g.setColour(juce::Colour(0xffd2e5ee)); g.drawText(juce::String(beat + 1, 0), x + 3, ruler.getY(), 44, 18, juce::Justification::left);
        }
        waves.draw(g, area.reduced(0, 3), *clip, clock(), double(clip->startTick), clip->startTick + duration, &timing);
        for (const auto p : timing.markers) {
            const int x = area.getX() + juce::roundToInt(p.position * area.getWidth());
            g.setColour(juce::Colour(0xffffbc63)); g.drawVerticalLine(x, float(ruler.getY()), float(area.getBottom()));
            g.fillRect(x - 3, ruler.getY(), 7, 9);
        }
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!warp.isEnabled() || !timing.cycleTicks || !canvas().contains(e.getPosition())) return;
        auto edit = timing;
        const auto x = phase(e.x);
        const int index = hit(e.x);
        if (index >= 0) edit.markers.erase(edit.markers.begin() + index);
        else if (edit.markers.size() < 128 && x > 0 && x < 1) {
            const auto it = std::lower_bound(edit.markers.begin(), edit.markers.end(), x,
                [](auto p, double position) { return p.position < position; });
            edit.markers.insert(it, {x, timing.sourcePhase(x)});
        }
        commit(edit);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        drag = warp.isEnabled() && canvas().contains(e.getPosition()) ? hit(e.x) : -1;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag < 0) return;
        const double low = drag ? timing.markers[size_t(drag - 1)].position : 0.;
        const double high = size_t(drag + 1) < timing.markers.size() ? timing.markers[size_t(drag + 1)].position : 1.;
        timing.markers[size_t(drag)].position = std::clamp(phase(e.x), std::nextafter(low, high), std::nextafter(high, low));
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (drag >= 0 && e.mouseWasDraggedSinceMouseDown()) commit(timing);
        drag = -1;
    }
private:
    juce::Rectangle<int> canvas() const { return getLocalBounds().reduced(8, 4).withTrimmedTop(56); }
    double phase(int x) const { const auto r = canvas(); return double(x - r.getX()) / std::max(1, r.getWidth()); }
    int hit(int x) const {
        for (size_t i = 0; i < timing.markers.size(); ++i)
            if (std::abs(phase(x) - timing.markers[i].position) * canvas().getWidth() <= 5) return int(i);
        return -1;
    }
    void commit(ostfstudio::ClipTiming edit) { if (edit.valid() && onEdit) onEdit(edit); }
    void sync() {
        loop.setToggleState(timing.loop, juce::dontSendNotification);
        warp.setToggleState(timing.cycleTicks > 0, juce::dontSendNotification);
        beats.setEnabled(warp.isEnabled() && timing.cycleTicks > 0);
        beats.setText(juce::String((timing.cycleTicks ? double(timing.cycleTicks) : nativeLength) / ppq, 3).trimCharactersAtEnd("0").trimCharactersAtEnd("."), juce::dontSendNotification);
        repaint();
    }
    AudioClipWaveforms& waves;
    ostfstudio::ClipTiming timing;
    double nativeLength = 0;
    juce::int64 ppq = 1;
    int drag = -1;
    juce::TextButton loop{TRANS("Loop")}, warp{TRANS("Warp (re-pitch)")}, melodyne{"Melodyne"}, apply{TRANS("Apply edited WAV")}, splice{"Splice Sounds"};
    juce::ComboBox beats;
};
