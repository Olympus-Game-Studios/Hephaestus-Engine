#include "LuaDebugger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

LuaDebugger& LuaDebugger::Instance() {
    static LuaDebugger debugger;
    return debugger;
}

std::string LuaDebugger::NormalizePath(const std::string& path) {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(std::filesystem::path(path), error);
    if (error) {
        normalized = std::filesystem::path(path);
    }
    std::string result = normalized.lexically_normal().generic_string();
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return result;
}

void LuaDebugger::Attach(lua_State* state) {
    m_state = state;
    m_sourceKeys.clear();
    UpdateHook();
}

void LuaDebugger::Detach(lua_State* state) {
    if (m_state != state) {
        return;
    }
#if LUA_AVAILABLE
    if (m_state) {
        lua_sethook(m_state, nullptr, 0, 0);
    }
#endif
    m_state = nullptr;
    m_stepMode = Action::Continue;
    m_stepThread = nullptr;
    m_pauseRequested = false;
    m_sourceKeys.clear();
}

void LuaDebugger::SetBreakpoints(const std::string& path, const std::vector<int>& lines) {
    const std::string key = NormalizePath(path);
    if (lines.empty()) {
        m_breakpoints.erase(key);
    } else {
        m_breakpoints[key] = std::set<int>(lines.begin(), lines.end());
    }
    UpdateHook();
}

void LuaDebugger::RequestPause() {
    m_pauseRequested = true;
    UpdateHook();
}

void LuaDebugger::RequestResume(Action action) {
    if (!IsPaused()) {
        return;
    }
    m_requestedAction = action;
    if (onResumeRequested) {
        onResumeRequested();
    }
}

LuaDebugger::Action LuaDebugger::TakeRequestedAction() {
    const Action action = m_requestedAction;
    m_requestedAction = Action::Continue;
    return action;
}

void LuaDebugger::UpdateHook() {
#if LUA_AVAILABLE
    if (!m_state) {
        return;
    }
    // The hook costs a call per executed line, so it is only installed while something can stop.
    const bool needed = !m_breakpoints.empty() || m_pauseRequested || m_stepMode != Action::Continue;
    lua_sethook(m_state, needed ? &LuaDebugger::Hook : nullptr, needed ? LUA_MASKLINE : 0, 0);
#endif
}

#if LUA_AVAILABLE

void LuaDebugger::Hook(lua_State* state, lua_Debug* ar) {
    LuaDebugger& debugger = Instance();
    if (debugger.m_inHook || ar->event != LUA_HOOKLINE) {
        return;
    }
    debugger.OnLine(state, ar);
}

int LuaDebugger::StackDepth(lua_State* state) {
    lua_Debug ar;
    int depth = 0;
    while (lua_getstack(state, depth, &ar)) {
        ++depth;
    }
    return depth;
}

void LuaDebugger::OnLine(lua_State* state, lua_Debug* ar) {
    std::string reason;
    if (m_pauseRequested) {
        reason = "Paused";
    } else if (m_stepMode == Action::StepInto ||
               (m_stepMode == Action::StepOver && (state != m_stepThread || StackDepth(state) <= m_stepDepth)) ||
               (m_stepMode == Action::StepOut && (state != m_stepThread || StackDepth(state) < m_stepDepth))) {
        reason = "Step";
    } else if (!m_breakpoints.empty()) {
        if (!lua_getinfo(state, "S", ar) || !ar->source || ar->source[0] != '@') {
            return;
        }
        auto cached = m_sourceKeys.find(ar->source);
        if (cached == m_sourceKeys.end()) {
            cached = m_sourceKeys.emplace(ar->source, NormalizePath(ar->source + 1)).first;
        }
        const auto file = m_breakpoints.find(cached->second);
        if (file == m_breakpoints.end() || !file->second.count(ar->currentline)) {
            return;
        }
        reason = "Breakpoint";
    } else {
        return;
    }

    PauseState pause;
    pause.reason = reason;
    lua_Debug frame;
    for (int level = 0; lua_getstack(state, level, &frame); ++level) {
        if (!lua_getinfo(state, "Sln", &frame)) {
            continue;
        }
        Frame entry;
        entry.source = frame.source && frame.source[0] == '@' ? frame.source + 1 : frame.short_src;
        entry.line = frame.currentline;
        entry.function = frame.name ? frame.name
                       : (frame.what && std::string(frame.what) == "main") ? "(script)"
                       : "(anonymous)";
        if (std::string(frame.what ? frame.what : "") == "C") {
            entry.function = std::string("[C] ") + (frame.name ? frame.name : "");
        }
        pause.frames.push_back(std::move(entry));
    }

    m_pauseRequested = false;
    m_stepMode = Action::Continue;
    m_pausedState = state;
    m_inHook = true;
    m_requestedAction = Action::Continue;
    const Action action = onPause ? onPause(pause) : Action::Continue;
    m_inHook = false;
    m_pausedState = nullptr;

    if (action == Action::Stop) {
        UpdateHook();
        luaL_error(state, "stopped by the debugger");
        return;
    }
    if (action != Action::Continue) {
        m_stepMode = action;
        m_stepThread = state;
        m_stepDepth = StackDepth(state);
    }
    UpdateHook();
}

namespace {

std::string Quote(const char* text, std::size_t length) {
    constexpr std::size_t kMax = 80;
    std::string result = "\"" + std::string(text, std::min(length, kMax));
    if (length > kMax) {
        result += "...";
    }
    return result + "\"";
}

std::string FormatNumber(lua_State* state, int index) {
    char buffer[64];
    if (lua_isinteger(state, index)) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(lua_tointeger(state, index)));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(lua_tonumber(state, index)));
    }
    return buffer;
}

} // namespace

