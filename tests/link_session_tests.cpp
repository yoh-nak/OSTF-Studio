#include "ui/LinkSession.h"
#include <chrono>
#include <cmath>
#include <thread>
#include <iostream>
#include <string_view>
int main(int argc, char** argv) {
    ostfstudio::LinkSession studio(120);
    if (argc == 2 && std::string_view(argv[1]) == "--observe") {
        for (int i = 0; i < 300; ++i) {
            const auto state = studio.state(4);
            if (state.peers) {
                std::cout << "External Link peers: " << state.peers << ", tempo: " << state.tempo
                          << ", playing: " << state.playing << "\n";
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return 8;
    }
    studio.setTempo(108);
    auto before = studio.micros();
    auto start = studio.startAt(2, 4, true);
    if (start < before || !studio.state(4).playing || std::abs(studio.state(4).tempo - 108) > .01) return 1;
    studio.stop();
    if (studio.state(4).playing) return 2;
    if (argc == 1) return 0;
    ostfstudio::LinkSession peer(130);
    auto wait = [](auto check) {
        for (int i = 0; i < 120; ++i) {
            if (check()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    };
    if (!wait([&] { return studio.state(4).peers && peer.state(4).peers; })) return 3;
    peer.setTempo(117);
    if (!wait([&] { return std::abs(studio.state(4).tempo - 117) < .01; })) return 4;
    studio.startAt(0, 4, true);
    if (!wait([&] { return peer.state(4).playing; })) return 5;
    if (std::abs(std::remainder(studio.state(4).beat - peer.state(4).beat, 4.0)) > .03) return 6;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto beforeFollow = peer.micros();
    const auto resumed = peer.followStart(4);
    if (resumed.micros < beforeFollow || resumed.elapsedBeats < 0
        || (resumed.elapsedBeats > 0 && resumed.micros - beforeFollow > 100000)) return 9;
    peer.stop();
    if (!wait([&] { return !studio.state(4).playing; })) return 7;
    std::cout << "Two Link peers verified tempo, beat phase, start and stop.\n";
}
