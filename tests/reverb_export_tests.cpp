#include "plugins/effects/knobber/Controls.h"
#include "plugins/effects/knobber/Reverb.h"
#include "ui/AudioExport.h"
#include <iostream>
#include <juce_audio_processors/juce_audio_processors.h>
#include <stdexcept>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace ae = ostfstudio::audioexport;
using namespace juce;
static void require(bool condition, String message) {
  if (!condition)
    throw std::runtime_error(message.toStdString());
}
#if JUCE_WINDOWS
static void checkM4a(const File &file, int hz, int expected) {
  using Microsoft::WRL::ComPtr;
  auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const juce::ScopeGuard uninit{[&] {
    if (SUCCEEDED(init))
      CoUninitialize();
  }};
  require(SUCCEEDED(MFStartup(MF_VERSION)), "Media Foundation decoder");
  const juce::ScopeGuard shutdown{[] { MFShutdown(); }};
  ComPtr<IMFSourceReader> reader;
  ComPtr<IMFMediaType> type, actual;
  const auto stream = MF_SOURCE_READER_FIRST_AUDIO_STREAM;
  require(
      SUCCEEDED(MFCreateSourceReaderFromURL(
          file.getFullPathName().toWideCharPointer(), nullptr, &reader)) &&
          SUCCEEDED(MFCreateMediaType(&type)) &&
          SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
          SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float)) &&
          SUCCEEDED(reader->SetCurrentMediaType(stream, nullptr, type.Get())) &&
          SUCCEEDED(reader->GetCurrentMediaType(stream, &actual)),
      "Decode M4A");
  require(MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0) ==
                  2 &&
              MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                   0) == UINT32(hz),
          "AAC stereo / rate");
  int frames = 0;
  float peak = 0;
  for (;;) {
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    require(SUCCEEDED(reader->ReadSample(stream, 0, nullptr, &flags, nullptr,
                                         &sample)),
            "Read AAC");
    if (sample) {
      ComPtr<IMFMediaBuffer> buffer;
      BYTE *data = nullptr;
      DWORD bytes = 0;
      require(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) &&
                  SUCCEEDED(buffer->Lock(&data, nullptr, &bytes)),
              "AAC decoded block");
      for (DWORD i = 0; i < bytes / sizeof(float); ++i)
        peak = std::max(peak, std::abs(reinterpret_cast<float *>(data)[i]));
      buffer->Unlock();
      frames += int(bytes / (2 * sizeof(float)));
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
      break;
  }
  require(std::abs(frames - expected) < hz / 8 && peak > .1f,
          "AAC decoded duration / signal");
}
#endif
static void exportTests() {
  AudioFormatManager decoder;
  decoder.registerBasicFormats();
  for (auto f : ae::availableFormats()) {
    for (auto hz : ae::rates(f)) {
      for (int q = 0; q < ae::options(f).size(); ++q) {
        // All advertised rates and choices, including every native Windows
        // bitrate.
        if (hz != 44100 && hz != 48000 && q != ae::defaultQuality(f))
          continue;
        TemporaryFile out("." + String(ae::extension(f)));
        const int frames = hz / 3;
        int cursor = 0;
        auto r =
            ae::encode(out.getFile(), {f, hz, q}, frames, [&](auto &b, int n) {
              for (int i = 0; i < n; ++i, ++cursor) {
                float s =
                    .4f * float(std::sin(cursor * 2 *
                                         MathConstants<double>::pi * 440 / hz));
                b.setSample(0, i, s);
                b.setSample(1, i, .5f * s);
              }
              return true;
            });
        String setting = String(ae::name(f)) + " " + String(hz) + " " +
                         ae::options(f)[q] + " ";
        require(r.wasOk() && cursor == frames && out.getFile().getSize() > 24,
                setting + r.getErrorMessage());
        std::unique_ptr<AudioFormatReader> reader(
            decoder.createReaderFor(out.getFile()));
#if JUCE_WINDOWS
        if (f == ae::Format::aac) {
          checkM4a(out.getFile(), hz, frames);
          continue;
        }
#endif
        require(reader != nullptr, setting + "decode");
        require(reader->numChannels == 2 &&
                    std::abs(reader->lengthInSamples / reader->sampleRate -
                             frames / double(hz)) < .13,
                setting + "duration");
        AudioBuffer<float> audio(2, 4096);
        reader->read(&audio, 0, 4096, reader->lengthInSamples / 4, true, true);
        require(audio.getMagnitude(
                    0, 0, std::min(4096, int(reader->lengthInSamples) / 2)) >
                    .02f,
                setting + "signal");
        if (f == ae::Format::wav || f == ae::Format::flac ||
            f == ae::Format::aiff) {
          require(reader->bitsPerSample == ae::options(f)[q].getIntValue() &&
                      reader->sampleRate == hz &&
                      reader->lengthInSamples == frames,
                  setting + "resolution");
          if (f == ae::Format::wav && q == 2)
            require(reader->usesFloatingPointData, setting + "float encoding");
        }
      }
    }
    std::cout << ae::name(f) << " passed\n";
  }
  TemporaryFile sentinel(".wav");
  sentinel.getFile().replaceWithText("original");
  require(ae::encode(
              sentinel.getFile(), {}, 2048,
              [](auto &, int) {
                return false;
              }).failed() &&
              sentinel.getFile().loadFileAsString() == "original",
          "Cancellation replaced destination");
}
static AudioProcessorParameter &param(AudioProcessor &p, String name) {
  for (auto *v : p.getParameters())
    if (v->getName(128) == name)
      return *v;
  throw std::runtime_error(("Missing " + name).toStdString());
}
static void reverbTests(const File &file, const File &samples) {
  require(knobverb::Reverb::limitWet(.4f)==.4f && knobverb::Reverb::limitWet(-.7f)==-.7f
          && knobverb::Reverb::limitWet(2.f)<=1.f,"Linear wet tail / bounded peaks");
  knobverb::WetWaveform waveform;
  waveform.prepare(48000);
  for (int i=0; i<960; ++i) waveform.push(i==2 ? -.3f : 0.f, i==8 ? .7f : 0.f);
  auto history=waveform.read();
  require(history.back().low==-.3f && history.back().high==.7f && history.front().high==0,"Stereo waveform timing");
  knobber::Delay left(4096), right(4096);
  knobverb::Predelay transition;
  auto tap=transition.read(left,right,.75,-.5,0,48000);
  for (int i=0; i<=960; ++i) {
    const auto next=transition.read(left,right,.75,-.5,1500,48000);
    require(std::abs(next.left-tap.left)<.001 && std::abs(next.right-tap.right)<.001,"Pre-delay discontinuity");
    tap=next;
  }
  require(std::abs(tap.left)<.00001 && std::abs(tap.right)<.00001,"Pre-delay did not settle");
  VST3PluginFormat vst;
  OwnedArray<PluginDescription> types;
  vst.findAllTypesForFile(types, file.getFullPathName());
  require(types.size() == 1 && !types[0]->isInstrument, "VST3 scan");
  String error;
  auto p = vst.createInstanceFromDescription(*types[0], 48000, 256, error);
  require(p && p->getNumPrograms() == 49, "Factory programs " + error);
  for (auto c : knobverb::controls)
    require(param(*p, c.name).isAutomatable(), c.name);
  for (int i = 0; i < 49; ++i)
    require(p->getProgramName(i) == knobverb::types[i], "Program name");
  p->setCurrentProgram(1);
  param(*p, "Dry / Wet").setValueNotifyingHost(.37f);
  MemoryBlock state;
  p->getStateInformation(state);
  p->setCurrentProgram(7);
  p->setStateInformation(state.getData(), int(state.getSize()));
  require(std::abs(param(*p, "Dry / Wet").getValue() - .37f) < .001f,
          "State recall");
  {
    auto dry=std::make_unique<knobverb::Reverb>();
    {
      std::unique_ptr<AudioProcessorEditor> editor(dry->createEditor());
      ComboBox* algorithms=nullptr;
      for(auto* child:editor->getChildren())
        if(auto* combo=dynamic_cast<ComboBox*>(child); combo && combo->getNumItems()==dry->getNumPrograms())algorithms=combo;
      require(algorithms!=nullptr,"Reverb type selector");
      Thread::sleep(80);
      algorithms->setSelectedId(8,sendNotificationAsync);
      Timer::callPendingTimersSynchronously();
      require(algorithms->getSelectedId()==8,"Meter tick replaced pending preset");
      algorithms->onChange();
      require(dry->getCurrentProgram()==7,"Pending preset did not reach DSP");
    }
    auto* wetControl=dry->state.getParameter("mixamount");
    dry->ccMap.learn(wetControl->getParameterIndex());
    MidiBuffer learn; learn.addEvent(MidiMessage::controllerEvent(3,74,100),0);
    dry->ccMap.process(learn);
    require(std::abs(wetControl->getValue()-100.f/127)<.01,"MIDI CC learn");
    MemoryBlock saved; dry->getStateInformation(saved);
    dry->ccMap.forget(wetControl->getParameterIndex());
    dry->setStateInformation(saved.getData(),int(saved.getSize()));
    learn.clear();learn.addEvent(MidiMessage::controllerEvent(2,74,0),0); dry->ccMap.process(learn);
    require(wetControl->getValue()>.75f,"CC channels are independent");
    learn.clear();learn.addEvent(MidiMessage::controllerEvent(3,74,0),0); dry->ccMap.process(learn);
    require(wetControl->getValue()==0,"Restored CC map");
    wetControl->setValueNotifyingHost(0);
    dry->setRateAndBufferSizeDetails(48000,256);
    dry->prepareToPlay(48000,256);
    AudioBuffer<float> audio(2,256);
    MidiBuffer events;
    for(int b=0;b<100;++b) {
      for(int ch=0;ch<2;++ch)
        for(int i=0;i<256;++i) audio.setSample(ch,i,.5f*std::sin((256*b+i)*.06f));
      dry->processBlock(audio,events);
    }
    require(dry->wetPeak.load()==0 && dry->wetRmsDb.load()==-84 && dry->inputPeak.load()>.45f && dry->outputPeak.load()>.1f,
            "Dry-only mix must meter input and output but no audible wet signal");
  }
  if (samples != File{})
    samples.createDirectory();
  for (int program : {0, 9, 29}) {
    p->setCurrentProgram(program);
    param(*p, "Dry / Wet").setValueNotifyingHost(1);
    p->setRateAndBufferSizeDetails(48000, 256);
    p->prepareToPlay(48000, 256);
    AudioBuffer<float> response(2, 48000);
    for (int start = 0; start < 48000; start += 256) {
      int n = std::min(256, 48000 - start);
      AudioBuffer<float> block(response.getArrayOfWritePointers(), 2, start, n);
      block.clear();
      if (!start) {
        block.setSample(0, 0, .8f);
        block.setSample(1, 0, .4f);
      }
      MidiBuffer midi;
      p->processBlock(block, midi);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          require(std::isfinite(block.getSample(ch, i)), "Non-finite reverb");
    }
    const int tailStart = program == 0 ? 240 : 4800;
    const float peak = response.getMagnitude(0, 48000),
                tail = response.getMagnitude(tailStart, 48000 - tailStart);
    std::cout << p->getProgramName(program) << " peak " << peak << " late "
              << tail << "\n";
    require(tail > .00001f, "Missing reverb tail " + String(program));
    if (samples != File{}) {
      int pos = 0;
      auto result = ae::encode(
          samples.getChildFile(
              File::createLegalFileName(p->getProgramName(program)) + ".wav"),
          {}, 48000, [&](auto &b, int n) {
            for (int ch = 0; ch < 2; ++ch)
              b.copyFrom(ch, 0, response, ch, pos, n);
            pos += n;
            return true;
          });
      require(result.wasOk(), result.getErrorMessage());
    }
    p->releaseResources();
  }
  std::unique_ptr<AudioProcessorEditor> editor(p->createEditorIfNeeded());
  require(editor != nullptr, "Editor");
  if (samples != File{}) {
    auto native = std::make_unique<knobverb::Reverb>();
    native->setCurrentProgram(9);
    native->setRateAndBufferSizeDetails(48000, 256);
    native->prepareToPlay(48000, 256);
    juce::Random noise(19);
    juce::AudioBuffer<float> input(2, 256);
    juce::MidiBuffer events;
    for (int b = 0; b < 32; ++b) {
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 256; ++i)
          input.setSample(ch, i, (noise.nextFloat() - .5f) * .6f);
      native->processBlock(input, events);
    }
    std::unique_ptr<AudioProcessorEditor> ui(native->createEditor());
    ui->setSize(850, 640);
    auto image = ui->createComponentSnapshot(ui->getLocalBounds());
    auto stream =
        samples.getChildFile("SUPER KNOBBER REVERB.png").createOutputStream();
    require(stream && stream->setPosition(0) && stream->truncate().wasOk() &&
                PNGImageFormat().writeImageToStream(image, *stream),
            "Editor snapshot");
  }
}
int main(int argc, char **argv) {
  ScopedJuceInitialiser_GUI gui;
  try {
    require(argc >= 2, "Expected VST3 path");
    reverbTests(File(String::fromUTF8(argv[1])),
                argc == 3 ? File(String::fromUTF8(argv[2])) : File{});
    exportTests();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
