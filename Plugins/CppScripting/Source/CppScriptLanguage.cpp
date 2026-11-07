#include "HephaestusScript.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;

namespace {

constexpr const char* kCacheFolder = ".hephaestus/build/cpp-scripts";
constexpr const char* kBuildKey = __DATE__ " " __TIME__;

struct Module {
    HMODULE handle = nullptr;
    const HPCppScriptExports* exports = nullptr;
    int references = 0;
};

struct Runtime {
    const HPScriptHostAPI* host = nullptr;
    fs::path projectRoot;
    std::map<fs::path, Module> modules;
};

struct Instance {
    Runtime* runtime = nullptr;
    fs::path dll;
    Module* module = nullptr;
    void* script = nullptr;
    std::string scriptPath;
    bool faulted = false;
};

void HostLog(const HPScriptHostAPI* host, uint32_t level, const std::string& message) {
    if (host && host->log) {
        host->log(host->host, level, message.c_str());
    }
}

void WriteError(char* error, size_t errorSize, const std::string& message) {
    if (error && errorSize > 0) {
        std::snprintf(error, errorSize, "%s", message.c_str());
    }
}

std::optional<std::string> ReadText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

uint64_t Fnv1a(std::string_view text, uint64_t hash = 14695981039346656037ull) {
    for (unsigned char c : text) {
        hash = (hash ^ c) * 1099511628211ull;
    }
    return hash;
}

std::string Hex(uint64_t value, int digits) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer + 16 - digits);
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string CacheStem(const fs::path& projectRoot, const fs::path& source) {
    const std::string relative = Lower(source.lexically_normal().lexically_relative(projectRoot).generic_string());
    return source.stem().string() + "-" + Hex(Fnv1a(relative), 8);
}

std::string CacheName(const fs::path& projectRoot, const fs::path& source, const std::string& text) {
    return CacheStem(projectRoot, source) + "-" + Hex(Fnv1a(text, Fnv1a(kBuildKey)), 16) + ".dll";
}

bool DeclaresScript(const std::string& text) {
    return text.find("HP_SCRIPT(") != std::string::npos;
}

fs::path ModuleFolder() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleFolder), &self);
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    return fs::path(path).parent_path();
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Wide(const std::string& text, UINT codePage = CP_UTF8) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

int Run(const std::wstring& application, std::wstring commandLine, const std::wstring* environment,
        std::string& output) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        application.empty() ? nullptr : application.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment ? const_cast<wchar_t*>(environment->c_str()) : nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!started) {
        CloseHandle(readPipe);
        return -1;
    }
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, read);
    }
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return static_cast<int>(exitCode);
}

std::wstring Quote(const fs::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

struct Compiler {
    fs::path cl;
    std::wstring environment;
    std::string problem;
};

// Captures the environment vcvars64.bat sets up once per process; running it costs a second or more.
const Compiler& FindCompiler() {
    static std::once_flag once;
    static Compiler compiler;
    std::call_once(once, [] {
        wchar_t programFiles[MAX_PATH] = {};
        if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles, MAX_PATH)) {
            compiler.problem = "Program Files (x86) is not set, so Visual Studio cannot be found";
            return;
        }
        const fs::path vswhere = fs::path(programFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        std::string installation;
        if (!fs::exists(vswhere) ||
            Run(vswhere.wstring(),
                Quote(vswhere) + L" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
                                 L" -property installationPath -utf8",
                nullptr, installation) != 0) {
            compiler.problem = "Visual Studio with the \"Desktop development with C++\" workload was not found. "
                               "Install it (the free Community edition works) to compile C++ scripts";
            return;
        }
        installation.erase(installation.find_last_not_of("\r\n ") + 1);
        const fs::path vcvars = fs::path(Wide(installation)) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
        if (installation.empty() || !fs::exists(vcvars)) {
            compiler.problem = "Visual Studio was found but its C++ tools are missing (" + installation + ")";
            return;
        }
        wchar_t comspec[MAX_PATH] = {};
        GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
        std::string variables;
        if (Run(comspec, L"cmd.exe /d /u /s /c \"" + Quote(vcvars) + L" >nul 2>nul && set\"", nullptr, variables) != 0) {
            compiler.problem = "vcvars64.bat failed, so the C++ compiler environment could not be set up";
            return;
        }
        std::wstring text(reinterpret_cast<const wchar_t*>(variables.data()), variables.size() / sizeof(wchar_t));
        std::wstringstream lines(text);
        std::wstring path;
        for (std::wstring line; std::getline(lines, line);) {
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            const size_t equals = line.find(L'=');
            if (line.empty() || equals == std::wstring::npos || equals == 0) {
                continue;
            }
            if (_wcsnicmp(line.c_str(), L"PATH=", 5) == 0) {
                path = line.substr(5);
            }
            compiler.environment += line;
            compiler.environment.push_back(L'\0');
        }
        compiler.environment.push_back(L'\0');
        std::wstringstream folders(path);
        for (std::wstring folder; std::getline(folders, folder, L';');) {
            if (!folder.empty() && fs::exists(fs::path(folder) / "cl.exe")) {
                compiler.cl = fs::path(folder) / "cl.exe";
                break;
            }
        }
        if (compiler.cl.empty()) {
            compiler.problem = "cl.exe was not on the Visual Studio developer PATH";
        }
    });
    return compiler;
}

// MSVC writes "C:\path\file.cpp(12): error ..."; the editor Console links "file:12: ...".
std::vector<std::string> Diagnostics(const std::string& output, const fs::path& source,
                                     const std::string& displayPath) {
    std::vector<std::string> lines;
    std::istringstream stream(output);
    const std::string sourceText = Lower(source.string());
    const std::string fileName = Lower(source.filename().string());
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || Lower(line) == fileName) {
            continue;
        }
        const size_t open = line.find('(');
        const size_t close = open == std::string::npos ? std::string::npos : line.find("): ", open);
        if (close != std::string::npos) {
            std::string file = line.substr(0, open);
            std::string position = line.substr(open + 1, close - open - 1);
            const size_t comma = position.find(',');
            if (comma != std::string::npos) {
                position.resize(comma);
            }
            if (!position.empty() && std::all_of(position.begin(), position.end(),
                                                [](unsigned char c) { return std::isdigit(c) != 0; })) {
                if (Lower(file) == sourceText) {
                    file = displayPath;
                }
                line = file + ":" + position + ": " + line.substr(close + 3);
            }
        }
        lines.push_back(line);
    }
    return lines;
}

