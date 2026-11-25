#include "VoiceBank.h"
#include "MeiVoiceData.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#elif JUCE_LINUX
#include <iconv.h>
#endif

namespace ostfstudio::singing {
namespace {
juce::String readAliases(const juce::File& file) {
    juce::MemoryBlock bytes;
    if (file.getSize() > 2 * 1024 * 1024 || !file.loadFileAsData(bytes) || bytes.isEmpty()) return {};
    auto* data = static_cast<const char*>(bytes.getData());
    const auto count = int(bytes.getSize());
    if (juce::CharPointer_UTF8::isValidString(data, count))
        return juce::String::fromUTF8(data, count).trimCharactersAtStart(juce::String::charToString(0xfeff));
#if JUCE_WINDOWS
    std::wstring wide(size_t(count), L'\0');
    const int n = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, data, count, wide.data(), count);
    if (n > 0) return juce::String(wide.data(), size_t(n));
#elif JUCE_MAC
    if (auto str = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(data), count,
                                           kCFStringEncodingDOSJapanese, false)) {
        std::vector<char> utf8(size_t(CFStringGetMaximumSizeForEncoding(CFStringGetLength(str), kCFStringEncodingUTF8)) + 1);
        const auto ok = CFStringGetCString(str, utf8.data(), CFIndex(utf8.size()), kCFStringEncodingUTF8);
        CFRelease(str);
        if (ok) return juce::String::fromUTF8(utf8.data());
    }
#elif JUCE_LINUX
    auto decoder = iconv_open("UTF-8", "CP932");
    if (decoder != (iconv_t)-1) {
        std::vector<char> utf8(size_t(count) * 4);
        char* dest = utf8.data();
        char* src = static_cast<char*>(bytes.getData());
        size_t remaining = size_t(count), space = utf8.size();
        auto result = iconv(decoder, &src, &remaining, &dest, &space);
        iconv_close(decoder);
        if (result != size_t(-1)) return juce::String::fromUTF8(utf8.data(), int(dest - utf8.data()));
    }
#endif
    return {};
}

// Estimate the period in the steady vowel after the consonant. Work at about 8 kHz
// for pitch detection; keep the original samples and sample rate for playback.
double periodAt(const std::vector<float>& x, int start, double sr) {
    const int stride = std::max(1, int(sr / 8000));
    const int maxLag = int(sr / (65 * stride)), minLag = std::max(2, int(sr / (800 * stride)));
    const int n = std::min(int(sr * .052 / stride), (int(x.size()) - start) / stride - maxLag - 1);
    if (n < maxLag) return 0;
    std::vector<double> corr(maxLag + 1);
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double cross = 0, a = 0, b = 0;
        for (int i = 0; i < n; ++i) {
            const double p = x[start + i * stride], q = x[start + (i + lag) * stride];
            cross += p * q; a += p * p; b += q * q;
        }
        corr[lag] = cross / std::sqrt(a * b + 1e-18);
    }
    const double threshold = std::max(.62, *std::max_element(corr.begin() + minLag, corr.end()) * .93);
    for (int lag = minLag + 1; lag < maxLag; ++lag) {
        const double peak = corr[lag];
        if (peak >= threshold && peak > corr[lag - 1] && peak >= corr[lag + 1]) {
            const double delta = .5 * (corr[lag - 1] - corr[lag + 1]) / (corr[lag - 1] - 2 * peak + corr[lag + 1]);
            return (lag + delta) * stride;
        }
    }
    return 0;
}

