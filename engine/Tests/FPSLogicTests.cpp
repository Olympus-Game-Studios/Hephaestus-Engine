#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool Near(double left, double right, double epsilon = 1e-5)
{
    return std::abs(left - right) <= epsilon;
}

int AutoShots(lua_State* L, double dt, double seconds, double rps)
{
    const int top = lua_gettop(L);
    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "tickFire");
    lua_remove(L, -2);
    lua_newtable(L);
    lua_pushstring(L, "auto");
    lua_setfield(L, -2, "fireMode");
    lua_pushnumber(L, rps);
    lua_setfield(L, -2, "roundsPerSecond");
    lua_pushnumber(L, 999);
    lua_setfield(L, -2, "magazine");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "now");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "nextFire");
    const int fireFn = top + 1;
    const int stateIdx = top + 2;
    int shots = 0;
    double remaining = seconds;
    while (remaining > 1e-12) {
        const double step = remaining < dt ? remaining : dt;
        lua_pushvalue(L, fireFn);
        lua_pushvalue(L, stateIdx);
        lua_pushnumber(L, step);
        lua_pushboolean(L, 1);
        lua_pushboolean(L, 0);
        if (lua_pcall(L, 4, 1, 0) != LUA_OK) {
            std::cerr << lua_tostring(L, -1) << '\n';
            lua_settop(L, top);
            return -1;
        }
        shots += static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        remaining -= step;
    }
    lua_settop(L, top);
    return shots;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: FPSLogicTests <repo_root>\n";
        return 2;
    }
    const fs::path scripts = fs::path(argv[1]) / "Plugins" / "HephaestusFPS" / "Content" / "Scripts";
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    const std::string path = scripts.generic_string() + "/?.lua";
    lua_getglobal(L, "package");
    lua_pushstring(L, path.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    lua_getglobal(L, "require");
    lua_pushstring(L, "FPS.Logic");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        std::cerr << lua_tostring(L, -1) << '\n';
        lua_close(L);
        return 1;
    }
    lua_setglobal(L, "Logic");

    bool ok = true;
    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "clampPitch");
    lua_pushnumber(L, 2.0);
    lua_pushnumber(L, -1.0);
    lua_pushnumber(L, 1.0);
    lua_call(L, 3, 1);
    ok &= Check(Near(lua_tonumber(L, -1), 1.0), "pitch clamps high");
    lua_pop(L, 1);
    lua_getfield(L, -1, "clampPitch");
    lua_pushnumber(L, -2.0);
    lua_pushnumber(L, -1.0);
    lua_pushnumber(L, 1.0);
    lua_call(L, 3, 1);
    ok &= Check(Near(lua_tonumber(L, -1), -1.0), "pitch clamps low");
    lua_pop(L, 1);
    lua_getfield(L, -1, "clampPitch");
    lua_pushnumber(L, 0.25);
    lua_pushnumber(L, -1.0);
    lua_pushnumber(L, 1.0);
    lua_call(L, 3, 1);
    ok &= Check(Near(lua_tonumber(L, -1), 0.25), "pitch inside limits");
    lua_pop(L, 2);

    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "yawBasis");
    lua_pushnumber(L, 0.8);
    lua_call(L, 1, 1);
    lua_getfield(L, -1, "forward");
    lua_getfield(L, -1, "x");
    const double fx = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "z");
    const double fz = lua_tonumber(L, -1);
    lua_pop(L, 3);
    ok &= Check(Near(fx, std::cos(0.8)) && Near(fz, -std::sin(0.8)),
                "positive yaw matches ActiveCameraSync wish basis");

    lua_getfield(L, -1, "aimRay");
    lua_newtable(L);
    lua_pushnumber(L, 1.0);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, 2.0);
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, 1.62);
    lua_pushnumber(L, 0.8);
    lua_pushnumber(L, 0.2);
    lua_call(L, 4, 2);
    lua_getfield(L, -2, "y");
    ok &= Check(Near(lua_tonumber(L, -1), 1.62), "aimRay origin uses eye height");
    lua_pop(L, 1);
    lua_getfield(L, -1, "x");
    const double dx = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "y");
    const double dy = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "z");
    const double dz = lua_tonumber(L, -1);
    lua_pop(L, 3);
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    ok &= Check(Near(length, 1.0, 1e-4), "aimRay direction is unit length");
    ok &= Check(Near(dx, std::cos(0.8) * std::cos(0.2)) && Near(dz, -std::sin(0.8) * std::cos(0.2)),
                "aimRay uses the same yaw sign as yawBasis");

    lua_getfield(L, -1, "sprintAllowed");
    lua_pushnumber(L, 0.6);
    lua_pushboolean(L, 0);
    lua_call(L, 2, 1);
    ok &= Check(lua_toboolean(L, -1), "sprint allowed with forward");
    lua_pop(L, 1);
    lua_getfield(L, -1, "sprintAllowed");
    lua_pushnumber(L, 0.6);
    lua_pushboolean(L, 1);
    lua_call(L, 2, 1);
    ok &= Check(!lua_toboolean(L, -1), "sprint denied while crouched");
    lua_pop(L, 2);

    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "tickFire");
    lua_newtable(L);
    lua_pushstring(L, "semi");
    lua_setfield(L, -2, "fireMode");
    lua_pushnumber(L, 10);
    lua_setfield(L, -2, "roundsPerSecond");
    lua_pushnumber(L, 12);
    lua_setfield(L, -2, "magazine");
    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 1.0);
    lua_pushboolean(L, 1);
    lua_pushboolean(L, 1);
    lua_call(L, 4, 1);
    ok &= Check(lua_tointeger(L, -1) == 1, "semi fires once on a large dt");
    lua_pop(L, 1);
    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 0.1);
    lua_pushboolean(L, 1);
    lua_pushboolean(L, 0);
    lua_call(L, 4, 1);
    ok &= Check(lua_tointeger(L, -1) == 0, "semi does not repeat without a new edge");
    lua_pop(L, 3);

    const int shots15 = AutoShots(L, 1.0 / 15.0, 1.0 + 1.0 / 120.0, 10.0);
    const int shots30 = AutoShots(L, 1.0 / 30.0, 1.0 + 1.0 / 120.0, 10.0);
    const int shots60 = AutoShots(L, 1.0 / 60.0, 1.0 + 1.0 / 120.0, 10.0);
    const int shots120 = AutoShots(L, 1.0 / 120.0, 1.0 + 1.0 / 120.0, 10.0);
    ok &= Check(shots15 == shots30 && shots30 == shots60 && shots60 == shots120 && shots15 > 0,
                "automatic 10 rps counts match across host steps");

    lua_settop(L, 0);
    lua_newtable(L);
    lua_pushstring(L, "auto");
    lua_setfield(L, -2, "fireMode");
    lua_pushnumber(L, 30);
    lua_setfield(L, -2, "roundsPerSecond");
    lua_pushnumber(L, 999);
    lua_setfield(L, -2, "magazine");
    const int hitchStateRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "tickFire");
    lua_remove(L, -2);
    lua_rawgeti(L, LUA_REGISTRYINDEX, hitchStateRef);
    lua_pushnumber(L, 1.0);
    lua_pushboolean(L, 1);
    lua_pushboolean(L, 0);
    if (lua_pcall(L, 4, 1, 0) != LUA_OK) {
        std::cerr << "[hitch] " << lua_tostring(L, -1) << '\n';
        lua_pop(L, 1);
        ok = false;
    } else {
    ok &= Check(lua_tointeger(L, -1) == 4, "hitch emits at most 4 automatic shots");
    lua_pop(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, hitchStateRef);
    lua_getfield(L, -1, "now");
    const double now = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "nextFire");
    ok &= Check(Near(lua_tonumber(L, -1), now + 1.0 / 30.0), "capped hitch sets nextFire to now + interval");
    lua_pop(L, 2);
    luaL_unref(L, LUA_REGISTRYINDEX, hitchStateRef);
    }

    lua_settop(L, 0);
    lua_newtable(L);
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "magazine");
    lua_pushnumber(L, 12);
    lua_setfield(L, -2, "magazineSize");
    lua_pushnumber(L, 5);
    lua_setfield(L, -2, "reserve");
    lua_pushnumber(L, 1.4);
    lua_setfield(L, -2, "reloadSeconds");
    const int reloadRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "tryStartReload");
    lua_rawgeti(L, LUA_REGISTRYINDEX, reloadRef);
    lua_call(L, 1, 1);
    ok &= Check(lua_toboolean(L, -1), "reload starts");
    lua_pop(L, 2);
    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "advanceReload");
    lua_rawgeti(L, LUA_REGISTRYINDEX, reloadRef);
    lua_pushnumber(L, 0.7);
    lua_call(L, 2, 1);
    ok &= Check(!lua_toboolean(L, -1), "reload not complete at half time");
    lua_pop(L, 2);
    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "advanceReload");
    lua_rawgeti(L, LUA_REGISTRYINDEX, reloadRef);
    lua_pushnumber(L, 0.7);
    lua_call(L, 2, 1);
    ok &= Check(lua_toboolean(L, -1), "reload completes from summed dt");
    lua_pop(L, 2);
    lua_rawgeti(L, LUA_REGISTRYINDEX, reloadRef);
    lua_getfield(L, -1, "magazine");
    ok &= Check(lua_tointeger(L, -1) == 5, "reload moves min(space, reserve)");
    lua_pop(L, 1);
    lua_getfield(L, -1, "reserve");
    ok &= Check(lua_tointeger(L, -1) == 0, "reserve empties");
    lua_pop(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, reloadRef);

    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "wrapSlot");
    lua_pushinteger(L, 4);
    lua_pushinteger(L, 4);
    lua_pushinteger(L, 1);
    lua_call(L, 3, 1);
    ok &= Check(lua_tointeger(L, -1) == 1, "slot wrap");
    lua_pop(L, 1);
    lua_getfield(L, -1, "selectSlot");
    lua_pushinteger(L, 5);
    lua_pushinteger(L, 4);
    lua_call(L, 2, 1);
    ok &= Check(lua_isnil(L, -1), "missing numeric slot is ignored");
    lua_pop(L, 2);

    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "pickInteract");
    lua_newtable(L);
    lua_pushstring(L, "door");
    lua_setfield(L, -2, "key");
    lua_pushnumber(L, 1.0);
    lua_setfield(L, -2, "distance");
    lua_newtable(L);
    lua_newtable(L);
    lua_pushnumber(L, 2.5);
    lua_setfield(L, -2, "maxDistance");
    lua_setfield(L, -2, "door");
    lua_pushnumber(L, 2.5);
    lua_call(L, 3, 1);
    ok &= Check(lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "door",
                "interact pick accepts a registered hit");
    lua_pop(L, 1);
    lua_getfield(L, -1, "pickInteract");
    lua_newtable(L);
    lua_pushstring(L, "wall");
    lua_setfield(L, -2, "key");
    lua_pushnumber(L, 0.5);
    lua_setfield(L, -2, "distance");
    lua_newtable(L);
    lua_pushnumber(L, 2.5);
    lua_call(L, 3, 1);
    ok &= Check(lua_isnil(L, -1), "unregistered hit is rejected");
    lua_pop(L, 2);

    lua_getglobal(L, "Logic");
    lua_getfield(L, -1, "createHealth");
    lua_pushnumber(L, 100);
    lua_call(L, 1, 1);
    lua_getfield(L, -2, "applyDamage");
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 40);
    lua_call(L, 2, 1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "health");
    ok &= Check(Near(lua_tonumber(L, -1), 60), "health subtracts");
    lua_pop(L, 1);
    lua_getfield(L, -2, "heal");
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 100);
    lua_call(L, 2, 0);
    lua_getfield(L, -1, "health");
    ok &= Check(Near(lua_tonumber(L, -1), 100), "heal clamps at max");
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "invulnerable");
    lua_getfield(L, -2, "applyDamage");
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 50);
    lua_call(L, 2, 1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "health");
    ok &= Check(Near(lua_tonumber(L, -1), 100), "invulnerable ignores damage");
    lua_pop(L, 1);
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "invulnerable");
    lua_getfield(L, -2, "applyDamage");
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 200);
    lua_call(L, 2, 1);
    ok &= Check(lua_toboolean(L, -1), "death fires once");
    lua_pop(L, 1);
    lua_getfield(L, -2, "applyDamage");
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 10);
    lua_call(L, 2, 1);
    ok &= Check(!lua_toboolean(L, -1), "second death does not fire");
    lua_pop(L, 3);

    lua_getglobal(L, "require");
    lua_pushstring(L, "FPS.Registry");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        std::cerr << lua_tostring(L, -1) << '\n';
        ok = false;
    } else {
        lua_getfield(L, -1, "ensureSession");
        lua_call(L, 0, 0);
        lua_getfield(L, -1, "health");
        lua_pushinteger(L, 42);
        lua_newtable(L);
        lua_pushnumber(L, 7);
        lua_setfield(L, -2, "health");
        lua_settable(L, -3);
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "sessionOpen");
        lua_getfield(L, -1, "ensureSession");
        lua_call(L, 0, 0);
        lua_getfield(L, -1, "health");
        lua_pushinteger(L, 42);
        lua_gettable(L, -2);
        ok &= Check(lua_isnil(L, -1), "ensureSession drops previous health at recycled id");
        lua_pop(L, 3);
    }

    lua_close(L);
    if (!ok) {
        return 1;
    }
    std::cout << "FPSLogicTests passed\n";
    return 0;
}
