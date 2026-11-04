#include "Scripting/LuaDebugger.h"
#include "Scripting/LuaLanguageService.h"
#include "Scripting/LuaScriptEngine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    return condition;
}

bool HasCompletion(const std::vector<LuaLanguageService::Completion>& completions, const std::string& label)
{
    return std::any_of(completions.begin(), completions.end(),
                       [&](const auto& completion) { return completion.label == label; });
}

} // namespace

int main()
{
    bool pass = true;
    namespace fs = std::filesystem;
    const fs::path script = fs::temp_directory_path() / "hephaestus_lua_debugger_test.lua";
    const std::string source =
        "local base = 10\n"                  // 1
        "local function add(a, b)\n"         // 2
        "    local sum = a + b\n"            // 3
        "    return sum\n"                   // 4
        "end\n"                              // 5
        "result = add(base, 5)\n"            // 6
        "after = result * 2\n";              // 7
    std::ofstream(script, std::ios::trunc) << source;

    LuaScriptEngine engine;
    engine.Initialize(nullptr, nullptr);
    lua_State* L = engine.GetState();
    LuaDebugger& debugger = LuaDebugger::Instance();
    pass &= Check(debugger.IsAttached(), "the gameplay Lua state attaches to the debugger");

    // Break inside add(), read locals and evaluate in the frame, then step over twice.
    std::vector<int> pausedLines;
    std::string sumValue;
    std::string evaluated;
    std::string callerLocal;
    std::string topFunction;
    int pauses = 0;
    debugger.onPause = [&](const LuaDebugger::PauseState& state) {
        ++pauses;
        pausedLines.push_back(state.frames.empty() ? 0 : state.frames.front().line);
        if (pauses == 1) {
            topFunction = state.frames.front().function;
            evaluated = debugger.Evaluate("a * b", 0).value;
            callerLocal = debugger.Evaluate("base", 1).value;
            return LuaDebugger::Action::StepOver;
        }
        if (pauses == 2) {
            for (const auto& variable : debugger.Locals(0)) {
                if (variable.name == "sum") sumValue = variable.value;
            }
            return LuaDebugger::Action::StepOut;
        }
        return LuaDebugger::Action::Continue;
    };
    debugger.SetBreakpoints(script.string(), { 3 });
    const std::string chunkName = "@" + script.string();
    const bool loaded = luaL_loadbuffer(L, source.c_str(), source.size(), chunkName.c_str()) == LUA_OK;
    const bool ran = loaded && lua_pcall(L, 0, 0, 0) == LUA_OK;
    pass &= Check(ran, "the script runs to completion under the debugger");
    pass &= Check(!pausedLines.empty() && pausedLines[0] == 3, "execution stops at the line 3 breakpoint");
    pass &= Check(topFunction == "add", "the top frame names the function");
    pass &= Check(evaluated == "50", "watch expressions see the paused frame's locals");
    pass &= Check(callerLocal == "10", "watch expressions can be evaluated in a caller's frame");
    pass &= Check(pausedLines.size() >= 2 && pausedLines[1] == 4, "step over stops on the next line");
    pass &= Check(sumValue == "15", "locals report current values");
    // Returning does not start a new line, so step out stops at the caller's next line.
    pass &= Check(pausedLines.size() >= 3 && pausedLines[2] == 7, "step out stops at the caller's next line");

    // Stop unwinds the running chunk with an error instead of finishing it.
    debugger.onPause = [](const LuaDebugger::PauseState&) { return LuaDebugger::Action::Stop; };
    const bool reloaded = luaL_loadbuffer(L, source.c_str(), source.size(), chunkName.c_str()) == LUA_OK;
    const int status = reloaded ? lua_pcall(L, 0, 0, 0) : LUA_OK;
    const std::string stopMessage = status != LUA_OK && lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_settop(L, 0);
    pass &= Check(status != LUA_OK && stopMessage.find("stopped by the debugger") != std::string::npos,
                  "Stop ends the script with a debugger error");
    debugger.SetBreakpoints(script.string(), {});
    debugger.onPause = nullptr;

    bool ok = false;
    pass &= Check(engine.EvaluateConsole("1 + 2, 'x'", ok) == "3, \"x\"" && ok, "console input returns every value");
    pass &= Check(!engine.EvaluateConsole("nil + 1", ok).empty() && !ok, "console errors are reported, not thrown");

    const auto globals = LuaLanguageService::Complete(source, 7, 1);
    pass &= Check(HasCompletion(globals, "local") && HasCompletion(globals, "print") &&
                  HasCompletion(globals, "add") && HasCompletion(globals, "base"),
                  "Lua completion offers keywords, the API and the file's own names");
    const std::string memberLine = "math.";
    const auto members = LuaLanguageService::Complete(memberLine, 1, static_cast<uint32_t>(memberLine.size() + 1));
    pass &= Check(HasCompletion(members, "floor") && !HasCompletion(members, "local"),
                  "after a dot, completion lists that table's members");
    pass &= Check(LuaLanguageService::Hover(source, 4, 13).find("local sum") != std::string::npos,
                  "hover describes a local and where it is declared");
    pass &= Check(LuaLanguageService::Hover("print(1)", 1, 2).find("Lua standard library") != std::string::npos,
                  "hover names where an API function comes from");

    fs::remove(script);
    std::cout << (pass ? "All Lua debugger tests passed\n" : "Lua debugger tests FAILED\n");
    return pass ? 0 : 1;
}
