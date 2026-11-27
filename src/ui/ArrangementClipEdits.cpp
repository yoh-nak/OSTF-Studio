#include "ui/MainComponent.h"
#include "ui/AudioRegionEdits.h"

using ostfstudio::ProjectAudio;
using Tick = juce::int64;

std::vector<MainComponent::ClipHit> MainComponent::clipsOn(size_t track) const {
    std::vector<ClipHit> list;
    if (!project_ || track >= project_->sourceTrackCount()) return list;
    for (const auto& clip : project_->audio) if (clip.track == track)
        list.push_back({track, clip.regionId, clip.startTick, std::llround(waveforms_.endTick(clip, project_->clock)), true});
    for (const auto& r : project_->trackLanes[track].regions) if (r.clipId && r.exactBars)
        list.push_back({track, juce::String(r.clipId), std::llround((r.exactBars->start - 1) * project_->ticksPerBar),
                        std::llround((r.exactBars->end - 1) * project_->ticksPerBar), false});
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return list;
}

std::optional<MainComponent::ClipHit> MainComponent::clipAt(size_t track, Tick tick) const {
    for (const auto& hit : clipsOn(track)) if (hit.start <= tick && tick < hit.end) return hit;
    return {};
}

void MainComponent::rememberAudioEdit(std::vector<ProjectAudio> before, juce::MemoryBlock beforeScore, const char* label) {
    rememberScoreEdit(beforeScore.isEmpty() ? project_->sourceScore : std::move(beforeScore), label);
    auto& revision = noteHistory_.back();
    revision.beforeAudio.emplace(std::move(before));
    revision.afterAudio.emplace(project_->audio);
    size_t bytes = 0;
    std::set<const void*> media;
    for (size_t i = noteHistory_.size(); i > 0; --i) {
        const auto& edit = noteHistory_[i - 1];
        for (auto* audio : {&edit.beforeAudio, &edit.afterAudio})
            if (*audio) for (const auto& region : **audio)
                if (media.insert(region.embedded.getData()).second) bytes += region.embedded.getSize();
        if (bytes > 48 * 1024 * 1024 && i < noteHistory_.size()) {
            noteHistory_.erase(noteHistory_.begin(), noteHistory_.begin() + i);
            break;
        }
    }
    noteHistoryCursor_ = noteHistory_.size();
}

void MainComponent::insertTimelineBars(Tick tick,int count) {
    if (!project_ || recordTrack_ || livePerformanceActive_ || chordReviewBlocksSwitch()) return;
    const auto at=tick-tick%project_->ticksPerBar, delta=count*project_->ticksPerBar;
    auto nextAudio=project_->audio;
    const auto fail=[this](const juce::String& reason){statusLabel_.setText(juce::translate(reason),juce::dontSendNotification);};
    for (size_t i=0;i<project_->audio.size();++i) {
        const auto& region=project_->audio[i];
        if(region.startTick>=at) { nextAudio[i].startTick+=delta; continue; }
        const auto end=std::llround(waveforms_.endTick(region,project_->clock));
        if(end<=at) continue;
        if (project_->importedProduction) { fail("Cannot split an audio region owned by an imported Production companion.");return; }
        const auto seconds=waveforms_.nativeSeconds(region);
        if(seconds<=0 || nextAudio.size()>=project_->maxAudioRegions) {fail(seconds<=0?"Missing WAV":"Too many audio regions.");return;}
        auto right=ostfstudio::rightSlice(region,at,end,project_->clock,seconds);
        right.startTick+=delta;
        nextAudio[i].durationTicks=at-region.startTick;
        nextAudio.push_back(std::move(right));
    }
    auto beforePan=project_->panAutomation;
    if (project_->importedProduction && !beforePan.empty()) {fail("Imported Production automation cannot be rewritten.");return;}
    const bool playing=transportActive_;
    if(playing) stopTransport();
    juce::MemoryBlock before;
    const auto result=project_->insertBars(at,count,before);
    if(result.failed()) {fail(result.getErrorMessage());if(playing) startTransport(false);return;}
    project_->audio.swap(nextAudio);
    for(auto& [_,env]:project_->panAutomation) for(auto& point:env.points) if(point.tick>=at) point.tick+=delta;
    rememberAudioEdit(std::move(nextAudio),std::move(before),"Insert bars");
    noteHistory_.back().beforePan=std::move(beforePan);
    noteHistory_.back().afterPan=project_->panAutomation;
    waveforms_.load(project_->audio);
    live_.bind(*project_,true);
    theory_.reset(); refreshTheory(); refreshScaleEditors();
    markSessionEdited(); finishNoteEdit(playing);
}

