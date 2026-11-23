#include "ui/AnalysisService.h"

namespace ostfstudio {
juce::File analysisTool(const juce::String& name, const juce::StringArray& installPaths) {
    for (const auto& path : installPaths)
        if (juce::File::isAbsolutePath(path) && juce::File(path).existsAsFile()) return juce::File(path);
#if JUCE_WINDOWS
    auto exe = name + ".exe";
    const auto separator = ";";
#else
    auto exe = name;
    const auto separator = ":";
#endif
    auto paths = juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}), separator, {});
#if !JUCE_WINDOWS
    paths.addArray({"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"});
#endif
    for (auto path : paths) {
        path = path.trim().unquoted();
        if (!juce::File::isAbsolutePath(path)) continue;
        const auto candidate = juce::File(path).getChildFile(exe);
        if (candidate.existsAsFile()) return candidate;
    }
    return {};
}
juce::Result runAnalysisTool(const juce::StringArray& args, int timeoutMs) {
    juce::ChildProcess tool;
    if (!tool.start(args, 0)) return juce::Result::fail(TRANS("Could not start the analysis tool."));
    auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (!tool.waitForProcessToFinish(100)) {
        if ((job && job->shouldExit()) || juce::Time::getMillisecondCounterHiRes() >= deadline) {
            tool.kill();
            tool.waitForProcessToFinish(2000);
            return juce::Result::fail(TRANS("Analysis cancelled or timed out."));
        }
    }
    return tool.getExitCode() == 0 ? juce::Result::ok() : juce::Result::fail(
        TRANS("The analysis tool returned an error. Check the installation, account and connection."));
}
namespace {
juce::File codexExecutable() {
    auto file = analysisTool("codex");
#if JUCE_WINDOWS
    if (!file.existsAsFile()) {
        auto base = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
        if (juce::File::isAbsolutePath(base)) {
            auto versions = juce::File(base).getChildFile("OpenAI/Codex/bin").findChildFiles(juce::File::findDirectories, false);
            std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) {
                return a.getLastModificationTime() > b.getLastModificationTime();
            });
            for (auto& version : versions)
                if ((file = version.getChildFile("codex.exe")).existsAsFile()) break;
        }
    }
#endif
    return file;
}
class ChatGpt final : public AnalysisProvider {
public:
    juce::String name() const override { return "ChatGPT"; }
    bool connected() const override {
        auto cli = codexExecutable();
        juce::ChildProcess probe;
        if (!cli.existsAsFile() || !probe.start({cli.getFullPathName(), "login", "status"})) return false;
        if (!probe.waitForProcessToFinish(6000)) { probe.kill(); return false; }
        return probe.getExitCode() == 0 && probe.readAllProcessOutput().containsIgnoreCase("using ChatGPT");
    }
    juce::Result connect() const override {
        const auto cli = codexExecutable();
        if (!cli.existsAsFile()) return juce::Result::fail(TRANS("Install the official Codex CLI to connect ChatGPT."));
        if (connected()) return juce::Result::ok();
        auto r = runAnalysisTool({cli.getFullPathName(), "login"}, 300000);
        if (r.failed()) return r;
        return connected() ? juce::Result::ok() : juce::Result::fail(TRANS("ChatGPT browser sign-in was not completed."));
    }
    juce::Result request(const juce::String& prompt, const juce::String& schema,
                         const juce::File& image, juce::var& response) const override {
        const auto cli = codexExecutable();
        if (!connected()) return juce::Result::fail(TRANS("Reconnect ChatGPT before starting AI analysis."));
        AnalysisWorkspace work;
        if (auto r = work.dir.createDirectory(); r.failed()) return r;
        const auto shape = work.dir.getChildFile("proposal.schema.json"), reply = work.dir.getChildFile("proposal.json");
        if (!shape.replaceWithText(schema)) return juce::Result::fail(TRANS("Could not prepare the analysis."));
        juce::StringArray args{cli.getFullPathName(), "exec", "--ignore-user-config", "--ignore-rules", "--skip-git-repo-check",
            "--ephemeral", "--sandbox", "read-only", "--disable", "shell_tool", "-c", "web_search=disabled",
            "-c", "apps._default.enabled=false", "-c", "forced_login_method=chatgpt", "-c", "approval_policy=never", "-C", work.dir.getFullPathName(),
            "--output-schema", shape.getFullPathName(), "-o", reply.getFullPathName()};
        if (image.existsAsFile()) { args.add("--image"); args.add(image.getFullPathName()); }
        args.add("--");
        args.add(prompt);
        if (auto r = runAnalysisTool(args, 300000); r.failed()) return r;
        if (!reply.existsAsFile() || reply.getSize() > 2 * 1024 * 1024 || juce::JSON::parse(reply.loadFileAsString(), response).failed())
            return juce::Result::fail(TRANS("Invalid musical proposal from AI."));
        return juce::Result::ok();
    }
};
}
AnalysisConnection chatGptProvider() { return std::make_shared<ChatGpt>(); }
}