std::string LuaDebugger::TypeName(lua_State* state, int index) {
    const int type = lua_type(state, index);
    if (type == LUA_TUSERDATA || type == LUA_TTABLE) {
        if (lua_getmetatable(state, index)) {
            lua_pushstring(state, "__name");
            lua_rawget(state, -2);
            std::string name = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : "";
            lua_pop(state, 2);
            if (!name.empty()) {
                return name;
            }
        }
    }
    return lua_typename(state, type);
}

std::string LuaDebugger::Describe(lua_State* state, int index, int depth) {
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return lua_toboolean(state, index) ? "true" : "false";
        case LUA_TNUMBER: return FormatNumber(state, index);
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char* text = lua_tolstring(state, index, &length);
            return Quote(text, length);
        }
        case LUA_TTABLE: {
            if (depth <= 0) {
                return "{...}";
            }
            std::string result = "{";
            int shown = 0;
            constexpr int kMaxEntries = 4;
            lua_pushnil(state);
            while (lua_next(state, index)) {
                if (shown == kMaxEntries) {
                    result += ", ...";
                    lua_pop(state, 2);
                    break;
                }
                if (shown++) {
                    result += ", ";
                }
                if (lua_type(state, -2) == LUA_TSTRING) {
                    result += std::string(lua_tostring(state, -2)) + " = ";
                } else if (lua_type(state, -2) == LUA_TNUMBER) {
                    result += "[" + FormatNumber(state, -2) + "] = ";
                }
                result += Describe(state, -1, depth - 1);
                lua_pop(state, 1);
            }
            return result + "}";
        }
        case LUA_TFUNCTION: {
            lua_Debug ar;
            lua_pushvalue(state, index);
            if (lua_getinfo(state, ">S", &ar) && ar.source && ar.source[0] == '@') {
                return "function " + std::filesystem::path(ar.source + 1).filename().string() + ":" +
                       std::to_string(ar.linedefined);
            }
            return "function";
        }
        default: return TypeName(state, index);
    }
}

std::vector<LuaDebugger::Variable> LuaDebugger::Locals(int frame) const {
    std::vector<Variable> variables;
    lua_State* state = m_pausedState;
    lua_Debug ar;
    if (!state || !lua_getstack(state, frame, &ar)) {
        return variables;
    }
    for (int i = 1;; ++i) {
        const char* name = lua_getlocal(state, &ar, i);
        if (!name) {
            break;
        }
        if (name[0] != '(') {
            variables.push_back({ name, Describe(state, -1), TypeName(state, -1) });
        }
        lua_pop(state, 1);
    }
    if (lua_getinfo(state, "f", &ar)) {
        const int function = lua_gettop(state);
        for (int i = 1;; ++i) {
            const char* name = lua_getupvalue(state, function, i);
            if (!name) {
                break;
            }
            if (name[0] && std::string(name) != "_ENV") {
                variables.push_back({ name, Describe(state, -1), "upvalue " + TypeName(state, -1) });
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }
    return variables;
}

LuaDebugger::Variable LuaDebugger::Evaluate(const std::string& expression, int frame) const {
    Variable result{ expression, "", "" };
    lua_State* state = m_pausedState;
    lua_Debug ar;
    if (!state || !lua_getstack(state, frame, &ar)) {
        result.value = "not paused";
        result.type = "error";
        return result;
    }
    const int top = lua_gettop(state);

    // Scope: the frame's locals shadow its upvalues, which fall back to the script's _ENV.
    lua_newtable(state);
    const int scope = lua_gettop(state);
    lua_pushglobaltable(state);
    const int fallback = lua_gettop(state);
    if (lua_getinfo(state, "f", &ar)) {
        const int function = lua_gettop(state);
        for (int i = 1;; ++i) {
            const char* name = lua_getupvalue(state, function, i);
            if (!name) {
                break;
            }
            if (std::string(name) == "_ENV") {
                lua_replace(state, fallback);
            } else if (name[0]) {
                lua_setfield(state, scope, name);
            } else {
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 1);
    }
    for (int i = 1;; ++i) {
        const char* name = lua_getlocal(state, &ar, i);
        if (!name) {
            break;
        }
        if (name[0] != '(') {
            lua_setfield(state, scope, name);
        } else {
            lua_pop(state, 1);
        }
    }
    lua_newtable(state);
    lua_pushvalue(state, fallback);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, scope);

    std::string code = "return " + expression;
    if (luaL_loadbuffer(state, code.c_str(), code.size(), "=watch") != LUA_OK) {
        lua_pop(state, 1);
        code = expression;
        if (luaL_loadbuffer(state, code.c_str(), code.size(), "=watch") != LUA_OK) {
            result.value = lua_tostring(state, -1) ? lua_tostring(state, -1) : "syntax error";
            result.type = "error";
            lua_settop(state, top);
            return result;
        }
    }
    lua_pushvalue(state, scope);
    lua_setupvalue(state, -2, 1);
    if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
        result.value = lua_tostring(state, -1) ? lua_tostring(state, -1) : "error";
        result.type = "error";
    } else {
        result.value = Describe(state, -1);
        result.type = TypeName(state, -1);
    }
    lua_settop(state, top);
    return result;
}

#else

std::vector<LuaDebugger::Variable> LuaDebugger::Locals(int) const { return {}; }

LuaDebugger::Variable LuaDebugger::Evaluate(const std::string& expression, int) const {
    return { expression, "Lua is not available in this build", "error" };
}

std::string LuaDebugger::Describe(lua_State*, int, int) { return {}; }
std::string LuaDebugger::TypeName(lua_State*, int) { return {}; }

#endif
