#include "ui/ScoreView.h"
#include "ui/MediaImport.h"

using namespace ostfstudio;
namespace {
constexpr auto dark = 0xff202a30;
class ReadingGrid final : public juce::Component {
  public:
    std::function<void(juce::Graphics&)> draw;
    void paint(juce::Graphics& g) override {
        if (draw)
            draw(g);
    }
};
} // namespace
class ScoreView::Panel final : public juce::Component {
  public:
    Panel() {
        for (auto* c : std::array<juce::Component*, 18>{&grid, &title, &position, &track, &staff, &previous, &next,
                                                        &counter, &pdf, &xml, &status, &density, &voices, &follow,
                                                        &chords, &scales, &lyrics, &wording})
            addAndMakeVisible(c);
        title.setFont(juce::FontOptions(22.f, juce::Font::bold));
        position.setComponentID("reading-position");
        position.setJustificationType(juce::Justification::centredRight);
        position.setColour(juce::Label::textColourId, juce::Colour(0xffb6ced0));
        grid.setComponentID("reading-grid");
        counter.setComponentID("reading-bar-range");
        previous.setTitle(TRANS("Previous bars"));
        next.setTitle(TRANS("Next bars"));
        chords.setToggleState(options.showChords, juce::dontSendNotification);
        lyrics.setToggleState(options.showLyrics, juce::dontSendNotification);
        scales.setToggleState(options.showScales, juce::dontSendNotification);
        for (auto* toggle : {&chords, &lyrics, &scales})
            toggle->onClick = [this] {
                options.showChords = chords.getToggleState();
                options.showLyrics = lyrics.getToggleState();
                options.showScales = scales.getToggleState();
                update();
            };
        wording.setTitle(TRANS("Lyric display"));
        for (const auto* label : {"Lyric text", "Pronunciation", "Text and pronunciation"})
            wording.addItem(juce::translate(label), wording.getNumItems() + 1);
        wording.setSelectedId(1, juce::dontSendNotification);
        wording.onChange = [this] {
            options.lyricDisplay = static_cast<LyricDisplay>(wording.getSelectedId() - 1);
            grid.repaint();
        };
        for (int i = 1; i <= 7; ++i)
            staff.addItem(juce::translate(staffName(static_cast<StaffStyle>(i))), i);
        track.setTitle(TRANS("Score track"));
        staff.setTitle(TRANS("Staff type"));
        density.setTitle(TRANS("Bars per line"));
        for (int n : {1, 2, 4, 8, 16})
            density.addItem(juce::String(n) + " " + TRANS("bars / line"), n);
        density.setSelectedId(4, juce::dontSendNotification);
        density.onChange = [this] {
            options.barsPerLine = density.getSelectedId();
            resized();
        };
        voices.setTitle(TRANS("Lyric voice"));
        voices.onChange = [this] {
            options.lyricVoice = voices.getSelectedId() > 1 ? voices.getText() : juce::String{};
            grid.repaint();
        };
        follow.setToggleState(true, juce::dontSendNotification);
        follow.onClick = [this] { updatePage(); };
        track.onChange = [this] { update(); };
        staff.onChange = [this] {
            if (index() >= 0)
                styles[score.parts[index()].id] = static_cast<StaffStyle>(staff.getSelectedId());
            update();
        };
        previous.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
        pdf.onClick = [this] { save(true); };
        xml.onClick = [this] { save(false); };
        grid.draw = [this](juce::Graphics& g) {
            if (!lead && index() < 0)
                return;
            drawReadingGrid(g, grid.getLocalBounds(), score, style(), notes, lead, firstBar, columns, rows, options,
                            tick);
        };
        counter.setJustificationType(juce::Justification::centred);
    }
    void setSong(const OstfpPreview& project, std::optional<std::size_t> preferred, std::optional<bool> switchMode) {
        auto selected =
            index() >= 0 && index() < static_cast<int>(score.parts.size()) ? score.parts[index()].id : juce::String{};
        score = notationScore(project);
        title.setText(score.title, juce::dontSendNotification);
        juce::StringArray names;
        for (const auto& mark : score.lyrics)
            if (mark.voice.isNotEmpty())
                names.addIfNotAlreadyThere(mark.voice);
        voices.clear(juce::dontSendNotification);
        voices.addItem(TRANS("All lyric voices"), 1);
        for (int i = 0; i < names.size(); ++i)
            voices.addItem(names[i], i + 2);
        if (!names.contains(options.lyricVoice))
            options.lyricVoice = juce::String{};
        voices.setSelectedId(names.indexOf(options.lyricVoice) + 2, juce::dontSendNotification);
        if (switchMode) {
            lead = *switchMode;
        }
        track.clear(juce::dontSendNotification);
        int choice = 0, part = 0;
        for (std::size_t i = 0; i < project.trackKinds.size(); ++i) {
            if (project.trackKinds[i] == TrackKind::audio)
                continue;
            const auto& p = score.parts[part];
            if (p.id == selected)
                choice = part;
            track.addItem(p.name, ++part);
        }
        if (preferred && (selected.isEmpty() || preferred != arrangementTrack) &&
            *preferred < project.trackKinds.size() && project.trackKinds[*preferred] != TrackKind::audio)
            choice = static_cast<int>(std::count_if(
                project.trackKinds.begin(), project.trackKinds.begin() + static_cast<std::ptrdiff_t>(*preferred),
                [](auto k) { return k != TrackKind::audio; }));
        if (preferred)
            arrangementTrack = preferred;
        if (!score.parts.empty())
            track.setSelectedId(choice + 1, juce::dontSendNotification);
        update();
    }
    void playbackPosition(juce::int64 value) {
        if (value == tick)
            return;
        tick = value;
        if (tick >= 0 && follow.getToggleState() && score.barTicks > 0 &&
            (tick < firstBar * score.barTicks || tick >= (firstBar + columns * rows) * score.barTicks))
            updatePage();
        else {
            updatePosition();
            grid.repaint();
        }
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(dark)); }
    void resized() override {
        auto area = getLocalBounds().reduced(10, 6);
        auto top = area.removeFromTop(34);
        follow.setBounds(top.removeFromRight(135));
        position.setBounds(top.removeFromRight(std::min(360, top.getWidth() / 2)));
        title.setBounds(top);
        juce::Rectangle<int> line;
        auto place = [&](juce::Component& c, int width) {
            if (!c.isVisible())
                return;
            if (line.getWidth() < width)
                line = area.removeFromTop(32);
            c.setBounds(line.removeFromLeft(width).reduced(2, 2));
        };
        place(density, 112);
        place(chords, 85);
        place(scales, 85);
        place(lyrics, 85);
        place(voices, 160);
        place(wording, 155);
        place(track, 195);
        place(staff, 170);
        auto bottom = area.removeFromBottom(33);
        pdf.setBounds(bottom.removeFromRight(131).reduced(2));
        if (!lead)
            xml.setBounds(bottom.removeFromRight(105).reduced(2));
        previous.setBounds(bottom.removeFromLeft(32).reduced(1));
        counter.setBounds(bottom.removeFromLeft(std::min(155, std::max(0, bottom.getWidth() - 32))));
        next.setBounds(bottom.removeFromLeft(32).reduced(1));
        status.setBounds(area.removeFromBottom(status.getText().isNotEmpty() ? 22 : 0));
        grid.setBounds(area.reduced(0, 4));
        columns = lead ? options.barsPerLine : std::clamp(grid.getWidth() / 330, 1, 4);
        const int rowHeight =
            lead ? (options.showLyrics && options.showScales ? 128 : 106) : (style() == StaffStyle::grand ? 192 : 132);
        rows = std::max(1, grid.getHeight() / rowHeight);
        updatePage();
    }

  private:
    int index() const { return track.getSelectedId() - 1; }
    StaffStyle style() const {
        if (index() < 0)
            return StaffStyle::treble;
        const auto& p = score.parts[index()];
        const auto found = styles.find(p.id);
        return found == styles.end() ? p.preferred : found->second;
    }
    void update() {
        const bool has = index() >= 0;
        track.setVisible(!lead);
        staff.setVisible(!lead);
        for (auto* c : std::array<juce::Component*, 4>{&density, &chords, &scales, &lyrics})
            c->setVisible(lead);
        voices.setVisible(lead && options.showLyrics && !score.lyrics.empty());
        wording.setVisible(voices.isVisible());
        lyrics.setEnabled(!score.lyrics.empty());
        chords.setEnabled(!score.chords.empty());
        scales.setEnabled(!score.scales.empty());
        staff.setEnabled(has);
        staff.setSelectedId(static_cast<int>(style()), juce::dontSendNotification);
        pdf.setEnabled(!busy && (lead || has));
        xml.setVisible(!lead);
        xml.setEnabled(!busy && has);
        notes = lead ? MeasureNotes{} : partMeasures(score, index(), style());
        status.setText(!lead && !has ? TRANS("No note tracks in this project.") : juce::String{},
                       juce::dontSendNotification);
        resized();
    }
    void step(int direction) {
        if (tick >= 0)
            follow.setToggleState(false, juce::dontSendNotification);
        firstBar += direction * columns * rows;
        updatePage();
    }
    void updatePage() {
        const int count = columns * rows;
        const int anchor =
            tick >= 0 && follow.getToggleState() && score.barTicks > 0 ? int(tick / score.barTicks) : firstBar;
        firstBar = std::clamp(anchor, 0, std::max(0, score.bars - 1)) / count * count;
        counter.setText(juce::String(firstBar + 1) + " - " + juce::String(std::min(firstBar + count, score.bars)) +
                            " / " + juce::String(score.bars),
                        juce::dontSendNotification);
        previous.setEnabled(firstBar > 0);
        next.setEnabled(firstBar + count < score.bars);
        updatePosition();
        grid.repaint();
    }
    void updatePosition() {
        if (score.barTicks <= 0 || score.beats <= 0 || score.bars == 0)
            return;
        const auto at =
            std::clamp<juce::int64>(tick >= 0 ? tick : firstBar * score.barTicks, 0, score.bars * score.barTicks - 1);
        auto key = score.key;
        for (const auto& change : score.keys)
            if (change.at <= at)
                key = change.text;
        juce::StringArray values;
        if (key.isNotEmpty())
            values.add(key);
        values.add(juce::String(score.beats) + "/" + juce::String(score.beatType));
        juce::String tempo;
        for (const auto& change : score.tempos)
            if (change.at <= at)
                tempo = change.text;
        if (tempo.isNotEmpty())
            values.add(tempo + " BPM");
        if (tick >= 0)
            values.add(TRANS("Bar ") + juce::String(1 + at / score.barTicks) + "." +
                       juce::String(1 + at % score.barTicks * score.beats / score.barTicks));
        position.setText(values.joinIntoString("   "), juce::dontSendNotification);
    }
    void save(bool asPdf) {
        const auto ext = asPdf ? ".pdf" : ".musicxml";
        const auto stem = juce::File::createLegalFileName(score.title + " - " +
                                                          (lead ? TRANS("Lead sheet") : score.parts[index()].name));
        auto file = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(stem + ext);
        chooser = std::make_unique<juce::FileChooser>(asPdf ? TRANS("Save printable PDF") : TRANS("Save MusicXML"),
                                                      file, juce::String("*") + ext);
        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [safe = juce::Component::SafePointer<Panel>(this), asPdf, ext, document = score, part = index(),
             notation = style(), bars = notes, isLead = lead, layout = options](const juce::FileChooser& fc) {
                if (!safe || fc.getResult() == juce::File{})
                    return;
                const auto dest = fc.getResult();
                if (!dest.hasFileExtension(ext)) {
                    safe->status.setText(TRANS("Choose the indicated file extension."), juce::dontSendNotification);
                    return;
                }
                safe->busy = true;
                safe->pdf.setEnabled(false);
                safe->xml.setEnabled(false);
                safe->status.setText(TRANS("Writing pages..."), juce::dontSendNotification);
                safe->resized();
                safe->worker.addJob([safe, asPdf, dest, document, part, notation, bars, isLead, layout] {
                    juce::TemporaryFile temp(dest);
                    juce::Result result = juce::Result::ok();
                    {
                        juce::FileOutputStream out(temp.getFile());
                        result = out.openedOk()
                                     ? (asPdf ? writeSheetPdf(out, document, part, notation, bars, isLead, layout)
                                              : writePartXml(out, document, part, notation, bars))
                                     : out.getStatus();
                        out.flush();
                        if (out.getStatus().failed())
                            result = out.getStatus();
                    }
                    if (result.wasOk() && !temp.overwriteTargetFileWithTemporary())
                        result = juce::Result::fail(TRANS("Could not save file."));
                    juce::MessageManager::callAsync([safe, asPdf, dest, result] {
                        if (!safe)
                            return;
                        safe->busy = false;
                        safe->update();
                        auto msg = result.failed() ? result.getErrorMessage() : dest.getFullPathName();
                        if (result.wasOk() && asPdf) {
                            if (dest.startAsProcess())
                                msg = TRANS("PDF saved. Print from your PDF viewer.");
                        }
                        safe->status.setText(msg, juce::dontSendNotification);
                        safe->resized();
                    });
                });
            });
    }
    NotationScore score;
    MeasureNotes notes;
    std::map<juce::String, StaffStyle> styles;
    std::optional<std::size_t> arrangementTrack;
    bool lead = true, busy = false;
    int firstBar = 0, columns = 4, rows = 1;
    juce::int64 tick = -1;
    SheetOptions options;
    juce::ComboBox track, staff, density, voices, wording;
    juce::ToggleButton chords{TRANS("Chords")}, scales{TRANS("Scales")}, lyrics{TRANS("Lyrics")};
    juce::ToggleButton follow{TRANS("Follow playback")};
    juce::TextButton previous{"<"}, next{">"}, pdf{TRANS("PDF / Print...")}, xml{"MusicXML..."};
    juce::Label title, position, counter, status;
    ReadingGrid grid;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool worker{1};
};
ScoreView::ScoreView() : panel_(std::make_unique<Panel>()) {
    setComponentID("notation-workspace");
    addAndMakeVisible(*panel_);
}
ScoreView::~ScoreView() = default;
void ScoreView::resized() { panel_->setBounds(getLocalBounds()); }
void ScoreView::showScore(const OstfpPreview& p, bool lead, std::optional<std::size_t> track) {
    panel_->setSong(p, track, lead);
}
void ScoreView::refresh(const OstfpPreview& p) { panel_->setSong(p, {}, {}); }
void ScoreView::playbackPosition(juce::int64 t) { panel_->playbackPosition(t); }

