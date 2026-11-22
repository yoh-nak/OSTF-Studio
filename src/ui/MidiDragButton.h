#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class MidiDragButton final : public juce::TextButton {
public:
    MidiDragButton() : TextButton("Drag MIDI to a track") {}

    std::function<bool(juce::OutputStream&)> writeMidi;
    std::function<void(const juce::String&)> reportError;

    void mouseDown(const juce::MouseEvent& event) override {
        started_ = false;
        TextButton::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (started_ || event.getDistanceFromDragStart() < 5 || !writeMidi) return;
        started_ = true;
        if (const auto error = dragMidi(export_.getFile(), this, writeMidi); error.isNotEmpty() && reportError)
            reportError(error);
    }

    static juce::String dragMidi(const juce::File& file, juce::Component* source,
                                const std::function<bool(juce::OutputStream&)>& writeMidi) {
        if (!file.deleteFile()) {
            return "Could not replace MIDI file";
        }
        {
            juce::FileOutputStream stream(file);
            const bool written = stream.openedOk() && writeMidi(stream);
            stream.flush();
            if (!written || stream.getStatus().failed()) {
                return "Could not write MIDI file";
            }
        }
        if (!juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, source))
            return "MIDI drag could not start";
        return {};
    }

private:
    juce::TemporaryFile export_ {".mid"};
    bool started_ = false;
};
