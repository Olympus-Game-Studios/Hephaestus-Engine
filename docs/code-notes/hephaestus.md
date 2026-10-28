# Hephaestus code notes

## Shared engine library migration

- `CMakeLists.txt:HephaestusGame` — `LUA_AVAILABLE` follows `LUA_FOUND` for every target, so `LuaScriptEngine.h` compiles the same way in the editor, the game and the shared runtime library. `HEPHAESTUS_GAME_SCRIPTING` only controls whether `engine/Game/GameMain.cpp:main` starts the Lua engine; when it is OFF the game still links Lua and ships its DLL (the package stager already requires `lua54.dll`).
