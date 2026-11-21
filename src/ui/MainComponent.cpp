#include "ui/MainComponent.h"

namespace {
constexpr auto bg = 0xff121822;
constexpr auto panel = 0xff1b2734;
constexpr auto muted = 0xff9eafbf;
constexpr auto accent = 0xff46bdb2;
const juce::StringArray tracks {"DRUMS", "BASS", "SYNTH", "VOCAL", "AUDIO"};
}

MainComponent::MainComponent() {
    setSize(1100, 690);
    addAndMakeVisible(openButton_);
    addAndMakeVisible(aboutButton_);
    addAndMakeVisible(statusLabel_);
    openButton_.onClick = [this] { inspectOSTF(); };
    aboutButton_.onClick = [] {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "OSTF Studio - Bootstrap", "UI-only development scaffold. No OSTF 5.0 parser, DAW transport, VST3 host or audio engine yet.");
    };
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
    statusLabel_.setFont(juce::FontOptions(13.0f));
    statusLabel_.setText("Bootstrap only - no project opened", juce::dontSendNotification);
}

void MainComponent::inspectOSTF() {
    chooser_ = std::make_unique<juce::FileChooser>("Select an .ostf JSON document", juce::File{}, "*.ostf;*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser) {
            if (self == nullptr) return;
            auto selected = chooser.getResult();
            if (selected == juce::File{}) return;
            juce::FileInputStream input(selected);
            if (!input.openedOk()) {
                self->statusLabel_.setText("Could not read: " + selected.getFileName(), juce::dontSendNotification);
                return;
            }
            juce::var parsed;
            auto result = juce::JSON::parse(input.readEntireStreamAsString(), parsed);
            if (result.wasOk() && !parsed.isVoid()) {
                self->statusLabel_.setText("JSON object or array read: " + selected.getFileName(), juce::dontSendNotification);
            } else {
                self->statusLabel_.setText("Expected a JSON object or array: " + selected.getFileName(), juce::dontSendNotification);
            }
        });
}

void MainComponent::resized() {
    auto header = getLocalBounds().removeFromTop(72).reduced(18, 14);
    aboutButton_.setBounds(header.removeFromRight(90));
    header.removeFromRight(10);
    openButton_.setBounds(header.removeFromRight(168));
    statusLabel_.setBounds(20, getHeight() - 37, getWidth() - 40, 24);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(bg));
    auto area = getLocalBounds().reduced(18);
    auto top = area.removeFromTop(54);
    auto title = top.removeFromLeft(230);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    g.drawText("OSTF Studio", title.removeFromTop(29), juce::Justification::centredLeft);
    g.setColour(juce::Colour(muted));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("GLOBAL MUSICAL TIMELINE / DEMO", title, juce::Justification::centredLeft);
    layoutDemo(g, area.withTrimmedBottom(26));
}

void MainComponent::layoutDemo(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);
    area.reduce(13, 13);
    auto labels = area.removeFromLeft(115);
    const juce::StringArray names {"BARS", "SECTION", "CHORD", "KEY / SCALE", "BPM", "PERFORMANCE"};
    constexpr int trackHeadingHeight = 24;
    const int laneHeight = juce::jmin(42, (area.getHeight() - trackHeadingHeight) / (names.size() + tracks.size()));
    if (laneHeight < 12 || area.getWidth() < 40) return;
    const int rowHeight = laneHeight - 4;
    for (int lane = 0; lane < names.size(); ++lane) {
        auto y = area.getY() + lane * laneHeight;
        g.setColour(juce::Colour(muted));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(names[lane], labels.getX() + 5, y, labels.getWidth() - 10, rowHeight, juce::Justification::centredLeft);
        auto laneArea = juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowHeight);
        g.setColour(juce::Colour(0xff233443));
        g.fillRoundedRectangle(laneArea.toFloat(), 4.0f);
        for (int block = 0; block < 4; ++block) {
            auto r = juce::Rectangle<int>(laneArea.getX() + block * laneArea.getWidth() / 4 + 3, y + 3,
                                          laneArea.getWidth() / 4 - 6, rowHeight - 6);
            if (lane == 1) g.setColour(juce::Colour(0xff314d72));
            else if (lane == 2) g.setColour(juce::Colour(0xff493c64));
            else if (lane == 5) g.setColour(juce::Colour(0xff2c6058));
            else g.setColour(juce::Colour(0xff31404e));
            g.fillRoundedRectangle(r.toFloat(), 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.88f));
            g.setFont(juce::FontOptions(11.0f));
            const juce::String text = lane == 0 ? juce::String(1 + block * 8) + "-" + juce::String(8 + block * 8)
                : lane == 1 ? juce::StringArray{"Intro", "Verse", "Build", "Drop"}[block]
                : lane == 2 ? juce::StringArray{"Am", "F - G", "Am - G", "Am"}[block]
                : lane == 3 ? juce::StringArray{"A Aeolian", "A Aeolian", "A Dorian", "A Aeolian"}[block]
                : lane == 4 ? "128" : juce::StringArray{"Start", "Variation", "Build", "Peak"}[block];
            g.drawText(text, r, juce::Justification::centred);
        }
    }
    auto trackY = area.getY() + names.size() * laneHeight;
    g.setColour(juce::Colour(accent));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("TRACK ARRANGEMENT / DEMO", labels.getX() + 4, trackY,
               area.getWidth() + labels.getWidth() - 4, trackHeadingHeight, juce::Justification::centredLeft);
    trackY += trackHeadingHeight;
    for (int i = 0; i < tracks.size(); ++i) {
        int y = trackY + i * laneHeight;
        g.setColour(juce::Colours::white.withAlpha(0.80f));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(tracks[i], labels.getX() + 5, y, labels.getWidth() - 10, rowHeight, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff253746));
        g.fillRoundedRectangle(juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowHeight).toFloat(), 3.0f);
        for (int k = 0; k < 4; ++k) {
            if (((i + 2 * k) % 5) == 1) continue;
            auto r = juce::Rectangle<int>(area.getX() + k * area.getWidth()/4 + 4, y + 3, area.getWidth()/4 - 8, rowHeight - 6);
            g.setColour(juce::Colour(i == 3 ? 0xff8e5c83 : 0xff347b96));
            g.fillRoundedRectangle(r.toFloat(), 3.0f);
        }
    }
}
