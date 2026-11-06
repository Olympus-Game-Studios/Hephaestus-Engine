#pragma once

namespace Hephaestus {

class InputSystem;

// Reads GLFW joystick 1 into the input system. GLFW must already be initialized.
void PollGlfwGamepad(InputSystem& input);

} // namespace Hephaestus