std::optional<RecordedMora> readMora(std::unique_ptr<juce::InputStream> stream, double offset, double consonant, double cutoff) {
    juce::WavAudioFormat wav;
    if (!stream) return {};
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
    if (!reader || reader->sampleRate < 8000 || reader->sampleRate > 192000 || reader->numChannels == 0) return {};
    const double sr = reader->sampleRate;
    if (!std::isfinite(offset) || !std::isfinite(consonant) || !std::isfinite(cutoff)
        || offset < 0 || consonant < 0 || std::max({offset, consonant, std::abs(cutoff)}) > 3600000) return {};
    const auto start = juce::int64(sr * offset / 1000);
    const auto end = cutoff < 0 ? std::min(reader->lengthInSamples, start + juce::int64(-cutoff * sr / 1000))
                                : reader->lengthInSamples - juce::int64(cutoff * sr / 1000);
    if (end - start < sr * .075 || consonant > 3500) return {};
    const int count = int(std::min<juce::int64>(end - start, juce::int64(4 * sr)));
    juce::AudioBuffer<float> audio(1, count);
    if (!reader->read(&audio, 0, count, start, true, false)) return {};
    const auto* data = audio.getReadPointer(0);
    const float peak = audio.getMagnitude(0, 0, count);
    if (!std::isfinite(peak) || peak < .001f) return {};
    int trim = 0;
    while (trim + 1 < count && std::abs(data[trim]) < peak * .006f) ++trim;
    trim = std::max(0, trim - int(.003 * sr));
    RecordedMora sample;
    sample.frames.assign(data + trim, data + count);
    sample.rate = sr;
    const int size = int(sample.frames.size());
    const int vowelStart = std::max(0, int(consonant * sr / 1000) - trim);
    const int latest = size - int(.07 * sr) - 2;
    if (latest < vowelStart) return {};
    for (const double fraction : {.42, .24, .60, .12}) {
        const int at = std::clamp(int(size * fraction), vowelStart, latest);
        sample.period = periodAt(sample.frames, at, sr);
        if (sample.period <= 0) continue;
        const int cycles = std::min(12, int((size - at - 2) / sample.period));
        if (cycles < 3) continue;
        sample.loopStart = at;
        sample.consonantFrames = std::min(at, consonant > 0 ? vowelStart : int(.045 * sr));
        sample.loopEnd = at + int(std::round(cycles * sample.period));
        sample.fade = std::max(1, int(std::round(sample.period)));
        sample.preparePitchMarks();
        return sample;
    }
    return {};
}
}

