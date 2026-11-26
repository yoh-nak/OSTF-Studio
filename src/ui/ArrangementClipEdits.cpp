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

void MainComponent::clipCommand(ClipOp op, std::optional<ClipHit> hit, size_t destination, Tick tick) {
    if (!project_ || recordTrack_ || destination >= project_->sourceTrackCount() || (op != ClipOp::copy && chordReviewBlocksSwitch())) return;
    auto fail = [this](const juce::String& error) { statusLabel_.setText(juce::translate(error), juce::dontSendNotification); };
    if (hit) {
        auto current = clipAt(hit->track, hit->start);
        if (!current || current->id != hit->id) return;
        hit = current;
    }
    if (!hit && op != ClipOp::paste) return;
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
