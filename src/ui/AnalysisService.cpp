#include "ui/AnalysisService.h"
#include <thread>
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <sys/stat.h>
#endif

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
    return tool.getExitCode() == 0
               ? juce::Result::ok()
               : juce::Result::fail(
                     TRANS("The analysis tool returned an error. Check the installation, account and connection."));
}
namespace {
// The CLI owns its OAuth flow and credentials. Give Studio its own profile on
// every invocation, without changing the environment of the DAW or its plugins.
juce::Result studioCodex(const juce::File& home, juce::StringArray args, int timeout, juce::String* status = nullptr,
                         const SignInLink& signIn = {}) {
    if (auto result = home.createDirectory(); result.failed()) return result;
    juce::MemoryOutputStream output;
    juce::String console;
    bool linkSent = false;
    auto received = [&](const char* bytes, int count) {
        if (status && output.getDataSize() + size_t(count) <= 16 * 1024 * 1024) output.write(bytes, size_t(count));
        if (!signIn) return;
        if (console.length() > 16384) console = console.getLastCharacters(4096);
        console += juce::String::fromUTF8(bytes, count);
        if (!signIn || linkSent) return;
        const auto start = console.indexOf("https://auth.openai.com/oauth/authorize?");
        if (start < 0) return;
        const auto tail = console.substring(start);
        const auto end = tail.indexOfAnyOf(" \t\r\n");
        if (end < 0) return;
        linkSent = true;
        signIn(juce::URL(tail.substring(0, end)));
    };
    args.insert(1, "-c");
    args.insert(2, "cli_auth_credentials_store=file");
    args.insert(3, "-c");
    args.insert(4, "forced_login_method=chatgpt");
#if JUCE_WINDOWS
    // Quote for CommandLineToArgvW, including trailing slashes and literal quotes.
    std::wstring command;
    for (const auto& arg : args) {
        if (!command.empty()) command += L' ';
        command += L'"';
        size_t slashes = 0;
        for (auto ch : std::wstring(arg.toWideCharPointer())) {
            if (ch == L'\\') {
                ++slashes;
                continue;
            }
            command.append(ch == L'"' ? 2 * slashes + 1 : slashes, L'\\');
            command += ch;
            slashes = 0;
        }
        command.append(2 * slashes, L'\\');
        command += L'"';
    }
    std::vector<std::wstring> entries;
    auto raw = GetEnvironmentStringsW();
    if (!raw) return juce::Result::fail(TRANS("Could not start the analysis tool."));
    for (auto p = raw; *p; p += wcslen(p) + 1)
        if (_wcsnicmp(p, L"CODEX_HOME=", 11) != 0) entries.emplace_back(p);
    FreeEnvironmentStringsW(raw);
    entries.emplace_back(std::wstring(L"CODEX_HOME=") + home.getFullPathName().toWideCharPointer());
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> environment;
    for (const auto& entry : entries)
        environment.insert(environment.end(), entry.c_str(), entry.c_str() + entry.size() + 1);
    environment.push_back(0);
    const bool capture = status || signIn;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE reader = nullptr, log = INVALID_HANDLE_VALUE;
    if (capture) {
        if (!CreatePipe(&reader, &log, &security, 0))
            return juce::Result::fail(TRANS("Could not start the analysis tool."));
        SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
    } else {
        log = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE) return juce::Result::fail(TRANS("Could not start the analysis tool."));
    }
    const juce::ScopeGuard closeReader([&] {
        if (reader) CloseHandle(reader);
    });
    auto drain = [&] {
        DWORD available = 0, count = 0;
        char data[2048];
        while (reader && PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr) && available &&
               ReadFile(reader, data, std::min<DWORD>(available, sizeof(data)), &count, nullptr) && count)
            received(data, int(count));
    };
    auto input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        CloseHandle(log);
        return juce::Result::fail(TRANS("Could not start the analysis tool."));
    }
    STARTUPINFOW startup{};
    startup.hStdInput = input;
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = log;
    PROCESS_INFORMATION process{};
    const auto started = CreateProcessW(args[0].toWideCharPointer(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, environment.data(),
                                        home.getFullPathName().toWideCharPointer(), &startup, &process);
    CloseHandle(input);
    CloseHandle(log);
    if (!started) return juce::Result::fail(TRANS("Could not start the analysis tool."));
    CloseHandle(process.hThread);
    auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeout;
    while (WaitForSingleObject(process.hProcess, 100) == WAIT_TIMEOUT) {
        drain();
        if ((job && job->shouldExit()) || juce::Time::getMillisecondCounterHiRes() >= deadline) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 2000);
            CloseHandle(process.hProcess);
            return juce::Result::fail(TRANS("Analysis cancelled or timed out."));
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    drain();
    if (status) *status = output.toUTF8();
#else
    if (::chmod(home.getFullPathName().toRawUTF8(), 0700) != 0)
        return juce::Result::fail(TRANS("Could not prepare the analysis."));
    args.insert(0, "CODEX_HOME=" + home.getFullPathName());
    args.insert(0, "/usr/bin/env");
    if (!status && !signIn) return runAnalysisTool(args, timeout);
    juce::ChildProcess probe;
    if (!probe.start(args)) return juce::Result::fail(TRANS("Could not start the analysis tool."));
    std::thread readOutput([&] {
        char c;
        while (probe.readProcessOutput(&c, 1) == 1)
            received(&c, 1);
    });
    auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeout;
    bool cancelled = false;
    while (!probe.waitForProcessToFinish(100)) {
        if ((job && job->shouldExit()) || juce::Time::getMillisecondCounterHiRes() >= deadline) {
            probe.kill();
            cancelled = true;
            break;
        }
    }
    readOutput.join();
    if (cancelled) return juce::Result::fail(TRANS("Analysis cancelled or timed out."));
    if (status) *status = output.toUTF8();
    const auto code = probe.getExitCode();
#endif
    return code == 0 ? juce::Result::ok()
                     : juce::Result::fail(TRANS(
                           "The analysis tool returned an error. Check the installation, account and connection."));
}

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
    const juce::File executable, home;
    const juce::String model;
