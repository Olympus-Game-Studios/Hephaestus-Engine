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
    if (luaL_dostring(L, R"(
        local s = { fireMode = "auto", roundsPerSecond = 10, magazine = 999 }
        Logic.tickFire(s, 1 / 60, true, true)
        for _ = 1, 60 do Logic.tickFire(s, 1 / 60, false, false) end
        local repress = Logic.tickFire(s, 1 / 60, true, true)
        s.reloadRemaining = 1
        for _ = 1, 60 do Logic.tickFire(s, 1 / 60, true, false) end
        s.reloadRemaining = 0
        local afterReload = Logic.tickFire(s, 1 / 60, true, false)
        return repress, afterReload
    )") != LUA_OK) {
        std::cerr << "[idle] " << lua_tostring(L, -1) << '\n';
        ok = false;
    } else {
        ok &= Check(lua_tointeger(L, -2) == 1, "automatic re-press after idle fires one shot");
        ok &= Check(lua_tointeger(L, -1) == 1, "automatic fire after a held reload fires one shot");
    }
    lua_settop(L, 0);

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

    static const char* kGunplayChecks = R"lua(
local Gunplay = require("FPS.Gunplay")
local function near(a, b, eps) return math.abs(a - b) <= (eps or 1e-6) end
local failures = {}
local function check(cond, message) if not cond then failures[#failures + 1] = message end end

local weapon = { hipSpread = 2, moveSpread = 2.5, airSpread = 4, maxSpread = 7, adsSpread = 0.1,
                 adsBloomScale = 0.5, bloomPerShot = 0.6, maxBloom = 1.0, bloomDecay = 2,
                 damage = 30, falloffStart = 25, falloffEnd = 50, falloffMinMultiplier = 0.6,
                 headMultiplier = 1.5, limbMultiplier = 0.9 }
check(near(Gunplay.spreadDegrees(weapon, { adsBlend = 0, moveFraction = 0, bloom = 0 }), 2), "standing hip spread")
check(near(Gunplay.spreadDegrees(weapon, { adsBlend = 0, moveFraction = 1, bloom = 0 }), 4.5), "moving widens spread")
check(near(Gunplay.spreadDegrees(weapon, { adsBlend = 0, moveFraction = 1, airborne = true, bloom = 1 }), 7),
      "spread clamps at maxSpread")
check(near(Gunplay.spreadDegrees(weapon, { adsBlend = 1, moveFraction = 1, bloom = 1 }), 0.6), "ADS spread is tight")
local bloom = Gunplay.addBloom(Gunplay.addBloom(0, weapon), weapon)
check(near(bloom, 1.0), "bloom grows per shot and caps")
check(near(Gunplay.decayBloom(bloom, weapon, 0.25), 0.5), "bloom decays over time")

check(near(Gunplay.shotDamage(weapon, 10, "body"), 30), "full damage inside falloff start")
check(near(Gunplay.shotDamage(weapon, 37.5, "body"), 24), "falloff is linear between start and end")
check(near(Gunplay.shotDamage(weapon, 80, "body"), 18), "falloff floors at the minimum")
check(near(Gunplay.shotDamage(weapon, 10, "head"), 45), "head zone multiplies by 1.5")
check(near(Gunplay.shotDamage(weapon, 10, "limb"), 27), "limb zone multiplies by 0.9")

local pattern = Gunplay.parsePattern("0.5,0.1; 0.7,-0.2;0.9,0.3")
check(#pattern == 3 and near(pattern[2].pitch, 0.7) and near(pattern[2].yaw, -0.2), "pattern parses pitch,yaw pairs")
local function fixed() return 0.5 end
local p, y = Gunplay.recoilForShot(pattern, 2, 0.2, 0.2, fixed)
check(near(p, 0.7) and near(y, -0.2), "recoil follows the learnable pattern")
p, y = Gunplay.recoilForShot(pattern, 9, 0.2, 0.2, fixed)
check(near(p, 0.9) and near(y, 0.3), "the last pattern entry repeats")
p = Gunplay.recoilForShot(pattern, 1, 0.2, 0, function() return 0.999999 end)
check(p > 0.69 and p <= 0.7, "jitter stays inside its range")

check(near(Gunplay.absorbCounterInput(0.05, -0.02), 0.03), "pulling down spends accumulated recoil")
check(near(Gunplay.absorbCounterInput(0.05, -0.2), 0), "counter input never overshoots")
check(near(Gunplay.absorbCounterInput(0.05, 0.02), 0.05), "looking up keeps accumulated recoil")
local delta, rest = Gunplay.recoverRecoil(0.05, 0.1, 0.1)
check(near(delta, -0.01) and near(rest, 0.04), "recovery returns the aim at its rate")
delta, rest = Gunplay.recoverRecoil(0.005, 0.1, 0.1)
check(near(delta, -0.005) and near(rest, 0), "recovery stops at the original aim")

local spring = Gunplay.newSpring()
spring.v = 1
local peak = 0
for _ = 1, 120 do
    Gunplay.springStep(spring, 0, 260, 22, 1 / 120)
    peak = math.max(peak, spring.x)
end
check(peak > 0 and math.abs(spring.x) < 1e-3, "kick spring rises and settles back to rest")
local long = Gunplay.newSpring()
long.v = 1
Gunplay.springStep(long, 0, 260, 22, 0.25)
check(math.abs(long.x) < 1, "spring stays stable through a long frame")

for _, u in ipairs({ { 0.0, 0.0 }, { 0.999, 0.3 }, { 0.5, 0.75 } }) do
    local x, yy, z = Gunplay.spreadDirection(1, 0, 0, math.rad(3), u[1], u[2])
    local angle = math.deg(math.acos(math.max(-1, math.min(1, x))))
    check(near(x * x + yy * yy + z * z, 1, 1e-9) and angle <= 3.0001, "spread stays inside the cone")
end
local vx, vy, vz = Gunplay.spreadDirection(0, 1, 0, math.rad(2), 0.8, 0.1)
check(vy > math.cos(math.rad(2.0001)), "spread handles a vertical aim")

check(near(Gunplay.fireBlock(0, true, 0.22, 0.016), 0.22), "sprinting holds the sprint-to-fire block")
check(near(Gunplay.fireBlock(0.22, false, 0.22, 0.02), 0.2), "the block counts down after sprinting")

local cues = { { time = 0.1, sound = "a" }, { time = 0.5, sound = "b" }, { time = 0.9, sound = "bolt", emptyOnly = true } }
local crossed = Gunplay.reloadCuesCrossed(cues, 0.4, 0.95, false)
check(#crossed == 1 and crossed[1] == "b", "tactical reload skips the empty-only bolt cue")
crossed = Gunplay.reloadCuesCrossed(cues, 0.4, 0.95, true)
check(#crossed == 2 and crossed[2] == "bolt", "empty reload plays the bolt cue")

local Logic = require("FPS.Logic")
local state = { fireMode = "auto", roundsPerSecond = 10, magazine = 30, fireBlockedRemaining = 0.2 }
check(Logic.tickFire(state, 0.016, true, true) == 0, "a fire block stops shots")
state.fireBlockedRemaining = 0
check(Logic.tickFire(state, 0.016, true, false) == 1, "shots resume when the block ends")
local reload = { magazine = 0, magazineSize = 30, reserve = 60, reloadSeconds = 2.0, reloadEmptySeconds = 2.6 }
check(Logic.tryStartReload(reload) and near(reload.reloadRemaining, 2.6) and reload.reloadFromEmpty,
      "an empty magazine uses the empty reload time")
local tactical = { magazine = 5, magazineSize = 30, reserve = 60, reloadSeconds = 2.0, reloadEmptySeconds = 2.6 }
check(Logic.tryStartReload(tactical) and near(tactical.reloadRemaining, 2.0) and not tactical.reloadFromEmpty,
      "a partial magazine uses the tactical reload time")

if #failures > 0 then error(table.concat(failures, "\n")) end
)lua";
    if (luaL_dostring(L, kGunplayChecks) != LUA_OK) {
        std::cerr << "[FAIL] gunplay checks:\n" << lua_tostring(L, -1) << '\n';
        ok = false;
    }

    lua_close(L);
    if (!ok) {
        return 1;
    }
    std::cout << "FPSLogicTests passed\n";
    return 0;
}
