#include "SingingScore.h"
#include "VoiceBank.h"
#include "../../core/VocalDelivery.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace {
using namespace ostfstudio::singing;

class SyllableVoice {
  public:
    int channel = -1, key = -1, accentPhone = 0;
    void start(int ch, int pitch, float velocity, Phone p, double sr, const VoiceBank* bank) {
        const bool connected = key >= 0 && channel == ch && envelope > .01;
        if (!connected) { envelope = 0; recorded = nullptr; }
        channel = ch; key = pitch; gain = velocity;
        if (!connected) phase = 0;
        released = false; noteAge = 0;
        articulate(p, sr, bank, connected);
        hz = juce::MidiMessage::getMidiNoteInHertz(pitch);
        if (!connected) for (auto& res : resonators) res.y1 = res.y2 = 0;
    }
    void articulate(Phone p, double sr, const VoiceBank* bank, bool legato) {
        constexpr float freq[5][3] = {
            {760, 1200, 2600}, {290, 2250, 3100}, {330, 1250, 2350}, {490, 1880, 2650}, {490, 900, 2450}};
        constexpr float bandwidth[3] = {90, 120, 175};
        englishSound = p.english ? &VoiceBank::english()[size_t(p.english & 63)] : nullptr;
        const int arpabet = accentPhone & 63;
        // CH, F, HH, K, P, S, SH, T and TH have no sung fundamental.
        unvoicedEnglishConsonant = bank && (arpabet==17 || arpabet==20 || arpabet==22 || arpabet==24
            || arpabet==29 || arpabet==31 || arpabet==32 || arpabet==33 || arpabet==34);
        morphFromPrevious = waveReady && legato;
        waveReady = false;
        joinedRecording = false;
        phone = p;
        age = 0;
        recorded = !englishSound && bank ? bank->find(p) : nullptr;
        vowelSample = false;
        if (!englishSound && bank && !recorded && p.onset != closure && p.onset != moraN) {
            recorded = bank->find({p.vowel, vowelOnly});
            vowelSample = recorded != nullptr;
        }
        nasalBody = nullptr;
        nasalAttack = nullptr;
        if (bank && arpabet >= 26 && arpabet <= 28) {
            // A CV contains both the nasal closure and an open vowel. Keep
            // the distinctive /m/ or /n/ attack, then sustain the bank's /N/.
            if (arpabet != 28 && recorded && !vowelSample
                && recorded->consonantFrames - recorded->voicedStart >= 2 * recorded->period)
                nasalAttack = recorded;
            nasalBody = bank->find({2, moraN});
            if (!nasalBody && !nasalAttack) {
                // A bank with only open vowels can still model a closed nasal.
                recorded = nullptr;
                vowelSample = false;
                p.onset = phone.onset = moraN;
                p.vowel = phone.vowel = 2;
            }
        }
        // A connected vowel enters after the initial breath of the recording.
        position = legato && recorded && p.onset == vowelOnly && p.durationMs > 0
            ? std::min(.014 * recorded->rate, .5 * recorded->consonantFrames) : 0.;
        attackSeconds = 0.;
        if (recorded) {
            attackStep = recorded->rate / sr;
            const double frames = p.onset == vowelOnly || vowelSample ? 0. : recorded->voicedStart;
            if (frames > 0 && p.durationMs > 0) {
                const double samples = sr * .001 * p.durationMs;
                if (arpabet > 15) {
                    attackStep = std::clamp(frames / samples, .55 * attackStep, 1.75 * attackStep);
                } else if (arpabet == 0 && recorded->pitchMarks.size() >= 3) {
                    // A short mora needs time for its sung vowel. Consume leading
                    // silence first, then shorten only the remaining consonant.
                    const double budget = .5 * samples;
                    position = std::clamp(frames - budget * attackStep, 0., double(recorded->attackFirst));
                    attackStep = std::max(attackStep, (frames - position) / budget);
                }
            }
            attackSeconds = std::max(0., frames - position) / (sr * attackStep);
        }
        const int onset = p.onset;
        nasal = onset == m || onset == n || onset == moraN;
        stop = onset == velar || onset == g || onset == t || onset == d || onset == pStop || onset == b;
        fricative = onset == sibilant || onset == sh || onset == z || onset == j || onset == ch || onset == ts
            || onset == h || onset == f || onset == v;
        voiced = onset == g || onset == d || onset == b || onset == z || onset == j || onset == v;
        double first = freq[p.vowel][0], second = freq[p.vowel][1];
        if (p.glide == 1 || onset == y || onset == sh || onset == j || onset == ch) { first = 305; second = 2200; }
        else if (p.glide == 2 || onset == w || onset == b || onset == pStop || onset == m) { first = 280; second = 800; }
        else if (nasal || onset == r || onset == t || onset == d) { first = 280; second = 1570; }
        else if (onset == velar || onset == g) { first = 380; second = 1800; }
        const double initial[3] = {first, second, freq[p.vowel][2]};
        transition = 1 - std::exp(-1 / (.017 * sr));
        for (int i = 0; i < 3; ++i) {
            const double pole = std::exp(-juce::MathConstants<double>::pi * bandwidth[i] / sr);
            auto coefficient = [&](double f) { return 2 * pole * std::cos(juce::MathConstants<double>::twoPi * std::min(.44 * sr, f) / sr); };
            auto& res = resonators[i];
            res.a = coefficient(initial[i]); res.target = coefficient(freq[p.vowel][i]);
            res.b = -pole * pole; res.drive = (1 - pole) * (i == 0 ? 1.0 : .65);
        }
    }
    void stopNote() { released = true; }
    bool endingOn(int ch) const { return channel == ch && released && envelope > 0; }
    float sample(double sr, double bendRatio, float expression, const juce::dsp::FFT& fft) {
        envelope = std::clamp(envelope + (released ? -36.0 : 110.0) / sr, 0.0, 1.0);
        if (released && envelope == 0) { key = channel = -1; return 0; }
        const double time = age++ / sr, held = noteAge++ / sr;
        if (phone.onset == closure) return 0;
        const double strength = phone.intensity / 127.0;
        double cents = 0;
        if (phone.delivery == vibrato) cents = vibratoCents(held,strength);
        if (phone.delivery == scoop) cents = scoopCents(held,strength);
        const double sustain = std::clamp((held - .15) / .28, 0., 1.);
        const auto pitch = hz * bendRatio * (cents == 0 ? 1 : std::exp2(cents / 1200));
        // Gently accent the nucleus of a stressed English syllable. Follow
        // the scored vowel length so a short word retains its consonants.
        double accent = 1.;
        if (accentPhone && (accentPhone & 63) <= 15) {
            const double progress = time / std::max(.08, phone.durationMs ? .001*phone.durationMs : .26);
            const double rise = std::min(1., progress/.24);
            accent = (accentPhone & 64) ? 1. + .12*rise*std::exp(-2.6*progress) : .94;
        }
        double arc = 1.;
        if (phone.durationMs >= 120) {
            const double u = std::clamp(time * 1000. / phone.durationMs,0.,1.);
            // A longer vowel blooms after the attack and eases into the next syllable.
            arc += .065*std::sin(juce::MathConstants<double>::pi*u) - .12*u*u*u;
        }
        const auto level = gain * envelope * expression * accent * arc * (1. - sustain * (.035 + .025 * std::sin(6.3 * held)));
        if (englishSound) {
            const auto& sound = *englishSound;
            const auto length = double(sound.frames.size() - 2);
            float tone;
            if ((phone.english & 63) <= 15) {
                // Stretch the middle of the vowel and leave time for the off-glide.
                const double fraction = phone.durationMs ? std::clamp(time * 1000 / phone.durationMs, 0., 1.)
                                                        : std::min(.85, time / .24);
                const auto centre = length * (.10 + .76 * fraction * fraction * (3. - 2. * fraction));
                tone = voicedSample(sound, centre, pitch, sr, fft);
            } else if ((phone.english & 63) >= 25 && (phone.english & 63) <= 28 && !sound.pitchMarks.empty()) {
                // Carry the sung pitch through L, M, N and NG into the vowel.
                const double fraction = std::min(1., time / std::max(.025, phone.durationMs * .001));
                tone = voicedSample(sound, length * (.13 + .70 * fraction), pitch, sr, fft);
            } else {
                const double seconds = phone.durationMs ? phone.durationMs / 1000. : .055;
                const double pos = time * length / seconds;
                tone = pos < length ? sound.sampleAt(pos) : 0.f;
            }
            return float(level * tone);
        }
        if (nasalBody || nasalAttack) {
            const double duration = phone.durationMs > 0 ? phone.durationMs * .001 : .195;
            const double prefix = std::min(.026, duration * .35);
            if (nasalAttack && (time < prefix || !nasalBody)) {
                const double at = std::min(nasalAttack->voicedStart + time * nasalAttack->rate,
                                           nasalAttack->consonantFrames - nasalAttack->period);
                return float(level * voicedSample(*nasalAttack, at, pitch, sr, fft));
            }
            const double progress = std::clamp(time / duration, 0., 1.);
            const double at = std::lerp(double(nasalBody->vowelFirst), double(nasalBody->vowelLast), progress);
            return float(level * voicedSample(*nasalBody, at, pitch, sr, fft));
        }
        // Use only the unvoiced portion of a CV for the /s/ in "lights".
        if (unvoicedEnglishConsonant)
            return float(level * unvoicedAttack(time,sr));
        float recording = 0;
        if (recorded) {
            const double onset = attackSeconds;
            if (recorded->pitchMarks.size() < 3) {
                recording = recorded->read(position,time < onset ? attackStep : pitch*recorded->period/sr);
                lastAttack = recording;
            } else if (time < onset) {
                recording = recorded->cycleSample(position,attackStep);
                position += attackStep;
                lastAttack = recording;
            } else {
                // Pitch analysis found a contiguous voiced region here. Marks in the
                // breath or release can jump between harmonics on consecutive cycles.
                const double elapsed = time - onset;
                const double duration = phone.durationMs > 0 ? std::max(.020, phone.durationMs * .001 - onset) : 0.;
                const double at = recorded->sungPosition(elapsed, duration, phone.onset != vowelOnly && !vowelSample);
                const auto sustained = voicedSample(*recorded, at, pitch, sr, fft);
                // Join the unvoiced attack once. Mixing the raw pitched vowel and
                // its retuned copy produces beating whenever their phases diverge.
                if (!joinedRecording) { joinDelta = onset > 0 ? lastAttack-sustained : 0.f; joinedRecording = true; }
                const auto smooth = std::clamp(1.-elapsed/.003,0.,1.);
                recording = sustained + joinDelta*float(smooth*smooth);
            }
        }
        if (recorded && !vowelSample) return float(level * recording);
        if (vowelSample) {
            // Only the recording supplies voiced energy. Model an unavailable consonant
            // with a short obstruction or noise burst ahead of the recorded vowel.
            noise = noise * 1664525u + 1013904223u;
            const double white = double(noise) / 2147483648.0 - 1.0;
            const double hiss = white - previousNoise;
            previousNoise = white;
            const double attack = fricative ? .055 : stop ? .032 : .025;
            const double opening = std::clamp(time/attack,0.,1.);
            const double noiseGain = fricative ? .10*(1.-opening) :
                stop && time>=.009 ? .08*std::exp(-150.*(time-.009)) : 0.;
            return float(level * (recording * opening + hiss * noiseGain));
        }
        const double step = std::min(.45, pitch / sr);
        phase += step; phase -= std::floor(phase);
        // A band-limited glottal edge excites the higher formants as well as the fundamental.
        double edge = 2 * phase - 1;
        if (phase < step) { const double t = phase/step; edge -= 2*t-t*t-1; }
        else if (phase > 1-step) { const double t = (phase-1)/step; edge -= t*t+2*t+1; }
        const double pulse = .82*edge + .18*std::sin(phase*juce::MathConstants<double>::twoPi);
        noise = noise * 1664525u + 1013904223u;
        const double white = double(noise) / 2147483648.0 - 1.0;
        const double hiss = white - previousNoise;
        previousNoise = white;
        const auto onset = phone.onset;
        const double attack = fricative ? .075 : stop ? .042 : nasal ? .065 : .035;
        const double opened = std::min(1.0, time / attack);
        double source = pulse + white * .012, unfiltered = 0;
        if (fricative || stop) {
            const double burst = stop ? (time < .014 ? 0 : std::exp(-95 * (time - .014))) : 1 - opened;
            source = pulse * (opened + (voiced ? .40 : 0) * (1 - opened)) + white * burst * .38;
            unfiltered = (onset == h || onset == f ? white * .10 : hiss * (onset == sh || onset == j ? .13 : .19)) * burst;
        }
        if (nasal) {
            const double nasalMix = onset == moraN ? 1 : 1 - opened;
            source *= 1 - .88 * nasalMix;
            unfiltered += nasalMix * .46 * std::sin(juce::MathConstants<double>::twoPi * phase);
        }
        if (onset == r && time > .009 && time < .022) source *= .10;
        double out = 0;
        for (auto& res : resonators) {
            res.a += (res.target - res.a) * transition;
            const double value = res.drive * source + res.a * res.y1 + res.b * res.y2;
            res.y2 = res.y1; res.y1 = value; out += value;
        }
        double tone = .45 * std::tanh(out) + unfiltered;
        return float(level * tone);
    }
  private:
    struct Resonator { double a{}, target{}, b{}, drive{}, y1{}, y2{}; };
    std::array<Resonator, 3> resonators{};
    const RecordedMora* recorded = nullptr;
    const RecordedMora* englishSound = nullptr;
    const RecordedMora* nasalAttack = nullptr;
    const RecordedMora* nasalBody = nullptr;
    bool unvoicedEnglishConsonant = false;
    float unvoicedAttack(double time,double sr) {
        const double allotted = phone.durationMs>0 ? .001*phone.durationMs : .065;
        if (recorded && !vowelSample && recorded->voicedStart-recorded->attackFirst >= .003*recorded->rate) {
            const int frames=recorded->voicedStart-recorded->attackFirst;
            const double natural=frames/recorded->rate;
            const double duration=stop ? std::min(allotted,natural) : allotted;
            if (time>=duration) return 0;
            const double step=frames/(duration*sr);
            const double taper=std::clamp((duration-time)/.003,0.,1.);
            return float(taper)*recorded->cycleSample(recorded->attackFirst+time*frames/duration,step);
        }
        // A vowel-only custom bank has no recorded hiss or release burst.
        if (time>=allotted) return 0;
        noise=noise*1664525u+1013904223u;
        const double white=double(noise)/2147483648.0-1.;
        const double hiss=white-previousNoise;
        previousNoise=white;
        const double env=std::min(1.,time/.002)*std::clamp((allotted-time)/.004,0.,1.);
        return float(.085*hiss*env*(stop ? std::exp(-time/.012) : 1.));
    }
    bool waveReady = false, morphFromPrevious = false, joinedRecording = false;
    double attackSeconds = 0., attackStep = 1.;
    float lastAttack = 0.f, joinDelta = 0.f;
    RecordedMora::Wave previousWave{}, nextWave{};
    int frameAge = 0, frameLength = 1;
    float voicedSample(const RecordedMora& source, double at, double pitch, double sr, const juce::dsp::FFT& fft) {
        const int hop = std::max(1, juce::roundToInt(sr * .005));
        if (!waveReady || frameAge >= frameLength) {
            const bool connected = !waveReady && morphFromPrevious && age < sr * .010;
            if (connected) {
                // Morph the timbre of adjacent voiced phones on the same pitch clock.
                const float blend = std::min(1.f,float(frameAge) / frameLength);
                for (size_t i = 0; i < previousWave.size(); ++i)
                    previousWave[i] = std::lerp(previousWave[i], nextWave[i], blend);
            } else if (waveReady) previousWave = nextWave;
            source.renderCycle(nextWave, fft, at, pitch, sr);
            if (!waveReady && !connected) previousWave = nextWave;
            waveReady = true;
            morphFromPrevious = false;
            frameLength = connected ? 2 * hop : hop;
            frameAge = 0;
        }
        const double position = phase * RecordedMora::waveSize;
        const int i = int(position);
        auto read = [&](const RecordedMora::Wave& wave) { return std::lerp(wave[i], wave[i+1], float(position-i)); };
        const float out = std::lerp(read(previousWave), read(nextWave), float(frameAge++) / frameLength);
        phase += std::min(.45, pitch / sr);
        phase -= std::floor(phase);
        return out;
    }
    Phone phone;
    double hz{}, phase{}, gain{}, envelope{}, position{}, transition{}, previousNoise{};
    unsigned noise = 1397;
    uint64_t age = 0, noteAge = 0;
    bool released = false, nasal = false, stop = false, voiced = false, fricative = false, vowelSample = false;
};
// Connect the waveform at phone boundaries without rendering a second pitched source.
class Voice : public SyllableVoice {
  public:
    void start(int ch, int pitch, float velocity, Phone p, double sr, const VoiceBank* bank) {
        preparePhones(p, bank);
        beginTransition(ch, segments[0], sr);
        accentPhone = p.english;
        SyllableVoice::start(ch, pitch, velocity, segments[0], sr, bank);
    }
    void articulate(Phone p, double sr, const VoiceBank* bank) {
        preparePhones(p, bank);
        beginTransition(channel, segments[0], sr);
        accentPhone = p.english;
        SyllableVoice::articulate(segments[0], sr, bank, true);
    }
    void stopNote() { count = 0; SyllableVoice::stopNote(); }
    float sample(double sr, double bend, float expression, const juce::dsp::FFT& fft) {
        if (stage+1 < count && ++stageFrames >= segments[stage].durationMs * .001 * sr) {
            const auto next = segments[++stage];
            beginTransition(channel,next,sr);
            SyllableVoice::articulate(next,sr,sourceBank,true);
            stageFrames = 0;
        }
        const float current = SyllableVoice::sample(sr, bend, expression, fft);
        if (remaining == length) spliceOffset = lastOutput - current;
        const float t = float(remaining) / length;
        if (remaining > 0) --remaining;
        lastOutput = current + spliceOffset * (t * t * (3.f - 2.f * t));
        return lastOutput;
    }
  private:
    float lastOutput = 0.f, spliceOffset = 0.f;
    std::array<Phone,3> segments{};
    const VoiceBank* sourceBank = nullptr;
    int stage = 0, count = 0, stageFrames = 0;

