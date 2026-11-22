#include <JuceHeader.h>
#include <BinaryData.h>
#include "ui/MainComponent.h"

namespace {
class StartupScreen final : public juce::Component {
public:
    StartupScreen() {
        logo_ = juce::ImageCache::getFromMemory(BinaryData::AppIcon_png, BinaryData::AppIcon_pngSize);
        addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowHasDropShadow);
        centreWithSize(460, 205);
        setVisible(true);
    }

    void setStage(juce::String stage, float progress) {
        stage_ = std::move(stage);
        progress_ = progress;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff293642), 0, 0, juce::Colour(0xff10151c),
                                              0, static_cast<float>(area.getBottom()), false));
        g.fillAll();
        g.setColour(juce::Colour(0xff52616e));
        g.drawRect(area, 1);
        g.drawImageWithin(logo_, 29, 31, 70, 70, juce::RectanglePlacement::centred);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(32.0f, juce::Font::bold));
        g.drawText("OSTF Studio", 120, 34, 300, 39, juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(13.0f));
        g.setColour(juce::Colour(0xffb1bfce));
        g.drawText("DAW Preview", 121, 75, 300, 22, juce::Justification::centredLeft);
        g.drawText(stage_, 32, 126, 396, 23, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff46545f));
        g.fillRoundedRectangle(32.0f, 162.0f, 396.0f, 4.0f, 2.0f);
        g.setColour(juce::Colour(0xff51c5b4));
        g.fillRoundedRectangle(32.0f, 162.0f, 396.0f * progress_, 4.0f, 2.0f);
    }

private:
    juce::Image logo_;
    juce::String stage_ {"Starting OSTF Studio..."};
    float progress_ = 0.2f;
};
}

class OSTFStudioApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "OSTF Studio"; }
    const juce::String getApplicationVersion() override { return "0.1.0-bootstrap"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        const auto args = getCommandLineParameterArray();
        if (args.size() == 2 && args[0] == "--scan-vst3") {
            juce::MessageManager::callAsync([this, token = args[1]] {
                setApplicationReturnValue(PluginBrowser::runScanWorker(token));
                quit();
            });
            return;
        }
        startup_ = std::make_unique<StartupScreen>();
        juce::Timer::callAfterDelay(45, [this] {
            if (!startup_) return;
            startup_->setStage("Preparing timeline and demo project...", 0.65f);
            juce::Timer::callAfterDelay(45, [this] {
                if (!startup_) return;
                mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
                startup_.reset();
            });
        });
    }
    void shutdown() override { startup_.reset(); mainWindow_.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff121822), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(680, 550, 8192, 8192);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
#if JUCE_WINDOWS
            if (auto* peer = getPeer()) {
                peer->setIcon(juce::ImageCache::getFromMemory(BinaryData::AppIcon_png,
                                                              BinaryData::AppIcon_pngSize));
            }
#endif
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<StartupScreen> startup_;
};

START_JUCE_APPLICATION(OSTFStudioApplication)
