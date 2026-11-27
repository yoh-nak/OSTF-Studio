#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace knobverb {
// Five seconds of stereo extrema. A busy editor can skip a column without
// blocking the audio callback. Columns are recorded on the audio clock.
class WetWaveform {
public:
  struct Column { float low{}, high{}; };
  using Columns = std::array<Column, 250>;
  void prepare(double rate) {
    const juce::SpinLock::ScopedLockType guard(lock);
    columns = {}; pending = {}; write = count = 0;
    period = std::max(1,juce::roundToInt(rate*.020));
  }
  void push(float l, float r) noexcept {
    pending.low = std::min({pending.low,l,r});
    pending.high = std::max({pending.high,l,r});
    if (++count < period) return;
    const juce::SpinLock::ScopedTryLockType guard(lock);
    if (guard.isLocked()) {
      columns[write] = pending;
      write = (write+1)%columns.size();
    }
    count = 0; pending = {};
  }
  Columns read() const {
    const juce::SpinLock::ScopedLockType guard(lock);
    Columns result;
    std::rotate_copy(columns.begin(),columns.begin()+write,columns.end(),result.begin());
    return result;
  }
private:
  mutable juce::SpinLock lock;
  Columns columns{};
  Column pending{};
  size_t write=0;
  int count=0, period=960;
};
}