public:
    ChatGpt(juce::File cli, juce::File profile, juce::String selected)
        : executable(std::move(cli)), home(std::move(profile)), model(std::move(selected)) {}
    juce::StringArray availableModels() const override {
        auto visible = [](const juce::var& catalog) {
            juce::StringArray result;
            if (const auto* models = catalog["models"].getArray())
                for (const auto& entry : *models)
                    if (entry["visibility"].toString() == "list" && entry["slug"].toString().isNotEmpty())
                        result.addIfNotAlreadyThere(entry["slug"].toString());
            return result;
        };
        auto models = visible(juce::JSON::parse(home.getChildFile("models_cache.json")));
        if (!models.isEmpty()) return models;
        juce::String json;
        if (executable.existsAsFile() && studioCodex(home, {executable.getFullPathName(), "debug", "models"}, 20000, &json).wasOk()) {
            const int begin = json.indexOfChar('{'), end = json.lastIndexOfChar('}');
            if (begin >= 0 && end > begin) models = visible(juce::JSON::parse(json.substring(begin, end + 1)));
        }
        return models;
    }
    juce::String name() const override { return "ChatGPT"; }
    bool connected() const override {
        auto& cli = executable;
        juce::String status;
        return cli.existsAsFile() && studioCodex(home,{cli.getFullPathName(), "login", "status"}, 6000, &status).wasOk()
            && status.containsIgnoreCase("using ChatGPT");
    }
    juce::Result disconnect() const override {
        const auto& cli = executable;
        if (!cli.existsAsFile()) return juce::Result::fail(TRANS("Install the official Codex CLI to connect ChatGPT."));
        return studioCodex(home,{cli.getFullPathName(),"logout"},10000);
    }
    juce::Result connect(const SignInLink& link) const override {
        const auto& cli = executable;
        if (!cli.existsAsFile()) return juce::Result::fail(TRANS("Install the official Codex CLI to connect ChatGPT."));
        auto r = studioCodex(home,{cli.getFullPathName(), "login"}, 300000,nullptr,link);
        if (r.failed()) return r;
        return connected() ? juce::Result::ok() : juce::Result::fail(TRANS("ChatGPT browser sign-in was not completed."));
    }
    juce::Result request(const juce::String& prompt, const juce::String& schema,
                         const juce::File& image, juce::var& response) const override {
        const auto& cli = executable;
        if (!connected()) return juce::Result::fail(TRANS("Reconnect ChatGPT before starting AI analysis."));
        AnalysisWorkspace work;
        if (auto r = work.dir.createDirectory(); r.failed()) return r;
        const auto shape = work.dir.getChildFile("proposal.schema.json"), reply = work.dir.getChildFile("proposal.json");
        if (!shape.replaceWithText(schema)) return juce::Result::fail(TRANS("Could not prepare the analysis."));
        juce::StringArray args{cli.getFullPathName(), "exec", "--ignore-user-config", "--ignore-rules", "--skip-git-repo-check",
            "--ephemeral", "--sandbox", "read-only", "--disable", "shell_tool", "-c", "web_search=disabled",
            "-c", "apps._default.enabled=false", "-c", "forced_login_method=chatgpt", "-c", "approval_policy=never", "-C", work.dir.getFullPathName(),
            "--output-schema", shape.getFullPathName(), "-o", reply.getFullPathName()};
        if (model.isNotEmpty()) { args.add("--model"); args.add(model); }
        if (image.existsAsFile()) { args.add("--image"); args.add(image.getFullPathName()); }
        args.add("--");
        args.add(prompt);
        if (auto r = studioCodex(home,args, 300000); r.failed()) return r;
        if (!reply.existsAsFile() || reply.getSize() > 2 * 1024 * 1024 || juce::JSON::parse(reply.loadFileAsString(), response).failed())
            return juce::Result::fail(TRANS("Invalid musical proposal from AI."));
        return juce::Result::ok();
    }
};
} // namespace
AnalysisConnection chatGptProvider(juce::File executable, juce::File profile, juce::String model) {
    if (executable == juce::File{}) executable = codexExecutable();
    if (profile == juce::File{}) profile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("OSTF Studio/ChatGPT");
    return std::make_shared<ChatGpt>(std::move(executable), std::move(profile), std::move(model));
}
}
