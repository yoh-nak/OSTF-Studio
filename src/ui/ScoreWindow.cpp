#include "ui/ScoreWindow.h"
#include "ui/MediaImport.h"

using namespace ostfstudio;
namespace {
constexpr auto dark = 0xff202a30;
constexpr auto workspace = 0xff344047;
void present(juce::DocumentWindow& w) {
    w.setVisible(true);
    w.toFront(true);
}
class Paper final : public juce::Component {
  public:
    std::function<void(juce::Graphics&)> draw;
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(workspace));
        const auto paper = juce::Rectangle<float>(0, 0, 595, 842);
        const auto fit = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                             .getTransformToFit(paper, getLocalBounds().reduced(10).toFloat());
        g.addTransform(fit);
        g.reduceClipRegion(paper.toNearestInt());
        if (draw)
            draw(g);
    }
};
} // namespace
class ScoreWindow::Panel final : public juce::Component {
  public:
    Panel() {
        for (auto* c : std::array<juce::Component*, 13>{&paper, &mode, &track, &staff, &previous, &next, &counter, &pdf,
                                                        &xml, &status, &density, &voices, &follow})
            addAndMakeVisible(c);
        mode.addItem(TRANS("Lead sheet"), 1);
        mode.addItem(TRANS("Score"), 2);
        for (int i = 1; i <= 7; ++i)
            staff.addItem(juce::translate(staffName(static_cast<StaffStyle>(i))), i);
        track.setTitle(TRANS("Score track"));
        staff.setTitle(TRANS("Staff type"));
        for (int n : {1, 2, 4, 8})
            density.addItem(juce::String(n) + " " + TRANS("bars / line"), n);
        density.setSelectedId(4, juce::dontSendNotification);
        density.onChange = [this] {
            options.barsPerLine = density.getSelectedId();
            page = 0;
            updatePage();
        };
        voices.setTitle(TRANS("Lyric voice"));
        voices.onChange = [this] {
            options.lyricVoice = voices.getSelectedId() > 1 ? voices.getText() : juce::String{};
            paper.repaint();
        };
        follow.setToggleState(true, juce::dontSendNotification);
        mode.onChange = [this] {
            lead = mode.getSelectedId() == 1;
            page = 0;
            update();
        };
        track.onChange = [this] {
            page = 0;
            update();
        };
        staff.onChange = [this] {
            if (index() >= 0)
                styles[score.parts[index()].id] = static_cast<StaffStyle>(staff.getSelectedId());
            page = 0;
            update();
        };
        previous.onClick = [this] {
            --page;
            updatePage();
        };
        next.onClick = [this] {
            ++page;
            updatePage();
        };
        pdf.onClick = [this] { save(true); };
        xml.onClick = [this] { save(false); };
        paper.draw = [this](juce::Graphics& g) {
            drawSheetPage(g, score, index(), style(), notes, lead, page, options, tick);
        };
        counter.setJustificationType(juce::Justification::centred);
    }
    void setSong(const OstfpPreview& project, std::optional<std::size_t> preferred, std::optional<bool> switchMode) {
        auto selected =
            index() >= 0 && index() < static_cast<int>(score.parts.size()) ? score.parts[index()].id : juce::String{};
        score = notationScore(project);
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
            page = 0;
        }
        mode.setSelectedId(lead ? 1 : 2, juce::dontSendNotification);
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
        if (preferred && *preferred < project.trackKinds.size() && project.trackKinds[*preferred] != TrackKind::audio)
            choice = static_cast<int>(std::count_if(
                project.trackKinds.begin(), project.trackKinds.begin() + static_cast<std::ptrdiff_t>(*preferred),
                [](auto k) { return k != TrackKind::audio; }));
        if (!score.parts.empty())
            track.setSelectedId(choice + 1, juce::dontSendNotification);
        update();
    }
    void playbackPosition(juce::int64 value) {
        if (value == tick)
            return;
        tick = value;
        if (tick >= 0 && follow.getToggleState() && score.barTicks > 0) {
            const int newPage = static_cast<int>(tick / score.barTicks) / barsPerPage(lead, style(), options);
            if (newPage != page) {
                page = newPage;
                updatePage();
                return;
            }
        }
        paper.repaint();
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(dark)); }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(31);
        mode.setBounds(top.removeFromLeft(120).reduced(2, 1));
        follow.setBounds(top.removeFromRight(116).reduced(2, 1));
        if (!lead) {
            track.setBounds(top.removeFromLeft(std::min(200, top.getWidth() / 2)).reduced(2, 1));
            staff.setBounds(top.removeFromLeft(155).reduced(2, 1));
        } else {
            density.setBounds(top.removeFromLeft(std::min(118, top.getWidth() / 2)).reduced(2, 1));
            voices.setBounds(top.removeFromLeft(std::min(178, top.getWidth())).reduced(2, 1));
        }
        auto bottom = area.removeFromBottom(32);
        previous.setBounds(bottom.removeFromLeft(40).reduced(1, 1));
        counter.setBounds(bottom.removeFromLeft(64));
        next.setBounds(bottom.removeFromLeft(40).reduced(1, 1));
        pdf.setBounds(bottom.removeFromRight(148).reduced(2, 1));
        xml.setBounds(bottom.removeFromRight(125).reduced(2, 1));
        status.setBounds(area.removeFromBottom(22));
        paper.setBounds(area.reduced(0, 4));
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
        density.setVisible(lead);
        voices.setVisible(lead && !score.lyrics.empty());
        staff.setEnabled(has);
        staff.setSelectedId(static_cast<int>(style()), juce::dontSendNotification);
        pdf.setEnabled(!busy && (lead || has));
        xml.setVisible(!lead);
        xml.setEnabled(!busy && has);
        notes = lead ? MeasureNotes{} : partMeasures(score, index(), style());
        status.setText(!lead && !has ? TRANS("No note tracks in this project.") : juce::String{},
                       juce::dontSendNotification);
        resized();
        updatePage();
    }
    void updatePage() {
        const int pages = sheetPages(score, lead, style(), options);
        page = juce::jlimit(0, pages - 1, page);
        counter.setText(juce::String(page + 1) + " / " + juce::String(pages), juce::dontSendNotification);
        previous.setEnabled(page > 0);
        next.setEnabled(page + 1 < pages);
        paper.repaint();
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
                    });
                });
            });
    }
    NotationScore score;
    MeasureNotes notes;
    std::map<juce::String, StaffStyle> styles;
    bool lead = true, busy = false;
    int page = 0;
    juce::int64 tick = -1;
    SheetOptions options;
    juce::ComboBox mode, track, staff, density, voices;
    juce::ToggleButton follow{TRANS("Follow playback")};
    juce::TextButton previous{"<"}, next{">"}, pdf{TRANS("PDF / Print...")}, xml{"MusicXML..."};
    juce::Label counter, status;
    Paper paper;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool worker{1};
};
ScoreWindow::ScoreWindow()
    : DocumentWindow(TRANS("Lead sheet and score"), juce::Colour(dark), allButtons), panel_(new Panel()) {
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(480, 380, 2400, 2200);
    setContentOwned(panel_, false);
    centreWithSize(735, 795);
}
void ScoreWindow::showScore(const OstfpPreview& p, bool lead, std::optional<std::size_t> track) {
    panel_->setSong(p, track, lead);
    present(*this);
}
void ScoreWindow::refresh(const OstfpPreview& p) { panel_->setSong(p, {}, {}); }
void ScoreWindow::playbackPosition(juce::int64 t) { panel_->playbackPosition(t); }

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
