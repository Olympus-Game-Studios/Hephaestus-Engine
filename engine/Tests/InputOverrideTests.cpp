#include "Input/InputSystem.h"

#include <cmath>
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

bool Near(float left, float right)
{
    return std::abs(left - right) <= 0.001f;
}

} // namespace

int main()
{
    bool ok = true;
    Hephaestus::InputSystem input;

    input.BeginFrame();
    input.SetActionOverride("Jump", 1.0f);
    ok &= Check(input.IsActionDown("Jump"), "Action override drives down state");
    ok &= Check(input.IsActionPressed("Jump"), "Override applied after BeginFrame has a rising edge");

    input.BeginFrame();
    ok &= Check(input.IsActionDown("Jump"), "Action override persists across frames");
    ok &= Check(!input.IsActionPressed("Jump"), "Persistent override does not repeat pressed edge");

    input.ClearActionOverrides();
    ok &= Check(input.IsActionReleased("Jump"), "Clearing override emits release in the current frame");

    input.SetAxisOverride("MoveForward", 0.75f);
    ok &= Check(Near(input.GetAxis("MoveForward"), 0.75f), "Axis override replaces mapped input");
    input.SetAxisOverride("MoveForward", 2.0f);
    ok &= Check(Near(input.GetAxis("MoveForward"), 1.0f), "Axis override is normalized");
    input.ClearAxisOverrides();
    ok &= Check(Near(input.GetAxis("MoveForward"), 0.0f), "Axis override clears");

    input.SetActionOverride("MissingAction", 1.0f);
    input.SetAxisOverride("MissingAxis", 1.0f);
    ok &= Check(!input.IsActionDown("MissingAction"), "Unknown actions cannot be injected");
    ok &= Check(Near(input.GetAxis("MissingAxis"), 0.0f), "Unknown axes cannot be injected");

    input.SetActionOverride("Jump", 1.0f);
    input.SetAxisOverride("MoveForward", -1.0f);
    input.Reset();
    ok &= Check(!input.IsActionDown("Jump"), "Reset clears action overrides");
    ok &= Check(Near(input.GetAxis("MoveForward"), 0.0f), "Reset clears axis overrides");

    input.QueueMouseDelta(40.0, -80.0);
    ok &= Check(input.GetMouseDeltaX() == 0.0 && input.GetMouseDeltaY() == 0.0,
                "queued mouse delta is not visible until BeginFrame");
    input.BeginFrame();
    ok &= Check(input.GetMouseDeltaX() == 40.0 && input.GetMouseDeltaY() == -80.0,
                "queued mouse delta applies unclamped on the next BeginFrame");
    input.AddMouseDelta(3.0, 4.0);
    ok &= Check(input.GetMouseDeltaX() == 43.0 && input.GetMouseDeltaY() == -76.0,
                "host device collection preserves injected mouse delta");
    input.BeginFrame();
    ok &= Check(input.GetMouseDeltaX() == 0.0 && input.GetMouseDeltaY() == 0.0,
                "queued mouse delta is consumed after one frame");

    input.QueueActionOverride("Jump", 1.0f);
    input.BeginFrame();
    ok &= Check(input.IsActionPressed("Jump"), "between-frame injection retains rising edge");
    input.BeginFrame();
    ok &= Check(input.IsActionDown("Jump") && !input.IsActionPressed("Jump"),
                "queued action remains held without repeating its edge");
    input.QueueActionOverride("Jump", 0.0f);
    input.BeginFrame();
    ok &= Check(input.IsActionReleased("Jump"), "queued release retains falling edge");
    input.QueueActionOverride("Jump", 1.0f);
    input.ClearOverrides();
    input.BeginFrame();
    ok &= Check(!input.IsActionDown("Jump"), "clear cancels pending injection");

    if (ok) {
        std::cout << "InputOverrideTests passed\n";
        return 0;
    }
    return 1;
}