// Track the phase of the voiced recording at import time. A bright vowel can
// have several positive crossings per cycle. Compare the surrounding waveform
// to avoid switching between harmonics as the singer changes pitch or timbre.
void RecordedMora::preparePitchMarks() {
    pitchMarks.clear();
    voicedStart = consonantFrames;
    if (period < 2 || frames.size() < 3) return;
    const int end = int(frames.size()) - 2;
    const int radius = std::max(1,int(.28 * period));
    const int stride = std::max(1,int(period / 24));
    filtered.clear();
    // Symmetric half-band filter centred on the corresponding original sample.
    for (int octave=0; octave<6; ++octave) {
        const auto& in = octave ? filtered.back() : frames;
        if (in.size()<16) break;
        std::vector<float> out((in.size()+1)/2);
        constexpr float taps[]{-.001683f,0.f,.012384f,0.f,-.052345f,0.f,.291644f,.5f,
                                 .291644f,0.f,-.052345f,0.f,.012384f,0.f,-.001683f};
        for (size_t i=0; i<out.size(); ++i) {
            double sum=0;
            for (int k=-7;k<=7;++k) sum+=taps[k+7]*in[size_t(std::clamp(int(2*i)+k,0,int(in.size())-1))];
            out[i]=float(sum);
        }
        filtered.push_back(std::move(out));
    }
    const int start = std::clamp(loopStart,0,end);
    int anchor = start;
    float bestSlope = 0;
    for (int i=start; i<=std::min(end,start+int(period)); ++i) {
        const auto slope=frames[i+1]-frames[i];
        if (frames[i]<=0 && frames[i+1]>0 && slope>bestSlope) {
            bestSlope=slope;
            anchor=i;
        }
    }
    pitchMarks.push_back(anchor);
    const auto follow = [&](int direction) {
        double localPeriod = period;
        int mark=anchor;
        for (;;) {
            const int predicted = mark + direction*std::max(1,int(std::round(localPeriod)));
            if(predicted<0 || predicted>end) break;
            int best = predicted;
            double bestScore = -2;
            for (int i = std::max(0,predicted-radius); i <= std::min(end,predicted+radius); ++i) {
                if (frames[i] > 0 || frames[i+1] <= 0) continue;
                double cross = 0, a = 0, b = 0;
                for (int shift = -radius; shift <= radius; shift += stride) {
                    const auto x = frames[std::clamp(mark+shift,0,end)];
                    const auto y = frames[std::clamp(i+shift,0,end)];
                    cross += x*y; a += x*x; b += y*y;
                }
                const double score = cross/std::sqrt(a*b + 1.e-14) - .19 * std::abs(i-predicted)/radius;
                if (score > bestScore) { bestScore = score; best = i; }
            }
            if ((best-mark)*direction<=0) break;
            localPeriod = std::clamp(.78*localPeriod+.22*std::abs(best-mark),.78*period,1.22*period);
            pitchMarks.push_back(best);
            mark=best;
        }
    };
    follow(-1);
    std::reverse(pitchMarks.begin(),pitchMarks.end());
    follow(1);

    // Use the evolving vowel in the recording. The short crossfade loop is a
    // fallback for raw playback and may contain only twelve glottal pulses.
    vowelFirst = loopStart;
    vowelLast = std::max(loopStart,loopEnd-int(period));
    if (pitchMarks.size() >= 4) {
        const auto anchorIt = std::lower_bound(pitchMarks.begin(),pitchMarks.end(),anchor);
        const int anchorIndex = int(anchorIt-pitchMarks.begin());
        const auto similar = [&](int i) {
            const int a=pitchMarks[i], b=pitchMarks[i+1], c=pitchMarks[i+2];
            if (std::abs((b-a)-(c-b))>.18*period) return false;
            double xy=0., xx=0., yy=0.;
            for (int n=0;n<48;++n) {
                const double x=frames[a+(b-a)*n/48], y=frames[b+(c-b)*n/48];
                xy+=x*y; xx+=x*x; yy+=y*y;
            }
            return std::min(xx,yy)>.004 && xy>.80*std::sqrt(xx*yy);
        };
        int first=anchorIndex, last=anchorIndex;
        while(first>1 && pitchMarks[first-1]>=consonantFrames && first+1<int(pitchMarks.size()) && similar(first-1)) --first;
        while(last+3<int(pitchMarks.size()) && similar(last)) ++last;
        if (last-first>=3) {
            vowelFirst=pitchMarks[first];
            vowelLast=pitchMarks[last];
        }
    }

    // A nasal or voiced stop can already carry the singer's original pitch
    // well before the configured vowel boundary. Require adjacent, similar
    // cycles so that a single burst of unvoiced noise cannot start retuning.
    int stable = 0;
    for (size_t i=0; i+2<pitchMarks.size() && pitchMarks[i]<consonantFrames; ++i) {
        const auto a=pitchMarks[i], b=pitchMarks[i+1], c=pitchMarks[i+2];
        double energyA=0., energyB=0., cross=0.;
        for (int n=0;n<32;++n) {
            const auto x=frames[a+(b-a)*n/32], y=frames[b+(c-b)*n/32];
            energyA+=x*x; energyB+=y*y; cross+=x*y;
        }
        const bool voiced=std::abs((b-a)-(c-b))<=.15*period && std::min(energyA,energyB)>.0001
            && cross>.84*std::sqrt(energyA*energyB);
        stable=voiced ? stable+1 : 0;
        if(stable==3) { voicedStart=pitchMarks[i-2]; break; }
    }
}

