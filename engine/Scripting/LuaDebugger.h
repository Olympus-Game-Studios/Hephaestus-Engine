#pragma once

#include "LuaScriptEngine.h"

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Line breakpoints and stepping for the Lua state that runs gameplay scripts.
 *
 * Everything runs on the gameplay thread. When execution stops, onPause is
 * called from inside the Lua line hook and Lua stays suspended until it
 * returns, so a host keeps its UI alive by running an event loop there.
 * Locals() and Evaluate() are only valid while paused.
 */
class LuaDebugger {
public:
    enum class Action { Continue, StepOver, StepInto, StepOut, Stop };

    struct Frame {
        std::string source; // File path as the script was loaded, without Lua's "@".
        int line = 0;
        std::string function;
    };

    struct Variable {
        std::string name;
        std::string value;
        std::string type;
    };

    struct PauseState {
        std::string reason;
        std::vector<Frame> frames;
    };

    static LuaDebugger& Instance();

    /// Called by LuaScriptEngine when its state is ready and before it closes.
    void Attach(lua_State* state);
    void Detach(lua_State* state);

    /// Replaces the breakpoints of one file. Lines are 1-based.
    void SetBreakpoints(const std::string& path, const std::vector<int>& lines);

    /// Stops at the next Lua line that runs.
    void RequestPause();

    /// Ends the current pause with @p action; the host's onPause loop should return it.
    void RequestResume(Action action);

    bool IsPaused() const { return m_pausedState != nullptr; }
    /// Play mode has finished stopping; breakpoints may pause again.
    void StopFinished() { m_stopping = false; }
    bool IsAttached() const { return m_state != nullptr; }

    /// Blocks Lua until it returns the next action. Called on the gameplay thread.
    std::function<Action(const PauseState&)> onPause;
    /// Asks the host to leave the loop it runs inside onPause.
    std::function<void()> onResumeRequested;

    /// Locals and upvalues of @p frame (0 is where execution stopped).
    std::vector<Variable> Locals(int frame) const;

    /// Evaluates @p expression (or a statement) with @p frame's locals and upvalues in scope.
    Variable Evaluate(const std::string& expression, int frame) const;

    /// The pending action RequestResume stored, cleared when read.
    Action TakeRequestedAction();

    /// Readable one-line description of the value at @p index, without calling metamethods.
    static std::string Describe(lua_State* state, int index, int depth = 1);
    static std::string TypeName(lua_State* state, int index);

    /// Path form used to match breakpoints with chunk names: absolute, '/', lower case on Windows.
    static std::string NormalizePath(const std::string& path);

private:
    LuaDebugger() = default;

#if LUA_AVAILABLE
    static void Hook(lua_State* state, lua_Debug* ar);
    void OnLine(lua_State* state, lua_Debug* ar);
    static int StackDepth(lua_State* state);
#endif
    void UpdateHook();

    lua_State* m_state = nullptr;
    lua_State* m_pausedState = nullptr;
    bool m_inHook = false;
    bool m_pauseRequested = false;
    bool m_stopping = false;
    Action m_stepMode = Action::Continue;
    int m_stepDepth = 0;
    lua_State* m_stepThread = nullptr;
    Action m_requestedAction = Action::Continue;
    std::unordered_map<std::string, std::set<int>> m_breakpoints;
    std::unordered_map<std::string, std::string> m_sourceKeys;
};
