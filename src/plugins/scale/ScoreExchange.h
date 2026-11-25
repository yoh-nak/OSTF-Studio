#pragma once
#include <pluginterfaces/vst/ivstcomponent.h>

namespace ostfstudio::scale {
inline constexpr Steinberg::TUID scoreInputId = INLINE_UID(0x936c3445,0xfa574289,0x8448c98d,0x68f70b51);
// Version 1, same process, called on the host message thread, borrowed bytes for this call only.
struct ScoreInput : Steinberg::FUnknown {
    virtual Steinberg::tresult PLUGIN_API offerScore(const char* utf8, Steinberg::int32 size) = 0;
};
}
