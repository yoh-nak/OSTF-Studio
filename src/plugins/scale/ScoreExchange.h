#pragma once
#include <pluginterfaces/vst/ivstcomponent.h>

namespace ostfstudio::scale {
inline constexpr char clipboardReturnPrefix[] = "OSTF Studio scale return 1 ";
inline constexpr Steinberg::TUID scoreInputId = INLINE_UID(0x936c3445,0xfa574289,0x8448c98d,0x68f70b51);
// Version 1, same process, called on the host message thread, borrowed bytes for this call only.
struct ScoreInput : Steinberg::FUnknown {
    virtual Steinberg::tresult PLUGIN_API offerScore(const char* utf8, Steinberg::int32 size) = 0;
};

// In-process message-thread channel scoped to the Studio instance and project.
// The receiver retains the writer; score bytes are borrowed only for the call.
struct ProjectWriter : Steinberg::FUnknown {
    virtual Steinberg::tresult PLUGIN_API applyScales(const char* baseHash, const char* score, Steinberg::int32 size) = 0;
};
inline constexpr Steinberg::TUID projectScalesId = INLINE_UID(0xce3c27f1,0x59ae4f15,0xb9c87f85,0xa24541ae);
struct ProjectScales : Steinberg::FUnknown {
    virtual Steinberg::tresult PLUGIN_API syncProject(ProjectWriter*, const char* score, Steinberg::int32 size) = 0;
};
}
