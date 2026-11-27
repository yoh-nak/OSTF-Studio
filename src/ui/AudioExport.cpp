#include "AudioExport.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>
#endif
namespace ostfstudio::audioexport {
const char *extension(Format f) {
  constexpr const char *names[] = {"wav",  "aiff", "flac", "ogg",
                                   "opus", "mp3",  "m4a"};
  return names[int(f)];
}
const char *name(Format f) {
  constexpr const char *names[] = {"WAV",  "AIFF", "FLAC",      "Ogg Vorbis",
                                   "Opus", "MP3",  "AAC (.m4a)"};
  return names[int(f)];
}
#if JUCE_MAC
static juce::File lameExecutable() {
  for (auto path :
       {"/opt/homebrew/bin/lame", "/usr/local/bin/lame", "/usr/bin/lame"})
    if (juce::File(path).existsAsFile())
      return juce::File(path);
  return {};
}
#endif
juce::Array<Format> availableFormats() {
  juce::Array<Format> formats{Format::wav, Format::aiff, Format::flac,
                              Format::vorbis, Format::opus};
#if JUCE_WINDOWS
  formats.add(Format::mp3);
  formats.add(Format::aac);
#elif JUCE_MAC
  if (lameExecutable().existsAsFile())
    formats.add(Format::mp3);
  formats.add(Format::aac);
#endif
  return formats;
}
static std::unique_ptr<juce::AudioFormat> codec(Format f) {
  switch (f) {
  case Format::wav:
    return std::make_unique<juce::WavAudioFormat>();
  case Format::aiff:
    return std::make_unique<juce::AiffAudioFormat>();
  case Format::flac:
    return std::make_unique<juce::FlacAudioFormat>();
  case Format::vorbis:
    return std::make_unique<juce::OggVorbisAudioFormat>();
  case Format::opus:
    return std::make_unique<juce::OpusAudioFormat>();
  default:
    return {};
  }
}
juce::Array<int> rates(Format f) {
  if (auto c = codec(f))
    return c->getPossibleSampleRates();
  return {44100, 48000};
}
juce::StringArray options(Format f) {
  if (f == Format::mp3)
    return {"128 kbps", "192 kbps", "256 kbps", "320 kbps"};
  if (f == Format::aac)
    return {"96 kbps", "128 kbps", "160 kbps", "192 kbps"};
  if (f == Format::vorbis || f == Format::opus)
    return codec(f)->getQualityOptions();
  juce::StringArray labels{"16-bit PCM", "24-bit PCM"};
  if (f == Format::wav)
    labels.add("32-bit float");
  return labels;
}
int defaultQuality(Format f) {
  if (f == Format::opus)
    return 0;
  if (f == Format::vorbis)
    return std::max(0, options(f).indexOf("192 kbps"));
  return f == Format::aac ? 3 : 1;
}
static juce::Result writeJuce(const juce::File &file, Settings s,
                              juce::int64 frames, const Render &render) {
  auto c = codec(s.format);
  const int bits = s.format == Format::vorbis || s.format == Format::opus
                       ? 32
                       : options(s.format)[s.quality].getIntValue();
  auto spec =
      juce::AudioFormatWriterOptions{}
          .withSampleRate(s.rate)
          .withNumChannels(2)
          .withBitsPerSample(bits)
          .withQualityOptionIndex(s.format == Format::flac ? 5 : s.quality);
  if (s.format == Format::wav && bits == 32)
    spec = spec.withSampleFormat(
        juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
  std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
  auto writer = c && out ? c->createWriterFor(out, spec) : nullptr;
  if (!writer)
    return juce::Result::fail("Could not open the audio encoder.");
  juce::AudioBuffer<float> block(2, blockSize);
  for (juce::int64 n = 0; n < frames;) {
    const int count = int(std::min<juce::int64>(frames - n, blockSize));
    if (!render(block, count))
      return juce::Result::fail("Cancelled");
    if (!writer->writeFromAudioSampleBuffer(block, 0, count))
      return juce::Result::fail("Could not write audio data.");
    n += count;
  }
  writer.reset();
  return file.getSize() > 0 ? juce::Result::ok()
                            : juce::Result::fail("Empty audio file.");
}
#if JUCE_WINDOWS
using Microsoft::WRL::ComPtr;
static juce::Result writeCompressed(const juce::File &file, Settings s,
                                    juce::int64 frames, const Render &render) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const juce::ScopeGuard uninit{[&] {
    if (SUCCEEDED(init))
      CoUninitialize();
  }};
  if (FAILED(MFStartup(MF_VERSION)))
    return juce::Result::fail("Windows Media Foundation is unavailable.");
  const juce::ScopeGuard shutdown{[] { MFShutdown(); }};
  ComPtr<IMFSinkWriter> sink;
  ComPtr<IMFMediaType> output, input;
  DWORD stream = 0;
  auto error = [](HRESULT hr) {
    return juce::Result::fail("Windows audio encoder: 0x" +
                              juce::String::toHexString(int(hr)));
  };
  HRESULT hr = MFCreateSinkWriterFromURL(
      file.getFullPathName().toWideCharPointer(), nullptr, nullptr, &sink);
  if (FAILED(hr))
    return error(hr);
  const auto setType = [&](ComPtr<IMFMediaType> &t, const GUID &subtype,
                           int bytes) {
    HRESULT r = MFCreateMediaType(&t);
    if (SUCCEEDED(r))
      r = t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(r))
      r = t->SetGUID(MF_MT_SUBTYPE, subtype);
    if (SUCCEEDED(r))
      r = t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    if (SUCCEEDED(r))
      r = t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, s.rate);
    if (SUCCEEDED(r))
      r = t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(r))
      r = t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes);
    return r;
  };
  ComPtr<IMFCollection> supported;
  const auto subtype =
      s.format == Format::mp3 ? MFAudioFormat_MP3 : MFAudioFormat_AAC;
  const auto bytesPerSecond =
      UINT32(options(s.format)[s.quality].getIntValue() * 125);
  if (FAILED(hr = MFTranscodeGetAudioOutputAvailableTypes(
                 subtype, MFT_ENUM_FLAG_ALL, nullptr, &supported)))
    return error(hr);
  DWORD count = 0;
  if (FAILED(hr = supported->GetElementCount(&count)))
    return error(hr);
  for (DWORD i = 0; i < count; ++i) {
    ComPtr<IUnknown> item;
    ComPtr<IMFMediaType> candidate;
    if (FAILED(supported->GetElement(i, &item)) || FAILED(item.As(&candidate)))
      continue;
    if (MFGetAttributeUINT32(candidate.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0) ==
            2 &&
        MFGetAttributeUINT32(candidate.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND,
                             0) == UINT32(s.rate) &&
        MFGetAttributeUINT32(candidate.Get(), MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                             0) == bytesPerSecond) {
      output = candidate;
      break;
    }
  }
  if (!output)
    return juce::Result::fail(
        "The system encoder does not support this bitrate and sample rate.");
  if (FAILED(hr = sink->AddStream(output.Get(), &stream)) ||
      FAILED(hr = setType(input, MFAudioFormat_PCM, s.rate * 4)) ||
      FAILED(hr = input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4)) ||
      FAILED(hr = sink->SetInputMediaType(stream, input.Get(), nullptr)) ||
      FAILED(hr = sink->BeginWriting()))
    return error(hr);
  juce::AudioBuffer<float> block(2, blockSize);
  for (juce::int64 n = 0; n < frames;) {
    const int count = int(std::min<juce::int64>(frames - n, blockSize));
    if (!render(block, count))
      return juce::Result::fail("Cancelled");
    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFSample> sample;
    BYTE *bytes = nullptr;
    if (FAILED(hr = MFCreateMemoryBuffer(count * 4, &buffer)) ||
        FAILED(hr = buffer->Lock(&bytes, nullptr, nullptr)))
      return error(hr);
    auto *pcm = reinterpret_cast<int16_t *>(bytes);
    for (int i = 0; i < count; ++i)
      for (int ch = 0; ch < 2; ++ch)
        *pcm++ = int16_t(juce::roundToInt(
            juce::jlimit(-1.f, 1.f, block.getSample(ch, i)) * 32767.f));
    const auto start = n * 10000000 / s.rate;
    n += count;
    if (FAILED(hr = buffer->Unlock()) ||
        FAILED(hr = buffer->SetCurrentLength(count * 4)) ||
        FAILED(hr = MFCreateSample(&sample)) ||
        FAILED(hr = sample->AddBuffer(buffer.Get())) ||
        FAILED(hr = sample->SetSampleTime(start)) ||
        FAILED(hr = sample->SetSampleDuration(n * 10000000 / s.rate - start)) ||
        FAILED(hr = sink->WriteSample(stream, sample.Get())))
      return error(hr);
  }
  return FAILED(hr = sink->Finalize()) ? error(hr) : juce::Result::ok();
}
#endif
juce::Result encode(const juce::File &file, Settings s, juce::int64 frames,
                    const Render &render,
                    const std::function<bool()> &cancelled) {
  if (frames <= 0 || !availableFormats().contains(s.format) ||
      !rates(s.format).contains(s.rate) ||
      !juce::isPositiveAndBelow(s.quality, options(s.format).size()))
    return juce::Result::fail("Unsupported audio export settings.");
  juce::TemporaryFile temp(file);
  auto result = juce::Result::fail("Audio encoder unavailable.");
  if (s.format != Format::mp3 && s.format != Format::aac)
    result = writeJuce(temp.getFile(), s, frames, render);
#if JUCE_WINDOWS
  else
    result = writeCompressed(temp.getFile(), s, frames, render);
#elif JUCE_MAC
  else {
    juce::TemporaryFile wav(".wav");
    result = writeJuce(wav.getFile(), {Format::wav, s.rate, 1}, frames, render);
    if (result.wasOk()) {
      auto bitrate = options(s.format)[s.quality].getIntValue();
      juce::StringArray args;
      if (s.format == Format::aac)
        args = juce::StringArray{
            "/usr/bin/afconvert",        "-f", "m4af", "-d", "aac", "-b",
            juce::String(bitrate * 1000)};
      else
        args = juce::StringArray{lameExecutable().getFullPathName(), "--silent",
                                 "-b", juce::String(bitrate)};
      args.add(wav.getFile().getFullPathName());
      args.add(temp.getFile().getFullPathName());
      juce::ChildProcess process;
      if (!process.start(args, 0))
        return juce::Result::fail("Could not start the audio encoder.");
      while (!process.waitForProcessToFinish(100))
        if (cancelled && cancelled()) {
          process.kill();
          process.waitForProcessToFinish(-1);
          return juce::Result::fail("Cancelled");
        }
      if (process.getExitCode() != 0)
        result = juce::Result::fail("Audio conversion failed.");
    }
  }
#endif
  if (result.failed())
    return result;
  if (cancelled && cancelled())
    return juce::Result::fail("Cancelled");
  return temp.getFile().getSize() > 0 && temp.overwriteTargetFileWithTemporary()
             ? juce::Result::ok()
             : juce::Result::fail("Could not finish the audio file.");
}
} // namespace ostfstudio::audioexport
