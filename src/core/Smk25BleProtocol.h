#pragma once
#include "core/Smk25Preset.h"
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ostfstudio::smkble {
using Bytes = std::vector<std::uint8_t>;
inline void appendLE(Bytes& out, unsigned n, int width) {
    for (int i = 0; i < width; ++i) out.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
}
inline unsigned little(std::span<const std::uint8_t> in) {
    unsigned n = 0;
    for (size_t i = 0; i < in.size(); ++i) n |= unsigned(in[i]) << (8 * i);
    return n;
}
inline Bytes frame(std::uint8_t command, const Bytes& payload) {
    Bytes out{0, 0x59, command};
    appendLE(out, unsigned(payload.size()), 3);
    out.insert(out.end(), payload.begin(), payload.end());
    std::uint8_t sum = 0;
    for (auto b : payload) sum += b;
    out.push_back(static_cast<std::uint8_t>(~sum));
    return out;
}
inline Bytes memoryRequest(unsigned address, unsigned size) {
    Bytes payload{5}; // SMK-25 II user presets
    appendLE(payload, address, 4);
    appendLE(payload, size, 3);
    return payload;
}
enum class Failure { protocol, model, verify };
class TransferError : public std::runtime_error {
public:
    explicit TransferError(Failure f) : std::runtime_error("SMK-25 II transfer"), reason(f) {}
    Failure reason;
};
using Exchange = std::function<Bytes(const Bytes&)>;

// Exchange returns one fully assembled notification frame. Validate the full
// envelope even for acknowledgements before advancing to the next write.
inline Bytes reply(const Exchange& exchange, std::uint8_t op, const Bytes& payload, std::uint8_t expected) {
    const auto data = exchange(frame(op, payload));
    if (data.size() < 7 || data[0] != 0 || data[1] != 0x59 || data[2] != expected
        || little(std::span(data).subspan(3, 3)) != data.size() - 7) throw TransferError(Failure::protocol);
    Bytes result(data.begin() + 6, data.end() - 1);
    if (frame(expected, result).back() != data.back()) throw TransferError(Failure::protocol);
    return result;
}
inline void identify(const Exchange& exchange) {
    const auto bytes = reply(exchange, 0x11, {}, 0x11);
    std::string name(bytes.begin(), bytes.end());
    auto end = name.find_first_of("_");
    if (end == std::string::npos || name.substr(0, end) != "SMK25II") throw TransferError(Failure::model);
}
inline unsigned slotAddress(int slot) {
    if (slot < 0 || slot >= 8) throw TransferError(Failure::protocol);
    return unsigned(slot * Smk25Preset::fileSize);
}
inline Smk25Preset::Image readPreset(const Exchange& exchange, int slot) {
    Smk25Preset::Image result;
    auto base = slotAddress(slot);
    for (unsigned i = 0; i < result.size();) {
        const auto n = std::min(1000u, unsigned(result.size()) - i);
        auto request = memoryRequest(base + i, n);
        auto data = reply(exchange, 0x23, request, 0x23);
        if (data.size() != request.size() + n || !std::equal(request.begin(), request.end(), data.begin()))
            throw TransferError(Failure::protocol);
        std::copy(data.begin() + request.size(), data.end(), result.begin() + i);
        i += n;
    }
    return result;
}
inline void write(const Exchange& exchange, unsigned address, std::span<const std::uint8_t> value) {
    auto request = memoryRequest(address, unsigned(value.size()));
    request.insert(request.end(), value.begin(), value.end());
    if (reply(exchange, 0x22, request, 0) != Bytes{0}) throw TransferError(Failure::protocol);
}
// The caller verifies identity and saves the read-before-write image first.
inline void writePreset(const Exchange& exchange, int slot, const Smk25Preset::Image& previous,
                        const Smk25Preset::Image& desired, unsigned payloadLimit) {
    const auto base = slotAddress(slot);
    if (!payloadLimit) throw TransferError(Failure::protocol);
    bool changed = false;
    for (size_t i = 0; i < desired.size();) {
        if (desired[i] == previous[i]) { ++i; continue; }
        size_t end = i + 1;
        while (end < desired.size() && end - i < std::min(173u, payloadLimit) && desired[end] != previous[end]) ++end;
        write(exchange, base + unsigned(i), std::span(desired).subspan(i, end - i));
        changed = true;
        i = end;
    }
    if (changed) {
        if (readPreset(exchange, slot) != desired) throw TransferError(Failure::verify);
        write(exchange, 0, {}); // vendor save command for user settings
        if (readPreset(exchange, slot) != desired) throw TransferError(Failure::verify);
    }
}
}
