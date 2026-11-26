#include <juce_audio_formats/juce_audio_formats.h>
#include <signalsmith-stretch.h>
#include <cmath>
#include <iostream>
#include <optional>

using Buffer = juce::AudioBuffer<float>;

// Estimate a single steady fundamental in the middle of a user-selected note.
// Pick the first strong local correlation peak to avoid octave-down estimates.
static std::optional<double> fundamental(const Buffer& source, int start, int frames, double rate) {
    const int maxLag = int(rate / 65), minLag = std::max(2, int(rate / 1200));
    const int window = std::min(int(.07 * rate), frames - maxLag);
    if (window < maxLag) return {};
    const int centre = start + (frames - window - maxLag) / 2;
    int channel = 0;
    for (int c=1; c<source.getNumChannels(); ++c)
        if (source.getRMSLevel(c,centre,window) > source.getRMSLevel(channel,centre,window)) channel = c;
    if (source.getRMSLevel(channel,centre,window) < .001f) return {};
    const auto* samples = source.getReadPointer(channel,centre);
    std::vector<double> correlations(maxLag+1);
    for (int lag=minLag-1; lag<=maxLag; ++lag) {
        double xy=0, xx=0, yy=0;
        for (int i=0; i<window; ++i) {
            const double a=samples[i], b=samples[i+lag];
            xy+=a*b; xx+=a*a; yy+=b*b;
        }
        correlations[lag]=xy/std::sqrt(std::max(1.e-24,xx*yy));
    }
    const double threshold=std::max(.86, .97 * *std::max_element(correlations.begin(),correlations.end()));
    for (int i=minLag; i<maxLag; ++i) {
        const auto left=correlations[i-1], mid=correlations[i], right=correlations[i+1];
        if (mid < threshold || mid <= left || mid <= right) continue;
        const double fractional=.5*(left-right)/(left-2*mid+right);
        return rate/(i+fractional);
    }
    return {};
}

static void shiftRange(Buffer& audio,int start,int frames,double rate,double semitones) {
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    const int channels=audio.getNumChannels();
    stretch.presetDefault(channels,float(rate));
    stretch.setTransposeSemitones(float(semitones));
    const int latency=stretch.inputLatency()+stretch.outputLatency();
    Buffer shifted(channels,frames+latency), input(channels,frames+latency);
    input.clear();
    for (int c=0; c<channels; ++c) input.copyFrom(c,0,audio,c,start,frames);
    stretch.process(input.getArrayOfReadPointers(),input.getNumSamples(),shifted.getArrayOfWritePointers(),shifted.getNumSamples());
    const int fade=std::max(1,std::min(frames/2,int(rate*.01)));
    for (int c=0; c<channels; ++c) {
        auto* dest=audio.getWritePointer(c,start);
        const auto* wet=shifted.getReadPointer(c,latency);
        for(int i=0;i<frames;++i) dest[i]+=std::min({1.f,float(i)/fade,float(frames-1-i)/fade})*(wet[i]-dest[i]);
    }
}

static bool writeWav(const juce::File& dest,const Buffer& audio,double rate) {
    if (dest.exists()) return false;
    std::unique_ptr<juce::OutputStream> stream=dest.createOutputStream();
    juce::WavAudioFormat wav;
    auto options=juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(audio.getNumChannels()).withBitsPerSample(24);
    auto writer=wav.createWriterFor(stream,options);
    return writer && writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples());
}

static int selfTest(const juce::File& directory) {
    constexpr int rate=48000, frames=24000;
    Buffer audio(1,2*frames);
    for(int i=0;i<2*frames;++i)
        audio.setSample(0,i,float(.22*std::sin(juce::MathConstants<double>::twoPi*(i<frames?196.:220.)*i/rate)));
    Buffer original; original.makeCopyOf(audio);
    const auto before=fundamental(audio,frames,frames,rate);
    if(!before) return 1;
    const double target=juce::MidiMessage::getMidiNoteInHertz(64);
    shiftRange(audio,frames,frames,rate,12*std::log2(target / *before));
    const auto after=fundamental(audio,frames,frames,rate);
    std::cout<<"Before "<<*before<<" Hz, target "<<target<<" Hz, rendered "<<after.value_or(0)<<" Hz, "<<audio.getNumSamples()<<" frames\n";
    if(!after || std::abs(1200*std::log2(*after/target))>10
       || !std::equal(original.getReadPointer(0),original.getReadPointer(0)+frames,audio.getReadPointer(0))) return 2;
    if(directory!=juce::File{} && (!writeWav(directory.getChildFile("pitch-before.wav"),original,rate)
        || !writeWav(directory.getChildFile("pitch-after.wav"),audio,rate))) return 3;
    return 0;
}

int main(int argc,char** argv) {
    if(argc>=2 && juce::String(argv[1])=="--self-test")
        return selfTest(argc==3 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File{});
    if(argc!=6) {std::cerr<<"input.wav start-seconds end-seconds target-MIDI-note new-output.wav\n"; return 1;}
    const auto dir=juce::File::getCurrentWorkingDirectory();
    juce::WavAudioFormat wav;
    const auto src=dir.getChildFile(juce::String::fromUTF8(argv[1]));
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(src.createInputStream().release(),true));
    if(!reader || reader->numChannels<1 || reader->numChannels>2 || reader->sampleRate<8000 || reader->sampleRate>192000
        || reader->lengthInSamples<=0 || reader->lengthInSamples>30000000) return 2;
    const double rate=reader->sampleRate;
    double from,until,targetNote;
    try {
        auto number=[](const std::string& s){size_t n=0; const double v=std::stod(s,&n); if(n!=s.size() || !std::isfinite(v)) throw std::invalid_argument(s); return v;};
        from=number(argv[2]); until=number(argv[3]); targetNote=number(argv[4]);
    } catch(const std::exception&) {return 3;}
    if(from<0 || until<=from || until>reader->lengthInSamples/rate || targetNote<0 || targetNote>127) return 3;
    const int start=int(std::llround(from*rate)), frames=int(std::llround(until*rate))-start;
    Buffer audio(int(reader->numChannels),int(reader->lengthInSamples));
    if(!reader->read(&audio,0,audio.getNumSamples(),0,true,true)) return 4;
    const auto hz=fundamental(audio,start,frames,rate);
    if(!hz) {std::cerr<<"No stable monophonic pitch detected in selection\n"; return 5;}
    const double target=440.*std::exp2((targetNote-69)/12);
    shiftRange(audio,start,frames,rate,12*std::log2(target / *hz));
    std::cout<<*hz<<" Hz -> "<<fundamental(audio,start,frames,rate).value_or(0)<<" Hz ("<<target<<" requested)\n";
    return writeWav(dir.getChildFile(juce::String::fromUTF8(argv[5])),audio,rate)?0:6;
}
