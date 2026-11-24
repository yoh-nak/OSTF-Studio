#include "StemFiles.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace {
class Separator final : public juce::AudioProcessor {
public:
    Separator() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}
    ~Separator() override { pool.removeAllJobs(true, -1); }
    const juce::String getName() const override { return "OSTF Stem Separator"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        const auto in = layout.getMainInputChannelSet();
        return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == layout.getMainOutputChannelSet();
    }
    void prepareToPlay(double, int) override { endCapture(); }
    void releaseResources() override { endCapture(); }
    void processBlock(juce::AudioBuffer<float>& block, juce::MidiBuffer&) override {
        if (!recording.load()) return;
        const juce::SpinLock::ScopedLockType lock(captureLock);
        if (!recording.load()) return;
        const int count = std::min(block.getNumSamples(), captured.getNumSamples() - frames);
        for (int ch = 0; ch < std::min(block.getNumChannels(), captured.getNumChannels()); ++ch)
            captured.copyFrom(ch, frames, block, ch, 0, count);
        frames += count;
        if (frames == captured.getNumSamples()) { recording = false; captureReady = true; }
    }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& data) override {
        juce::MemoryOutputStream out(data, false);
        out.writeString(result().getFullPathName());
    }
    void setStateInformation(const void* data, int size) override {
        const auto path = juce::MemoryInputStream(data, size_t(size), false).readString();
        if (!juce::File::isAbsolutePath(path)) return;
        const juce::File folder(path);
        if (folder.getChildFile("Drums.wav").existsAsFile()) { const juce::ScopedLock lock(stateLock); output = folder; }
    }
    bool capture() {
        if (busy || recording || captureReady) return false;
        const auto rate = getSampleRate();
        const int channels = getTotalNumInputChannels();
        if (rate < 8000 || channels < 1 || channels > 2) { setStatus("Start the host audio engine first."); return false; }
        juce::AudioBuffer<float> storage(channels, (16 * 1024 * 1024 - 4096) / (channels * 2));
        const juce::SpinLock::ScopedLockType lock(captureLock);
        captured = std::move(storage);
        frames = 0;
        captureRate = rate;
        recording = true;
        return true;
    }
    void endCapture() {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        if (recording.exchange(false)) captureReady = true;
    }
    void analyseCapture() {
        if (!captureReady.exchange(false)) return;
        auto audio = std::make_shared<juce::AudioBuffer<float>>();
        int count;
        double rate;
        {
            const juce::SpinLock::ScopedLockType lock(captureLock);
            *audio = std::move(captured);
            count = frames;
            rate = captureRate;
        }
        if (!count) { setStatus("No audio received. Play the source track during capture."); return; }
        start("Capture", [audio, count, rate](const juce::File& dir) {
            juce::MemoryBlock encoded;
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(encoded, false);
            auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(audio->getNumChannels()).withBitsPerSample(24);
            auto writer = wav.createWriterFor(stream, options);
            if (!writer || !writer->writeFromAudioSampleBuffer(*audio, 0, count)) return juce::Result::fail("Could not capture audio.");
            writer.reset();
            std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::MemoryInputStream(encoded, false), true));
            return reader ? ostfstudio::stems::exportParts(*reader, dir) : juce::Result::fail("Invalid capture.");
        });
    }
    void analyseFile(const juce::File& file) {
        if (recording || captureReady || busy) return;
        start(file.getFileNameWithoutExtension(), [file](const juce::File& dir) {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            return reader ? ostfstudio::stems::exportParts(*reader, dir) : juce::Result::fail("Unsupported audio file.");
        });
    }
    juce::File result() const { const juce::ScopedLock lock(stateLock); return output; }
    juce::String message() const { const juce::ScopedLock lock(stateLock); return status; }
    std::atomic<bool> recording{false}, captureReady{false}, busy{false};
private:
    void setStatus(juce::String text) { const juce::ScopedLock lock(stateLock); status = std::move(text); }
    void start(const juce::String& name, std::function<juce::Result(const juce::File&)> work) {
        if (busy.exchange(true)) return;
        setStatus("Separating audio and transcribing notes...");
        pool.addJob([this, name, work = std::move(work)] {
            const auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile("OSTF Studio/Stems").getChildFile(juce::File::createLegalFileName(name).substring(0, 64) + "-" + juce::Uuid().toString());
            auto r = dir.createDirectory();
            if (r.wasOk()) r = work(dir);
            if (r.wasOk()) {
                const juce::ScopedLock lock(stateLock);
                output = dir;
                status = "Drag WAV or MIDI to a track. Chords can also go on Studio's chord lane.";
            } else {
                dir.deleteRecursively();
                setStatus(r.getErrorMessage());
            }
            busy = false;
        });
    }
    juce::SpinLock captureLock;
    juce::AudioBuffer<float> captured;
    int frames = 0;
    double captureRate = 0;
    juce::CriticalSection stateLock;
    juce::File output;
    juce::String status{"Drop an audio file here or capture the playing track."};
    juce::ThreadPool pool{1};
};

