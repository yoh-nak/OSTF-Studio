#include "Reverb.h"

namespace knobverb {
namespace {
struct Dial : juce::Component {
  juce::Slider slider{juce::Slider::RotaryHorizontalVerticalDrag,
                      juce::Slider::TextBoxBelow};
  juce::Label label;
  juce::AudioProcessorValueTreeState::SliderAttachment attachment;
  Dial(juce::AudioProcessorValueTreeState &state, const Control &c)
      : attachment(state, c.id, slider) {
    label.setText(c.name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    slider.setName(c.name);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 18);
    slider.setNumDecimalPlacesToDisplay(2);
    addAndMakeVisible(slider);
    addAndMakeVisible(label);
  }
  void resized() override {
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(20));
    slider.setBounds(area);
  }
};
// The four controls used to judge a sound stay above every detail page.
constexpr std::array<int, 4> primary{62, 1, 2, 63};
int pageFor(int n) {
  if (n < 16 || (n >= 66 && n < 74)) return 0;
  if (n <= 23) return 1;
  if (n < 32 || (n >= 56 && n <= 65)) return 2;
  if (n < 50) return 3;
  if (n < 56 || (n >= 88 && n < 96)) return 4;
  return 5;
}
class Editor final : public ostfstudio::StudioPluginEditor,
                     private juce::Timer {
  Reverb &processor;
  juce::Label title;
  juce::ComboBox algorithm, source, style, view, analyzer;
  juce::Viewport scroll;
  juce::Component panel;
  std::vector<std::unique_ptr<Dial>> dials;
  std::array<float, 31> spectrum{}, input{}, preEq{}, postEq{}, filtered{};
  std::array<float, 3> peaks{};
  std::array<float, 96> envelope{};
  juce::Rectangle<int> graph;
  static void fill(juce::ComboBox &box, auto &names) {
    for (const auto *n : names)
      box.addItem(n, box.getNumItems() + 1);
  }
  void timerCallback() override {
    algorithm.setSelectedId(processor.getCurrentProgram() + 1,
                            juce::dontSendNotification);
    source.setSelectedId(processor.sourceIndex.load() + 1,
                         juce::dontSendNotification);
    style.setSelectedId(processor.styleIndex.load() + 1,
                        juce::dontSendNotification);
    processor.inputSpectrum.read(input);
    processor.preEqSpectrum.read(preEq);
    processor.wetSpectrum.read(spectrum);
    std::move(envelope.begin() + 1, envelope.end(), envelope.begin());
    processor.postEqSpectrum.read(postEq);
    processor.filteredSpectrum.read(filtered);
    const std::array<float,3> current{processor.inputPeak.exchange(0),processor.wetPeak.exchange(0),processor.outputPeak.exchange(0)};
    for (size_t i=0; i<peaks.size(); ++i) peaks[i]=std::max(current[i], peaks[i]*.87f);
    envelope.back() = current[1];
    repaint(graph);
  }

public:
  explicit Editor(Reverb &p) : StudioPluginEditor(p), processor(p) {
    title.setText("SUPER KNOBBER REVERB", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions{21.f, juce::Font::bold}));
    addAndMakeVisible(title);
    fill(algorithm, types);
    fill(source, sources);
    fill(style, styles);
    algorithm.setName("Reverb type");
    source.setName("Source profile");
    style.setName("Style");
    const char *pages[] = {"Space & reflections", "Decay bands",
                           "EQ & colour",    "Motion & dynamics",
                           "Pitch & delay", "Textures"};
    fill(view, pages);
    view.setSelectedId(1, juce::dontSendNotification);
    const char *meters[] = {"Input / wet spectrum", "Post EQ: before / after",
                            "Decay targets (RT60)", "Wet level history", "Input filter: before / after", "Wet waveform / tail"};
    fill(analyzer, meters);
    analyzer.setSelectedId(1, juce::dontSendNotification);
    for (auto *box : {&algorithm, &source, &style, &view, &analyzer})
      addAndMakeVisible(box);
    algorithm.onChange = source.onChange = style.onChange = [this] {
      processor.select(algorithm.getSelectedId() - 1,
                       source.getSelectedId() - 1, style.getSelectedId() - 1);
    };
    view.onChange = [this] {
      scroll.setViewPosition(0, 0);
      resized();
    };
    for (const auto &c : controls) {
      auto dial = std::make_unique<Dial>(p.state, c);
      panel.addChildComponent(*dial);
      dials.push_back(std::move(dial));
    }
    for (int i : primary) addAndMakeVisible(*dials[size_t(i)]);
    dials[62]->slider.setTextValueSuffix(" %");
    dials[1]->slider.setTextValueSuffix(" s");
    dials[2]->slider.setTextValueSuffix(" ms");
    dials[63]->slider.setTextValueSuffix(" dB");
    scroll.setViewedComponent(&panel, false);
    scroll.setScrollBarsShown(true, false);
    addAndMakeVisible(scroll);
    setResizable(true, true);
    setResizeLimits(560, 510, 1800, 1200);
    setSize(850, 660);
    timerCallback();
    startTimerHz(18);
  }
  ~Editor() override {
    stopTimer();
    scroll.setViewedComponent(nullptr, false);
  }
  void paint(juce::Graphics &g) override {
    StudioPluginEditor::paint(g);
    g.setColour(juce::Colours::lightgrey);
    for (auto *box : {&algorithm, &source, &style}) {
      auto caption = box->getBounds().translated(2, -16).withHeight(16);
      g.drawText(box->getName(), caption, juce::Justification::centredLeft);
    }
    g.setColour(juce::Colour(face.panel));
    g.fillRoundedRectangle(graph.toFloat(), 5);
    g.setColour(juce::Colour(face.accent));
    auto r = graph.toFloat().reduced(6);
    auto level = [](float gain) {
      return juce::jlimit(
          0.f, 1.f, 1 + juce::Decibels::gainToDecibels(gain, -84.f) / 84.f);
    };
    auto meters = r.removeFromRight(94).reduced(3, 1);
    const char* names[]{"IN", "WET", "OUT"};
    for (size_t i=0; i<peaks.size(); ++i) {
      auto row = meters.removeFromTop(meters.getHeight() / int(peaks.size()-i));
      auto label = row.removeFromTop(16);
      g.setColour(juce::Colours::white);
      g.drawText(names[i], label, juce::Justification::centredLeft);
      const auto db = juce::Decibels::gainToDecibels(peaks[i], -84.f);
      g.drawText(db <= -84 ? juce::String("-inf") : juce::String(db,1),label,juce::Justification::centredRight);
      auto bar = row.removeFromTop(5);
      g.setColour(juce::Colour(0xff15232c)); g.fillRect(bar);
      g.setColour(peaks[i]>=1 ? juce::Colours::orange : juce::Colour(face.accent)); g.fillRect(bar.withWidth(bar.getWidth()*level(peaks[i])));
    }
    r.removeFromRight(12);
    const int mode = analyzer.getSelectedId();
    if (mode == 6) {
      auto labels = r.removeFromBottom(16);
      g.setColour(juce::Colours::lightgrey);
      g.drawText("-5 s",labels,juce::Justification::centredLeft);
      g.drawText("0 s",labels,juce::Justification::centredRight);
      const float mid=r.getCentreY();
      g.drawHorizontalLine(int(mid),r.getX(),r.getRight());
      const auto samples=processor.waveform.read();
      const float step=r.getWidth()/samples.size();
      g.setColour(juce::Colour(face.accent));
      for (size_t i=0;i<samples.size();++i) {
        const float top=mid-.5f*r.getHeight()*level(samples[i].high);
        const float bottom=mid+.5f*r.getHeight()*level(-samples[i].low);
        if(bottom>top) g.fillRect(r.getX()+step*i,top,step,bottom-top);
      }
      return;
    }
    if (mode != 3) {
      auto legend = r.removeFromTop(19);
      g.setColour(juce::Colours::lightblue);
      g.drawText(mode==1 ? "Input" : mode==2 ? "Before EQ" : mode==5 ? "Input" : "Wet level",legend,juce::Justification::centredLeft);
      g.setColour(juce::Colour(face.accent));
      g.drawText(mode==1 ? "Wet in mix" : mode==2 ? "After EQ" : mode==5 ? "After filter" : "Last 5 s",legend,juce::Justification::centredRight);
      auto axis=r.removeFromBottom(15);
      auto dbAxis=r.removeFromLeft(30);
      g.setColour(juce::Colours::lightgrey);
      for (int d : {0, -24, -48, -72}) {
        float y=r.getY()-d*r.getHeight()/84;
        g.drawText(juce::String(d),dbAxis.withY(y-2).withHeight(13),juce::Justification::centredLeft);
        g.setColour(juce::Colours::lightgrey.withAlpha(.18f)); g.drawHorizontalLine(int(y),r.getX(),r.getRight()); g.setColour(juce::Colours::lightgrey);
      }
      if(mode != 4) {
        axis.removeFromLeft(30);
        g.drawText("20 Hz",axis,juce::Justification::centredLeft);
        g.drawText("640 Hz",axis,juce::Justification::centred);
        g.drawText("20 kHz",axis,juce::Justification::centredRight);
      }
    }
    g.setColour(juce::Colour(face.accent));
    if (mode == 3) {
      const float decay = processor.state.getRawParameterValue("decay")->load();
      const char *bands[] = {"Sub", "Low", "Mid", "High", "Air"};
      for (int i = 0; i < 5; ++i) {
        float seconds = std::max(
            .1f, decay *
                     processor.state.getRawParameterValue(controls[16 + i].id)
                         ->load() *
                     .01f);
        float width = r.getWidth() / 5, x = r.getX() + i * width;
        float h =
            (r.getHeight() - 18) *
            juce::jlimit(0.f, 1.f, std::log(seconds / .1f) / std::log(800.f));
        g.fillRect(x, r.getBottom() - h, width - 3, h);
        g.drawText(juce::String(bands[i]) + " " + juce::String(seconds, 1) +
                       " s",
                   int(x), int(r.getY()), int(width), 16,
                   juce::Justification::centred);
      }
    } else if (analyzer.getSelectedId() == 4) {
      juce::Path trace;
      trace.startNewSubPath(
          r.getX(), r.getBottom() - r.getHeight() * level(envelope.front()));
      for (size_t i = 1; i < envelope.size(); ++i)
        trace.lineTo(r.getX() + r.getWidth() * i / (envelope.size() - 1),
                     r.getBottom() - r.getHeight() * level(envelope[i]));
      g.strokePath(trace, juce::PathStrokeType(1.6f));
    } else {
      const auto &reference = analyzer.getSelectedId() == 2 ? preEq : input;
      const auto &signal = mode == 2 ? postEq : mode == 5 ? filtered : spectrum;
      const float w = r.getWidth() / spectrum.size();
      juce::Path trace;
      for (size_t i = 0; i < spectrum.size(); ++i) {
        const float x = r.getX() + float(i) * w,
                    h = r.getHeight() * level(signal[i]);
        g.fillRect(x, r.getBottom() - h, w - 1, h);
        const float y = r.getBottom() - r.getHeight() * level(reference[i]);
        if (i)
          trace.lineTo(x + w * .5f, y);
        else
          trace.startNewSubPath(x + w * .5f, y);
      }
      g.setColour(juce::Colours::lightblue);
      g.strokePath(trace, juce::PathStrokeType(1.5f));
    }
  }
  void resized() override {
    auto area = getLocalBounds().reduced(12);
    auto head = area.removeFromTop(37);
    title.setBounds(head.removeFromLeft(280));
    analyzer.setBounds(head.removeFromRight(218).reduced(2));
    graph = area.removeFromTop(std::clamp(getHeight()/4, 136, 260)).reduced(2);
    area.removeFromTop(16);
    auto row = area.removeFromTop(36);
    int w = row.getWidth() / 3;
    algorithm.setBounds(row.removeFromLeft(w).reduced(2));
    source.setBounds(row.removeFromLeft(w).reduced(2));
    style.setBounds(row.reduced(2));
    auto mainRow = area.removeFromTop(std::clamp(getHeight()/6, 80, 118)).reduced(2);
    for(size_t i=0; i<primary.size(); ++i)
      dials[size_t(primary[i])]->setBounds(mainRow.removeFromLeft(mainRow.getWidth()/int(primary.size()-i)).reduced(4,2));
    view.setBounds(area.removeFromTop(35).removeFromLeft(220).reduced(2));
    scroll.setBounds(area);
    const int width =
        std::max(1, area.getWidth() - scroll.getScrollBarThickness());
    const int cols = std::max(1, width / 110);
    int slot = 0;
    for (size_t i = 0; i < dials.size(); ++i) {
      if (std::find(primary.begin(),primary.end(),int(i))!=primary.end()) continue;
      const bool active = pageFor(int(i)) == view.getSelectedId() - 1;
      dials[i]->setVisible(active);
      if (active) {
        dials[i]->setBounds((slot % cols) * width / cols, (slot / cols) * 94,
                            width / cols, 92);
        ++slot;
      }
    }
    panel.setSize(width, ((slot + cols - 1) / cols) * 94);
  }
};
} // namespace
juce::AudioProcessorEditor *Reverb::createEditor() { return new Editor(*this); }
} // namespace knobverb
