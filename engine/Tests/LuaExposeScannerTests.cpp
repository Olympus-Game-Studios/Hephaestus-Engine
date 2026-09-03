#include "Scripting/LuaExposeScanner.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool TestValidDeclarations()
{
    const auto result = LuaExposeScanner::Scan(R"(
Expose.number("moveSpeed", 5.0)
Expose.integer("ammo", 30)
Expose.bool("canSprint", true)
Expose.string("doorId", "main_door")
Expose.vec2("screenOffset", 1.0, -2.0)
Expose.vec3("spawnOffset", 0.0, 1.0, 2.0)
Expose.color("tint", 1.0, 0.8, 0.6, 1.0)
)");

    bool ok = true;
    ok &= Check(result.valid, "valid declarations accepted");
    ok &= Check(result.diagnostics.empty(), "valid declarations have no diagnostics");
    ok &= Check(result.fields.size() == 7, "all supported types discovered");
    if (result.fields.size() == 7) {
        ok &= Check(result.fields[0].name == "moveSpeed", "source order preserved");
        ok &= Check(result.fields[6].name == "tint", "last declaration preserved");
        ok &= Check(result.fields[0].defaultValue.IsNumber(), "number default represented");
        ok &= Check(result.fields[2].defaultValue.IsBool(), "bool default represented");
        ok &= Check(result.fields[3].defaultValue.IsString(), "string default represented");
        ok &= Check(result.fields[4].defaultValue.IsVec2(), "vec2 default represented");
        ok &= Check(result.fields[5].defaultValue.IsVec3(), "vec3 default represented");
        ok &= Check(result.fields[6].defaultValue.IsVec4(), "color default represented as vec4");
    }
    return ok;
}

bool TestIgnoredTextAndMultiline()
{
    const auto result = LuaExposeScanner::Scan(R"(
-- Expose.number("lineComment", 1)
local example = 'Expose.number("stringValue", 2)'
--[=[
Expose.bool("blockComment", true)
]=]
Expose.vec3(
    "offset",
    0.0,
    1.0,
    0.0,
)
)");

    return Check(result.valid, "ignored text scan remains valid") &&
           Check(result.fields.size() == 1, "comments and strings ignored") &&
           Check(result.fields.front().name == "offset", "multiline declaration parsed");
}

bool TestMalformedDeclarations()
{
    const auto result = LuaExposeScanner::Scan(R"(
Expose.number()
Expose.number("missingDefault")
Expose.number(42, 5)
Expose.number("wrongType", "fast")
Expose.unknown("thing", 1)
Expose.number("expression", DEFAULT_SPEED * 2)
Expose.integer("fraction", 1.5)
Expose.bool("validAfterErrors", false)
)");

    return Check(result.valid, "non-conflicting malformed declarations are recoverable") &&
           Check(result.fields.size() == 1, "valid declaration survives malformed neighbors") &&
           Check(result.fields.front().name == "validAfterErrors", "scanner recovers after errors") &&
           Check(result.diagnostics.size() == 7, "all malformed declarations diagnosed");
}

bool TestDuplicateNamesInvalidateScan()
{
    const auto result = LuaExposeScanner::Scan(R"(
Expose.number("speed", 5)
Expose.bool("speed", true)
)");

    return Check(!result.valid, "duplicate name invalidates scan") &&
           Check(result.fields.empty(), "invalid duplicate scan exposes no partial metadata") &&
           Check(result.diagnostics.size() == 1 && result.diagnostics.front().conflict,
                 "duplicate conflict diagnosed");
}

bool TestDeterministicHash()
{
    const std::string source = "Expose.number(\"speed\", 5)\n";
    const std::string hash = LuaExposeScanner::ComputeContentHash(source);
    return Check(hash == LuaExposeScanner::ComputeContentHash(source), "content hash deterministic") &&
           Check(hash != LuaExposeScanner::ComputeContentHash(source + " "), "content hash changes with source");
}

} // namespace

int main()
{
    bool ok = true;
    ok &= TestValidDeclarations();
    ok &= TestIgnoredTextAndMultiline();
    ok &= TestMalformedDeclarations();
    ok &= TestDuplicateNamesInvalidateScan();
    ok &= TestDeterministicHash();
    if (ok) {
        std::cout << "LuaExposeScannerTests passed\n";
        return 0;
    }
    return 1;
}