    // A selected Japanese CV bank supplies every English vowel and consonant.
    // With KAL selected, a Japanese mora uses that singer's onset, glide and vowel.
    // Only split internal articulation time; the score note and MIDI pitch stay intact.
    void preparePhones(Phone p, const VoiceBank* bank) {
        sourceBank = bank;
        count = 1; stage = stageFrames = 0;
        segments[0] = p;
        const int en = p.english & 63;
        const int ms = p.durationMs > 0 ? p.durationMs : 195;
        if (bank && en) {
            // ARPAbet order in the bundled pronunciation dictionary.
            constexpr int onsets[] = {b,ch,d,z,f,g,h,j,velar,r,m,n,moraN,pStop,r,sibilant,sh,t,sibilant,v,w,y,z,j};
            auto& first = segments[0];
            first.english = 0;
            first.vowel = en <= 15 ? japaneseVowelForEnglish(en) : p.vowel;
            first.onset = en <= 15 ? vowelOnly : onsets[en-16];
            const int offglide = en == 5 || en == 12 ? 2 : (en == 6 || en == 9 || en == 13 ? 1 : -1);
            if (offglide >= 0 && ms >= 24) {
                segments[1] = first;
                first.durationMs = ms * 2 / 3;
                segments[1].vowel = offglide;
                segments[1].durationMs = ms - first.durationMs;
                count = 2;
            }
        } else if (!bank && !en && p.onset != closure) {
            constexpr int vowels[] = {1,11,15,7,4};
            constexpr int onsets[] = {0,31,24,28,21,38,33,18,27,22,16,29,26,30,37,36,32,23,17,33,20,35,0,0};
            count = 0;
            auto add = [&](int phone, int duration) {
                auto& part = segments[count++];
                part = p; part.english = phone;
                part.durationMs = std::max(1,duration);
            };
            if (p.onset == moraN) { add(28,ms); return; }
            int left = ms;
            if (onsets[p.onset]) {
                const bool hiss = p.onset == sh || p.onset == sibilant || p.onset == f || p.onset == h;
                const int attack = std::min(hiss ? 65 : 42,std::max(1,ms/4));
                add(onsets[p.onset],attack); left -= attack;
            }
            if (p.glide && left >= 12) {
                const int glide = std::min(32,left/3);
                add(p.glide == 1 ? 37 : 36,glide); left -= glide;
            }
            add(vowels[p.vowel],left);
        }
    }
    int remaining = 0, length = 1;
    void beginTransition(int ch, Phone incoming, double sr) {
        remaining = 0;
        if (key < 0 || channel != ch) return;
        const int en = incoming.english & 63;
        const bool vowel = en ? en <= 15 : incoming.onset == vowelOnly;
        const bool sonorant = en ? en >= 25 && en <= 28 :
            incoming.onset == m || incoming.onset == n || incoming.onset == r || incoming.onset == moraN;
        // One short splice removes sample discontinuities without carrying a second sung pitch.
        double seconds = vowel ? .006 : sonorant ? .005 : .003;
        if (incoming.durationMs > 0) seconds = std::min(seconds,incoming.durationMs * .00035);
        length = remaining = std::max(1, int(seconds * sr));
    }
};
class Singer;
class Editor final : public juce::AudioProcessorEditor, private juce::ChangeListener {
  public:
    explicit Editor(Singer&);
    ~Editor() override;
    void resized() override;

