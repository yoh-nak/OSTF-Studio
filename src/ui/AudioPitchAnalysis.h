#pragma once
#include "ui/AudioModelPitches.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <numeric>

namespace ostfstudio {
// A conservative single-fundamental guide for recordings without a pinned model.
// Keep estimates outside the score and stop promptly when the user changes clips.
inline std::vector<AudioModelPitches::Observation> analysePitchGuide(const ProjectAudio& clip,const std::atomic_bool& cancel) {
    std::vector<AudioModelPitches::Observation> notes;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::InputStream> stream;
    if (clip.embedded.isEmpty()) stream=clip.localFile.createInputStream();
    else stream=std::make_unique<juce::MemoryInputStream>(clip.embedded,false);
    if (!stream) return notes;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(),true));
    if (!reader || reader->sampleRate<8000 || reader->sampleRate>384000 || reader->numChannels<1 || reader->numChannels>2) return notes;
    const int stride=std::max(1,int(reader->sampleRate/8000)), hop=int(reader->sampleRate*.02);
    const double rate=reader->sampleRate/stride;
    const int maxLag=int(rate/55), minLag=std::max(2,int(rate/1200)), window=int(rate*.032);
    const int block=(window+maxLag)*stride;
    const auto frames=clip.frameCount ? clip.frameCount : reader->lengthInSamples-clip.startFrame;
    if (clip.startFrame<0 || frames<=0 || frames>reader->lengthInSamples-clip.startFrame) return notes;
    juce::AudioBuffer<float> input(int(reader->numChannels),block);
    std::vector<double> samples(window+maxLag), correlations(maxLag+1);
    int lastPitch=-1;
    for (juce::int64 first=0;first<frames && !cancel.load();first+=hop) {
        if (!reader->read(&input,0,block,clip.startFrame+first,true,true)) break;
        if (frames-first<block) input.clear(int(frames-first),block-int(frames-first));
        const int channel=reader->numChannels==2 && input.getRMSLevel(1,0,block)>input.getRMSLevel(0,0,block) ? 1 : 0;
        if (input.getRMSLevel(channel,0,block)<.003f) { lastPitch=-1; continue; }
        const auto* src=input.getReadPointer(channel);
        for (int i=0;i<int(samples.size());++i)
            samples[i]=std::accumulate(src+i*stride,src+(i+1)*stride,0.)/stride;
        const double mean=std::accumulate(samples.begin(),samples.end(),0.)/samples.size();
        for (auto& v:samples) v-=mean;
        double best=0;
        for (int lag=minLag-1;lag<=maxLag;++lag) {
            double xy=0,xx=0,yy=0;
            for (int j=0;j<window;++j) {
                const double a=samples[j],b=samples[j+lag]; xy+=a*b; xx+=a*a; yy+=b*b;
            }
            best=std::max(best,correlations[lag]=xy/std::sqrt(std::max(1.e-20,xx*yy)));
        }
        int pitch=-1; double hz=0;
        for (int lag=minLag;lag<maxLag;++lag) {
            const double v=correlations[lag],l=correlations[lag-1],r=correlations[lag+1];
            if (v<std::max(.88,.97*best) || v<=l || v<=r) continue;
            hz=rate/(lag+.5*(l-r)/(l+r-2*v));
            pitch=juce::roundToInt(69+12*std::log2(hz/440.)); break;
        }
        const int count=int(std::min<juce::int64>(hop,frames-first));
        if (pitch>=0 && pitch==lastPitch) notes.back().frames+=count;
        else if (pitch>=0) notes.push_back({first,count,hz});
        lastPitch=pitch;
    }
    std::erase_if(notes,[hop](const auto& n){return n.frames<2*hop;});
    return notes;
}
}
