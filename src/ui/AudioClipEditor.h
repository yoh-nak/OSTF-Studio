#pragma once
#include "ui/TimelinePan.h"
#include "ui/AudioClipWaveforms.h"
#include "ui/AudioPitchAnalysis.h"
#include "ui/ArrangementRuler.h"

// The ruler, waveform and warp handles share the visible arrangement time range.
class AudioClipEditor final : public juce::Component, public juce::SettableTooltipClient {
public:
    TimelinePan viewportPan;
    explicit AudioClipEditor(AudioClipWaveforms& source) : waves(source) {
        setComponentID("audio-clip-editor");
        loop.setComponentID("audio-loop");
        warp.setComponentID("audio-warp");
        setWantsKeyboardFocus(true);
        for (auto* button : {&loop, &warp, &link, &lower, &raise, &restore}) addAndMakeVisible(button);
        lower.setComponentID("audio-pitch-down"); raise.setComponentID("audio-pitch-up");
        lower.onClick = [this] { adjustPitch(-1); };
        raise.onClick = [this] { adjustPitch(1); };
        restore.onClick = [this] { adjustPitch(0); };
        link.setClickingTogglesState(true);
        link.setTooltip(TRANS("Apply warp changes to aligned clips on other tracks with the same length and warp map"));
        addAndMakeVisible(beats);
        addAndMakeVisible(recordRuler);
        addAndMakeVisible(channels);
        channels.addItem(TRANS("Mono input"), 1);
        channels.addItem(TRANS("Stereo input"), 2);
        channels.setSelectedId(1, juce::dontSendNotification);
        loop.setClickingTogglesState(true);
        warp.setClickingTogglesState(true);
        beats.setEditableText(true);
        for (int value : {1, 2, 3, 4, 6, 8, 12, 16, 32}) beats.addItem(juce::String(value), value);
        beats.setTooltip(TRANS("Length of one cycle in quarter notes"));
        loop.setTooltip(TRANS("Enable loop, then drag the clip's right edge in the arrangement"));
        warp.setTooltip(TRANS("Fit audio to beats and preserve pitch. Previously saved rate-based warps keep their playback mode."));
        setTooltip(TRANS("Double-click the waveform to add a warp marker. Drag markers sideways and notes vertically."));
        loop.onClick = [this] { auto edit = timing; edit.loop = loop.getToggleState(); commit(edit); };
        warp.onClick = [this] {
            auto edit = timing;
            edit.cycleTicks = warp.getToggleState() ? std::max<juce::int64>(1, std::llround(nativeLength)) : 0;
            edit.preservePitch = edit.cycleTicks>0;
            edit.markers.clear();
            commit(edit);
        };
        beats.onChange = [this] {
            const auto value = beats.getText().trim().getDoubleValue();
            if (timing.cycleTicks && std::isfinite(value) && value > 0 && value <= 1024) {
                auto edit = timing;
                edit.cycleTicks = std::max<juce::int64>(1, std::llround(value * ppq));
                commit(edit);
            } else sync();
        };
    }
    ~AudioClipEditor() override { if (cancelAnalysis) cancelAnalysis->store(true); }
    bool linkWarp() const { return link.getToggleState(); }
    std::function<const ostfstudio::ProjectAudio*()> sourceClip;
    std::function<const ostfstudio::TempoMap&()> clock;
    std::function<void(const ostfstudio::ClipTiming&)> onEdit;
    std::function<void()> selectWave;
    ArrangementRuler recordRuler;
    juce::ComboBox channels;
    void setTimelineColumns(int left, int right) {
        timelineLeft = left;
        timelineRight = right;
        resized();
    }
    void bind(juce::int64 resolution, juce::int64 barLength, int first, bool editable,
              std::shared_ptr<const ostfstudio::ImportedProduction> production = {}) {
        if (pitchProduction.owner_before(production) || production.owner_before(pitchProduction)) {
            pitchProduction = production;
            pitchModel = ostfstudio::AudioModelPitches(production ? juce::JSON::parse(production->productionBytes.toString()) : juce::var{});
            guideKey.clear();
        }
        setPitchLabel({});
        ticksPerBar = std::max<juce::int64>(1, barLength);
        firstBar = first;
        ppq = std::max<juce::int64>(1, resolution);
        auto* clip = sourceClip();
        drag = -1;
        pitchDrag=-1;
        timing = clip ? clip->timing : ostfstudio::ClipTiming{};
        nativeLength = clip ? waves.nativeTicks(*clip, clock()) : 0;
        const bool available = editable && clip && nativeLength > 0;
        updateGuide(clip);
        link.setEnabled(available);
        loop.setEnabled(available); warp.setEnabled(available);
        resized();
        channels.setEnabled(editable);
        sync();
    }
    void resized() override {
        const auto left = std::clamp(timelineLeft, 0, getWidth());
        auto tools = getLocalBounds().withWidth(left).reduced(8, 3);
        const int h = std::clamp(tools.getHeight() / 6, 1, 29);
        auto row = tools.removeFromTop(h);
        channels.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 1));
        loop.setBounds(row.reduced(2, 1));
        row = tools.removeFromTop(h);
        warp.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 1));
        beatCaption = row.removeFromRight(std::min(40, row.getWidth() / 3));
        beats.setBounds(row.reduced(2, 1));
        link.setBounds(tools.removeFromTop(h).reduced(2,1));
        pitchCaption = tools.removeFromTop(h);
        row = tools.removeFromTop(h);
        lower.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2,1));
        raise.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2,1));
        restore.setBounds(row.reduced(2,1));
        warpHint = tools.removeFromTop(40);
        recordRuler.setBounds(left, 0, std::max(0, std::min(timelineRight, getWidth()) - left), std::min(23, getHeight()));
    }
    void paint(juce::Graphics& g) override {
        noteBoxes.clear();
        const auto area = canvas();
        g.setColour(juce::Colour(0xffd2e5ee));
        g.drawText(TRANS("beats"), beatCaption, juce::Justification::centredLeft);
        const bool hasNote=juce::isPositiveAndBelow(selectedNote,int(guide.size()));
        juce::String label=TRANS("Pitch") + "  /  ";
        if (hasNote) {
            const auto& note=guide[size_t(selectedNote)];
            label+=juce::MidiMessage::getMidiNoteName(note.pitch+offset(note),true,true,4);
        } else label+=TRANS("Select a note below the waveform");
        g.drawFittedText(label,pitchCaption,juce::Justification::centredLeft,2);
        g.drawFittedText(TRANS("Double-click the waveform to add a warp marker. Drag markers sideways and notes vertically."),warpHint,juce::Justification::centredLeft,2);
        if (area.isEmpty() || !recordRuler.visibleBars) return;
        g.setColour(juce::Colour(0xff1b303d));
        g.fillRect(area);
        const auto visible = recordRuler.visibleBars();
        if (visible.isEmpty()) return;
        const double step = std::max(1., std::ceil(8 * visible.getLength() / area.getWidth()));
        g.setColour(juce::Colour(0xff365467));
        for (double bar = std::ceil(visible.getStart() / step) * step; bar < visible.getEnd(); bar += step)
            g.drawVerticalLine(xAtTick((bar - firstBar) * ticksPerBar), float(area.getY()), float(area.getBottom()));
        auto* clip = sourceClip();
        if (!clip || nativeLength <= 0) {
            g.setColour(juce::Colour(0xffd2e5ee));
            g.drawFittedText(clip ? TRANS("Missing WAV") : TRANS("Drop a WAV onto this audio track"),
                             area, juce::Justification::centred, 2);
            return;
        }
        const double start = std::max(double(clip->startTick), tickAtX(area.getX()));
        const double clipEnd = waves.endTick(*clip, clock());
        const double end = std::min(clipEnd, tickAtX(area.getRight()));
        if (end <= start) return;
        auto body = area.withLeft(xAtTick(start)).withRight(xAtTick(end));
        waves.draw(g, body.withBottom(pitchArea().getY()).reduced(0, 3), *clip, clock(), start, end, &timing);
        drawNotes(g,*clip,start,end);
        if (timing.cycleTicks <= 0 || timing.markers.empty()) return;
        const double period = timing.cycleTicks;
        const double cyclePixels = period / ticksPerBar / visible.getLength() * area.getWidth();
        const auto base = clip->sourceTick() - timing.phaseOffset * period;
        if (cyclePixels < 2. || (!timing.loop && start >= base + period)) return;
        for (double origin = base + std::max(0., std::floor((start - base) / period)) * period;
             origin < end; origin += period) {
            for (const auto marker : timing.markers) {
                const double tick = origin + marker.position * period;
                if (tick < std::max(start, double(clip->sourceTick())) || tick >= end) continue;
                const int x = xAtTick(tick);
                g.setColour(juce::Colour(0xffffbc63));
                g.drawVerticalLine(x, float(area.getY()), float(pitchArea().getY()));
                g.fillRect(x - 3, area.getY(), 7, 9);
            }
            if (!timing.loop) break;
        }
    }
    void mouseMove(const juce::MouseEvent& event) override {
        for (const auto& box:noteBoxes) if (box.bounds.contains(event.getPosition())) {
            const auto& n=guide[size_t(box.index)];
            const int steps=offset(n);
            auto label=juce::MidiMessage::getMidiNoteName(n.pitch+steps,true,true,4);
            if (n.overlap) label+="  "+TRANS("Overlapping pitches");
            else if (steps) label+="  ("+juce::String(steps>0?"+":"")+juce::String(steps)+" st)";
            setPitchLabel(label); return;
        }
        juce::StringArray labels;
        if (!pitchProduction.expired() && overClip(event.getPosition())) {
            const auto& clip=*sourceClip();
            const auto tick=tickAtX(event.x);
            if (clip.frameCount>0 && clip.frameCount<=std::numeric_limits<int>::max()) {
                const auto whole=static_cast<juce::int64>(std::floor(tick));
                const auto seconds=clock().secondsAt(whole)+(tick-whole)*(clock().secondsAt(whole+1)-clock().secondsAt(whole));
                const auto frame=timing.frameAt((seconds-clock().secondsAt(clip.sourceTick()))*clip.sampleRate,
                                                tick-clip.sourceTick(),int(clip.frameCount));
                for (double hz : pitchModel.frequenciesAt(clip,clip.startFrame+static_cast<juce::int64>(std::floor(frame)))) {
                    const double midi=69.+12.*std::log2(hz/440.);
                    auto label=juce::String(hz,1)+" Hz";
                    if (midi>=0 && midi<=127)
                        label=juce::MidiMessage::getMidiNoteName(juce::roundToInt(midi),true,true,4)+"  "+label;
                    labels.add(label);
                }
            }
        }
        setPitchLabel(labels.joinIntoString("  /  "));
    }
    juce::String getTooltip() override {
        return pitchLabel.isEmpty() ? juce::SettableTooltipClient::getTooltip() : pitchLabel;
    }
    void mouseExit(const juce::MouseEvent&) override { setPitchLabel({}); }
    void paintOverChildren(juce::Graphics& g) override {
        if (pitchLabel.isEmpty()) return;
        auto area=canvas().reduced(4).removeFromTop(22);
        const int width=juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(),pitchLabel)+14;
        auto box=area.removeFromLeft(std::min(area.getWidth(),width));
        g.setColour(juce::Colour(0xff13242d));
        g.fillRoundedRectangle(box.toFloat(),3.f);
        g.setColour(juce::Colours::white);
        g.drawText(pitchLabel,box.reduced(5,0),juce::Justification::centredLeft,true);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (TimelinePan::requested(e.mods)) return;
        if (!warp.isEnabled() || !overClip(e.getPosition()) || pitchArea().contains(e.getPosition())) return;
        auto edit = timing;
        if (!edit.cycleTicks) {
            edit.cycleTicks=std::max<juce::int64>(1,std::llround(nativeLength));
            edit.preservePitch=true;
        }
        const auto x = timing.cycleTicks ? phase(e.x, cycleStart(e.x)) : sourcePhaseAt(tickAtX(e.x),*sourceClip());
        const int index = timing.cycleTicks ? hit(e.x) : -1;
        if (index >= 0) edit.markers.erase(edit.markers.begin() + index);
        else if (edit.markers.size() < 128 && x > 0 && x < 1) {
            const auto it = std::lower_bound(edit.markers.begin(), edit.markers.end(), x,
                [](auto p, double position) { return p.position < position; });
            edit.markers.insert(it, {x, timing.sourcePhase(x)});
        }
        commit(edit);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (viewportPan.begin(*this, e, canvas())) return;
        drag = -1;
        if (!overClip(e.getPosition())) return;
        const auto box=std::find_if(noteBoxes.begin(),noteBoxes.end(),[&](const auto& n){return n.bounds.contains(e.getPosition());});
        const int index=box==noteBoxes.end() ? -1 : box->index;
        if (selectWave) selectWave();
        if (isShowing()) grabKeyboardFocus();
        selectedNote=-1;
        selectedMarker=-1;
        if (pitchArea().contains(e.getPosition())) {
            if (juce::isPositiveAndBelow(index,int(guide.size())) && warp.isEnabled() && !guide[size_t(index)].overlap) {
                selectedNote=index;
                pitchDrag=index; pitchInitial=offset(guide[size_t(index)]); pitchDelta=0;
            }
            updatePitchButtons(); repaint();
            return;
        }
        updatePitchButtons(); repaint();
        if (timing.cycleTicks > 0 && warp.isEnabled()) {
            dragCycle = cycleStart(e.x);
            drag = hit(e.x);
            selectedMarker=drag;
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (viewportPan.move(e)) return;
        if (pitchDrag>=0) {
            pitchDelta=std::clamp(juce::roundToInt(-e.getDistanceFromDragStartY()/rowHeight()),-24-pitchInitial,24-pitchInitial);
            repaint(pitchArea()); return;
        }
        if (drag < 0) return;
        const double low = drag ? timing.markers[size_t(drag - 1)].position : 0.;
        const double high = size_t(drag + 1) < timing.markers.size() ? timing.markers[size_t(drag + 1)].position : 1.;
        timing.markers[size_t(drag)].position = std::clamp(phase(e.x, dragCycle), std::nextafter(low, high), std::nextafter(high, low));
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (viewportPan.end(*this)) return;
        if (pitchDrag>=0) {
            auto n=guide[size_t(pitchDrag)];
            pitchDrag=-1;
            if (pitchDelta) {
                setOffset(n,pitchInitial+pitchDelta);
            }
            repaint(); return;
        }
        if (drag >= 0 && e.mouseWasDraggedSinceMouseDown()) commit(timing);
        drag = -1;
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (!warp.isEnabled() || key.getModifiers().isAnyModifierKeyDown()) return false;
        if (juce::isPositiveAndBelow(selectedNote,int(guide.size())) && (key==juce::KeyPress::upKey || key==juce::KeyPress::downKey)) {
            const auto n=guide[size_t(selectedNote)];
            setOffset(n,std::clamp(offset(n)+(key==juce::KeyPress::upKey ? 1 : -1),-24,24));
            return true;
        }
        if (juce::isPositiveAndBelow(selectedMarker,int(timing.markers.size())) && (key==juce::KeyPress::leftKey || key==juce::KeyPress::rightKey)) {
            auto edit=timing;
            edit.markers[size_t(selectedMarker)].position+=(key==juce::KeyPress::rightKey ? 1. : -1.)/edit.cycleTicks;
            commit(edit); return true;
        }
        return false;
    }