void MainComponent::clipCommand(ClipOp op, std::optional<ClipHit> hit, size_t destination, Tick tick) {
    if (!project_ || recordTrack_ || destination >= project_->sourceTrackCount() || (op != ClipOp::copy && chordReviewBlocksSwitch())) return;
    auto fail = [this](const juce::String& error) { statusLabel_.setText(juce::translate(error), juce::dontSendNotification); };
    if (hit) {
        auto current = clipAt(hit->track, hit->start);
        if (!current || current->id != hit->id) return;
        hit = current;
    }
    if (!hit && op != ClipOp::paste) return;
    if (op == ClipOp::move && hit && destination == hit->track) {
        const auto selected = bounceClipSelection(*hit);
        if (selected.size() > 1) {
            const auto first = std::min_element(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
            const auto delta = tick - first->start;
            if (delta == 0) return;
            std::set<std::pair<size_t, juce::String>> midiIds;
            std::set<juce::String> audioIds;
            Tick latestEnd = 0;
            for (const auto& clip : selected) {
                latestEnd = std::max(latestEnd, clip.end + delta);
                if (clip.audio) audioIds.insert(clip.id);
                else midiIds.emplace(clip.track, clip.id);
            }
            if (tick < 0) { fail("Invalid clip range."); return; }
            auto movedAudio = project_->audio;
            for (auto& region : movedAudio) if (audioIds.contains(region.regionId)) {
                const auto end = std::llround(waveforms_.endTick(region, project_->clock));
                const juce::Range<Tick> dest{region.startTick + delta, end + delta};
                for (const auto& other : clipsOn(region.track))
                    if (!audioIds.contains(other.id) && dest.intersects({other.start, other.end})) {
                        fail("The destination contains another clip.");
                        return;
                    }
                region.durationTicks = end - region.startTick;
                region.startTick = dest.getStart();
            }
            const bool playing = transportActive_;
            if (playing) stopTransport();
            juce::MemoryBlock before;
            const auto result = project_->moveArrangementClips(midiIds, delta, latestEnd, before);
            if (result.failed()) { fail(result.getErrorMessage()); if (playing) startTransport(false); return; }
            project_->audio.swap(movedAudio);
            rememberAudioEdit(std::move(movedAudio), std::move(before), "Move selected clips");
            waveforms_.load(project_->audio);
            theory_.reset();
            refreshTheory();
            markSessionEdited();
            selectEditorClip(1. + double(hit->start + delta) / project_->ticksPerBar, false);
            finishNoteEdit(playing);
            repaint();
            return;
        }
    }
    auto& audio = project_->audio;
    auto lookup = [&](const juce::String& id) {
        return std::find_if(audio.begin(), audio.end(), [&](const auto& region) { return region.regionId == id; });
    };
    if (op == ClipOp::copy) {
        copiedAudio_.reset();
        copiedMidi_ = {};
        if (hit->audio) {
            copiedAudio_ = *lookup(hit->id);
            copiedAudio_->durationTicks = hit->end - hit->start;
        } else copiedMidi_ = project_->copyMidiClip(hit->track, hit->id);
        return;
    }

    const bool isAudio = op == ClipOp::paste ? copiedAudio_.has_value() : hit->audio;
    const char* action = op == ClipOp::move ? "Move clip" : op == ClipOp::split ? "Split clip"
        : op == ClipOp::paste ? "Paste clip" : op == ClipOp::duplicate ? "Duplicate clip" : "Join clips";
    if (op == ClipOp::duplicate) tick = hit->end;
    if (op == ClipOp::split && (tick <= hit->start || tick >= hit->end)) { fail("Choose a point inside the clip."); return; }
    std::optional<ClipHit> next;
    if (op == ClipOp::join) {
        for (const auto& c : clipsOn(destination)) if (c.audio == isAudio && c.start >= hit->end) { next = c; break; }
        if (!next) { fail("No following clip on this track."); return; }
    }
    const bool playing = transportActive_;
    if (isAudio) {
        if (project_->trackKinds[destination] != ostfstudio::TrackKind::audio && (op == ClipOp::paste || !hit || destination != hit->track)) {
            fail("Choose a matching destination track."); return;
        }
        if ((op == ClipOp::split || op == ClipOp::paste || op == ClipOp::duplicate) && audio.size() >= project_->maxAudioRegions) {
            fail("Too many audio regions."); return;
        }
        auto source = hit && hit->audio ? lookup(hit->id) : audio.end();
        ProjectAudio update;
        Tick selection = tick;
        Tick end = 0;
        if (op == ClipOp::split) {
            const auto seconds = waveforms_.nativeSeconds(*source);
            if (seconds <= 0) { fail("Missing WAV"); return; }
            update = ostfstudio::rightSlice(*source, tick, hit->end, project_->clock, seconds);
        } else if (op == ClipOp::join) {
            auto r = ostfstudio::consolidateRegions(*source, *lookup(next->id), next->end, project_->clock, update);
            if (r.failed()) { fail(r.getErrorMessage()); return; }
            selection = hit->start;
        } else {
            update = op == ClipOp::paste ? *copiedAudio_ : *source;
            if (op != ClipOp::paste) update.durationTicks = hit->end - hit->start;
            if (tick < 0 || update.durationTicks <= 0 || tick > 0x0fffffff - update.durationTicks) { fail("Invalid clip range."); return; }
            end = tick + update.durationTicks;
            for (const auto& other : clipsOn(destination))
                if (!(op == ClipOp::move && other.id == update.regionId) && tick < other.end && end > other.start) {
                    fail("The destination contains another clip."); return;
                }
            if (op != ClipOp::move) update.regionId = "region-" + juce::Uuid().toString();
            update.startTick = tick;
            update.track = destination;
        }
        juce::MemoryBlock beforeScore;
        if (end > project_->playbackEndTick()) {
            std::unique_ptr<ostfstudio::OstfpPreview> extended;
            auto r = project_->extendedForImport(end, extended, beforeScore);
            if (r.wasOk()) r = project_->restoreNoteScore(extended->sourceScore);
            if (r.failed()) { fail(r.getErrorMessage()); return; }
            source = hit && hit->audio ? lookup(hit->id) : audio.end();
        }
        if (playing) stopTransport();
        auto before = audio;
        if (op == ClipOp::split) source->durationTicks = tick - source->startTick;
        selectedAudioId_ = update.regionId;
        if (op == ClipOp::join) { *source = std::move(update); audio.erase(lookup(next->id)); }
        else if (op == ClipOp::move) *source = std::move(update);
        else audio.push_back(std::move(update));
        rememberAudioEdit(std::move(before), std::move(beforeScore), action);
        markSessionEdited();
        waveforms_.load(audio);
        tick = selection;
    } else {
        if (op == ClipOp::paste && !copiedMidi_.data.isObject()) return;
        juce::MemoryBlock before;
        juce::Result r = juce::Result::ok();
        if (playing) stopTransport();
        if (op == ClipOp::split)
            r = project_->editMidiClip(destination, hit->id, ostfstudio::OstfpPreview::MidiClipEdit::split, tick, before);
        else {
            const auto from = next ? next : hit;
            const auto payload = op == ClipOp::paste ? copiedMidi_ : project_->copyMidiClip(from->track, from->id);
            const bool moving = next || op == ClipOp::move;
            r = project_->placeMidiClip(payload, destination, next ? next->start : tick, moving ? std::optional<size_t>(from->track) : std::nullopt,
                                       next ? hit->id : juce::String{}, before);
            if (next) tick = hit->start;
        }
        if (r.failed()) { fail(r.getErrorMessage()); if (playing) startTransport(false); return; }
        rememberScoreEdit(std::move(before), action);
        theory_.reset();
        refreshTheory();
    }
    selectTrack(destination);
    selectEditorClip(1. + double(tick) / project_->ticksPerBar, false);
    clipInsertionTick_ = tick;
    finishNoteEdit(playing);
    repaint();
}
