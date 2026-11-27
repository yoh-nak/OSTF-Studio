#include "ui/MainComponent.h"

namespace ae = ostfstudio::audioexport;
namespace {
using Tick = juce::int64;
struct Segment {
  Tick tick, endTick, endFrame;
};
class RenderJob final : public juce::ThreadWithProgressWindow {
public:
  explicit RenderJob(juce::Component *parent)
      : ThreadWithProgressWindow(TRANS("Rendering audio"), true, true, -1,
                                 TRANS("Cancel"), parent) {}
  std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins;
  PreviewTransport transport;
  ae::Settings settings;
  juce::File file;
  juce::String trackName;
  std::vector<Segment> segments;
  std::vector<ostfstudio::ProjectAudio> clips;
  Tick frames = 0, position = 0;
  juce::Result result = juce::Result::ok();
  std::function<void(RenderJob &)> completed;
  bool read(juce::AudioBuffer<float> &block, int size) {
    if (threadShouldExit())
      return false;
    transport.renderBlock(block.getArrayOfWritePointers(), 2, size);
    position += size;
    setProgress(double(position) / frames);
    return true;
  }
  void run() override {
    if (segments.empty()) {
      result = ae::encode(
          file, settings, frames, [this](auto &b, int n) { return read(b, n); },
          [this] { return threadShouldExit(); });
      return;
    }
    juce::AudioBuffer<float> block(2, ae::blockSize);
    juce::WavAudioFormat wav;
    for (auto part : segments) {
      auto &clip = clips.emplace_back();
      clip.path = juce::File::createLegalFileName(trackName) + "-" +
                  juce::String(int(clips.size())) + ".wav";
      clip.startTick = part.tick;
      clip.durationTicks = part.endTick - part.tick;
      std::unique_ptr<juce::OutputStream> out =
          std::make_unique<juce::MemoryOutputStream>(clip.embedded.writable(),
                                                     false);
      auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}
                                                 .withNumChannels(2)
                                                 .withSampleRate(settings.rate)
                                                 .withBitsPerSample(24));
      if (!writer) {
        result = juce::Result::fail("Could not open the audio encoder.");
        return;
      }
      while (position < part.endFrame) {
        const int count =
            int(std::min<Tick>(ae::blockSize, part.endFrame - position));
        if (!read(block, count)) {
          result = juce::Result::fail("Cancelled");
          return;
        }
        if (!writer->writeFromAudioSampleBuffer(block, 0, count)) {
          result = juce::Result::fail("Could not write audio data.");
          return;
        }
      }
    }
  }
  void threadComplete(bool cancelled) override {
    transport.stop();
    plugins.clear();
    if (cancelled && (position != frames || !segments.empty()))
      result = juce::Result::fail("Cancelled");
    completed(*this);
    clips.clear();
  }
};
} // namespace
bool MainComponent::canBounce(size_t track) const {
  return canEditTracks() && !recordTrack_ && !rendering_ &&
         track < project_->sourceTrackCount() &&
         project_->sourceTrackCount() < project_->maxTracks &&
         !project_->importedProduction;
}
void MainComponent::chooseAudioExport() {
  if (!canEditTracks() || rendering_ || recordTrack_)
    return;
  auto *dialog = new juce::AlertWindow(TRANS("Export mix as audio..."), {},
                                       juce::MessageBoxIconType::NoIcon, this);
  auto formats = ae::availableFormats();
  juce::StringArray names;
  for (auto f : formats)
    names.add(ae::name(f));
  dialog->addComboBox("format", names, TRANS("File format"));
  dialog->addComboBox("rate", {"48000 Hz"}, TRANS("Sample rate"));
  dialog->addComboBox("quality", {"24-bit PCM"},
                      TRANS("Bit depth / encoding rate"));
  dialog->addComboBox("tail", {"0 s", "2 s", "5 s", "10 s", "30 s"},
                      TRANS("Effect tail after song end"));
  dialog->getComboBoxComponent("tail")->setSelectedItemIndex(1);
  auto *format = dialog->getComboBoxComponent("format");
  format->onChange = [dialog, formats, format] {
    auto f = formats[format->getSelectedItemIndex()];
    auto *r = dialog->getComboBoxComponent("rate");
    const int previous = r->getSelectedId();
    r->clear(juce::dontSendNotification);
    for (auto hz : ae::rates(f))
      r->addItem(juce::String(hz) + " Hz", hz);
    r->setSelectedId(ae::rates(f).contains(previous) ? previous : 48000,
                     juce::dontSendNotification);
    auto *q = dialog->getComboBoxComponent("quality");
    q->clear(juce::dontSendNotification);
    q->addItemList(ae::options(f), 1);
    q->setSelectedItemIndex(ae::defaultQuality(f), juce::dontSendNotification);
  };
  format->onChange();
  dialog->addButton(TRANS("Export"), 1,
                    juce::KeyPress(juce::KeyPress::returnKey));
  dialog->addButton(TRANS("Cancel"), 0);
  auto safe = juce::Component::SafePointer<MainComponent>(this);
  dialog->enterModalState(
      true,
      juce::ModalCallbackFunction::create([dialog, safe, formats,
                                           generation =
                                               projectGeneration_](int ok) {
        if (!ok || !safe || generation != safe->projectGeneration_)
          return;
        ae::Settings s{
            formats[dialog->getComboBoxComponent("format")
                        ->getSelectedItemIndex()],
            dialog->getComboBoxComponent("rate")->getSelectedId(),
            dialog->getComboBoxComponent("quality")->getSelectedItemIndex()};
        const int tail =
            dialog->getComboBoxComponent("tail")->getText().getIntValue();
        const auto ext = juce::String(ae::extension(s.format));
        auto title = juce::File::createLegalFileName(safe->project_->title);
        if (title.isEmpty())
          title = "Mix";
        auto start =
            juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile(title + "." + ext);
        safe->chooser_ = std::make_unique<juce::FileChooser>(
            TRANS("Export mix as audio..."), start, "*." + ext);
        safe->chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, generation, s, tail, ext](const juce::FileChooser &fc) {
              if (!safe || generation != safe->projectGeneration_ ||
                  fc.getResult() == juce::File{})
                return;
              auto chosen = fc.getResult();
              // Keep overwrite consent tied to the exact destination the
              // chooser displayed.
              if (!chosen.hasFileExtension(ext)) {
                auto corrected = chosen.withFileExtension(ext);
                if (corrected.exists()) {
                  safe->statusLabel_.setText(
                      TRANS("Choose the exact output filename and extension."),
                      juce::dontSendNotification);
                  return;
                }
                chosen = corrected;
              }
              safe->renderAudio({}, s, tail, chosen);
            });
      }),
      true);
}
void MainComponent::renderAudio(std::optional<size_t> track,
                                ae::Settings settings, int tail,
                                const juce::File &target) {
  if (!canEditTracks() || rendering_ || recordTrack_ ||
      (track && !canBounce(*track)))
    return;
  if (!track)
    for (const auto &clip : project_->audio)
      if (clip.localFile == target) {
        statusLabel_.setText(
            TRANS("Choose a different file. This WAV is used by the project."),
            juce::dontSendNotification);
        return;
      }
  auto job = std::make_unique<RenderJob>(this);
  job->settings = settings;
  job->file = target;
  juce::String error;
  auto report = [this](juce::String message) {
    statusLabel_.setText(message, juce::dontSendNotification);
  };
  auto clone =
      [&](juce::AudioPluginInstance &src) -> juce::AudioPluginInstance * {
    juce::String reason;
    auto copy = pluginFormats_.createPluginInstance(
        src.getPluginDescription(), settings.rate, ae::blockSize, reason);
    if (!copy) {
      error = src.getName() + "  " + reason;
      return nullptr;
    }
    juce::MemoryBlock state;
    {
      const juce::ScopedLock lock(src.getCallbackLock());
      src.getStateInformation(state);
    }
    copy->setNonRealtime(true);
    if (state.getSize())
      copy->setStateInformation(state.getData(), int(state.getSize()));
    return job->plugins.emplace_back(std::move(copy)).get();
  };
  std::vector<PreviewTransport::TrackSetup> routes(trackControls_.size());
  PreviewTransport::Inserts inserts{};
  const auto dest = [&](size_t i) {
    return trackControls_[i]->outputTrack.value_or(i);
  };
  const auto owner = track ? dest(*track) : 0;
  if (track && owner >= routes.size()) {
    report(TRANS("Choose a local instrument before bouncing."));
    return;
  }
  std::set<size_t> keys;
  if (track)
    for (const auto &fx : trackControls_[owner]->effects.slots) {
      int key = project_->trackIds.indexOf(fx.keyTrackId);
      if (key >= 0 && fx.effect && !fx.power.bypassed->load())
        keys.insert(dest(size_t(key)));
    }
  auto chain = [&](const EffectChain &slots, size_t index) {
    for (size_t i = 0; i < slots.slots.size() && error.isEmpty(); ++i) {
      const auto &fx = slots.slots[i];
      if (fx.power.bypassed->load())
        continue;
      if (!fx.effect) {
        if (fx.id.isNotEmpty())
          error = TRANS("Plug-in unavailable") + "  " + fx.id;
        continue;
      }
      auto *copy = clone(*fx.effect);
      inserts[index][i] = copy;
      const int key = project_->trackIds.indexOf(fx.keyTrackId);
      if (copy && key >= 0)
        inserts.keyTracks[copy] = dest(size_t(key));
    }
  };
  if (!track)
    for (size_t i = 0; i < outputControls_.size(); ++i)
      chain(outputControls_[i].effects, i);
  for (size_t i = 0; i < routes.size() && error.isEmpty(); ++i) {
    const auto &ctl = *trackControls_[i];
    const auto destination = dest(i);
    if (destination >= routes.size()) {
      error = TRANS("Missing instrument destination.");
      break;
    }
    auto &route = routes[i];
    route.outputTrack = ctl.outputTrack;
    route.midiChannel = ctl.midiChannel;
    route.playSource = !track || i == *track || keys.contains(destination);
    const bool renderStrip = !track || i == owner || keys.contains(i);
    if (route.playSource && midiOutputs_.contains(project_->trackIds[int(i)])) {
      if (track || mixer_.isAudible(i)) {
        error = TRANS("Record the external MIDI instrument to audio first.") +
                "  " + juce::String::fromUTF8(project_->trackLanes[i].label);
        break;
      }
      route.playSource = false;
    }
    if (renderStrip && !ctl.outputTrack &&
        !ctl.instrumentPower.bypassed->load()) {
      if (ctl.instance && ctl.playableInstrument)
        route.instrument = clone(*ctl.instance);
      else if ((ctl.pluginPath != juce::File{} || ctl.inactiveInstrument) &&
               (track || mixer_.isAudible(i)))
        error =
            TRANS("Plug-in unavailable") + "  " + ctl.plugin.getButtonText();
    }
    if (track && i == *track &&
        project_->trackKinds[i] != ostfstudio::TrackKind::audio &&
        !trackControls_[owner]->playableInstrument) {
      error = TRANS("Choose a local instrument before bouncing.");
    }
    if (!track || i == owner)
      chain(ctl.effects, PreviewTransport::outputCount + i);
    job->transport.setChannelMix(
        i, track ? ostfstudio::ChannelMix{} : mixer_.channel(i),
        track ? i == owner : mixer_.isAudible(i), 0);
  }
  if (error.isNotEmpty()) {
    report(error);
    return;
  }
  job->transport.setOutputMix(track ? ostfstudio::MixerRouting{}
                                    : mixer_.routing());
  job->transport.setOutputPairs(0, -1);
  auto prepared = job->transport.preparePlayback(
      *project_, routes, inserts, settings.rate, ae::blockSize,
      track ? 0 : auditionShift_, track ? 1.0 : auditionRate_);
  if (prepared.failed()) {
    report(prepared.getErrorMessage());
    return;
  }
  job->frames = job->transport.setRenderTail(tail);
  if (track) {
    job->trackName =
        juce::String::fromUTF8(project_->trackLanes[*track].label) +
        " - Bounce";
    // Split at score ticks so each embedded 24-bit WAV fits the existing OSTFP
    // asset limit.
    const auto end = project_->playbackEndTick();
    constexpr Tick maxFrames = (16 * 1024 * 1024 - 8192) / 6;
    Tick frame = 0, tick = 0;
    while (frame < job->frames) {
      const bool last = job->frames - frame <= maxFrames;
      const auto next = last ? end
                             : Tick(std::floor(project_->clock.tickAt(
                                   double(frame + maxFrames) / settings.rate)));
      const auto boundary =
          last ? job->frames
               : Tick(std::llround(project_->clock.secondsAt(next) *
                                   settings.rate));
      if (next <= tick || boundary <= frame || boundary - frame > maxFrames) {
        report(TRANS("Could not split the bounce at a score tick."));
        return;
      }
      job->segments.push_back({tick, next, boundary});
      tick = next;
      frame = boundary;
    }
    std::set<juce::String> media;
    Tick bytes = job->frames * 6 + 8192 * job->segments.size();
    for (const auto &a : project_->audio)
      if (media.insert(a.mediaKey()).second)
        bytes += a.embedded.isEmpty() ? a.localFile.getSize()
                                      : Tick(a.embedded.getSize());
    if (bytes > 68 * 1024 * 1024 ||
        project_->audio.size() + job->segments.size() >
            project_->maxAudioRegions) {
      report(TRANS("Bounce exceeds this project's audio capacity."));
      return;
    }
  }
  job->completed = [safe = juce::Component::SafePointer<MainComponent>(this),
                    generation = projectGeneration_, track](RenderJob &done) {
    if (!safe)
      return;
    safe->rendering_ = false;
    // Release the offline buffers after ThreadWithProgressWindow returns
    // from its completion callback on the message thread.
    juce::MessageManager::callAsync([safe, finished = &done] {
      if (safe && !safe->rendering_ && safe->renderJob_.get() == finished)
        safe->renderJob_.reset();
    });
    if (done.result.failed()) {
      safe->statusLabel_.setText(juce::translate(done.result.getErrorMessage()),
                                 juce::dontSendNotification);
      return;
    }
    if (track) {
      if (generation != safe->projectGeneration_ || !safe->canBounce(*track))
        return;
      Bounce bounce{done.trackName, std::move(done.clips)};
      const auto count = safe->project_->sourceTrackCount();
      safe->editTracks(ostfstudio::TrackKind::audio, {}, {},
                       ostfstudio::MixerTrackGroup::pitched, nullptr, 0, {},
                       &bounce);
      if (safe->project_->sourceTrackCount() == count + 1) {
        safe->markSessionEdited();
        safe->statusLabel_.setText(
            TRANS("Bounce created. Unmute the new track to hear it."),
            juce::dontSendNotification);
      }
    } else
      safe->statusLabel_.setText(TRANS("Exported audio") + "  " +
                                     done.file.getFullPathName(),
                                 juce::dontSendNotification);
  };
  rendering_ = true;
  renderJob_ = std::move(job);
  renderJob_->launchThread();
}
