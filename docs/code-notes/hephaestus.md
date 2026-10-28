# Hephaestus code notes

## Shared engine library migration

- `CMakeLists.txt:HephaestusGame` — `LUA_AVAILABLE` follows `LUA_FOUND` for every target, so `LuaScriptEngine.h` compiles the same way in the editor, the game and the shared runtime library. `HEPHAESTUS_GAME_SCRIPTING` only controls whether `engine/Game/GameMain.cpp:main` starts the Lua engine; when it is OFF the game still links Lua and ships its DLL (the package stager already requires `lua54.dll`).
- `engine/Gfx/EngineFrameRenderer.cpp:EngineFrameRenderer::recordBillboards` — light billboard icons (and their ID-buffer pass) are derived from `LightComponent::type` at draw time. The former `EditorIconComponent` was attached by a `Scene` constructor hook compiled only outside `HEPHAESTUS_GAME_RUNTIME`, which made `Scene` behave differently per host; it was never serialized, so removing it changes no saved data.