void PruneOlderBuilds(const fs::path& folder, const std::string& stem, const fs::path& keep) {
    const std::string kept = keep.stem().string();
    for (const fs::path& directory : {folder, folder / "obj"}) {
        std::error_code ec;
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (it->is_regular_file(ec) && name.rfind(stem + "-", 0) == 0 && name.rfind(kept + ".", 0) != 0) {
                std::error_code locked;
                fs::remove(it->path(), locked);
            }
        }
    }
}

template <class Report>
bool Compile(const fs::path& source, const fs::path& dll, const std::string& stem, const std::string& displayPath,
             std::string& error, Report&& report) {
    const Compiler& compiler = FindCompiler();
    if (!compiler.problem.empty()) {
        error = compiler.problem;
        return false;
    }
    std::error_code ec;
    const fs::path folder = dll.parent_path();
    const fs::path intermediate = folder / "obj";
    fs::create_directories(intermediate, ec);
    if (ec) {
        error = "cannot create " + intermediate.string() + ": " + ec.message();
        return false;
    }
    const fs::path include = ModuleFolder().parent_path() / "Include";
    const fs::path building = folder / (dll.stem().wstring() + L".building.dll");
    const fs::path pdb = folder / (dll.stem().wstring() + L".pdb");
    const std::wstring command =
        Quote(compiler.cl) +
        L" /nologo /std:c++20 /EHsc /MD /O2 /Zi /W3 /utf-8 /DNDEBUG /LD /diagnostics:classic" +
        L" /I" + Quote(include) + L" /I" + Quote(source.parent_path()) +
        L" /Fo" + Quote(intermediate / (dll.stem().wstring() + L".obj")) +
        L" /Fd" + Quote(intermediate / (dll.stem().wstring() + L".vc.pdb")) + L" " + Quote(source) +
        L" /link /NOLOGO /INCREMENTAL:NO /NOIMPLIB /NOEXP /OUT:" + Quote(building) + L" /PDB:" + Quote(pdb);
    std::string output;
    const int exitCode = Run(compiler.cl.wstring(), command, &compiler.environment, output);
    const auto messages = Diagnostics(output, source, displayPath);
    for (const std::string& message : messages) {
        report(message.find(": error") != std::string::npos || message.find(" error ") != std::string::npos
                   ? HP_LOG_ERROR : HP_LOG_WARNING,
               message);
    }
    if (exitCode != 0 || !fs::exists(building)) {
        fs::remove(building, ec);
        error = exitCode == -1 ? "cl.exe could not be started" : "failed to compile (see the messages above)";
        return false;
    }
    fs::rename(building, dll, ec);
    if (ec) {
        error = "cannot write " + dll.string() + ": " + ec.message();
        return false;
    }
    PruneOlderBuilds(folder, stem, dll);
    return true;
}

