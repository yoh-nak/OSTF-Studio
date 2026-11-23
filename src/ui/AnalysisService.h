#pragma once
#include <juce_core/juce_core.h>

namespace ostfstudio {
// All operations run off the message and audio threads. Credentials belong to the provider.
class AnalysisProvider {
public:
    virtual ~AnalysisProvider() = default;
    virtual juce::String name() const = 0;
    virtual bool connected() const = 0;
    virtual juce::Result connect() const = 0;
    virtual juce::Result request(const juce::String& prompt, const juce::String& schema,
                                 const juce::File& image, juce::var& response) const = 0;
};
using AnalysisConnection = std::shared_ptr<const AnalysisProvider>;
AnalysisConnection chatGptProvider();
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
