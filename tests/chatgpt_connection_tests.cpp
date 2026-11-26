#include "ui/AnalysisService.h"
#include <iostream>

int main(int argc, char **argv) {
    using namespace ostfstudio;
    const juce::String home = juce::SystemStats::getEnvironmentVariable("CODEX_HOME", {});
    if (argc > 1) {
        // This executable doubles as the CLI. Only accept the disposable profile
        // created by the parent test and the expected credential-store overrides.
        const juce::StringArray args(argv + 1, argc - 1);
        if (!juce::File::isAbsolutePath(home) || !juce::File(home).getFileName().startsWith("ostf-login-test-") ||
            !args.contains("cli_auth_credentials_store=file") || !args.contains("forced_login_method=chatgpt"))
            return 11;
        auto state = juce::File(home).getChildFile("fixture-state");
        if (args.contains("debug") && args.contains("models")) {
            std::cerr << "{catalog diagnostic}\\n";
            std::cout << R"({"models":[{"slug":"gpt-6.1-sol","display_name":"GPT-6.1-Sol","visibility":"list"},{"slug":"test-score","visibility":"list"},{"slug":"internal","visibility":"hide"}]})";
            return 0;
        }
        if (args.contains("exec")) {
            if (args[args.indexOf("--model") + 1] != "test-score" || !args[args.size() - 1].contains("AI Vocal Studio")) return 15;
            return juce::File(args[args.indexOf("-o")+1]).replaceWithText("{}") ? 0 : 16;
        }
        if (args.contains("logout"))
            return state.deleteFile() ? 0 : 12;
        if (!args.contains("login"))
            return 13;
        if (args.contains("status")) {
            if (!state.existsAsFile())
                return 1;
            std::cerr << "Logged in using ChatGPT\n";
            return 0;
        }
        std::cerr << "https://auth.openai.com/oauth/authorize?state=test\n";
        return state.appendText("x") ? 0 : 14;
    }
    const auto profile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("ostf-login-test-" + juce::Uuid().toString());
    juce::ScopeGuard cleanup{[&] { profile.deleteRecursively(); }};
    const auto service = chatGptProvider(juce::File::getSpecialLocation(juce::File::currentExecutableFile), profile, "test-score", AiStudioMode::vocal);
    int linkCount = 0;
    auto opened = [&](const juce::URL &url) {
        if (url.toString(true) == "https://auth.openai.com/oauth/authorize?state=test")
            ++linkCount;
    };
    if (service->connected() || service->connect(opened).failed() || !service->connected())
        return 1;
    if (service->connect(opened).failed() || linkCount != 2 || profile.getChildFile("fixture-state").getSize() != 2)
        return 2;
    if (!profile.getChildFile("models_cache.json").replaceWithText(R"({"models":[{"slug":"cached","visibility":"list"}]})")
        || service->availableModels() != juce::StringArray{"cached"}) return 6;
    juce::var response;
    juce::StringArray labels;
    if (service->availableModels(true, &labels) != juce::StringArray{"gpt-6.1-sol", "test-score"} ||
        labels != juce::StringArray{"GPT-6.1-Sol", "test-score"} ||
        service->request("example", "{}", {}, response).failed()) return 5;
    if (service->disconnect().failed() || service->connected())
        return 3;
    return juce::SystemStats::getEnvironmentVariable("CODEX_HOME", {}) == home ? 0 : 4;
}
