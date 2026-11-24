#pragma once
#include "ui/TimelinePan.h"
#include "ui/AudioClipWaveforms.h"
#include "ui/ArrangementRuler.h"

// The ruler, waveform and warp handles share the visible arrangement time range.
class AudioClipEditor final : public juce::Component, public juce::SettableTooltipClient {
public:
    TimelinePan viewportPan;
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
    std::function<void()> onMelodyne, onApply, onSplice, selectWave;
    std::function<bool()> hasWorkingCopy;
    ArrangementRuler recordRuler;
    juce::ComboBox channels;
    void setTimelineColumns(int left, int right) {
        timelineLeft = left;
        timelineRight = right;
        resized();
    }
    void bind(juce::int64 resolution, juce::int64 barLength, int first, bool editable) {
        ticksPerBar = std::max<juce::int64>(1, barLength);
        firstBar = first;
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
        const auto left = std::clamp(timelineLeft, 0, getWidth());
        auto tools = getLocalBounds().withWidth(left).reduced(8, 3);
        const int h = std::clamp(tools.getHeight() / 3, 1, 29);
        auto row = tools.removeFromTop(h);
        channels.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 1));
        loop.setBounds(row.reduced(2, 1));
        row = tools.removeFromTop(h);
        warp.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 1));
        beatCaption = row.removeFromRight(std::min(40, row.getWidth() / 3));
        beats.setBounds(row.reduced(2, 1));
        row = tools.removeFromTop(h);
        melodyne.setBounds(row.removeFromLeft(row.getWidth() / (apply.isVisible() ? 3 : 2)).reduced(2, 1));
        if (apply.isVisible()) apply.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 1));
        splice.setBounds(row.reduced(2, 1));
        recordRuler.setBounds(left, 0, std::max(0, std::min(timelineRight, getWidth()) - left), std::min(23, getHeight()));
    }
    void paint(juce::Graphics& g) override {
        const auto area = canvas();
        g.setColour(juce::Colour(0xffd2e5ee));
        g.drawText(TRANS("beats"), beatCaption, juce::Justification::centredLeft);
        if (area.isEmpty() || !recordRuler.visibleBars) return;
        g.setColour(juce::Colour(0xff1b303d));
        g.fillRect(area);
        const auto visible = recordRuler.visibleBars();
        if (visible.isEmpty()) return;
        const double step = std::max(1., std::ceil(8 * visible.getLength() / area.getWidth()));
        g.setColour(juce::Colour(0xff365467));
        for (double bar = std::ceil(visible.getStart() / step) * step; bar < visible.getEnd(); bar += step)
            g.drawVerticalLine(xAtTick((bar - firstBar) * ticksPerBar), float(area.getY()), float(area.getBottom()));
        auto* clip = sourceClip();
        if (!clip || nativeLength <= 0) {
            g.setColour(juce::Colour(0xffd2e5ee));
            g.drawFittedText(clip ? TRANS("Missing WAV") : TRANS("Drop a WAV from Splice or a folder onto this audio track"),
                             area, juce::Justification::centred, 2);
            return;
        }
        const double start = std::max(double(clip->startTick), tickAtX(area.getX()));
        const double clipEnd = waves.endTick(*clip, clock());
        const double end = std::min(clipEnd, tickAtX(area.getRight()));
        if (end <= start) return;
        auto body = area.withLeft(xAtTick(start)).withRight(xAtTick(end));
        waves.draw(g, body.reduced(0, 3), *clip, clock(), start, end, &timing);
        if (timing.cycleTicks <= 0 || timing.markers.empty()) return;
        const double period = timing.cycleTicks;
        const double cyclePixels = period / ticksPerBar / visible.getLength() * area.getWidth();
        const auto base = clip->sourceTick() - timing.phaseOffset * period;
        if (cyclePixels < 2. || (!timing.loop && start >= base + period)) return;
        for (double origin = base + std::max(0., std::floor((start - base) / period)) * period;
             origin < end; origin += period) {
            for (const auto marker : timing.markers) {
                const double tick = origin + marker.position * period;
                if (tick < std::max(start, double(clip->sourceTick())) || tick >= end) continue;
                const int x = xAtTick(tick);
                g.setColour(juce::Colour(0xffffbc63));
                g.drawVerticalLine(x, float(area.getY()), float(area.getBottom()));
                g.fillRect(x - 3, area.getY(), 7, 9);
            }
            if (!timing.loop) break;
        }
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (TimelinePan::requested(e.mods)) return;
        if (!warp.isEnabled() || !timing.cycleTicks || !overClip(e.getPosition())) return;
        auto edit = timing;
        const auto x = phase(e.x, cycleStart(e.x));
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
        if (viewportPan.begin(*this, e, canvas())) return;
        drag = -1;
        if (!overClip(e.getPosition())) return;
        if (selectWave) selectWave();
        if (timing.cycleTicks > 0 && warp.isEnabled()) {
            dragCycle = cycleStart(e.x);
            drag = hit(e.x);
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (viewportPan.move(e)) return;
        if (drag < 0) return;
        const double low = drag ? timing.markers[size_t(drag - 1)].position : 0.;
        const double high = size_t(drag + 1) < timing.markers.size() ? timing.markers[size_t(drag + 1)].position : 1.;
        timing.markers[size_t(drag)].position = std::clamp(phase(e.x, dragCycle), std::nextafter(low, high), std::nextafter(high, low));
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (viewportPan.end(*this)) return;
        if (drag >= 0 && e.mouseWasDraggedSinceMouseDown()) commit(timing);
        drag = -1;
    }
private:
    juce::Rectangle<int> canvas() const {
        return recordRuler.getBounds().withTop(recordRuler.getBottom()).withBottom(getHeight());
    }
    double tickAtX(int x) const {
        const auto bars = recordRuler.visibleBars();
        return (bars.getStart() - firstBar + (x - canvas().getX()) * bars.getLength() / std::max(1, canvas().getWidth())) * ticksPerBar;
    }
    int xAtTick(double tick) const {
        const auto bars = recordRuler.visibleBars();
        return canvas().getX() + juce::roundToInt((firstBar + tick / ticksPerBar - bars.getStart()) * canvas().getWidth() / bars.getLength());
    }
    bool overClip(juce::Point<int> point) const {
        auto* clip = sourceClip();
        if (!clip || !canvas().contains(point) || !recordRuler.visibleBars) return false;
        const auto tick = tickAtX(point.x);
        return tick >= clip->sourceTick() && tick < waves.endTick(*clip, clock())
            && (timing.loop || !timing.sourceLimitTicks || tick < clip->sourceTick() + timing.sourceLimitTicks)
            && (timing.loop || !timing.cycleTicks || tick < clip->sourceTick() + (1. - timing.phaseOffset) * timing.cycleTicks);
    }
    double cycleStart(int x) const {
        const double start = sourceClip()->sourceTick() - timing.phaseOffset * timing.cycleTicks;
        return timing.loop ? start + std::floor((tickAtX(x) - start) / timing.cycleTicks) * timing.cycleTicks : start;
    }
    double phase(int x, double origin) const { return (tickAtX(x) - origin) / timing.cycleTicks; }
    int hit(int x) const {
        const double origin = cycleStart(x);
        for (size_t i = 0; i < timing.markers.size(); ++i)
            if (std::abs(tickAtX(x) - (origin + timing.markers[i].position * timing.cycleTicks)) <= (tickAtX(x + 5) - tickAtX(x))) return int(i);
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
    int drag = -1, timelineLeft = 0, timelineRight = 0, firstBar = 1;
    juce::int64 ticksPerBar = 1;
    double dragCycle = 0;
    juce::Rectangle<int> beatCaption;
    juce::TextButton loop{TRANS("Loop")}, warp{TRANS("Warp (re-pitch)")}, melodyne{"Melodyne"}, apply{TRANS("Apply edited WAV")}, splice{"Splice Sounds"};
    juce::ComboBox beats;
};