class DragFile final : public juce::TextButton {
public:
    juce::File file;
    void mouseDown(const juce::MouseEvent& e) override { sent = false; TextButton::mouseDown(e); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!sent && e.mouseWasDraggedSinceMouseDown() && file.existsAsFile()) {
            sent = juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this);
        }
    }
private:
    bool sent = false;
};

class Editor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit Editor(Separator& p) : AudioProcessorEditor(p), processor(p) {
        title.setText(p.getName(), juce::dontSendNotification);
        title.setFont(juce::FontOptions(21.f, juce::Font::bold));
        for (auto* c : std::array<juce::Component*,6>{&title,&open,&capture,&folder,&status,&chords}) addAndMakeVisible(*c);
        for (size_t i = 0; i < rows.size(); ++i) {
            labels[i].setText(ostfstudio::stems::names[i], juce::dontSendNotification);
            addAndMakeVisible(labels[i]);
            for (auto& b : rows[i]) addAndMakeVisible(b);
            rows[i][0].setButtonText("WAV"); rows[i][1].setButtonText("MIDI");
        }
        chords.setButtonText("Chord progression MIDI");
        open.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Separate audio", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe = juce::Component::SafePointer<Editor>(this)](const juce::FileChooser& c) {
                    if (safe && c.getResult().existsAsFile()) safe->processor.analyseFile(c.getResult());
                });
        };
        capture.onClick = [this] {
            if (processor.recording) processor.endCapture();
            else processor.capture();
            timerCallback();
        };
        folder.onClick = [this] { processor.result().revealToUser(); };
        setSize(506, 338);
        timerCallback(); startTimerHz(6);
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        return files.size() == 1 && !processor.busy && !processor.recording && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac;mp3");
    }
    void filesDropped(const juce::StringArray& files, int, int) override {
        if (isInterestedInFileDrag(files)) processor.analyseFile(juce::File(files[0]));
    }
    void resized() override {
        auto area = getLocalBounds().reduced(16);
        title.setBounds(area.removeFromTop(34));
        auto actions = area.removeFromTop(36);
        open.setBounds(actions.removeFromLeft(120).reduced(2));
        capture.setBounds(actions.removeFromLeft(164).reduced(2));
        folder.setBounds(actions.reduced(2));
        area.removeFromTop(8);
        for (size_t i = 0; i < rows.size(); ++i) {
            auto row = area.removeFromTop(34);
            labels[i].setBounds(row.removeFromLeft(140));
            rows[i][0].setBounds(row.removeFromLeft(112).reduced(2));
            rows[i][1].setBounds(row.removeFromLeft(112).reduced(2));
        }
        chords.setBounds(area.removeFromTop(34).reduced(2));
        status.setBounds(area);
    }
private:
    void timerCallback() override {
        processor.analyseCapture();
        const auto dir = processor.result();
        const bool ready = dir != juce::File{};
        auto setFile = [&](DragFile& b, juce::String name) {
            b.file = ready ? dir.getChildFile(name) : juce::File{};
            b.setEnabled(ready && b.file.existsAsFile());
        };
        for (size_t i = 0; i < rows.size(); ++i) {
            setFile(rows[i][0], ostfstudio::stems::names[i] + ".wav");
            setFile(rows[i][1], ostfstudio::stems::names[i] + ".mid");
        }
        setFile(chords, "Chords.mid");
        folder.setEnabled(ready);
        open.setEnabled(!processor.busy && !processor.recording);
        capture.setEnabled(!processor.busy);
        capture.setButtonText(processor.recording ? "Finish capture" : "Capture track");
        status.setText(processor.recording ? "Capturing input. Play the track, then finish capture." : processor.message(), juce::dontSendNotification);
    }
    Separator& processor;
    juce::Label title, status;
    juce::TextButton open{"Open audio..."}, capture{"Capture track"}, folder{"Show files"};
    std::array<juce::Label,4> labels;
    std::array<std::array<DragFile,2>,4> rows;
    DragFile chords;
    std::unique_ptr<juce::FileChooser> chooser;
};
juce::AudioProcessorEditor* Separator::createEditor() { return new Editor(*this); }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Separator; }
