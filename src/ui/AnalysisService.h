#pragma once
#include <juce_core/juce_core.h>

namespace ostfstudio {
// All operations run off the message and audio threads. Credentials belong to the provider.
enum class AiStudioMode { arrangement, lyric, vocal };
using SignInLink = std::function<void(const juce::URL&)>;
class AnalysisProvider {
public:
    virtual ~AnalysisProvider() = default;
    virtual juce::StringArray availableModels(bool refresh = false, juce::StringArray* labels = nullptr) const { return {}; }
    virtual juce::String name() const = 0;
    virtual bool connected() const = 0;
    virtual juce::Result connect(const SignInLink& = {}) const = 0;
    virtual juce::Result disconnect() const = 0;
    virtual juce::Result request(const juce::String& prompt, const juce::String& schema,
                                 const juce::File& image, juce::var& response) const = 0;
};
using AnalysisConnection = std::shared_ptr<const AnalysisProvider>;
AnalysisConnection chatGptProvider(juce::File executable = {}, juce::File profile = {}, juce::String model = {}, AiStudioMode mode = AiStudioMode::arrangement);
#if JUCE_WINDOWS && defined(_MSC_VER)
void runWindowsScorePageRenderer(const juce::File& source, const juce::File& directory, int pageIndex);
#endif
juce::Result readScorePages(const juce::File&, const juce::File& directory,
                            const std::function<juce::Result(const juce::File&)>&);
juce::File analysisTool(const juce::String& name, const juce::StringArray& installPaths = {});
juce::Result runAnalysisTool(const juce::StringArray& args, int timeoutMs);
struct AnalysisWorkspace {
    const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("ostf-analysis-" + juce::Uuid().toString());
    ~AnalysisWorkspace() { dir.deleteRecursively(); }
};
}