// Structured exceptions (access violations and the like) stop the script instead of the editor.
DWORD Guarded(void (*body)(void*), void* context) {
    __try {
        body(context);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

void Fault(Instance& instance, const char* callback, DWORD code) {
    instance.faulted = true;
    char message[512];
    std::snprintf(message, sizeof(message), "%s: crashed in %s (exception 0x%08lX). The script is stopped until "
                  "it changes or play restarts", instance.scriptPath.c_str(), callback, code);
    HostLog(instance.runtime->host, HP_LOG_ERROR, message);
}

void Release(Runtime& runtime, const fs::path& dll) {
    const auto it = runtime.modules.find(dll);
    if (it != runtime.modules.end() && --it->second.references == 0) {
        FreeLibrary(it->second.handle);
        runtime.modules.erase(it);
    }
}

Module* Acquire(Runtime& runtime, const fs::path& dll, std::string& error) {
    auto it = runtime.modules.find(dll);
    if (it != runtime.modules.end()) {
        ++it->second.references;
        return &it->second;
    }
    HMODULE handle = LoadLibraryExW(dll.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle) {
        error = "cannot load " + dll.string() + " (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    const auto get = reinterpret_cast<HPGetCppScriptFn>(GetProcAddress(handle, HP_CPP_SCRIPT_EXPORT_NAME));
    const HPCppScriptExports* exports = get ? get() : nullptr;
    if (!exports || exports->structSize < sizeof(HPCppScriptExports) ||
        exports->sdkVersion != HP_CPP_SCRIPT_SDK_VERSION) {
        FreeLibrary(handle);
        error = get ? "the script was built for another SDK version"
                    : "no HP_SCRIPT(...) in this file, so it has no script class";
        return nullptr;
    }
    Module& module = runtime.modules[dll];
    module = Module{handle, exports, 1};
    return &module;
}

fs::path ProjectRootOf(const HPScriptHostAPI* host) {
    if (!host || host->structSize < offsetof(HPScriptHostAPI, getProjectRoot) + sizeof(host->getProjectRoot) ||
        !host->getProjectRoot) {
        return {};
    }
    const size_t size = host->getProjectRoot(host->host, nullptr, 0);
    if (size <= 1) {
        return {};
    }
    std::string text(size, '\0');
    host->getProjectRoot(host->host, text.data(), text.size());
    text.resize(size - 1);
    return fs::path(text);
}


void* CreateRuntime(const HPScriptHostAPI* host) {
    if (!host || host->structSize < sizeof(HPScriptHostAPI)) {
        HostLog(host, HP_LOG_ERROR, "C++ scripts need the engine this plugin was built with; rebuild the plugin");
        return nullptr;
    }
    auto* runtime = new Runtime();
    runtime->host = host;
    runtime->projectRoot = ProjectRootOf(host);
    return runtime;
}

void DestroyRuntime(void* handle) {
    auto* runtime = static_cast<Runtime*>(handle);
    for (auto& [path, module] : runtime->modules) {
        FreeLibrary(module.handle);
    }
    delete runtime;
}

void* CreateInstance(void* handle, const HPScriptInstanceInfo* info, char* error, size_t errorSize) {
    auto& runtime = *static_cast<Runtime*>(handle);
    const fs::path source(info->resolvedPath ? info->resolvedPath : "");
    const std::string displayPath = info->scriptPath ? info->scriptPath : source.string();
    if (runtime.projectRoot.empty()) {
        WriteError(error, errorSize, "the project folder is unknown, so the compiled script has nowhere to go");
        return nullptr;
    }
    const auto text = ReadText(source);
    if (!text) {
        WriteError(error, errorSize, "cannot read " + source.string());
        return nullptr;
    }
    const fs::path dll = runtime.projectRoot / kCacheFolder / CacheName(runtime.projectRoot, source, *text);
    std::string message;
    if (!runtime.modules.count(dll) && !fs::exists(dll)) {
        HostLog(runtime.host, HP_LOG_INFO, "Compiling " + displayPath);
        const auto report = [&](uint32_t level, const std::string& line) { HostLog(runtime.host, level, line); };
        if (!Compile(source, dll, CacheStem(runtime.projectRoot, source), displayPath, message, report)) {
            WriteError(error, errorSize, FindCompiler().problem.empty()
                ? message : "no compiled copy of this script exists and it cannot be compiled here. " + message);
            return nullptr;
        }
    }
    Module* module = Acquire(runtime, dll, message);
    if (!module) {
        WriteError(error, errorSize, message);
        return nullptr;
    }
    auto instance = std::make_unique<Instance>();
    instance->runtime = &runtime;
    instance->dll = dll;
    instance->module = module;
    instance->scriptPath = displayPath;
    struct Context {
        const HPCppScriptExports* exports;
        const HPScriptHostAPI* host;
        const HPScriptInstanceInfo* info;
        char* error;
        size_t errorSize;
        void* script;
    } context{module->exports, runtime.host, info, error, errorSize, nullptr};
    const DWORD code = Guarded([](void* c) {
        auto& ctx = *static_cast<Context*>(c);
        ctx.script = ctx.exports->create(ctx.host, ctx.info, ctx.error, ctx.errorSize);
    }, &context);
    if (code != 0 || !context.script) {
        if (code != 0) {
            std::snprintf(error, errorSize, "crashed while starting (exception 0x%08lX)", code);
        } else if (!error[0]) {
            WriteError(error, errorSize, "the script could not be created");
        }
        Release(runtime, dll);
        return nullptr;
    }
    instance->script = context.script;
    return instance.release();
}

void DestroyInstance(void* runtime, void* handle) {
    (void)runtime;
    auto* instance = static_cast<Instance*>(handle);
    if (!instance->faulted) {
        struct Context {
            const HPCppScriptExports* exports;
            void* script;
        } context{instance->module->exports, instance->script};
        const DWORD code = Guarded([](void* c) {
            auto& ctx = *static_cast<Context*>(c);
            ctx.exports->destroy(ctx.script);
        }, &context);
        if (code != 0) {
            Fault(*instance, "OnDestroy", code);
        }
    }
    Release(*instance->runtime, instance->dll);
    delete instance;
}

struct CallContext {
    const HPCppScriptExports* exports;
    void* script;
    float deltaSeconds;
    const HPContactEvent* event;
    const char* function;
    const HPValue* args;
    uint32_t argCount;
    int32_t result;
};

void Dispatch(void* handle, const char* callback, void (*body)(void*), CallContext& context) {
    auto& instance = *static_cast<Instance*>(handle);
    if (instance.faulted) {
        return;
    }
    context.exports = instance.module->exports;
    context.script = instance.script;
    if (const DWORD code = Guarded(body, &context)) {
        Fault(instance, callback, code);
    }
}

void Awake(void*, void* handle) {
    CallContext context{};
    Dispatch(handle, "OnAwake", [](void* c) {
        auto& ctx = *static_cast<CallContext*>(c);
        ctx.exports->awake(ctx.script);
    }, context);
}

void Update(void*, void* handle, float deltaSeconds) {
    CallContext context{};
    context.deltaSeconds = deltaSeconds;
    Dispatch(handle, "OnUpdate", [](void* c) {
        auto& ctx = *static_cast<CallContext*>(c);
        ctx.exports->update(ctx.script, ctx.deltaSeconds);
    }, context);
}

void LateUpdate(void*, void* handle, float deltaSeconds) {
    CallContext context{};
    context.deltaSeconds = deltaSeconds;
    Dispatch(handle, "OnLateUpdate", [](void* c) {
        auto& ctx = *static_cast<CallContext*>(c);
        ctx.exports->lateUpdate(ctx.script, ctx.deltaSeconds);
    }, context);
}

void ContactCallback(void*, void* handle, const HPContactEvent* event) {
    CallContext context{};
    context.event = event;
    Dispatch(handle, "a physics callback", [](void* c) {
        auto& ctx = *static_cast<CallContext*>(c);
        ctx.exports->contact(ctx.script, ctx.event);
    }, context);
}

int32_t Call(void*, void* handle, const char* function, const HPValue* args, uint32_t argCount) {
    CallContext context{};
    context.function = function;
    context.args = args;
    context.argCount = argCount;
    Dispatch(handle, function, [](void* c) {
        auto& ctx = *static_cast<CallContext*>(c);
        ctx.result = ctx.exports->call(ctx.script, ctx.function, ctx.args, ctx.argCount);
    }, context);
    return context.result;
}


std::vector<std::string> SplitArguments(std::string_view text) {
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            current.push_back(c);
            if (c == '\\' && i + 1 < text.size()) {
                current.push_back(text[++i]);
            } else if (c == '"') {
                quoted = false;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
        } else if (c == '(' || c == '{' || c == '<') {
            ++depth;
        } else if (c == ')' || c == '}' || c == '>') {
            --depth;
        } else if (c == ',' && depth == 0) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    parts.push_back(current);
    for (std::string& part : parts) {
        const size_t first = part.find_first_not_of(" \t\r\n");
        const size_t last = part.find_last_not_of(" \t\r\n");
        part = first == std::string::npos ? std::string() : part.substr(first, last - first + 1);
    }
    return parts;
}

std::optional<double> ParseNumber(std::string text) {
    while (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'u' ||
                             text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) {
        text.pop_back();
    }
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end && *end == '\0' ? std::optional<double>(value) : std::nullopt;
}

std::optional<std::string> ParseString(const std::string& text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
        return std::nullopt;
    }
    std::string out;
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        char c = text[i];
        if (c == '\\' && i + 2 < text.size()) {
            c = text[++i];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        out.push_back(c);
    }
    return out;
}

std::string StripNamespace(const std::string& type) {
    const size_t colon = type.rfind("::");
    return colon == std::string::npos ? type : type.substr(colon + 2);
}

bool FieldValue(const std::string& type, const std::vector<std::string>& values, HPValue& out,
                std::string& stringStorage) {
    const std::string kind = StripNamespace(type);
    out = HPValue{};
    if (kind == "bool") {
        if (values.size() != 1 || (values[0] != "true" && values[0] != "false")) return false;
        out.type = HP_VALUE_BOOL;
        out.boolean = values[0] == "true";
        return true;
    }
    if (kind == "string") {
        const auto text = values.size() == 1 ? ParseString(values[0]) : std::nullopt;
        if (!text) return false;
        stringStorage = *text;
        out.type = HP_VALUE_STRING;
        out.string = stringStorage.c_str();
        return true;
    }
    const size_t count = kind == "Vec2" ? 2 : kind == "Vec3" ? 3 : (kind == "Vec4" || kind == "Color") ? 4 : 1;
    if (count == 1) {
        static const char* kNumbers[] = {"float", "double", "int", "int32_t", "int64_t", "uint32_t", "unsigned",
                                         "long", "short"};
        if (std::none_of(std::begin(kNumbers), std::end(kNumbers), [&](const char* n) { return kind == n; })) {
            return false;
        }
        const auto number = values.size() == 1 ? ParseNumber(values[0]) : std::nullopt;
        if (!number) return false;
        out.type = HP_VALUE_NUMBER;
        out.number = *number;
        return true;
    }
    if (values.size() != count) return false;
    for (size_t i = 0; i < count; ++i) {
        const auto number = ParseNumber(values[i]);
        if (!number) return false;
        out.vector[i] = static_cast<float>(*number);
    }
    out.type = count == 2 ? HP_VALUE_VEC2 : count == 3 ? HP_VALUE_VEC3 : HP_VALUE_VEC4;
    return true;
}

int32_t DescribeFields(const char* resolvedPath, HPFieldCallback callback, void* user) {
    const auto text = ReadText(fs::path(resolvedPath ? resolvedPath : ""));
    if (!text) {
        return 0;
    }
    std::istringstream lines(*text);
    bool inBlockComment = false;
    for (std::string line; std::getline(lines, line);) {
        if (inBlockComment) {
            const size_t end = line.find("*/");
            if (end == std::string::npos) continue;
            line.erase(0, end + 2);
            inBlockComment = false;
        }
        const size_t blockStart = line.find("/*");
        if (blockStart != std::string::npos && line.find("*/", blockStart) == std::string::npos) {
            inBlockComment = true;
            line.resize(blockStart);
        }
        const size_t at = line.find("HP_FIELD(");
        const size_t comment = line.find("//");
        if (at == std::string::npos || (comment != std::string::npos && comment < at) ||
            line.find("#define") != std::string::npos) {
            continue;
        }
        const size_t close = line.rfind(')');
        if (close == std::string::npos || close < at) continue;
        const auto parts = SplitArguments(std::string_view(line).substr(at + 9, close - at - 9));
        if (parts.size() < 3 || parts[1].empty()) continue;
        HPValue value{};
        std::string storage;
        if (FieldValue(parts[0], std::vector<std::string>(parts.begin() + 2, parts.end()), value, storage)) {
            callback(user, parts[1].c_str(), value);
        }
    }
    return 1;
}


int32_t PackageScript(const char* projectRoot, const char* scriptPath, const char* packageRoot, char* error,
                      size_t errorSize) {
    const fs::path root(projectRoot);
    const fs::path source(scriptPath);
    const auto text = ReadText(source);
    if (!text) {
        WriteError(error, errorSize, "cannot read " + source.string());
        return 0;
    }
    if (!DeclaresScript(*text)) {
        return 1;
    }
    const std::string name = CacheName(root, source, *text);
    const fs::path dll = root / kCacheFolder / name;
    std::string message;
    std::string diagnostics;
    const std::string displayPath = source.lexically_normal().lexically_relative(root).generic_string();
    if (!fs::exists(dll)) {
        const auto report = [&](uint32_t, const std::string& line) { diagnostics += line + "\n"; };
        if (!Compile(source, dll, CacheStem(root, source), displayPath, message, report)) {
            WriteError(error, errorSize, diagnostics + message);
            return 0;
        }
    }
    std::error_code ec;
    const fs::path target = fs::path(packageRoot) / kCacheFolder / name;
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(dll, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        WriteError(error, errorSize, "cannot copy " + dll.string() + ": " + ec.message());
        return 0;
    }
    return 1;
}


const HPSyntaxRule kRules[] = {
    {HP_SYNTAX_COMMENT, R"(//.*)"},
    {HP_SYNTAX_STRING, R"("(?:[^"\\]|\\.)*"?|'(?:[^'\\]|\\.)*'?)"},
    {HP_SYNTAX_KEYWORD, R"(^\s*#\s*\w+)"},
    {HP_SYNTAX_KEYWORD,
     R"(\b(?:alignas|auto|break|case|catch|class|const|constexpr|consteval|continue|co_await|co_return|co_yield|decltype|default|delete|do|else|enum|explicit|export|extern|false|final|for|friend|goto|if|inline|mutable|namespace|new|noexcept|nullptr|operator|override|private|protected|public|return|sizeof|static|static_assert|static_cast|dynamic_cast|reinterpret_cast|const_cast|struct|switch|template|this|throw|true|try|typedef|typename|union|using|virtual|volatile|while)\b)"},
    {HP_SYNTAX_TYPE,
     R"(\b(?:bool|char|double|float|int|long|short|signed|unsigned|void|size_t|u?int(?:8|16|32|64)_t|std::\w+|hp::\w+|[A-Z]\w*)\b)"},
    {HP_SYNTAX_FUNCTION, R"(\b[A-Za-z_]\w*(?=\s*\())"},
    {HP_SYNTAX_NUMBER, R"(\b(?:0[xX][0-9A-Fa-f']+|\d[\d']*(?:\.\d*)?(?:[eE][+-]?\d+)?)[uUlLfF]*\b)"},
};

const HPSyntaxBlock kBlocks[] = {
    {HP_SYNTAX_COMMENT, R"(/\*)", R"(\*/)"},
    {HP_SYNTAX_STRING, R"re(R"\()re", R"re(\)")re"},
};

const HPSyntax kSyntax = {kRules, static_cast<uint32_t>(std::size(kRules)), kBlocks,
                          static_cast<uint32_t>(std::size(kBlocks)), "//"};

struct ApiEntry {
    const char* label;
    uint32_t kind;
    const char* detail;
    const char* doc;
};

const ApiEntry kApi[] = {
    {"OnAwake", HP_COMPLETION_FUNCTION, "void OnAwake() override", "Called once, before the script's first update."},
    {"OnUpdate", HP_COMPLETION_FUNCTION, "void OnUpdate(float deltaSeconds) override", "Called every frame."},
    {"OnLateUpdate", HP_COMPLETION_FUNCTION, "void OnLateUpdate(float deltaSeconds) override",
     "Called every frame after every script's OnUpdate."},
    {"OnDestroy", HP_COMPLETION_FUNCTION, "void OnDestroy() override",
     "Called when the entity is removed, the script reloads or play stops."},
    {"OnTriggerEnter", HP_COMPLETION_FUNCTION, "void OnTriggerEnter(hp::Entity other, const hp::Contact* contact) override",
     "Another collider entered this trigger. contact may be null."},
    {"OnTriggerStay", HP_COMPLETION_FUNCTION, "void OnTriggerStay(hp::Entity other, const hp::Contact* contact) override",
     "Another collider is still inside this trigger."},
    {"OnTriggerExit", HP_COMPLETION_FUNCTION, "void OnTriggerExit(hp::Entity other, const hp::Contact* contact) override",
     "Another collider left this trigger. contact is always null."},
    {"OnCollisionEnter", HP_COMPLETION_FUNCTION,
     "void OnCollisionEnter(hp::Entity other, const hp::Contact* contact) override", "This body started touching another."},
    {"OnCollisionStay", HP_COMPLETION_FUNCTION,
     "void OnCollisionStay(hp::Entity other, const hp::Contact* contact) override", "This body is still touching another."},
    {"OnCollisionExit", HP_COMPLETION_FUNCTION,
     "void OnCollisionExit(hp::Entity other, const hp::Contact* contact) override", "This body stopped touching another."},
    {"OnCall", HP_COMPLETION_FUNCTION, "bool OnCall(std::string_view function, hp::Args args) override",
     "Another script ran Call(function, ...) on this entity. Return true when handled."},
    {"GetEntity", HP_COMPLETION_FUNCTION, "hp::Entity GetEntity() const", "The entity this script is attached to."},
    {"GetName", HP_COMPLETION_FUNCTION, "std::string GetName() const", "The entity's name."},
    {"IsValid", HP_COMPLETION_FUNCTION, "bool IsValid() const", "False once the entity has been destroyed."},
    {"GetPosition", HP_COMPLETION_FUNCTION, "hp::Vec3 GetPosition() const", "The entity's position."},
    {"SetPosition", HP_COMPLETION_FUNCTION, "void SetPosition(hp::Vec3 position) const", "Moves the entity."},
    {"Translate", HP_COMPLETION_FUNCTION, "void Translate(hp::Vec3 delta) const", "Moves the entity by delta."},
    {"GetRotation", HP_COMPLETION_FUNCTION, "hp::Vec3 GetRotation() const", "The entity's rotation in degrees."},
    {"SetRotation", HP_COMPLETION_FUNCTION, "void SetRotation(hp::Vec3 degrees) const", "Sets the rotation in degrees."},
    {"Rotate", HP_COMPLETION_FUNCTION, "void Rotate(hp::Vec3 degrees) const", "Adds degrees to the rotation."},
    {"GetScale", HP_COMPLETION_FUNCTION, "hp::Vec3 GetScale() const", "The entity's scale."},
    {"SetScale", HP_COMPLETION_FUNCTION, "void SetScale(hp::Vec3 scale) const", "Sets the entity's scale."},
    {"Call", HP_COMPLETION_FUNCTION, "bool Call(std::string_view function, args...) const",
     "Runs a function by name in the entity's script, in any language. Returns whether one handled it."},
    {"Find", HP_COMPLETION_FUNCTION, "hp::Entity hp::Scene::Find(std::string_view name)",
     "The first entity with this name, or an empty entity."},
    {"IsActionDown", HP_COMPLETION_FUNCTION, "bool hp::Input::IsActionDown(std::string_view action)",
     "True while the input action is held."},
    {"IsActionPressed", HP_COMPLETION_FUNCTION, "bool hp::Input::IsActionPressed(std::string_view action)",
     "True on the frame the input action was pressed."},
    {"IsActionReleased", HP_COMPLETION_FUNCTION, "bool hp::Input::IsActionReleased(std::string_view action)",
     "True on the frame the input action was released."},
    {"GetAxis", HP_COMPLETION_FUNCTION, "float hp::Input::GetAxis(std::string_view axis)", "The input axis value."},
    {"Log", HP_COMPLETION_FUNCTION, "void hp::Log(std::string_view message)", "Writes to the Console."},
    {"LogWarning", HP_COMPLETION_FUNCTION, "void hp::LogWarning(std::string_view message)", "Writes a warning."},
    {"LogError", HP_COMPLETION_FUNCTION, "void hp::LogError(std::string_view message)", "Writes an error."},
    {"SetName", HP_COMPLETION_FUNCTION, "void SetName(std::string_view name) const", "Renames the entity."},
    {"SetVisible", HP_COMPLETION_FUNCTION, "bool SetVisible(bool visible) const", "Shows or hides the entity's mesh."},
    {"IsVisible", HP_COMPLETION_FUNCTION, "bool IsVisible() const", "Whether the entity's mesh is shown."},
    {"GetBody", HP_COMPLETION_FUNCTION, "hp::Body GetBody() const", "The entity's physics body."},
    {"GetController", HP_COMPLETION_FUNCTION, "std::optional<hp::CharacterController> GetController() const", "The entity's Character Controller, or nothing."},
    {"GetAnimator", HP_COMPLETION_FUNCTION, "std::optional<hp::Animator> GetAnimator() const", "Animation control for an animated character, or nothing."},
    {"GetCamera", HP_COMPLETION_FUNCTION, "std::optional<hp::Camera> GetCamera() const", "The entity's Camera component, or nothing."},
    {"GetSocket", HP_COMPLETION_FUNCTION, "std::optional<hp::Socket> GetSocket(std::string_view name, bool worldSpace = true) const", "A socket of an animated character: position and rotation (radians)."},
    {"Raycast", HP_COMPLETION_FUNCTION, "std::optional<hp::RaycastHit> Raycast(origin, direction, maxDistance, ...)", "hp::Physics::Raycast, or the script's own Raycast that passes through its entity."},
    {"Instantiate", HP_COMPLETION_FUNCTION, "hp::Entity hp::Scene::Instantiate(std::string_view path, const hp::SpawnOptions& options = {})", "Spawns a prefab with an optional position, rotation (degrees) and Inspector overrides."},
    {"Destroy", HP_COMPLETION_FUNCTION, "void hp::Scene::Destroy(hp::Entity entity)", "Removes the entity once this frame's scripts have run."},
    {"Reload", HP_COMPLETION_FUNCTION, "bool hp::Scene::Reload()", "Restarts the current scene from its saved file."},
    {"FindAllWithPrefix", HP_COMPLETION_FUNCTION, "std::vector<hp::Entity> hp::Scene::FindAllWithPrefix(std::string_view prefix)", "Every entity whose name starts with prefix."},
    {"FindCameras", HP_COMPLETION_FUNCTION, "std::vector<hp::Entity> hp::Scene::FindCameras()", "Every entity with a Camera component."},
    {"FindFirstCamera", HP_COMPLETION_FUNCTION, "hp::Entity hp::Scene::FindFirstCamera()", "The first camera entity."},
    {"GetActiveCamera", HP_COMPLETION_FUNCTION, "hp::Entity hp::Scene::GetActiveCamera()", "The camera the player sees through."},
    {"SetActiveCamera", HP_COMPLETION_FUNCTION, "void hp::Scene::SetActiveCamera(hp::Entity camera)", "Makes this the camera the player sees through."},
    {"DeltaTime", HP_COMPLETION_FUNCTION, "float hp::Time::DeltaTime()", "Seconds since the last frame."},
    {"Quit", HP_COMPLETION_FUNCTION, "void hp::Application::Quit()", "Closes the game. In the editor, it stops Play."},
    {"IsKeyDown", HP_COMPLETION_FUNCTION, "bool hp::Input::IsKeyDown(std::string_view key)", "Raw key check by name, e.g. \"w\" or \"space\"."},
    {"IsMouseButtonDown", HP_COMPLETION_FUNCTION, "bool hp::Input::IsMouseButtonDown(hp::MouseButton button)", "True while the button is held."},
    {"GetMousePosition", HP_COMPLETION_FUNCTION, "hp::Vec2 hp::Input::GetMousePosition()", "Mouse position in the window, in pixels."},
    {"GetMouseDelta", HP_COMPLETION_FUNCTION, "hp::Vec2 hp::Input::GetMouseDelta()", "Mouse movement since the last frame."},
    {"GetScrollDelta", HP_COMPLETION_FUNCTION, "hp::Vec2 hp::Input::GetScrollDelta()", "Wheel movement since the last frame."},
    {"IsGamepadConnected", HP_COMPLETION_FUNCTION, "bool hp::Input::IsGamepadConnected()", "Whether player one's gamepad is plugged in."},
    {"SetCursorMode", HP_COMPLETION_FUNCTION, "void hp::Input::SetCursorMode(hp::CursorMode mode)", "Captured for mouse look, Normal for menus."},
    {"GetCursorMode", HP_COMPLETION_FUNCTION, "hp::CursorMode hp::Input::GetCursorMode()", "The current cursor mode."},
    {"IsAvailable", HP_COMPLETION_FUNCTION, "bool hp::Physics::IsAvailable() / hp::Audio::IsAvailable()", "Whether physics is running, or sound works on this machine."},
    {"Exists", HP_COMPLETION_FUNCTION, "bool hp::Body::Exists() const", "Whether the entity has a physics body."},
    {"GetLinearVelocity", HP_COMPLETION_FUNCTION, "hp::Vec3 hp::Body::GetLinearVelocity() const", "The body's velocity."},
    {"SetLinearVelocity", HP_COMPLETION_FUNCTION, "bool hp::Body::SetLinearVelocity(hp::Vec3 velocity, bool activate = true) const", "Sets the body's velocity."},
    {"SetAngularVelocity", HP_COMPLETION_FUNCTION, "bool hp::Body::SetAngularVelocity(hp::Vec3 velocity, bool activate = true) const", "Spins the body."},
    {"Move", HP_COMPLETION_FUNCTION, "bool Move(...) const", "Body::Move(x, z, speed) for simple movement; CharacterController::Move(x, z) sets this step's walk direction."},
    {"Jump", HP_COMPLETION_FUNCTION, "bool Jump(...) const", "Body::Jump(speed), or CharacterController::Jump() on the next physics step."},
    {"IsGrounded", HP_COMPLETION_FUNCTION, "bool IsGrounded() const", "Whether the body or character is standing on something."},
    {"SetCrouched", HP_COMPLETION_FUNCTION, "bool hp::CharacterController::SetCrouched(bool crouched) const", "Crouch or stand."},
    {"IsCrouched", HP_COMPLETION_FUNCTION, "bool hp::CharacterController::IsCrouched() const", "Whether the character is crouched."},
    {"IsStandBlocked", HP_COMPLETION_FUNCTION, "bool hp::CharacterController::IsStandBlocked() const", "True when there is no room to stand up."},
    {"GetGroundState", HP_COMPLETION_FUNCTION, "std::string hp::CharacterController::GetGroundState() const", "How the character is standing on something."},
    {"GetVelocity", HP_COMPLETION_FUNCTION, "hp::Vec3 hp::CharacterController::GetVelocity() const", "The character's velocity."},
    {"GetGroundNormal", HP_COMPLETION_FUNCTION, "hp::Vec3 hp::CharacterController::GetGroundNormal() const", "The slope underfoot."},
    {"GetGroundVelocity", HP_COMPLETION_FUNCTION, "hp::Vec3 hp::CharacterController::GetGroundVelocity() const", "How fast the ground is moving."},
    {"SetSpeedMultiplier", HP_COMPLETION_FUNCTION, "bool hp::CharacterController::SetSpeedMultiplier(float multiplier) const", "Scales walk speed."},
    {"GetSpeedMultiplier", HP_COMPLETION_FUNCTION, "float hp::CharacterController::GetSpeedMultiplier() const", "The walk speed scale."},
    {"Teleport", HP_COMPLETION_FUNCTION, "bool hp::CharacterController::Teleport(hp::Vec3 position) const", "Moves the character instantly."},
    {"SetFacingYaw", HP_COMPLETION_FUNCTION, "bool hp::CharacterController::SetFacingYaw(float radians) const", "Turns the character to face a direction."},
    {"Play", HP_COMPLETION_FUNCTION, "Play(...)", "Animator::Play(clip) plays a clip from the start; Audio::Play(path, loop, volume) plays a sound."},
    {"CrossFade", HP_COMPLETION_FUNCTION, "bool hp::Animator::CrossFade(std::string_view clip, float seconds) const", "Blends into a clip."},
    {"Pause", HP_COMPLETION_FUNCTION, "Pause(...)", "Pauses an animation or a sound."},
    {"Resume", HP_COMPLETION_FUNCTION, "Resume(...)", "Resumes an animation or a sound."},
    {"Stop", HP_COMPLETION_FUNCTION, "Stop(...)", "Stops an animation or a sound."},
    {"SetSpeed", HP_COMPLETION_FUNCTION, "bool hp::Animator::SetSpeed(float multiplier) const", "Playback speed; must be above 0."},
    {"IsPlaying", HP_COMPLETION_FUNCTION, "IsPlaying(...)", "Whether an animation (or a given clip) or a sound is playing."},
    {"GetCurrentAnimation", HP_COMPLETION_FUNCTION, "std::string hp::Animator::GetCurrentAnimation() const", "The clip that is playing."},
    {"GetTime", HP_COMPLETION_FUNCTION, "float hp::Animator::GetTime() const", "Playback time in seconds."},
    {"GetDuration", HP_COMPLETION_FUNCTION, "float hp::Animator::GetDuration() const", "Length of the current clip, or of a given clip."},
    {"SetFov", HP_COMPLETION_FUNCTION, "void hp::Camera::SetFov(float degrees) const", "Field of view in degrees."},
    {"SetVolume", HP_COMPLETION_FUNCTION, "bool hp::Audio::SetVolume(hp::Sound sound, float volume)", "Changes a sound's volume."},
    {"StopAll", HP_COMPLETION_FUNCTION, "void hp::Audio::StopAll()", "Silences every sound."},
    {"Body", HP_COMPLETION_TYPE, "hp::Body", "A physics body."},
    {"CharacterController", HP_COMPLETION_TYPE, "hp::CharacterController", "A Character Controller."},
    {"Animator", HP_COMPLETION_TYPE, "hp::Animator", "Clip playback on an animated character."},
    {"Camera", HP_COMPLETION_TYPE, "hp::Camera", "A Camera component."},
    {"Socket", HP_COMPLETION_TYPE, "hp::Socket", "position and rotation of a character socket."},
    {"SpawnOptions", HP_COMPLETION_TYPE, "hp::SpawnOptions", "position, rotation (degrees) and vars for Scene::Instantiate."},
    {"RaycastHit", HP_COMPLETION_TYPE, "hp::RaycastHit", "point, normal, distance and entity."},
    {"RaycastOptions", HP_COMPLETION_TYPE, "hp::RaycastOptions", "layer, ignoreSensors and ignore for Physics::Raycast."},
    {"Sound", HP_COMPLETION_TYPE, "hp::Sound", "A playing sound; 0 when it failed to start."},
    {"Scene", HP_COMPLETION_TYPE, "hp::Scene", "Finding, spawning and destroying entities."},
    {"Physics", HP_COMPLETION_TYPE, "hp::Physics", "Raycasts."},
    {"Audio", HP_COMPLETION_TYPE, "hp::Audio", "Playing sounds."},
    {"Input", HP_COMPLETION_TYPE, "hp::Input", "Actions, axes, keys, mouse and gamepad."},
    {"Time", HP_COMPLETION_TYPE, "hp::Time", "Frame timing."},
    {"ViewCamera", HP_COMPLETION_TYPE, "hp::ViewCamera", "The free script camera."},
    {"Display", HP_COMPLETION_FUNCTION, "hp::Screen hp::UI::Display(std::string_view path, std::string* error = nullptr)", "Shows a .hui layout, like Lua's display.ui."},
    {"DestroyCurrent", HP_COMPLETION_FUNCTION, "bool hp::UI::DestroyCurrent()", "Removes the most recently displayed layout."},
    {"DestroyAll", HP_COMPLETION_FUNCTION, "void hp::UI::DestroyAll()", "Removes every displayed layout."},
    {"Load", HP_COMPLETION_FUNCTION, "hp::Screen hp::UI::Load(std::string_view path, std::string* error = nullptr)", "Loads a .hui layout without display's bookkeeping."},
    {"CreateScreen", HP_COMPLETION_FUNCTION, "hp::Screen hp::UI::CreateScreen(std::string_view name)", "A new UI screen to build in code."},
    {"GetRoot", HP_COMPLETION_FUNCTION, "hp::Widget hp::Screen::GetRoot() const", "The screen's root canvas."},
    {"GetWidget", HP_COMPLETION_FUNCTION, "hp::Widget hp::Screen::GetWidget(std::string_view id) const", "A .hui widget by its ID."},
    {"AddPanel", HP_COMPLETION_FUNCTION, "hp::Widget hp::Widget::AddPanel() const", "Adds a panel."},
    {"AddText", HP_COMPLETION_FUNCTION, "hp::Widget hp::Widget::AddText(std::string_view text = {}) const", "Adds text."},
    {"AddButton", HP_COMPLETION_FUNCTION, "hp::Widget hp::Widget::AddButton(std::string_view label = \"Button\") const", "Adds a button."},
    {"AddImage", HP_COMPLETION_FUNCTION, "hp::Widget hp::Widget::AddImage(std::string_view imageId = {}) const", "Adds an image."},
    {"AddVerticalBox", HP_COMPLETION_FUNCTION, "hp::Widget hp::Widget::AddVerticalBox() const", "Adds a vertical box."},
    {"AddHorizontalBox", HP_COMPLETION_FUNCTION, "hp::Widget hp::Widget::AddHorizontalBox() const", "Adds a horizontal box."},
    {"AddGrid", HP_COMPLETION_FUNCTION, "hp::Widget hp::Widget::AddGrid() const", "Adds a grid."},
    {"SetText", HP_COMPLETION_FUNCTION, "bool hp::Widget::SetText(std::string_view text) const", "A text's text or a button's label."},
    {"SetOnClick", HP_COMPLETION_FUNCTION, "bool SetOnClick(hp::Widget button, std::string_view function) const", "Clicking the button runs function on this entity's script."},
    {"SetOnHover", HP_COMPLETION_FUNCTION, "bool SetOnHover(hp::Widget button, std::string_view function) const", "Hovering the button runs function(bool entered)."},
    {"SetStyle", HP_COMPLETION_FUNCTION, "bool hp::Widget::SetStyle(const hp::UIStyle& style) const", "Applies a style."},
    {"SetButtonStyle", HP_COMPLETION_FUNCTION, "bool hp::Widget::SetButtonStyle(hp::ButtonState state, const hp::UIStyle& style) const", "A button's style in one state."},
    {"RegisterThemeStyle", HP_COMPLETION_FUNCTION, "bool hp::UI::RegisterThemeStyle(std::string_view name, const hp::UIStyle& style)", "Adds a named style to the theme."},
    {"ApplyThemeStyle", HP_COMPLETION_FUNCTION, "bool hp::Widget::ApplyThemeStyle(std::string_view name) const", "Applies a theme style."},
    {"SetAnchor", HP_COMPLETION_FUNCTION, "bool hp::Widget::SetAnchor(hp::UIAnchor anchor) const", "Where the widget anchors in its parent."},
    {"SetBackgroundColor", HP_COMPLETION_FUNCTION, "bool hp::Widget::SetBackgroundColor(hp::Color color) const", "Panel or button background."},
    {"Widget", HP_COMPLETION_TYPE, "hp::Widget", "A UI widget."},
    {"Screen", HP_COMPLETION_TYPE, "hp::Screen", "A UI screen."},
    {"UIStyle", HP_COMPLETION_TYPE, "hp::UIStyle", "Style fields, named like Lua's style tables."},
    {"UIAnchor", HP_COMPLETION_TYPE, "hp::UIAnchor", "TopLeft, TopCenter, ..., BottomRight, Stretch."},
    {"UIAlign", HP_COMPLETION_TYPE, "hp::UIAlign", "Left, Center, Right, Top, Bottom, Stretch."},
    {"UI", HP_COMPLETION_TYPE, "hp::UI", "Game UI: displaying .hui layouts and building screens."},
    {"HP_FIELD", HP_COMPLETION_KEYWORD, "HP_FIELD(type, name, default...)",
     "Declares an Inspector field, e.g. HP_FIELD(float, speed, 5.0f) or HP_FIELD(hp::Vec3, offset, 0, 1, 0)."},
    {"HP_SCRIPT", HP_COMPLETION_KEYWORD, "HP_SCRIPT(ClassName)", "Makes the class this file's script."},
    {"Script", HP_COMPLETION_TYPE, "hp::Script", "Base class for gameplay scripts."},
    {"Entity", HP_COMPLETION_TYPE, "hp::Entity", "A scene entity."},
    {"Vec2", HP_COMPLETION_TYPE, "hp::Vec2", "Two floats: x, y."},
    {"Vec3", HP_COMPLETION_TYPE, "hp::Vec3", "Three floats: x, y, z."},
    {"Vec4", HP_COMPLETION_TYPE, "hp::Vec4", "Four floats; shown as a colour in the Inspector."},
    {"Contact", HP_COMPLETION_TYPE, "hp::Contact", "point, normal, relativeVelocity and penetration."},
    {"Value", HP_COMPLETION_TYPE, "hp::Value", "A value passed between scripts."},
    {"Args", HP_COMPLETION_TYPE, "hp::Args", "The arguments OnCall receives."},
};

const char* kKeywords[] = {"auto", "bool", "break", "case", "class", "const", "constexpr", "continue", "default",
                           "double", "else", "enum", "false", "float", "for", "if", "int", "namespace", "nullptr",
                           "override", "private", "protected", "public", "return", "static", "struct", "switch",
                           "this", "true", "using", "virtual", "void", "while", "include"};

int32_t Complete(const char*, const char*, uint32_t, uint32_t, HPCompletionCallback callback, void* user) {
    for (const ApiEntry& entry : kApi) {
        callback(user, entry.label, entry.kind, entry.detail);
    }
    for (const char* keyword : kKeywords) {
        callback(user, keyword, HP_COMPLETION_KEYWORD, nullptr);
    }
    return 1;
}

std::string WordAt(const char* source, uint32_t line, uint32_t column) {
    std::istringstream lines(source ? source : "");
    std::string text;
    for (uint32_t i = 0; i < line && std::getline(lines, text); ++i) {
    }
    size_t at = column > 0 ? column - 1 : 0;
    const auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    if (at >= text.size() || !isWord(text[at])) {
        return {};
    }
    size_t start = at;
    while (start > 0 && isWord(text[start - 1])) --start;
    while (at < text.size() && isWord(text[at])) ++at;
    return text.substr(start, at - start);
}

size_t Hover(const char*, const char* source, uint32_t line, uint32_t column, char* buffer, size_t bufferSize) {
    const std::string word = WordAt(source, line, column);
    for (const ApiEntry& entry : kApi) {
        if (word == entry.label) {
            const std::string text = std::string(entry.detail) + "\n" + entry.doc;
            if (buffer && bufferSize > text.size()) {
                std::memcpy(buffer, text.c_str(), text.size() + 1);
            }
            return text.size() + 1;
        }
    }
    return 0;
}

const char* const kTemplate = R"cpp(#include "HephaestusScript.h"

class $NAME : public hp::Script {
public:
    HP_FIELD(float, speed, 5.0f);

    void OnAwake() override {
    }

    void OnUpdate(float deltaSeconds) override {
    }
};

HP_SCRIPT($NAME)
)cpp";

const HPScriptLanguage kLanguage = {
    sizeof(HPScriptLanguage), HP_SCRIPT_LANGUAGE_API_VERSION, "cpp", "C++", ".cpp",
    CreateRuntime, DestroyRuntime, CreateInstance, DestroyInstance,
    Awake, Update, LateUpdate, ContactCallback, Call, DescribeFields,
    &kSyntax, Complete, Hover, PackageScript, kTemplate,
};

const HPScriptLanguage* const kLanguages[] = {&kLanguage};

} // namespace

extern "C" HP_SCRIPT_LANGUAGE_EXPORT const HPScriptLanguage* const* HP_GetScriptLanguages(uint32_t hostApiVersion,
                                                                                         uint32_t* count) {
    if (hostApiVersion != HP_SCRIPT_LANGUAGE_API_VERSION || !count) {
        return nullptr;
    }
    *count = 1;
    return kLanguages;
}
