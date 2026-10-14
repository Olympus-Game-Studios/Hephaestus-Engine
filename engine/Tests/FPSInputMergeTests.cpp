#include "FPSInputMerge.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    nlohmann::json dest = {
        {"version", 1},
        {"actions", nlohmann::json::array({{{"name", "Jump"},
                                            {"bindings", nlohmann::json::array(
                                                {{{"type", "keyboard"}, {"control", "SPACE"}}})}}})},
        {"axes", nlohmann::json::array()}
    };
    nlohmann::json fragment = {
        {"actions", nlohmann::json::array({
             {{"name", "Jump"}, {"bindings", nlohmann::json::array({{{"type", "keyboard"}, {"control", "F"}}})}},
             {{"name", "Crouch"}, {"bindings", nlohmann::json::array({{{"type", "keyboard"}, {"control", "LEFT_CTRL"}}})}},
         })},
        {"axes", nlohmann::json::array(
             {{{"name", "LookStickX"}, {"clamp", true}, {"bindings", nlohmann::json::array()}}})}
    };

    auto first = HephaestusFPS::MergeInputFragment(dest, fragment);
    bool ok = true;
    ok &= Check(first.changed && first.added.size() == 2, "missing Crouch and LookStickX are added");
    ok &= Check(dest["actions"][0]["bindings"][0]["control"] == "SPACE",
                "existing Jump bindings are not rewritten");
    ok &= Check(HephaestusFPS::HasNamedEntry(dest["actions"], "Crouch"), "Crouch appended once");

    auto second = HephaestusFPS::MergeInputFragment(dest, fragment);
    ok &= Check(!second.changed && second.added.empty(), "second merge is idempotent");

    if (!ok) {
        return 1;
    }
    std::cout << "FPSInputMergeTests passed\n";
    return 0;
}
