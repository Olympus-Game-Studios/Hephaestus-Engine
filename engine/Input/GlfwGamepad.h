#pragma once

namespace Hephaestus {

class InputSystem;

// Reads the first connected GLFW gamepad into the input system. GLFW must already be initialized.
void PollGlfwGamepad(InputSystem& input);

} // namespace Hephaestus