std::unique_ptr<VoiceBank> VoiceBank::load(const juce::File& directory, juce::String& error) {
    auto bank = std::make_unique<VoiceBank>();
    bank->folder = directory;
    auto metadata = directory.getChildFile("character.txt");
    if (!metadata.existsAsFile()) metadata = directory.getParentDirectory().getChildFile("character.txt");
    if (metadata.existsAsFile()) for (const auto& line : juce::StringArray::fromLines(readAliases(metadata))) {
        if (!line.containsChar('=')) continue;
        const auto key = line.upToFirstOccurrenceOf("=",false,false).trim().toLowerCase();
        const auto value = line.fromFirstOccurrenceOf("=",false,false).trim();
        if (key == "name" && value.isNotEmpty()) bank->name = value;
        if (key == "image" && value.isNotEmpty() && !juce::File::isAbsolutePath(value)) {
            const auto root = metadata.getParentDirectory();
            const auto image = root.getChildFile(value);
            if (image.isAChildOf(root) && image.existsAsFile()) bank->portrait = image;
        }
    }
    if (bank->name.isEmpty()) bank->name = directory.getFileName();
    size_t total = 0;
    bool tooLarge = false;
    auto add = [&](const juce::String& alias, const juce::File& file, double offset, double consonant, double cutoff) {
        if (tooLarge) return;
        const auto phones = pronounce(alias, true);
        if (phones.size() != 1 || phones.front().onset == closure || phones.front().onset == lengthMark) return;
        const int key = phones.front().sampleKey();
        if (bank->sounds.count(key) || !file.existsAsFile()) return;
        if (auto sample = readMora(file.createInputStream(), offset, consonant, cutoff)) {
            total += sample->frames.size();
            if (total > 16 * 1024 * 1024) { tooLarge = true; return; }
            bank->sounds.emplace(key, std::move(*sample));
        }
    };
    const auto oto = directory.getChildFile("oto.ini");
    if (oto.existsAsFile()) {
        for (const auto& line : juce::StringArray::fromLines(readAliases(oto))) {
            if (!line.containsChar('=')) continue;
            auto name = line.upToFirstOccurrenceOf("=", false, false).trim();
            if (!name.endsWithIgnoreCase(".wav") || name.containsAnyOf("/\\:")) continue;
            auto fields = juce::StringArray::fromTokens(line.fromFirstOccurrenceOf("=", false, false), ",", "");
            if (fields.size() < 4) continue;
            auto file = directory.getChildFile(name);
            auto alias = fields[0].trim();
            add(alias.isEmpty() ? file.getFileNameWithoutExtension() : alias, file,
                fields[1].getDoubleValue(), fields[2].getDoubleValue(), fields[3].getDoubleValue());
        }
    } else {
        for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*"))
            if (file.hasFileExtension("wav")) add(file.getFileNameWithoutExtension(), file, 0, 0, 0);
    }
    if (tooLarge || bank->sounds.empty()) {
        error = tooLarge ? "Voice bank exceeds 64 MB of decoded samples."
                         : "No pitched Japanese CV samples found. Select the folder containing kana WAV files or oto.ini.";
        return {};
    }
    return bank;
}

const std::array<RecordedMora, 40>& VoiceBank::english() {
    static const auto bank = [] {
        std::array<RecordedMora, 40> sounds;
        juce::MemoryInputStream in(MeiVoiceData::englishphones_bin, MeiVoiceData::englishphones_binSize, false);
        for (size_t i = 1; i < sounds.size(); ++i) {
            auto& sound = sounds[i];
            sound.rate = in.readInt();
            const int frames = in.readInt();
            jassert(frames > 64 && sound.rate > 0 && in.getNumBytesRemaining() >= frames * 2);
            sound.frames.resize(size_t(frames));
            for (auto& frame : sound.frames) frame = in.readShort() / 32768.f;
            sound.period = periodAt(sound.frames, frames / 4, sound.rate);
            if (sound.period <= 0) sound.period = sound.rate / 150.;
            sound.preparePitchMarks();
        }
        return sounds;
    }();
    return bank;
}

const VoiceBank& VoiceBank::factory() {
    static const VoiceBank bank = [] {
        VoiceBank voice;
        voice.name = "Mei / Japanese";
        juce::MemoryInputStream input(MeiVoiceData::meicv_zip, MeiVoiceData::meicv_zipSize, false);
        juce::ZipFile archive(input);
        for (int i = 0; i < archive.getNumEntries(); ++i) {
            const auto phones = pronounce(archive.getEntry(i)->filename.upToLastOccurrenceOf(".", false, false), true);
            if (phones.size() != 1) continue;
            auto sample = readMora(std::unique_ptr<juce::InputStream>(archive.createStreamForEntry(i)), 0, 0, 0);
            if (sample) voice.sounds.emplace(phones.front().sampleKey(), std::move(*sample));
        }
        return voice;
    }();
    return bank;
}
}
