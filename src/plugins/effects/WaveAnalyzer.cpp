#include "../StudioPluginEditor.h"
#include "AnalyzerMeters.h"
#include "AnalyzerSpectrum.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace {
using namespace ostfstudio::analyzer;
using Rect = juce::Rectangle<float>;
using Colour = juce::Colour;
using String = juce::String;

struct Frame {
    std::array<std::array<float, fftSize>, 2> samples{};
    Readings meters;
    double rate = 48000;
    std::uint64_t sequence = 0;
    int channels = 2, next = 0, valid = 0;
    float sample(int c, int fromOldest) const {
        return samples[c][(next - valid + fromOldest + fftSize) % fftSize];
    }
};

class Analyzer final : public juce::AudioProcessor {
public:
    Analyzer()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        span = new juce::AudioParameterChoice(juce::ParameterID{"waveSpan", 1}, "Waveform span",
                                              juce::StringArray{"10 ms", "25 ms", "50 ms", "Full buffer"}, 1);
        addParameter(span);
    }
    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& buses) const override {
        const auto in = buses.getMainInputChannelSet();
        return in == buses.getMainOutputChannelSet() &&
               (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    void getStateInformation(juce::MemoryBlock& data) override {
        juce::XmlElement xml("WaveAnalyzer");
        xml.setAttribute("span", span->getIndex());
        copyXmlToBinary(xml, data);
    }
    void setStateInformation(const void* data, int size) override {
        const auto xml = getXmlFromBinary(data, size);
        if (xml && xml->hasTagName("WaveAnalyzer"))
            span->setValueNotifyingHost(
                span->convertTo0to1(static_cast<float>(std::clamp(xml->getIntAttribute("span", 1), 0, 3))));
    }
    void prepareToPlay(double rate, int) override {
        current.rate = rate;
        current.channels = getTotalNumInputChannels();
        clear();
        publish();
    }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& input, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if (resetRequested.exchange(false))
            clear();
        if (input.getNumChannels() < 1)
            return;
        const float* l = input.getReadPointer(0);
        const float* r = input.getReadPointer(current.channels == 2 ? 1 : 0);
        for (int i = 0; i < input.getNumSamples(); ++i) {
            // Ignore invalid samples in analysis. The audio buffer is read-only.
            const float a = std::isfinite(l[i]) ? l[i] : 0;
            const float b = std::isfinite(r[i]) ? r[i] : 0;
            meters.push(a, b);
            current.samples[0][current.next] = a;
            current.samples[1][current.next] = b;
            current.next = (current.next + 1) % fftSize;
        }
        current.valid += std::min(input.getNumSamples(), fftSize - current.valid);
        if (requested.load(std::memory_order_relaxed))
            publish();
    }
    bool readIfNew(Frame& dest) {
        const juce::SpinLock::ScopedLockType guard(lock);
        requested.store(true, std::memory_order_relaxed);
        if (dest.sequence == published.sequence)
            return false;
        dest = published;
        return true;
    }
    void resetMeters() { resetRequested = true; }
    juce::AudioParameterChoice* span = nullptr;

private:
    void clear() {
        meters.prepare(current.rate, current.channels);
        current.valid = current.next = 0;
        current.meters = {};
    }
    void publish() {
        const juce::SpinLock::ScopedTryLockType guard(lock);
        if (guard.isLocked()) {
            current.meters = meters.values;
            ++current.sequence;
            published = current;
            requested.store(false, std::memory_order_relaxed);
        }
    }
    Meters meters;
    Frame current, published;
    juce::SpinLock lock;
    std::atomic<bool> requested{true}, resetRequested{false};
};

const Colour quiet{0xffb2c3cc}, ink{0xffeef4f5},
    left{0xff65e4cd}, right{0xfff3b477}, warning{0xffff7975};
