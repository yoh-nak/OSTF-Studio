#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ostfstudio {
class LinkSession {
public:
    explicit LinkSession(double bpm);
    ~LinkSession();
    struct State { double tempo, beat; bool playing; std::size_t peers; };
    State state(double quantum) const;
    struct Pulse { double tempo, beat; };
    Pulse audioPulse(std::int64_t micros, double quantum) const;
    void setTempo(double bpm);
    void stop();
    struct Launch { std::int64_t micros; double elapsedBeats, gridBeat; };
    Launch followStart(double quantum) const;
    std::int64_t startAt(double songBeat, double quantum, bool announce);
    std::int64_t micros() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
