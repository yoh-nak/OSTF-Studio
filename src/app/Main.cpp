#include <JuceHeader.h>
#include "ui/MainComponent.h"

class OSTFStudioApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "OSTF Studio"; }
    const juce::String getApplicationVersion() override { return "0.1.0-bootstrap"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { mainWindow_.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff121822), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(560, 420, 8192, 8192);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(OSTFStudioApplication)