  private:
    Singer& singer;
    juce::TextButton open{"Load OSTF..."};
    juce::TextButton importVoice{"Load voice folder..."};
    juce::ComboBox voicePicker;
    juce::ComboBox parts;
    juce::Label status, voiceStatus;
    juce::ImageComponent portrait;
    std::unique_ptr<juce::FileChooser> chooser;
    void refresh();
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
};
class Singer final : public juce::AudioProcessor, public juce::ChangeBroadcaster {
  public:
    const bool studioHost = juce::File::getSpecialLocation(juce::File::hostApplicationPath)
        .getFileNameWithoutExtension().equalsIgnoreCase("OSTF Studio");
    Singer() : AudioProcessor(BusesProperties().withOutput("Voice", juce::AudioChannelSet::stereo(), true)) { (void) VoiceBank::english(); }
    const juce::String getName() const override { return "OSTF Formant Singer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return .03; }
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::mono() ||
               l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Formant voice"; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new Editor(*this); }
    void prepareToPlay(double r, int) override {
        rate = r;
        reset();
    }
    void releaseResources() override {}
    void reset() override {
        voices = {};
        articulation = {};
        bend.fill(1.0);
        expression.fill(1.0f);
        lastPart = -1;
        lastIndex = -1;
        expected = -1;
    }
    bool load(const juce::String& source, int index) {
        auto next = readParts(juce::JSON::parse(source));
        if (next.empty())
            return false;
        juce::String nextText = source;
        const juce::SpinLock::ScopedLockType guard(lock);
        parts.swap(next);
        text.swapWith(nextText);
        selected = juce::jlimit(-1, int(parts.size()) - 1, index);
        ++generation;
        return true;
    }
    void select(int index) {
        const juce::SpinLock::ScopedLockType guard(lock);
        selected = juce::jlimit(-1, int(parts.size()) - 1, index);
        ++generation;
    }
    juce::StringArray trackNames(int& index) const {
        const juce::SpinLock::ScopedLockType guard(lock);
        juce::StringArray names;
        for (const auto& part : parts)
            names.add(part.name);
        index = selected;
        return names;
    }
    bool setVoice(const juce::File& directory, juce::String& error, bool recalling = false, int builtinChoice = 0) {
        auto next = directory == juce::File{} ? nullptr : VoiceBank::load(directory, error);
        if (!next && directory != juce::File{} && !recalling) return false;
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            bank.swap(next);
            builtin = builtinChoice == 1 ? 1 : 0;
            requestedVoice = directory;
            ++generation;
        }
        if (!recalling) updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
        return true;
    }
    juce::String voiceName() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        if (bank) return bank->name + "  /  " + juce::String(int(bank->sounds.size())) + " recorded morae";
        if (requestedVoice != juce::File{}) return "Voice folder unavailable: " + requestedVoice.getFileName() + "  /  using built-in voice";
        return builtin == 1 ? "KAL16 / CMU" : "Mei / MMDAgent";
    }
    int voiceChoice() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        return requestedVoice != juce::File{} ? 3 : builtin + 1;
    }
    juce::File voicePortrait() const {
        const juce::SpinLock::ScopedLockType guard(lock);
        return bank ? bank->portrait : juce::File{};
    }
    void getStateInformation(juce::MemoryBlock& b) override {
        juce::XmlElement xml("OSTFSinger");
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            xml.setAttribute("part", selected);
            xml.setAttribute("builtinVoice", builtin == 1 ? "kal16" : "mei");
            if (requestedVoice != juce::File{}) xml.setAttribute("voiceFolder", requestedVoice.getFullPathName());
            xml.addTextElement(text);
        }
        copyXmlToBinary(xml, b);
    }
    void setStateInformation(const void* b, int n) override {
        if (auto xml = getXmlFromBinary(b, n); xml && xml->hasTagName("OSTFSinger")) {
            const auto path = xml->getStringAttribute("voiceFolder");
            juce::String error;
            setVoice(juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File{}, error, true, xml->getStringAttribute("builtinVoice") == "kal16" ? 1 : 0);
            auto source = xml->getAllSubText();
            if (source.isNotEmpty() && !studioHost)
                load(source, xml->getIntAttribute("part", -1));
            else {
                const juce::SpinLock::ScopedLockType guard(lock);
                parts.clear();
                text.clear();
                selected = -1;
                ++generation;
            }
            sendChangeMessage();
        }
    }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals quiet;
        b.clear();
        const juce::SpinLock::ScopedTryLockType guard(lock);
        if (!guard.isLocked()) {
            midi.clear();
            return;
        }
        if (seen != generation) {
            reset();
            seen = generation;
        }
        const auto* sequence = selected >= 0 ? &parts[size_t(selected)].syllables : nullptr;
        double beat = 0, step = 0;
        if (sequence) {
            auto* head = getPlayHead();
            const auto pos = head ? head->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
            if (!pos || !pos->getIsPlaying() || !pos->getPpqPosition() || !pos->getBpm()) {
                reset();
                midi.clear();
                return;
            }
            beat = *pos->getPpqPosition();
            step = *pos->getBpm() / (60.0 * rate);
            if (lastPart != selected || std::abs(beat - expected) > step * 2) {
                reset();
                lastPart = selected;
                auto it = std::upper_bound(sequence->begin(), sequence->end(), beat,
                                           [](double at, const auto& s) { return at < s.start; });
                cursor = static_cast<int>(it - sequence->begin()) - 1;
            }
        }
        auto ev = midi.cbegin();
        const auto end = midi.cend();
        for (int i = 0; i < b.getNumSamples(); ++i) {
            if (sequence) {
                while (cursor + 1 < int(sequence->size()) && (*sequence)[size_t(cursor + 1)].start <= beat)
                    ++cursor;
                const int active = cursor >= 0 && beat < (*sequence)[size_t(cursor)].end ? cursor : -1;
                if (active != lastIndex) {
                    if (active >= 0) {
                        const auto& s = (*sequence)[size_t(active)];
                        auto phone = s.phone;
                        phone.durationMs = std::clamp(juce::roundToInt((s.end - s.start) * 1000 / (step * rate)), 1, 16383);
                        if (s.continuesNote && lastIndex >= 0 && lastIndex + 1 == active)
                            voices[0].articulate(phone, rate, activeBank());
                        else voices[0].start(1, s.key, s.velocity / 127.f, phone, rate, activeBank());
                    } else
                        voices[0].stopNote();
                    lastIndex = active;
                }
                beat += step;
            } else
                while (ev != end && (*ev).samplePosition <= i) {
                    handle((*ev).getMessage());
                    ++ev;
                }
            float sample = 0;
            for (auto& v : voices)
                if (v.key >= 0)
                    sample += v.sample(rate, bend[v.channel - 1], expression[v.channel - 1], synthesisFFT);
            const float magnitude = std::abs(sample);
            if (magnitude > .9f)
                sample = std::copysign(.9f + .1f * std::tanh((magnitude - .9f) * 10.f), sample);
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.setSample(ch, i, sample);
        }
        expected = beat;
        midi.clear();
    }

  private:
    void handle(const juce::MidiMessage& m) {
        if (isPhone(m)) {
            const auto* p = m.getSysExData();
            Phone phone{p[6], p[7]};
            if (m.getSysExDataSize() >= 11) { phone.glide = p[8]; phone.delivery = p[9]; phone.intensity = p[10]; }
            if (m.getSysExDataSize() >= 12) phone.english = p[11];
            if (m.getSysExDataSize() == 14) phone.durationMs = p[12] + 128 * p[13];
            articulation[p[4]][p[5]] = phone;
            for (auto& v : voices)
                if (v.channel == p[4] + 1 && v.key == p[5] && !v.endingOn(v.channel))
                    v.articulate(phone, rate, activeBank());
        } else if (m.isNoteOn()) {
            auto target = std::find_if(voices.begin(), voices.end(), [&](const Voice& v) {
                return v.channel == m.getChannel() && v.key == m.getNoteNumber();
            });
            if (target == voices.end()) target = std::find_if(voices.begin(), voices.end(),
                [&](const Voice& v) { return v.endingOn(m.getChannel()); });
            if (target == voices.end()) target = std::find_if(voices.begin(), voices.end(),
                [](const Voice& v) { return v.key < 0; });
            if (target == voices.end()) target = voices.begin();
            target->start(m.getChannel(), m.getNoteNumber(), m.getFloatVelocity(),
                          articulation[m.getChannel() - 1][m.getNoteNumber()], rate, activeBank());
        } else if (m.isPitchWheel()) {
            bend[m.getChannel() - 1] = std::exp2((m.getPitchWheelValue() - 8192) / (8192.0 * 6));
        } else if (m.isController() && m.getControllerNumber() == 11) {
            expression[m.getChannel() - 1] = m.getControllerValue() / 127.f;
        } else if (m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff()) {
            for (auto& v : voices)
                if (v.channel == m.getChannel() && (!m.isNoteOff() || v.key == m.getNoteNumber()))
                    v.stopNote();
        }
    }
    mutable juce::SpinLock lock;
    juce::String text;
    std::vector<Part> parts;
    int selected = -1, generation = 0, seen = -1, lastPart = -1, cursor = -1, lastIndex = -1;
    double rate = 48000, expected = -1;
    juce::dsp::FFT synthesisFFT{RecordedMora::waveOrder};
    std::array<Voice, 16> voices{};
    const VoiceBank& factoryBank = VoiceBank::factory();
    std::unique_ptr<VoiceBank> bank;
    int builtin = 0;
    const VoiceBank* activeBank() const { return bank ? bank.get() : (builtin == 1 ? nullptr : &factoryBank); }
    juce::File requestedVoice;
    std::array<double, 16> bend{};
    std::array<float, 16> expression{};
    std::array<std::array<Phone, 128>, 16> articulation{};
};
juce::Image loadPortrait(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > 8*1024*1024) return {};
    if (!file.hasFileExtension("bmp")) return juce::ImageFileFormat::loadFrom(file);
    // UTAU banks often supply an uncompressed Windows bitmap.
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes) || bytes.getSize() < 54) return {};
    const auto* p = static_cast<const juce::uint8*>(bytes.getData());
    const auto u32 = [&](int at) { return juce::ByteOrder::littleEndianInt(p+at); };
    const int w = int(u32(18)), h = int(u32(22)), bits = juce::ByteOrder::littleEndianShort(p+28);
    if (p[0]!='B' || p[1]!='M' || u32(14)<40 || u32(30)!=0 || (bits!=24 && bits!=32)
        || w<=0 || w>2048 || h==0 || h < -2048 || h>2048) return {};
    const int stride = (w*(bits/8)+3)&~3, height = std::abs(h);
    const size_t offset = u32(10);
    if (offset < 54 || offset > bytes.getSize() || size_t(height)*stride > bytes.getSize()-offset) return {};
    juce::Image img(juce::Image::RGB,w,height,false);
    juce::Image::BitmapData dest(img,juce::Image::BitmapData::writeOnly);
    for (int y=0;y<height;++y) {
        auto* src = p+offset+size_t(h>0 ? height-1-y : y)*stride;
        for (int x=0;x<w;++x,src+=bits/8) dest.setPixelColour(x,y,juce::Colour(src[2],src[1],src[0]));
    }
    return img;
}
Editor::Editor(Singer& s) : AudioProcessorEditor(s), singer(s) {
    addChildComponent(open);
    addChildComponent(parts);
    open.setVisible(!singer.studioHost);
    parts.setVisible(!singer.studioHost);
    addChildComponent(portrait);
    addAndMakeVisible(status);
    addAndMakeVisible(importVoice);
    voicePicker.setTitle("Singer voice");
    addAndMakeVisible(voicePicker);
    addAndMakeVisible(voiceStatus);
    singer.addChangeListener(this);
    refresh();
    parts.onChange = [this] { singer.select(parts.getSelectedId() - 2); };
    open.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Open an OSTF score", juce::File(), "*.ostf");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<Editor>(this)](const auto& fc) {
                                 if (!safe || !fc.getResult().existsAsFile())
                                     return;
                                 if (fc.getResult().getSize() > 32 * 1024 * 1024 ||
                                     !safe->singer.load(fc.getResult().loadFileAsString(), 0)) {
                                     safe->status.setText("Select an OSTF score under 32 MB with pitched notes.",
                                                          juce::dontSendNotification);
                                     return;
                                 }
                                 safe->refresh();
                             });
    };
    importVoice.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Choose a Japanese CV voice folder (kana WAV files or oto.ini)");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<Editor>(this)](const auto& fc) {
                if (!safe || !fc.getResult().isDirectory()) return;
                juce::String error;
                if (safe->singer.setVoice(fc.getResult(), error)) safe->refresh();
                else safe->voiceStatus.setText(error, juce::dontSendNotification);
            });
    };
    voicePicker.onChange = [this] {
        const int choice = voicePicker.getSelectedId();
        if (choice < 1 || choice > 2) return;
        juce::String error;
        singer.setVoice({},error,false,choice-1);
        refresh();
    };
    setSize(560, singer.studioHost ? 145 : 180);
}
Editor::~Editor() { singer.removeChangeListener(this); }
void Editor::refresh() {
    int index;
    const auto names = singer.trackNames(index);
    parts.clear(juce::dontSendNotification);
    parts.addItem("Studio track / MIDI input", 1);
    parts.addItemList(names, 2);
    parts.setSelectedId(index + 2, juce::dontSendNotification);
    status.setText(singer.studioHost ? "Studio track  /  singing the track's notes and lyrics" :
        "MIDI input or OSTF score playback", juce::dontSendNotification);
    voicePicker.clear(juce::dontSendNotification);
    voicePicker.addItem("Mei (Japanese CV)",1);
    voicePicker.addItem("KAL16 (English phones)",2);
    const auto choice = singer.voiceChoice();
    if (choice == 3) voicePicker.addItem(singer.voiceName(),3);
    voicePicker.setSelectedId(choice,juce::dontSendNotification);
    voiceStatus.setText(singer.voiceName(), juce::dontSendNotification);
    auto image = loadPortrait(singer.voicePortrait());
    portrait.setImage(image, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    portrait.setVisible(image.isValid());
    resized();
}
void Editor::resized() {
    auto r = getLocalBounds().reduced(12);
    if (portrait.isVisible()) { portrait.setBounds(r.removeFromLeft(94)); r.removeFromLeft(8); }
    status.setBounds(r.removeFromTop(32));
    r.removeFromTop(8);
    auto row = r.removeFromTop(singer.studioHost ? 0 : 32);
    open.setBounds(row.removeFromLeft(115));
    row.removeFromLeft(6);
    parts.setBounds(row);
    r.removeFromTop(7);
    voiceStatus.setBounds(r.removeFromTop(30));
    auto voices = r.removeFromTop(30);
    importVoice.setBounds(voices.removeFromRight(158));
    voices.removeFromRight(6);
    voicePicker.setBounds(voices);
}
} // namespace
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Singer; }