void label(juce::Graphics& g, Rect bounds, const String& s, float size, Colour colour,
           juce::Justification align = juce::Justification::centredLeft, bool bold = false) {
    g.setColour(colour);
    g.setFont(juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
    g.drawText(s, bounds, align, true);
}
String level(double magnitude) {
    return magnitude > 0 ? String(20 * std::log10(magnitude), 1) : String::fromUTF8("-\xe2\x88\x9e");
}
String loud(double v) {
    return std::isfinite(v) ? String(v, 1) : String::fromUTF8("-\xe2\x88\x9e");
}

class Editor final : public ostfstudio::StudioPluginEditor, private juce::Timer {
public:
    explicit Editor(Analyzer& p) : StudioPluginEditor(p), processor(p) {
        addAndMakeVisible(freeze);
        addAndMakeVisible(reset);
        addAndMakeVisible(span);
        freeze.setClickingTogglesState(true);
        freeze.onClick = [this] {
            freeze.setButtonText(freeze.getToggleState() ? "Resume" : "Freeze");
            repaint();
        };
        reset.onClick = [this] {
            processor.resetMeters();
            frame.meters = {};
            repaint();
        };
        span.addItemList(processor.span->choices, 1);
        span.onChange = [this] {
            processor.span->beginChangeGesture();
            *processor.span = span.getSelectedId() - 1;
            processor.span->endChangeGesture();
            repaint();
        };
        for (auto* button : {&freeze, &reset}) {
            button->setColour(juce::TextButton::buttonColourId, grid);
        }
        setResizable(true, true);
        setResizeLimits(720, 510, 1600, 1100);
        setSize(920, 620);
        timerCallback();
        startTimerHz(25);
    }
    void resized() override {
        auto area = getLocalBounds().toFloat().reduced(18);
        auto heading = area.removeFromTop(42).toNearestInt();
        reset.setBounds(heading.removeFromRight(88).reduced(1, 5));
        freeze.setBounds(heading.removeFromRight(75).reduced(2, 5));
        area.removeFromTop(8);
        stats = area.removeFromTop(70);
        area.removeFromTop(10);
        auto top = area.removeFromTop(std::round(area.getHeight() * .53f));
        area.removeFromTop(10);
        const float rightWidth = std::round(area.getWidth() * .27f);
        stereoArea = top.removeFromRight(rightWidth);
        top.removeFromRight(10);
        spectrumArea = top;
        levelsArea = area.removeFromRight(rightWidth);
        area.removeFromRight(10);
        waveArea = area;
        span.setBounds(waveArea.toNearestInt().removeFromTop(36).removeFromRight(120).reduced(8, 5));
    }
    void paint(juce::Graphics& g) override {
        StudioPluginEditor::paint(g);
        label(g, {18, 13, 290, 34}, JucePlugin_Name, 25, ink, juce::Justification::centredLeft, true);
        label(g, {310, 17, 155, 28},
              frame.valid ? String(frame.rate / 1000, 1) + " kHz   /   " + (frame.channels == 2 ? "Stereo" : "Mono")
                          : "Awaiting audio",
              14, quiet);
        metrics(g);
        surface(g, spectrumArea, "Frequency spectrum");
        surface(g, stereoArea, "Stereo field");
        surface(g, waveArea, "Waveform");
        surface(g, levelsArea, "Levels");
        spectrum(g);
        field(g);
        wave(g);
        levels(g);
    }
    void mouseMove(const juce::MouseEvent& e) override {
        hover = e.position;
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override {
        hover = {-1, -1};
        repaint();
    }

private:
    void timerCallback() override {
        if (!freeze.getToggleState() && processor.readIfNew(frame))
            transform();
        span.setSelectedId(processor.span->getIndex() + 1, juce::dontSendNotification);
        repaint();
    }
    void transform() {
        for (int c = 0; c < frame.channels; ++c)
            frequency.measure(c, frame.valid, [&](int i) { return frame.sample(c, i); });
    }
    void surface(juce::Graphics& g, Rect r, const char* title) {
        g.setColour(panel);
        g.fillRoundedRectangle(r, 7);
        label(g, r.reduced(12, 0).withHeight(35), title, 16, ink, juce::Justification::centredLeft, true);
    }
    void metrics(juce::Graphics& g) {
        auto row = stats;
        const float cell = (row.getWidth() - 24) / 4;
        auto metric = [&](const String& name, const String& value, const String& unit, Colour tint) {
            auto box = row.removeFromLeft(cell);
            row.removeFromLeft(8);
            g.setColour(panel);
            g.fillRoundedRectangle(box, 6);
            box = box.reduced(12, 4);
            label(g, box.removeFromTop(21), name, 13, quiet);
            auto units = box.removeFromRight(44);
            label(g, box, value, 28, tint, juce::Justification::centredLeft, true);
            label(g, units, unit, 13, quiet, juce::Justification::centredRight);
        };
        auto& m = frame.meters;
        metric("Momentary  /  400 ms", loud(m.momentary), "LUFS", ink);
        metric("Short-term  /  3 s", loud(m.shortTerm), "LUFS", ink);
        metric("Integrated", loud(m.integrated), "LUFS", left);
        const auto peak = std::max(m.maxTruePeak[0], m.maxTruePeak[1]);
        metric("True peak  /  max", level(peak), "dBTP", peak > 1 ? warning : right);
    }
    void spectrum(juce::Graphics& g) {
        const auto& bins = frequency.bins;
        auto graph = spectrumArea.withTrimmedTop(38).withTrimmedBottom(22).withTrimmedLeft(36).withTrimmedRight(12);
        const double topHz = std::min(20000.0, frame.rate * .5);
        const auto hzAt = [&](float x) { return 20 * std::pow(topHz / 20, x / graph.getWidth()); };
        const auto yAt = [&](double db) {
            return graph.getY() + static_cast<float>(std::clamp(-db / 90., 0., 1.)) * graph.getHeight();
        };
        for (int db = 0; db >= -90; db -= 18) {
            const float y = yAt(db);
            g.setColour(grid);
            g.drawHorizontalLine(juce::roundToInt(y), graph.getX(), graph.getRight());
            label(g, {graph.getX() - 34, y - 8, 30, 16}, String(db), 11, quiet, juce::Justification::centredRight);
        }
        for (int hz : {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000}) {
            if (hz > topHz)
                break;
            const float x =
                graph.getX() + graph.getWidth() * static_cast<float>(std::log(hz / 20.) / std::log(topHz / 20));
            g.setColour(grid);
            g.drawVerticalLine(juce::roundToInt(x), graph.getY(), graph.getBottom());
            label(g, {x - 16, graph.getBottom() + 2, 32, 17}, hz < 1000 ? String(hz) : String(hz / 1000) + "k", 11,
                  quiet, juce::Justification::centred);
        }
        for (int c = frame.channels - 1; c >= 0; --c) {
            juce::Path path;
            for (int px = 0; px < static_cast<int>(graph.getWidth()); ++px) {
                const auto binAt = [&](float x) {
                    return std::clamp(static_cast<int>(std::round(hzAt(x) * fftSize / frame.rate)), 0, fftSize / 2);
                };
                int a = binAt(px), b = binAt(px + 1);
                auto peak = *std::max_element(bins[c].begin() + a, bins[c].begin() + b + 1);
                const float x = graph.getX() + px, y = yAt(juce::Decibels::gainToDecibels(peak, -90.f));
                if (px == 0)
                    path.startNewSubPath(x, y);
                else
                    path.lineTo(x, y);
            }
            g.setColour(c == 0 ? left : right);
            g.strokePath(path, juce::PathStrokeType(c == 0 ? 1.5f : 2.2f));
        }
        String legend = frame.channels == 2 ? "L / R    dBFS" : "Mono    dBFS";
        if (graph.contains(hover)) {
            const auto hz = hzAt(hover.x - graph.getX());
            const int k = std::clamp(juce::roundToInt(hz * fftSize / frame.rate), 0, fftSize / 2);
            legend = String(juce::roundToInt(hz)) + " Hz    L " + level(bins[0][k]);
            if (frame.channels == 2)
                legend += "    R " + level(bins[1][k]);
            legend += " dBFS";
            g.setColour(quiet);
            g.drawVerticalLine(juce::roundToInt(hover.x), graph.getY(), graph.getBottom());
        }
        label(g, spectrumArea.reduced(12, 0).withHeight(35).withTrimmedLeft(145), legend, 12, quiet,
              juce::Justification::centredRight);
    }
    void field(juce::Graphics& g) {
        auto box = stereoArea.withTrimmedTop(37).withTrimmedBottom(32).reduced(10, 3);
        const auto centre = box.getCentre();
        const float radius = juce::jmin(box.getWidth(), box.getHeight()) * .48f;
        g.setColour(grid);
        g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1);
        g.drawLine(centre.x - radius, centre.y, centre.x + radius, centre.y);
        g.drawLine(centre.x, centre.y - radius, centre.x, centre.y + radius);
        if (frame.channels == 2) {
            g.setColour(left.withAlpha(.65f));
            for (int i = std::max(0, frame.valid - 768); i < frame.valid; ++i) {
                const float l = frame.sample(0, i), r = frame.sample(1, i);
                const float x = std::clamp((r - l) * .5f, -1.f, 1.f), y = std::clamp((l + r) * .5f, -1.f, 1.f);
                g.fillEllipse(centre.x + x * radius - 1, centre.y - y * radius - 1, 2, 2);
            }
        }
        auto& m = frame.meters;
        label(g, stereoArea.withTop(stereoArea.getBottom() - 30),
              "Correlation    " + (m.hasCorrelation ? String(m.correlation, 2) : String::fromUTF8("\xe2\x80\x94")),
              15, m.hasCorrelation && m.correlation < 0 ? warning : quiet, juce::Justification::centred);
    }
    void wave(juce::Graphics& g) {
        auto graph = waveArea.withTrimmedTop(38).withTrimmedBottom(19).reduced(11, 0);
        const int times[]{10, 25, 50};
        const int index = span.getSelectedId() - 1;
        const int requested =
            index >= 0 && index < 3 ? juce::roundToInt(times[index] * frame.rate / 1000) : fftSize;
        const int n = std::clamp(requested, 2, fftSize);
        for (int c = 0; c < frame.channels; ++c) {
            const float h = graph.getHeight() / frame.channels, mid = graph.getY() + (c + .5f) * h;
            g.setColour(grid);
            g.drawHorizontalLine(juce::roundToInt(mid), graph.getX(), graph.getRight());
            const auto colour = c ? right : left;
            label(g, {graph.getX(), mid - h * .49f, 20, 14}, c ? "R" : "L", 11, colour);
            const int width = std::max(1, static_cast<int>(graph.getWidth()) - 18);
            juce::Path trace;
            bool started = false;
            for (int px = 0; px < width; ++px) {
                int a = frame.valid - n + px * n / width, b = frame.valid - n + (px + 1) * n / width;
                if (a < 0 || a >= frame.valid)
                    continue;
                float low = frame.sample(c, a), high = low;
                for (int i = a + 1; i < b; ++i) {
                    auto s = frame.sample(c, i);
                    low = std::min(low, s);
                    high = std::max(high, s);
                }
                const float x = graph.getX() + 18 + px;
                const auto y = [&](float s) { return mid - std::clamp(s, -1.f, 1.f) * h * .46f; };
                if (!started) {
                    trace.startNewSubPath(x, y(low));
                    started = true;
                } else
                    trace.lineTo(x, y(low));
                trace.lineTo(x, y(high));
            }
            g.setColour(colour);
            g.strokePath(trace, juce::PathStrokeType(1.2f));
        }
        label(g, waveArea.withTop(waveArea.getBottom() - 20).reduced(12, 0),
              String(n * 1000. / frame.rate, 1) + " ms", 11, quiet, juce::Justification::centredRight);
    }
    void levels(juce::Graphics& g) {
        auto area = levelsArea.withTrimmedTop(38).reduced(12, 2);
        label(g, area.removeFromTop(22), "Sample peak / RMS    dBFS", 12, quiet);
        for (int c = 0; c < frame.channels; ++c) {
            const auto tint = c ? right : left;
            auto row = area.removeFromTop(22);
            label(g, row.removeFromLeft(22), c ? "R" : "L", 15, tint, juce::Justification::centredLeft, true);
            label(g, row, level(frame.meters.peak[c]) + "   /   " + level(frame.meters.rms[c]), 15, ink,
                  juce::Justification::centredRight);
            auto bar = area.removeFromTop(8);
            g.setColour(grid);
            g.fillRoundedRectangle(bar, 2);
            const auto frac = [](float v) {
                return std::clamp((juce::Decibels::gainToDecibels(v, -60.f) + 60) / 60, 0.f, 1.f);
            };
            g.setColour(tint);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * frac(frame.meters.rms[c])), 2);
            const float x = bar.getX() + (bar.getWidth() - 2) * frac(frame.meters.peak[c]);
            g.setColour(ink);
            if (frame.meters.peak[c] > 0)
                g.fillRect(x, bar.getY(), 2.f, bar.getHeight());
            area.removeFromTop(9);
        }
    }
    const Colour panel{juce::Colour(face.background).interpolatedWith(juce::Colour(face.panel),.48f)}, grid{face.panel};
    Analyzer& processor;
    Frame frame;
    Spectrum frequency;
    juce::TextButton freeze{"Freeze"}, reset{"Reset meters"};
    juce::ComboBox span;
    juce::Point<float> hover{-1, -1};
    Rect stats, spectrumArea, stereoArea, waveArea, levelsArea;
};
juce::AudioProcessorEditor* Analyzer::createEditor() {
    return new Editor(*this);
}
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Analyzer;
}