private:
    struct Note { double start,end; int pitch; bool overlap=false; };
    static bool sameRange(ostfstudio::PitchChange p,const Note& n) {
        return std::abs(p.start-n.start)<1.e-12 && std::abs(p.end-n.end)<1.e-12;
    }
    void updatePitchButtons() {
        const bool valid=warp.isEnabled() && juce::isPositiveAndBelow(selectedNote,int(guide.size())) && !guide[size_t(selectedNote)].overlap;
        const int shift=valid ? offset(guide[size_t(selectedNote)]) : 0;
        lower.setEnabled(valid && shift>-24); raise.setEnabled(valid && shift<24); restore.setEnabled(valid && shift!=0);
    }
    void adjustPitch(int delta) {
        if (!juce::isPositiveAndBelow(selectedNote,int(guide.size())) || !warp.isEnabled()) return;
        const auto n=guide[size_t(selectedNote)];
        if (!n.overlap) setOffset(n,delta ? std::clamp(offset(n)+delta,-24,24) : 0);
    }
    void setOffset(const Note& n,int steps) {
        auto edit=timing;
        std::erase_if(edit.pitches,[&](auto p){return sameRange(p,n);});
        if (steps) edit.pitches.push_back({n.start,n.end,steps});
        std::sort(edit.pitches.begin(),edit.pitches.end(),[](auto a,auto b){return a.start<b.start;});
        commit(edit);
    }
    struct NoteBox { juce::Rectangle<int> bounds; int index; };
    std::vector<Note> guide;
    std::vector<NoteBox> noteBoxes;
    int pitchDrag=-1,pitchInitial=0,pitchDelta=0,low=55,high=72;
    int selectedNote=-1,selectedMarker=-1;
    juce::String guideKey;
    std::shared_ptr<std::atomic_bool> cancelAnalysis;
    juce::ThreadPool analyser{1};
    bool analysing=false;
    void installGuide(std::vector<ostfstudio::AudioModelPitches::Observation> notes,juce::int64 frames) {
        guide.clear(); analysing=false;
        std::sort(notes.begin(),notes.end(),[](auto a,auto b){return a.start<b.start;});
        double furthest=0;
        for (size_t i=0;i<notes.size() && frames>0;++i) {
            const auto n=notes[i];
            const double start=double(n.start)/frames,end=double(n.start+n.frames)/frames;
            const int pitch=juce::roundToInt(69.+12.*std::log2(n.hz/440.));
            guide.push_back({start,end,pitch,start<furthest || (i+1<notes.size() && notes[i+1].start<n.start+n.frames)});
            furthest=std::max(furthest,end);
        }
        noteBoxes.clear(); fitNotes(); repaint();
    }
    int offset(const Note& n) const {
        auto it=std::lower_bound(timing.pitches.begin(),timing.pitches.end(),n.start-1.e-12,[](auto p,double s){return p.start<s;});
        return it!=timing.pitches.end() && sameRange(*it,n) ? it->semitones : 0;
    }
    void fitNotes() {
        if (guide.empty()) return;
        low=150; high=-24;
        for (const auto& n:guide) {const int p=n.pitch+offset(n); low=std::min(low,p); high=std::max(high,p);}
        low-=3; high+=3;
    }
    void updateGuide(const ostfstudio::ProjectAudio* clip) {
        const auto key=clip ? clip->mediaKey()+"|"+clip->regionId+"|"+clip->sha256+"|"+juce::String(clip->startFrame)+"|"+juce::String(waves.frameCount(*clip)) : juce::String{};
        if (key==guideKey) { fitNotes(); return; }
        if (cancelAnalysis) cancelAnalysis->store(true);
        guideKey=key; guide.clear(); analysing=false; noteBoxes.clear();
        selectedNote=-1; selectedMarker=-1;
        if (!clip || !waves.available(*clip)) return;
        auto notes=pitchModel.within(*clip);
        const auto frames=waves.frameCount(*clip);
        if (!notes.empty()) { installGuide(std::move(notes),frames); return; }
        analysing=true;
        cancelAnalysis=std::make_shared<std::atomic_bool>(false);
        analyser.addJob([self=juce::Component::SafePointer<AudioClipEditor>(this),cancel=cancelAnalysis,file=*clip,frames] {
            if (cancel->load()) return;
            auto found=ostfstudio::analysePitchGuide(file,*cancel);
            juce::MessageManager::callAsync([self,cancel,frames,found=std::move(found)]() mutable {
                if (!cancel->load() && self) self->installGuide(std::move(found),frames);
            });
        });
    }
    juce::Rectangle<int> pitchArea() const { const auto a=canvas(); return a.withTrimmedTop(a.getHeight()/2); }
    double rowHeight() const {return std::max(1.,double(pitchArea().getHeight())/(high-low+1));}
    double sourcePhaseAt(double tick,const ostfstudio::ProjectAudio& clip) const {
        const auto whole=juce::int64(std::floor(tick));
        const double seconds=clock().secondsAt(whole)+(tick-whole)*(clock().secondsAt(whole+1)-clock().secondsAt(whole));
        double p=timing.phaseOffset+(timing.cycleTicks ? (tick-clip.sourceTick())/timing.cycleTicks :
                    (seconds-clock().secondsAt(clip.sourceTick()))/waves.nativeSeconds(clip));
        return timing.sourcePhase(timing.loop ? p-std::floor(p) : std::clamp(p,0.,1.));
    }
    void drawNotes(juce::Graphics& g,const ostfstudio::ProjectAudio& clip,double start,double end) {
        const auto area=pitchArea();
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(area); g.setColour(juce::Colour(0xff192735)); g.fillRect(area);
        if (guide.empty()) {
            g.setColour(juce::Colours::lightgrey);
            g.drawText(analysing ? TRANS("Analysing pitch…") : TRANS("No stable note detected"),area.reduced(8),juce::Justification::centredLeft);
            return;
        }
        for (int p=low;p<=high;++p) {
            g.setColour(p%12==0 ? juce::Colour(0xff506478) : juce::Colour(0xff2d4050));
            g.drawHorizontalLine(area.getY()+juce::roundToInt((high-p)*rowHeight()),float(area.getX()),float(area.getRight()));
        }
        const double onset=clock().secondsAt(clip.sourceTick()), seconds=waves.nativeSeconds(clip);
        const auto tickAt=[&](double phase) {
            return timing.cycleTicks ? clip.sourceTick()+(phase-timing.phaseOffset)*timing.cycleTicks
                : clock().tickAt(onset+(phase-timing.phaseOffset)*seconds);
        };
        if (!timing.loop && timing.sourceLimitTicks) end=std::min(end,double(clip.sourceTick()+timing.sourceLimitTicks));
        const auto whole=juce::int64(std::floor(start));
        const double cycle=timing.cycleTicks ? (start-clip.sourceTick())/timing.cycleTicks :
            (clock().secondsAt(whole)-onset)/seconds;
        for (double k=timing.loop ? std::max(0.,std::floor(cycle+timing.phaseOffset)) : 0.;tickAt(k)<end;++k) {
            if (timing.loop && xAtTick(tickAt(k+1))-xAtTick(tickAt(k))<2) break;
            for (size_t i=0;i<guide.size();++i) {
                const auto& n=guide[i]; const double a=std::max({start,double(clip.sourceTick()),tickAt(k+timing.timelinePhase(n.start))});
                const double b=std::min(end,tickAt(k+timing.timelinePhase(n.end)));
                if (b<=a) continue;
                const int pitch=n.pitch+offset(n)+(pitchDrag==int(i)?pitchDelta:0);
                const auto box=juce::Rectangle<int>(xAtTick(a),area.getY()+juce::roundToInt((high-pitch)*rowHeight()),
                       std::max(1,xAtTick(b)-xAtTick(a)-1),std::max(2,int(rowHeight())-1)).getIntersection(area);
                g.setColour(n.overlap ? juce::Colour(0xff75818b) : juce::Colour(0xff80cfc1)); g.fillRect(box);
                if (selectedNote==int(i)) { g.setColour(juce::Colours::white); g.drawRect(box,1); }
                g.setColour(juce::Colour(0xff132830));
                if (box.getWidth()>24) g.drawFittedText(juce::MidiMessage::getMidiNoteName(pitch,true,true,4),box.reduced(2,0),juce::Justification::centredLeft,1);
                noteBoxes.push_back({box,int(i)});
            }
            if (!timing.loop) break;
        }
    }
    juce::Rectangle<int> canvas() const {
        return recordRuler.getBounds().withTop(recordRuler.getBottom()).withBottom(getHeight());
    }
    double tickAtX(int x) const {
        const auto bars = recordRuler.visibleBars();
        return (bars.getStart() - firstBar + (x - canvas().getX()) * bars.getLength() / std::max(1, canvas().getWidth())) * ticksPerBar;
    }
    int xAtTick(double tick) const {
        const auto bars = recordRuler.visibleBars();
        return canvas().getX() + juce::roundToInt((firstBar + tick / ticksPerBar - bars.getStart()) * canvas().getWidth() / bars.getLength());
    }
    bool overClip(juce::Point<int> point) const {
        auto* clip = sourceClip();
        if (!clip || !canvas().contains(point) || !recordRuler.visibleBars) return false;
        const auto tick = tickAtX(point.x);
        return tick >= clip->sourceTick() && tick < waves.endTick(*clip, clock())
            && (timing.loop || !timing.sourceLimitTicks || tick < clip->sourceTick() + timing.sourceLimitTicks)
            && (timing.loop || !timing.cycleTicks || tick < clip->sourceTick() + (1. - timing.phaseOffset) * timing.cycleTicks);
    }
    double cycleStart(int x) const {
        const double start = sourceClip()->sourceTick() - timing.phaseOffset * timing.cycleTicks;
        return timing.loop ? start + std::floor((tickAtX(x) - start) / timing.cycleTicks) * timing.cycleTicks : start;
    }
    double phase(int x, double origin) const { return (tickAtX(x) - origin) / timing.cycleTicks; }
    int hit(int x) const {
        const double origin = cycleStart(x);
        for (size_t i = 0; i < timing.markers.size(); ++i)
            if (std::abs(tickAtX(x) - (origin + timing.markers[i].position * timing.cycleTicks)) <= (tickAtX(x + 5) - tickAtX(x))) return int(i);
        return -1;
    }
    void commit(ostfstudio::ClipTiming edit) { if (edit.valid() && onEdit) onEdit(edit); }
    void sync() {
        updatePitchButtons();
        loop.setToggleState(timing.loop, juce::dontSendNotification);
        warp.setToggleState(timing.cycleTicks > 0, juce::dontSendNotification);
        beats.setEnabled(warp.isEnabled() && timing.cycleTicks > 0);
        beats.setText(juce::String((timing.cycleTicks ? double(timing.cycleTicks) : nativeLength) / ppq, 3).trimCharactersAtEnd("0").trimCharactersAtEnd("."), juce::dontSendNotification);
        repaint();
    }
    void setPitchLabel(const juce::String& text) {
        if (pitchLabel==text) return;
        pitchLabel=text;
        repaint(canvas().withHeight(26));
    }
    std::weak_ptr<const ostfstudio::ImportedProduction> pitchProduction;
    ostfstudio::AudioModelPitches pitchModel;
    juce::String pitchLabel;
    AudioClipWaveforms& waves;
    ostfstudio::ClipTiming timing;
    double nativeLength = 0;
    juce::int64 ppq = 1;
    int drag = -1, timelineLeft = 0, timelineRight = 0, firstBar = 1;
    juce::int64 ticksPerBar = 1;
    double dragCycle = 0;
    juce::Rectangle<int> beatCaption, pitchCaption, warpHint;
    juce::TextButton lower{TRANS("-1 semitone")}, raise{TRANS("+1 semitone")}, restore{TRANS("Reset pitch")};
    juce::TextButton loop{TRANS("Loop")}, warp{TRANS("Warp")}, link{TRANS("Link aligned stems")};
    juce::ComboBox beats;
};
