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
class ImportPanel final : public juce::Component {
  public:
    explicit ImportPanel(std::function<void(const juce::File&)> callback) : open(std::move(callback)) {
        for (auto* c : std::array<juce::Component*, 6>{&midi, &wav, &melody, &save, &openSong, &summary})
            addAndMakeVisible(c);
        summary.setText(TRANS("Choose a MIDI performance or a WAV recording to prepare a new OSTF project."),
                        juce::dontSendNotification);
        save.setEnabled(false);
        openSong.setEnabled(false);
        midi.onClick = [this] { choose(false, false); };
        wav.onClick = [this] { choose(true, false); };
        melody.onClick = [this] { choose(true, true); };
        save.onClick = [this] { saveAs(); };
        openSong.onClick = [this] { open(saved); };
    }
    void resized() override {
        auto a = getLocalBounds().reduced(16);
        auto top = a.removeFromTop(34);
        midi.setBounds(top.removeFromLeft(top.getWidth() / 3).reduced(2));
        wav.setBounds(top.removeFromLeft(top.getWidth() / 2).reduced(2));
        melody.setBounds(top.reduced(2));
        auto bottom = a.removeFromBottom(35);
        save.setBounds(bottom.removeFromLeft(bottom.getWidth() / 2).reduced(2));
        openSong.setBounds(bottom.reduced(2));
        summary.setBounds(a);
    }

  private:
    void choose(bool audio, bool transcribe) {
        chooser = std::make_unique<juce::FileChooser>(audio ? TRANS("Choose WAV") : TRANS("Choose MIDI"), juce::File{},
                                                      audio ? "*.wav;*.wave" : "*.mid;*.midi");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<ImportPanel>(this), transcribe](const juce::FileChooser& fc) {
                if (!safe || fc.getResult() == juce::File{})
                    return;
                safe->source = fc.getResult();
                safe->project.reset();
                safe->openSong.setEnabled(false);
                safe->save.setEnabled(false);
                for (auto* button : {&safe->midi, &safe->wav, &safe->melody})
                    button->setEnabled(false);
                safe->summary.setText(TRANS("Analysing source..."), juce::dontSendNotification);
                safe->worker.addJob([safe, source = safe->source, transcribe] {
                    std::unique_ptr<OstfpPreview> draft;
                    juce::String info;
                    auto r = prepareMediaImport(source, draft, info, transcribe);
                    juce::MessageManager::callAsync([safe, source, draft = std::move(draft), r, info]() mutable {
                        if (!safe)
                            return;
                        safe->project = std::move(draft);
                        safe->summary.setText(source.getFileName() + "\n\n" + (r.wasOk() ? info : r.getErrorMessage()),
                                              juce::dontSendNotification);
                        for (auto* button : {&safe->midi, &safe->wav, &safe->melody})
                            button->setEnabled(true);
                        safe->save.setEnabled(r.wasOk());
                    });
                });
            });
    }
    void saveAs() {
        chooser = std::make_unique<juce::FileChooser>(
            TRANS("Save new OSTF project"), source.withFileExtension(".ostf").getNonexistentSibling(), "*.ostf");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<ImportPanel>(this)](const juce::FileChooser& fc) {
                                 if (!safe || !safe->project || fc.getResult() == juce::File{})
                                     return;
                                 auto dest = fc.getResult();
                                 auto r = dest.hasFileExtension("ostf")
                                              ? saveMediaImport(*safe->project, dest)
                                              : juce::Result::fail(TRANS("Choose a .ostf filename."));
                                 if (r.wasOk())
                                     safe->saved = dest;
                                 safe->openSong.setEnabled(r.wasOk());
                                 safe->summary.setText(r.wasOk() ? dest.getFullPathName() : r.getErrorMessage(),
                                                       juce::dontSendNotification);
                             });
    }
    std::function<void(const juce::File&)> open;
    juce::TextButton midi{TRANS("Import MIDI...")}, wav{TRANS("Prepare WAV...")}, melody{TRANS("Transcribe melody...")},
        save{TRANS("Save new OSTF project")}, openSong{TRANS("Open in Studio")};
    juce::Label summary;
    juce::File source, saved;
    std::unique_ptr<OstfpPreview> project;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool worker{1};
};
class ImportWindow final : public juce::DocumentWindow {
  public:
    explicit ImportWindow(std::function<void(const juce::File&)> open)
        : DocumentWindow(TRANS("Import and transcription"), juce::Colour(dark), closeButton) {
        setUsingNativeTitleBar(true);
        setContentOwned(new ImportPanel(std::move(open)), false);
        centreWithSize(540, 240);
    }
    void closeButtonPressed() override { setVisible(false); }
};
} // namespace
std::unique_ptr<juce::DocumentWindow> makeMediaImportWindow(std::function<void(const juce::File&)> open) {
    return std::make_unique<ImportWindow>(std::move(open));
}
