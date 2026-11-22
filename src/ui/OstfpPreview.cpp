#include "ui/OstfpPreview.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ostfstudio {
namespace {
constexpr char mediaType[] = "application/vnd.ostf-project+zip";
constexpr int maxBytes = 4 * 1024 * 1024;

void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

juce::MemoryBlock readMember(juce::ZipFile& zip, int index) {
    const auto* entry = zip.getEntry(index);
    require(entry && !entry->isSymbolicLink && entry->uncompressedSize >= 0 && entry->uncompressedSize <= maxBytes,
            "Invalid or oversized OSTFP member");
    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
    require(stream != nullptr, "Cannot read OSTFP member");
    juce::MemoryBlock bytes;
    const auto size = stream->readIntoMemoryBlock(bytes, static_cast<juce::ssize_t>(entry->uncompressedSize + 1));
    require(size == static_cast<std::size_t>(entry->uncompressedSize), "OSTFP member size mismatch");
    return bytes;
}

juce::var parseObject(const juce::MemoryBlock& bytes) {
    juce::var value;
    require(juce::JSON::parse(bytes.toString(), value).wasOk() && value.isObject(), "Invalid OSTFP JSON object");
    return value;
}

const juce::Array<juce::var>& array(const juce::var& value) {
    const auto* entries = value.getArray();
    require(entries != nullptr, "Expected an OSTF array");
    return *entries;
}

juce::int64 integer(const juce::var& value) {
    require(value.isInt() || value.isInt64(), "Expected an integer OSTF position or meter");
    return static_cast<juce::int64>(value);
}

bool safeScorePath(const juce::String& path) {
    const auto parts = juce::StringArray::fromTokens(path, "/", "");
    if (path.isEmpty() || path.startsWithChar('/') || path.endsWithChar('/') || path.contains("//")) return false;
    for (const auto& part : parts) {
        if (part == "." || part == ".." || !part.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"))
            return false;
    }
    return !path.equalsIgnoreCase("mimetype") && !path.equalsIgnoreCase("manifest.json");
}

std::unique_ptr<OstfpPreview> projectScore(const juce::var& score) {
    require(score["format"] == "OSTF" && score["version"] == "5.0.0", "Expected an OSTF 5.0.0 score");
    const auto timeline = score["timeline"];
    const auto ppq = integer(score["ppq"]);
    const auto duration = integer(timeline["duration"]);
    const auto meters = timeline["meterChanges"];
    const auto& meterEntries = array(meters);
    require(meterEntries.size() == 1 && integer(meterEntries[0]["at"]) == 0, "Preview requires one meter starting at tick zero");
    const auto numerator = integer(meterEntries[0]["numerator"]);
    const auto denominator = integer(meterEntries[0]["denominator"]);
    require(ppq > 0 && ppq <= std::numeric_limits<int>::max() && numerator > 0 && numerator <= std::numeric_limits<int>::max()
            && denominator > 0 && duration > 0, "Invalid OSTF meter or duration");
    require(ppq <= std::numeric_limits<juce::int64>::max() / 4 / numerator, "OSTF meter exceeds preview tick range");
    const auto ticksPerWholeBars = ppq * numerator * 4;
    require(ticksPerWholeBars % denominator == 0, "Preview requires an integral number of ticks per bar");
    const auto ticksPerBar = ticksPerWholeBars / denominator;
    require(ticksPerBar > 0 && duration % ticksPerBar == 0 && duration / ticksPerBar <= 256,
            "Preview requires a whole-bar score of at most 256 bars");

    auto out = std::make_unique<OstfpPreview>();
    out->ticksPerBar = ticksPerBar;
    out->ppq = ppq;
    out->title = score["song"]["title"].toString();
    require(out->title.isNotEmpty(), "Missing song title");
    out->bars.endBar = static_cast<int>(duration / ticksPerBar) + 1;
    auto bar = [&](juce::int64 tick) {
        require(tick >= 0 && tick <= duration && tick % ticksPerBar == 0, "Preview requires bar-aligned regions");
        return static_cast<int>(tick / ticksPerBar) + 1;
    };
    auto addLane = [&](std::vector<ArrangementLane>& lanes, juce::String title, std::vector<ArrangementRegion> regions) {
        out->regions.push_back(std::move(regions));
        lanes.push_back({out->keepLabel(std::move(title)), out->regions.back()});
    };
    auto intervals = [&](const juce::var& value, const char* labelField) {
        std::vector<ArrangementRegion> result;
        if (value.isVoid()) return result;
        for (const auto& event : array(value)) {
            const auto start = integer(event["start"]);
            const auto length = integer(event["duration"]);
            const auto firstBar = bar(start);
            require(length > 0 && length <= duration - start, "Invalid OSTF region duration");
            const auto label = event[labelField].toString();
            result.push_back({{firstBar, bar(start + length)}, out->keepLabel(label.isNotEmpty() ? label : event["id"].toString())});
        }
        return result;
    };

    addLane(out->globalLanes, "SECTION", intervals(timeline["sections"], "name"));
    addLane(out->globalLanes, "CHORD", intervals(score["harmony"]["chords"], "label"));
    if (const auto chords = score["harmony"]["chords"]; chords.isArray())
        for (const auto& chord : array(chords))
            if (chord["voicing"].isObject()) ++out->chordVoicings;
    const auto key = score["song"]["key"]["label"].toString();
    if (key.isNotEmpty()) addLane(out->globalLanes, "KEY", {{out->bars, out->keepLabel(key)}});
    addLane(out->globalLanes, "METER", {{out->bars, out->keepLabel(juce::String(numerator) + "/" + juce::String(denominator))}});

    const auto tempo = timeline["tempoChanges"];
    const auto& changes = array(tempo);
    if (changes.size() == 1 && integer(changes[0]["at"]) == 0)
        out->previewBpm = static_cast<double>(changes[0]["bpm"]);
    std::vector<ArrangementRegion> tempos;
    for (int i = 0; i < changes.size(); ++i) {
        const auto first = bar(integer(changes[i]["at"]));
        const auto last = i + 1 < changes.size() ? bar(integer(changes[i + 1]["at"])) : out->bars.endBar;
        require(first < last, "Tempo changes must be ordered within the score");
        tempos.push_back({{first, last}, out->keepLabel(changes[i]["bpm"].toString())});
    }
    addLane(out->globalLanes, "BPM", std::move(tempos));

    const auto trackData = score["tracks"];
    const auto& tracks = array(trackData);
    require(tracks.size() <= 8, "Preview supports at most eight arrangement tracks");
    for (const auto& track : tracks) {
        addLane(out->trackLanes, track["name"].toString(), intervals(track["clips"], "id"));
        const auto role = track["role"].toString();
        auto& group = out->mixerGroups.emplace_back(role.equalsIgnoreCase("voicing") ? MixerTrackGroup::voicing
                             : role.equalsIgnoreCase("vocal") || role.equalsIgnoreCase("vocals") ? MixerTrackGroup::vocal
                             : MixerTrackGroup::other);
        auto& notes = out->pitchedNotes.emplace_back();
        const auto clips = track["clips"];
        if (clips.isVoid()) continue;
        for (const auto& clip : array(clips)) {
            const auto clipStart = integer(clip["start"]);
            const auto clipLength = integer(clip["duration"]);
            const auto events = clip["notes"];
            if (events.isVoid()) continue;
            for (const auto& event : array(events)) {
                if (event["pitch"].isVoid()) continue;
                const auto pitch = integer(event["pitch"]);
                const auto local = integer(event["start"]);
                const auto length = integer(event["duration"]);
                require(pitch >= 0 && pitch <= 127 && local >= 0 && local < clipLength && length > 0
                        && length <= duration - clipStart - local, "Invalid OSTF pitched note");
                notes.push_back({clipStart + local, clipStart + local + length, static_cast<int>(pitch)});
            }
        }
        if (group == MixerTrackGroup::other && !notes.empty()) group = MixerTrackGroup::pitched;
    }
    return out;
}
}

const char* OstfpPreview::keepLabel(juce::String label) {
    labels.push_back(std::move(label));
    return labels.back().toRawUTF8();
}

juce::Result readOstfpPreview(juce::InputStream& input, std::unique_ptr<OstfpPreview>& result) {
    try {
        require(input.getTotalLength() > 0 && input.getTotalLength() <= maxBytes && input.setPosition(0), "Invalid or oversized OSTFP file");
        unsigned char header[10] {};
        require(input.read(header, sizeof header) == sizeof header && juce::ByteOrder::littleEndianInt(header) == 0x04034b50
                && (header[6] & 1) == 0 && juce::ByteOrder::littleEndianShort(header + 8) == 0 && input.setPosition(0),
                "Expected a stored OSTFP mimetype header");
        juce::ZipFile zip(input);
        require(zip.getNumEntries() == 3 && zip.getEntry(0)->filename == "mimetype" && zip.getEntry(1)->filename == "manifest.json",
                "Preview requires a score-only OSTFP package (mimetype, manifest and score)");
        const auto mime = readMember(zip, 0);
        require(mime.getSize() == sizeof mediaType - 1 && std::memcmp(mime.getData(), mediaType, mime.getSize()) == 0,
                "Incorrect OSTFP media type");
        const auto manifest = parseObject(readMember(zip, 1));
        require(manifest["packageFormat"] == "OSTFP" && manifest["packageVersion"] == "1.0.0", "Unsupported OSTFP manifest version");
        require(array(manifest["documents"]).isEmpty() && array(manifest["assets"]).isEmpty() && array(manifest["bindings"]).isEmpty(),
                "Preview supports score-only OSTFP packages");
        const auto descriptor = manifest["score"];
        const auto path = descriptor["path"].toString();
        require(descriptor["format"] == "OSTF" && descriptor["version"] == "5.0.0" && safeScorePath(path)
                && zip.getEntry(2)->filename == path, "Invalid or unsupported OSTFP score entry");
        const auto scoreBytes = readMember(zip, 2);
        require(juce::SHA256(scoreBytes).toHexString() == descriptor["sha256"].toString(), "OSTFP score SHA-256 mismatch");
        result = projectScore(parseObject(scoreBytes));
        return juce::Result::ok();
    } catch (const std::runtime_error& error) {
        return juce::Result::fail(juce::String::fromUTF8(error.what()));
    }
}
}
