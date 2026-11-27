#include "ui/ScoreView.h"
#include "ui/NativePrint.h"
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
    std::function<void(std::size_t)> partChanged;
    std::function<void(int)> barsChanged;
    void enablePartSelection(bool enabled) { track.setEnabled(enabled); }
    void revealBar(int bar) {
        if (preview || (bar>=firstBar && bar<firstBar+columns*rows)) return;
        follow.setToggleState(false, juce::dontSendNotification);
        firstBar=bar;
        updatePage();
    }
    std::vector<std::size_t> partTracks;
    std::optional<std::size_t> selectedPartTrack() const {
        if (!juce::isPositiveAndBelow(index(),int(partTracks.size()))) return {};
        return partTracks[size_t(index())];
    }
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
            updatePage();
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
            updatePage();
        };
        follow.setToggleState(true, juce::dontSendNotification);
        follow.onClick = [this] { updatePage(); };
        track.onChange = [this] {
            update();
            if (auto value = selectedPartTrack(); value && partChanged) partChanged(*value);
        };
        staff.onChange = [this] {
            if (index() >= 0)
                styles[score.parts[index()].id] = static_cast<StaffStyle>(staff.getSelectedId());
            update();
        };
        previous.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
        pdf.setComponentID("sheet-print"); leavePreview.setComponentID("sheet-print-back");
        breaks.setComponentID("sheet-page-breaks");
        for (auto* control : std::array<juce::Component*, 8>{&paperSize, &orientation, &margin, &breaks, &breakLabel, &sections, &barNumbers, &leavePreview})
            addChildComponent(control);
        for (int mm : {5, 8, 12, 16, 20, 25, 30, 35})
            margin.addItem(TRANS("Margin") + "  " + juce::String(mm) + " mm", mm);
        paperSize.setComponentID("sheet-paper-size");
        orientation.setComponentID("sheet-orientation");
        paperSize.addItem("A4", 1); paperSize.addItem("US Letter", 2);
        orientation.addItem(TRANS("Portrait"), 1); orientation.addItem(TRANS("Landscape"), 2);
        paperSize.setSelectedId(1, juce::dontSendNotification);
        orientation.setSelectedId(1, juce::dontSendNotification);
        paperSize.onChange = orientation.onChange = [this] {
            options.usLetter = paperSize.getSelectedId() == 2;
            options.landscape = orientation.getSelectedId() == 2;
            updatePage();
        };
        margin.setSelectedId(options.marginMm, juce::dontSendNotification);
        margin.onChange = [this] { options.marginMm = margin.getSelectedId(); updatePage(); };
        breaks.setTextToShowWhenEmpty("9, 17, 33", juce::Colours::grey);
        breaks.setTitle(TRANS("Start new pages at bars"));
        breakLabel.setText(TRANS("Start new pages at bars"), juce::dontSendNotification);
        breaks.onTextChange = [this] {
            std::vector<int> parsed;
            validBreaks = true;
            for (const auto& token : juce::StringArray::fromTokens(breaks.getText(), ", ", "")) {
                if (token.isEmpty()) continue;
                const auto bar = token.getIntValue();
                if (!token.containsOnly("0123456789") || bar < 2 || bar > score.bars) { validBreaks = false; break; }
                parsed.push_back(bar);
            }
            if (validBreaks) options.pageBreakBars = std::move(parsed);
            status.setText(validBreaks ? juce::String{} : TRANS("Enter bar numbers between 2 and the final bar."), juce::dontSendNotification);
            pdf.setEnabled(validBreaks && !busy);
            resized();
        };
        sections.setToggleState(true, juce::dontSendNotification);
        barNumbers.setToggleState(true, juce::dontSendNotification);
        sections.onClick = barNumbers.onClick = [this] {
            options.showSections = sections.getToggleState();
            options.showBarNumbers = barNumbers.getToggleState();
            updatePage();
        };
        addChildComponent(printer);
        addChildComponent(credits);
        credits.setTitle(TRANS("Print credits"));
        credits.onClick = [this] {
            if (creditLines.isEmpty()) return;
            juce::PopupMenu menu;
            menu.addItem(1,TRANS("Select all"),true,score.printCredits.size()==creditLines.size());
            menu.addItem(2,TRANS("Clear selection"));
            menu.addSeparator();
            for (int i=0;i<creditLines.size();++i) menu.addItem(10+i,creditLines[i],true,!excluded.contains(creditLines[i]));
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&credits),
                [safe=juce::Component::SafePointer<Panel>(this),lines=creditLines](int id){
                    if (!safe || !id || safe->creditLines!=lines) return;
                    if (id==1) safe->excluded.clear();
                    else if (id==2) safe->excluded=lines;
                    else if(juce::isPositiveAndBelow(id-10,lines.size())) {
                        const auto& line=lines[id-10];
                        if(safe->excluded.contains(line)) safe->excluded.removeString(line); else safe->excluded.add(line);
                    }
                    safe->filterCredits(); safe->page=0; safe->updatePage();
                });
        };
        printer.onClick = [this] {
#if JUCE_WINDOWS || JUCE_MAC
            const auto result=printSheet(*this,score,index(),style(),notes,lead,options);
            if (result.failed()) status.setText(result.getErrorMessage(),juce::dontSendNotification);
            resized();
#endif
        };
        leavePreview.onClick = [this]{ preview = false; update(); };
        pdf.onClick = [this] {
            if (preview) save(true);
            else { preview = true; page = 0; update(); }
        };
        xml.onClick = [this] { save(false); };
        grid.draw = [this](juce::Graphics& g) {
            if (preview) {
                if (paper.isNull()) {
                    paper = juce::Image(juce::Image::RGB, options.paperPoints().x * 2, options.paperPoints().y * 2, true, juce::SoftwareImageType{});
                    {
                        juce::Graphics pg(paper);
                        pg.addTransform(juce::AffineTransform::scale(2.f));
                        drawSheetPage(pg, score, index(), style(), notes, lead, page, options);
                    }
                    paper.desaturate();
                }
                g.setColour(juce::Colours::white);
                g.drawImageWithin(paper, 0, 0, grid.getWidth(), grid.getHeight(), juce::RectanglePlacement::centred);
                return;
            }
            if (!lead && index() < 0)
                return;
            drawReadingGrid(g, grid.getLocalBounds(), score, style(), notes, lead, firstBar, columns, rows, options,
                            tick);
        };
        counter.setJustificationType(juce::Justification::centred);
    }
    void filterCredits() {
        score.printCredits=selectedCreditLines(creditLines,excluded);
        credits.setEnabled(!creditLines.isEmpty());
        credits.setButtonText(creditLines.isEmpty() ? TRANS("No song credits") :
            TRANS("Credits") + "  " + juce::String(score.printCredits.size()) + "/" + juce::String(creditLines.size()));
    }
    void openPrintPreview() { preview=true; page=0; update(); }
    bool showsLeadSheet() const { return lead; }
    void setSong(const OstfpPreview& project, std::optional<std::size_t> preferred, std::optional<bool> switchMode) {
        auto selected =
            index() >= 0 && index() < static_cast<int>(score.parts.size()) ? score.parts[index()].id : juce::String{};
        if (songKey!=project.documentId+project.title) excluded.clear();
        songKey=project.documentId+project.title;
        score = notationScore(project);
        creditLines=score.printCredits;
        filterCredits();
        title.setText(score.title, juce::dontSendNotification);
        juce::StringArray names;
        for (const auto& mark : score.lyrics)
            if (mark.voice.isNotEmpty())
                names.addIfNotAlreadyThere(mark.voice);
        voices.clear(juce::dontSendNotification);
        voices.addItem(TRANS("Primary lyrics"), 1);
        for (int i = 0; i < names.size(); ++i)
            voices.addItem(names[i], i + 2);
        if (!names.contains(options.lyricVoice))
            options.lyricVoice = juce::String{};
        voices.setSelectedId(names.indexOf(options.lyricVoice) + 2, juce::dontSendNotification);
        if (switchMode) {
            preview = false; page = 0; validBreaks = true;
            options.pageBreakBars.clear(); breaks.clear();
            lead = *switchMode;
        }
        track.clear(juce::dontSendNotification);
        partTracks.clear();
        int choice = 0, part = 0;
        for (std::size_t i = 0; i < project.trackKinds.size(); ++i) {
            if (project.trackKinds[i] == TrackKind::audio)
                continue;
            const auto& p = score.parts[part];
            if (p.id == selected)
                choice = part;
            partTracks.push_back(i);
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
        if (preview) return;
        if (tick >= 0 && follow.getToggleState() && score.barTicks > 0 &&
            (tick < score.barStart(firstBar) || tick >= score.barStart(firstBar + columns * rows)))
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
        place(paperSize, 100);
        place(orientation, 106);
        place(margin, 144);
        place(breakLabel, 150);
        place(breaks, 125);
        place(sections, 110);
        place(credits, 155);
        place(barNumbers, 136);
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
        printer.setVisible(preview);
        printer.setEnabled(validBreaks && !busy && (lead || index()>=0));
        if (printer.isVisible()) printer.setBounds(bottom.removeFromRight(105).reduced(2));
        if (preview) leavePreview.setBounds(bottom.removeFromRight(130).reduced(2));
        if (!preview && !lead)
            xml.setBounds(bottom.removeFromRight(105).reduced(2));
        previous.setBounds(bottom.removeFromLeft(32).reduced(1));
        counter.setBounds(bottom.removeFromLeft(std::min(155, std::max(0, bottom.getWidth() - 32))));
        next.setBounds(bottom.removeFromLeft(32).reduced(1));
        status.setBounds(area.removeFromBottom(status.getText().isNotEmpty() ? 22 : 0));
        grid.setBounds(area.reduced(0, 4));
        columns = lead ? options.barsPerLine : std::clamp(grid.getWidth() / 330, 1, 4);
        const int rowHeight = lead ? (options.showLyrics && options.showScales ? 128 : 106)
            : (style() == StaffStyle::grand ? 192 : 132) + (options.showLyrics ? 24 : 0);
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
        for (auto* c : std::array<juce::Component*, 6>{&paperSize,&orientation,&margin,&breakLabel,&breaks,&leavePreview}) c->setVisible(preview);
        sections.setVisible(lead || preview); barNumbers.setVisible(lead || preview);
        credits.setVisible(preview);
        follow.setVisible(!preview); position.setVisible(!preview);
        pdf.setButtonText(preview ? TRANS("Save PDF...") : TRANS("Print preview..."));
        title.setText(preview ? TRANS("Print preview") + "  /  " + score.title : score.title, juce::dontSendNotification);
        track.setVisible(!lead);
        staff.setVisible(!lead);
        density.setVisible(lead);
        scales.setVisible(lead);
        voices.setVisible(options.showLyrics && !score.lyrics.empty());
        wording.setVisible(options.showLyrics && (!score.lyrics.empty() || !score.displayPhrases.empty()));
        lyrics.setEnabled(!score.lyrics.empty() || !score.displayPhrases.empty());
        chords.setEnabled(!score.chords.empty());
        scales.setEnabled(!score.scales.empty());
        staff.setEnabled(has);
        staff.setSelectedId(static_cast<int>(style()), juce::dontSendNotification);
        pdf.setEnabled(!busy && (!preview || validBreaks) && (lead || has));
        xml.setVisible(!lead && !preview);
        xml.setEnabled(!busy && has);
        notes = lead ? MeasureNotes{} : partMeasures(score, index(), style());
        status.setText(!lead && !has ? TRANS("No note tracks in this project.") : juce::String{},
                       juce::dontSendNotification);
        resized();
    }
    void step(int direction) {
        if (preview) { page += direction; updatePage(); return; }
        if (tick >= 0)
            follow.setToggleState(false, juce::dontSendNotification);
        firstBar += direction * columns * rows;
        updatePage();
        if (barsChanged) barsChanged(firstBar);
    }
    void updatePage() {
        paper = {};
        if (preview) {
            const int total = sheetPages(score, lead, style(), options);
            page = std::clamp(page, 0, total-1);
            counter.setText(TRANS("Page") + " " + juce::String(page+1) + " / " + juce::String(total), juce::dontSendNotification);
            previous.setEnabled(page > 0); next.setEnabled(page+1 < total); grid.repaint();
            return;
        }
        const int count = columns * rows;
        const int anchor =
            tick >= 0 && follow.getToggleState() && score.barTicks > 0 ? score.barAt(tick) : firstBar;
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
            std::clamp<juce::int64>(tick >= 0 ? tick : score.barStart(firstBar), 0, score.barStart(score.bars) - 1);
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
            values.add(TRANS("Bar ") + juce::String(1 + score.barAt(at)) + "." +
                       juce::String(1 + (at - score.barStart(score.barAt(at))) * activeMeter(score.meters, double(at), {0,score.beats,score.beatType}).denominator / (4 * score.ppq)));
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
    bool preview = false, validBreaks = true;
    int page = 0;
    juce::Image paper;
    juce::ComboBox margin, paperSize, orientation;
    juce::Label breakLabel;
    juce::TextEditor breaks;
    juce::ToggleButton sections{TRANS("Sections")}, barNumbers{TRANS("Bar numbers")};
    juce::StringArray creditLines,excluded;
    juce::String songKey;
    juce::TextButton credits;
    juce::TextButton printer{TRANS("Print...")};
    juce::TextButton leavePreview{TRANS("Back to score")};
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
    panel_->barsChanged = [this](int bar){ if (barsChanged) barsChanged(bar); };
    panel_->partChanged = [this](size_t i){ if (partChanged) partChanged(i); };
    addAndMakeVisible(*panel_);
}
ScoreView::~ScoreView() = default;
void ScoreView::openPrintPreview() { panel_->openPrintPreview(); }
void ScoreView::resized() { panel_->setBounds(getLocalBounds()); }
void ScoreView::showScore(const OstfpPreview& p, bool lead, std::optional<std::size_t> track) {
    panel_->setSong(p, track, lead);
}
void ScoreView::refresh(const OstfpPreview& p) { panel_->setSong(p, {}, {}); }
std::optional<size_t> ScoreView::selectedPartTrack() const { return panel_->selectedPartTrack(); }
void ScoreView::enablePartSelection(bool enabled) { panel_->enablePartSelection(enabled); }
void ScoreView::revealBar(int bar) { panel_->revealBar(bar); }
bool ScoreView::showsLeadSheet() const { return panel_->showsLeadSheet(); }
void ScoreView::playbackPosition(juce::int64 t) { panel_->playbackPosition(t); }

namespace {
class ImportPanel final : public juce::Component, public juce::DragAndDropContainer {
  public:
    explicit ImportPanel(OpenImportedSong callback) : open(std::move(callback)) {
        for (juce::Component* c : std::array<juce::Component*, 5>{&chooseFile, &heading, &summary, &viewport, &openSong})
            addAndMakeVisible(c);
        addAndMakeVisible(mode);
        addAndMakeVisible(disclosure);
        addChildComponent(installOmr);
        addChildComponent(retryPdf);
        installOmr.onClick = [] { juce::URL("https://audiveris.github.io/audiveris/_pages/tutorials/install/binaries/").launchInDefaultBrowser(); };
        retryPdf.onClick = [this] { analyse(lastFile); };
        addChildComponent(cancel);
        cancel.onClick = [this] {
            ++analysisId;
            worker.removeAllJobs(true, 0);
            showResult({}, juce::Result::fail(TRANS("Analysis cancelled.")), {});
        };
        mode.onChange = [this] {
            disclosure.setText(mode.getSelectedId() > 1 ? TRANS("AI receives note measurements or rendered score pages.")
                                                        : TRANS("Analysis runs on this computer."), juce::dontSendNotification);
        };
        connections({});
        viewport.setViewedComponent(&rows, false);
        heading.setFont(juce::FontOptions(19.f, juce::Font::bold));
        heading.setText(TRANS("Score analysis"), juce::dontSendNotification);
        summary.setText(TRANS("Select an audio, MIDI, PDF or MusicXML file."), juce::dontSendNotification);
        openSong.setEnabled(false);
        chooseFile.onClick = [this] { choose(); };
        openSong.onClick = [this] {
            if (!project) return;
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, TRANS("Open in this window"),
                TRANS("Replace the current song with this score? Save any changes to the current song first."),
                TRANS("Open score"), TRANS("Cancel"), this,
                juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<ImportPanel>(this)](int answer) {
                    if (answer && safe && safe->project) {
                        safe->items.clear();
                        safe->openSong.setEnabled(false);
                        safe->open(std::move(safe->project));
                        safe->heading.setText(TRANS("Score analysis"), juce::dontSendNotification);
                        safe->summary.setText(TRANS("Select an audio, MIDI, PDF or MusicXML file."), juce::dontSendNotification);
                    }
                }));
        };
    }
    void connections(std::vector<AnalysisConnection> active) {
        const auto previous = mode.getText();
        providers = std::move(active);
        mode.clear(juce::dontSendNotification);
        mode.addItem(TRANS("Local analysis"), 1);
        int selected = 1;
        for (size_t i = 0; i < providers.size(); ++i) {
            const auto label = TRANS("AI analysis") + " / " + providers[i]->name();
            mode.addItem(label, int(i) + 2);
            if (previous == label) selected = int(i) + 2;
        }
        mode.setSelectedId(selected, juce::dontSendNotification);
        mode.onChange();
    }
    ~ImportPanel() override { worker.removeAllJobs(true, -1); }
    void resized() override {
        auto area = getLocalBounds().reduced(18);
        auto top = area.removeFromTop(36);
        chooseFile.setBounds(top.removeFromRight(std::min(218, top.getWidth() / 2)));
        heading.setBounds(top);
        auto controls = area.removeFromTop(34).reduced(0, 3);
        mode.setBounds(controls.removeFromLeft(std::min(210, controls.getWidth() / 2)));
        if (cancel.isVisible()) cancel.setBounds(controls.removeFromRight(92));
        disclosure.setBounds(controls);
        summary.setBounds(area.removeFromTop(52));
        if (retryPdf.isVisible()) {
            auto actions = area.removeFromTop(32);
            installOmr.setBounds(actions.removeFromLeft(165).reduced(2));
            retryPdf.setBounds(actions.removeFromLeft(135).reduced(2));
        }
        openSong.setBounds(area.removeFromBottom(38));
        area.reduce(0, 6);
        viewport.setBounds(area);
        const int width = std::max(1, area.getWidth() - 16);
        int y = 0;
        for (auto& item : items) {
            item->setBounds(0, y, width, 52);
            y += 55;
        }
        rows.setSize(width, y);
    }
  private:
    void showResult(std::unique_ptr<OstfpPreview> parsed, const juce::Result& result, const juce::String& info) {
        chooseFile.setEnabled(true);
        mode.setEnabled(true);
        cancel.setVisible(false);
        const bool missingOmr = result.getErrorMessage() == TRANS("Install Audiveris for local PDF recognition, then retry this PDF.");
        installOmr.setVisible(missingOmr);
        retryPdf.setVisible(missingOmr);
        resized();
        if (result.failed() || !parsed) {
            summary.setText(juce::translate(result.getErrorMessage()), juce::dontSendNotification);
            return;
        }
        project = std::move(parsed);
        heading.setText(project->title, juce::dontSendNotification);
        summary.setText(info + "\n" + TRANS("Drop a part on a matching track or elsewhere in the arrangement to add a track. Drop chords on the chord lane."),
                        juce::dontSendNotification);
        auto add = [this](int index, const juce::String& name) {
            auto row = std::make_unique<ImportResultRow>(name, *project, index);
            rows.addAndMakeVisible(*row);
            items.push_back(std::move(row));
        };
        for (const auto& lane : project->globalLanes)
            if (lane.kind == TimelineLane::chord && !lane.regions.empty())
                add(-1, TRANS("Chord progression"));
        for (int i = 0; i < int(project->sourceTrackCount()); ++i)
            add(i, juce::String::fromUTF8(project->trackLanes[i].label));
        openSong.setEnabled(true);
        resized();
    }
    void choose() {
        chooser = std::make_unique<juce::FileChooser>(TRANS("Import audio, MIDI, PDF or MusicXML"), juce::File{}, scoreImportFileFilter());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<ImportPanel>(this)](const auto& fc) {
                const auto source = fc.getResult();
                if (!safe || source == juce::File{}) return;
                safe->analyse(source);
            });
    }
    void analyse(juce::File source) {
        lastFile = source;
        installOmr.setVisible(false);
        retryPdf.setVisible(false);
        items.clear();
        project.reset();
        chooseFile.setEnabled(false);
        mode.setEnabled(false);
        cancel.setVisible(true);
        openSong.setEnabled(false);
        heading.setText(source.getFileName(), juce::dontSendNotification);
        summary.setText(TRANS("Analysing source..."), juce::dontSendNotification);
        resized();
        const int index = mode.getSelectedId() - 2;
        AnalysisConnection ai = index >= 0 && index < int(providers.size()) ? providers[size_t(index)] : nullptr;
        const int id = ++analysisId;
        worker.addJob([safe = juce::Component::SafePointer<ImportPanel>(this), source, ai, id] {
            std::unique_ptr<OstfpPreview> score;
            juce::String info;
            auto result = analyseScoreImport(source, score, info, ai);
            juce::MessageManager::callAsync([safe, result, info, id, score = std::move(score)]() mutable {
                if (safe && safe->analysisId == id) safe->showResult(std::move(score), result, info);
            });
        });
    }
    juce::File lastFile;
    juce::TextButton installOmr{TRANS("Get Audiveris")}, retryPdf{TRANS("Retry PDF")};
    int analysisId = 0;
    std::vector<AnalysisConnection> providers;
    juce::ComboBox mode;
    juce::TextButton cancel{TRANS("Cancel")};
    juce::Label disclosure;
    OpenImportedSong open;
    juce::TextButton chooseFile{TRANS("Import audio, MIDI, PDF or MusicXML")}, openSong{TRANS("Open score here as a new unsaved song")};
    juce::Label summary, heading;
    juce::Component rows;
    juce::Viewport viewport;
    std::unique_ptr<OstfpPreview> project;
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
        setResizable(true, true);
        setResizeLimits(520, 340, 2400, 1500);
        centreWithSize(820, 494);
    }
    void closeButtonPressed() override { setVisible(false); }
};
} // namespace
std::unique_ptr<juce::DocumentWindow> makeMediaImportWindow(OpenImportedSong open) {
    return std::make_unique<ImportWindow>(std::move(open));
}

void setImportConnections(juce::DocumentWindow& window, std::vector<AnalysisConnection> providers) {
    if (auto* panel = dynamic_cast<ImportPanel*>(window.getContentComponent())) panel->connections(std::move(providers));
}
