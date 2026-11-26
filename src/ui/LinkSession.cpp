#include "ui/LinkSession.h"
// Link uses Asio's callback API. Avoid pulling in a second coroutine ABI alongside WinRT on older Windows SDKs.
#define ASIO_DISABLE_CO_AWAIT
#define ASIO_DISABLE_STD_COROUTINE
#include <ableton/Link.hpp>
#include <cmath>
#include <thread>

namespace ostfstudio {
struct LinkSession::Impl {
    explicit Impl(double bpm) : link(bpm) { link.enableStartStopSync(true); link.enable(true); }
    ableton::Link link;
    std::chrono::microseconds lastTransportCommit{-1};
    void commitTransport(ableton::Link::SessionState session) {
        // Link orders start/stop messages by their commit microsecond and drops
        // equal timestamps. These commands run on the application thread.
        while (link.clock().micros() <= lastTransportCommit) std::this_thread::yield();
        link.commitAppSessionState(session);
        lastTransportCommit = link.clock().micros();
    }
};
LinkSession::LinkSession(double bpm) : impl_(std::make_unique<Impl>(bpm)) {}
LinkSession::~LinkSession() = default;
std::int64_t LinkSession::micros() const { return impl_->link.clock().micros().count(); }
LinkSession::State LinkSession::state(double quantum) const {
    auto& link = impl_->link;
    auto session = link.captureAppSessionState();
    return {session.tempo(), session.beatAtTime(link.clock().micros(), quantum), session.isPlaying(), link.numPeers()};
}
LinkSession::Pulse LinkSession::audioPulse(std::int64_t micros, double quantum) const {
    const auto state = impl_->link.captureAudioSessionState();
    return {state.tempo(), state.beatAtTime(std::chrono::microseconds(micros), quantum)};
}
void LinkSession::setTempo(double bpm) {
    auto& link = impl_->link;
    auto session = link.captureAppSessionState();
    session.setTempo(bpm, link.clock().micros());
    link.commitAppSessionState(session);
}
void LinkSession::stop() {
    auto& link = impl_->link;
    auto session = link.captureAppSessionState();
    session.setIsPlaying(false, link.clock().micros());
    impl_->commitTransport(session);
}
LinkSession::Launch LinkSession::followStart(double quantum) const {
    const auto& link = impl_->link;
    const auto state = link.captureAppSessionState();
    const auto origin = state.timeForIsPlaying();
    const auto audible = std::max(origin, link.clock().micros() + std::chrono::milliseconds(45));
    const double beat = state.beatAtTime(audible, quantum);
    return {audible.count(), beat - state.beatAtTime(origin, quantum), beat};
}

std::int64_t LinkSession::startAt(double beat, double quantum, bool announce) {
    auto& link = impl_->link;
    auto session = link.captureAppSessionState();
    auto earliest = link.clock().micros() + std::chrono::milliseconds(45);
    if (!link.numPeers()) session.requestBeatAtTime(beat, earliest, quantum);
    auto nowBeat = session.beatAtTime(earliest, quantum);
    auto boundary = beat + quantum * std::ceil((nowBeat - beat - 1e-8) / quantum);
    auto when = session.timeAtBeat(boundary, quantum);
    if (announce) {
        session.setIsPlaying(true, when);
        impl_->commitTransport(session);
    } else link.commitAppSessionState(session);
    return when.count();
}
}