namespace {
class ImportPanel final : public juce::Component, public juce::DragAndDropContainer {
  public:
    explicit ImportPanel(OpenImportedSong callback) : open(std::move(callback)) {
        for (auto *b : {&chooseFile, &chooseFolder, &openSong})
            addAndMakeVisible(b);
        for (auto *l : {&summary, &heading, &pdf})
            addAndMakeVisible(l);
        addAndMakeVisible(engine);
        engine.addItem(TRANS("Local separation"), 1);
        engine.addItem("Demucs (htdemucs)", 2);
        engine.setSelectedId(1, juce::dontSendNotification);
        addChildComponent(format);
        format.addItem(TRANS("Stems and MIDI"), 3);
        format.addItem(TRANS("Audio stems"), 1);
        format.addItem(TRANS("Draft MIDI parts"), 2);
        format.setSelectedId(3, juce::dontSendNotification);
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&rows, false);
        heading.setFont(juce::FontOptions(16.f, juce::Font::bold));
        pdf.setText(TRANS("PDF score recognition awaits an OMR connection."), juce::dontSendNotification);
        reset();
        chooseFile.onClick = [this] { choose(false); };
        chooseFolder.onClick = [this] { choose(true); };
        openSong.onClick = [this] {
            if (!project)
                return;
            std::unique_ptr<OstfpPreview> result;
            if (notes && format.getSelectedId()==3) {
                auto status=combineImportParts(*project,*notes,result);
                if (status.failed()) {
                    summary.setText(juce::translate(status.getErrorMessage()),juce::dontSendNotification);
                    return;
                }
            } else result=notes && format.getSelectedId()==2 ? std::move(notes) : std::move(project);
            reset();
            open(std::move(result));
        };
        waves.onChange = [this] { rows.repaint(); };
    }
    void resized() override {
        auto area = getLocalBounds().reduced(12);
        auto first = area.removeFromTop(35);
        engine.setBounds(first.removeFromRight(155).reduced(2));
        chooseFolder.setBounds(first.removeFromRight(155).reduced(2));
        chooseFile.setBounds(first.reduced(2));
        pdf.setBounds(area.removeFromTop(24));
        heading.setBounds(area.removeFromTop(30));
        summary.setBounds(area.removeFromTop(50));
        auto bottom = area.removeFromBottom(40);
        if (format.isVisible())
            format.setBounds(bottom.removeFromLeft(172).reduced(2));
        openSong.setBounds(bottom.reduced(2));
        viewport.setBounds(area);
        const int width = std::max(1, area.getWidth() - 16);
        int y = 0;
        for (auto& item : items) {
            const bool pair = notes && item->index >= 0;
            const bool right = pair && &item->source == notes.get();
            item->setBounds(right ? width / 2 : 0, y, pair ? width / 2 - 2 : width - 2, 48);
            if (!pair || right)
                y += 52;
        }
        rows.setSize(width, y);
    }
  private:
    void reset() {
        items.clear();
        waves.load({});
        project.reset();
        notes.reset();
        format.setVisible(false);
        openSong.setEnabled(false);
        heading.setText(TRANS("Part analysis"), juce::dontSendNotification);
        summary.setText(TRANS("Choose MIDI or WAV to see the individual parts."), juce::dontSendNotification);
        resized();
    }
    void showResult(std::unique_ptr<OstfpPreview> parsed, std::unique_ptr<OstfpPreview> drafts,
                    const juce::Result &result, const juce::String &info) {
        for (auto *b : {&chooseFile, &chooseFolder})
            b->setEnabled(true);
        engine.setEnabled(true);
        if (result.failed() || !parsed) {
            summary.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
            return;
        }
        project = std::move(parsed);
        notes = std::move(drafts);
        waves.load(project->audio);
        heading.setText(TRANS("Analysed parts") + "   /   " + project->title, juce::dontSendNotification);
        summary.setText(info + "\n" + TRANS("Drag a part to a matching track, or open the full set below."),
                        juce::dontSendNotification);
        auto add = [this](OstfpPreview &source, int i, const juce::String &text) {
            auto row = std::make_unique<ImportResultRow>(text, source, i);
            if (i >= 0 && source.trackKinds[i] == TrackKind::audio)
                row->waveforms = &waves;
            rows.addAndMakeVisible(*row);
            items.push_back(std::move(row));
        };
        for (int i = 0; i < int(project->sourceTrackCount()); ++i) {
            auto name = juce::String::fromUTF8(project->trackLanes[i].label);
            if (notes)
                name = juce::translate(name);
            add(*project, i, (notes ? "WAV   " : "MIDI   ") + name);
            if (notes)
                add(*notes, i, "MIDI   " + name);
        }
        if (juce::JSON::parse(project->sourceScore.toString())["harmony"]["chords"].size())
            add(*project, -1, TRANS("Drag estimated chords to the chord lane"));
        format.setVisible(notes != nullptr);
        format.setSelectedId(3, juce::dontSendNotification);
        openSong.setEnabled(true);
        resized();
    }
    void choose(bool folder) {
        chooser = std::make_unique<juce::FileChooser>(folder ? TRANS("Choose four-stem folder")
                                                             : TRANS("Analyse MIDI or WAV"),
                                                      juce::File{}, folder ? "*" : "*.mid;*.midi;*.wav;*.wave");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 (folder ? juce::FileBrowserComponent::canSelectDirectories
                                         : juce::FileBrowserComponent::canSelectFiles),
                             [safe = juce::Component::SafePointer<ImportPanel>(this)](const auto &fc) {
                                 const auto source = fc.getResult();
                                 if (!safe || source == juce::File{})
                                     return;
                                 safe->reset();
                                 safe->chooseFile.setEnabled(false);
                                 safe->chooseFolder.setEnabled(false);
                                 safe->engine.setEnabled(false);
                                 safe->summary.setText(TRANS("Analysing source...") + "  " + source.getFileName(),
                                                       juce::dontSendNotification);
                                 safe->worker.addJob([safe, source, useDemucs = safe->engine.getSelectedId() == 2] {
                                     std::unique_ptr<OstfpPreview> parts, drafts;
                                     juce::String info;
                                     const auto result = analyseImportParts(source, useDemucs, parts, drafts, info);
                                     juce::MessageManager::callAsync(
                                         [safe, result, info, p = std::move(parts), d = std::move(drafts)]() mutable {
                                             if (safe)
                                                 safe->showResult(std::move(p), std::move(d), result, info);
                                         });
                                 });
                             });
    }
    OpenImportedSong open;
    juce::TextButton chooseFile{TRANS("Analyse MIDI or WAV")}, chooseFolder{TRANS("Load four-stem folder...")},
        openSong{TRANS("Open as new unsaved song")};
    juce::ComboBox engine, format;
    juce::Label summary, heading, pdf;
    juce::Component rows;
    juce::Viewport viewport;
    AudioClipWaveforms waves;
    std::unique_ptr<OstfpPreview> project, notes;
    std::vector<std::unique_ptr<ImportResultRow>> items;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool worker{1};
};
class ImportWindow final : public juce::DocumentWindow {
  public:
    explicit ImportWindow(OpenImportedSong open)
        : DocumentWindow(TRANS("Import and transcription"), juce::Colour(dark), closeButton) {
        setUsingNativeTitleBar(true);
        setContentOwned(new ImportPanel(std::move(open)), false);
        centreWithSize(750, 468);
    }
    void closeButtonPressed() override { setVisible(false); }
};
} // namespace
std::unique_ptr<juce::DocumentWindow> makeMediaImportWindow(OpenImportedSong open) {
    return std::make_unique<ImportWindow>(std::move(open));
}
