#include "GlfwGamepad.h"

#include "InputSystem.h"

#include <GLFW/glfw3.h>

namespace Hephaestus {

void PollGlfwGamepad(InputSystem& input)
{
    GLFWgamepadstate state{};
    const bool connected = glfwJoystickIsGamepad(GLFW_JOYSTICK_1) == GLFW_TRUE &&
                           glfwGetGamepadState(GLFW_JOYSTICK_1, &state) == GLFW_TRUE;
    input.SetGamepadConnected(connected);
    if (!connected) {
        return;
    }
    for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
        input.SetGamepadButtonState(button, state.buttons[button] == GLFW_PRESS);
    }
    for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
        float value = state.axes[axis];
        if (axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER || axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            value = (value + 1.0f) * 0.5f;
        }
        input.SetGamepadAxisState(axis, value);
    }
}

} // namespace Hephaestus
