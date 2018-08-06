#pragma once

// Windows includes
#include "../os/windows_include.h"
#include <xinput.h>
#pragma comment(lib, "XInput.lib")

// C includes
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>

// C++ includes
#include <chrono>
#include <iostream>
#include <thread>

// local includes
#include "joystick_base.h"

namespace BoBRobotics {
namespace HID {
using namespace std::literals;

class JoystickWindows;

//! Joystick is set to JoystickLinux on Linux and JoystickWindows on Windows
using Joystick = JoystickWindows;

//! Controller axes, including thumbsticks, triggers and D-pad (Windows)
enum class JAxisWindows
{
    LeftStickHorizontal = 0,
    LeftStickVertical = 1,
    RightStickHorizontal = 3,
    RightStickVertical = 4,
    LeftTrigger = 2,
    RightTrigger = 5,
    DpadHorizontal = 6,
    DpadVertical = 7,
    LENGTH
};

//! JAxis is set to JAxisLinux on Linux and JAxisWindows on Windows
using JAxis = JAxisWindows;

/*!
 * \brief Controller buttons (Windows)
 * 
 * The left stick and right stick are also buttons (you can click them).
 */
enum class JButtonWindows
{
    Start = 4,
    Back,
    LeftStick,
    RightStick,
    LB,
    RB,
    A = 12,
    B,
    X,
    Y,
    LENGTH
};

//! JButton is set to JButtonLinux on Linux and JButtonWindows on Windows
using JButton = JButtonWindows;

/*!
 * \brief Class for reading from joysticks on Windows.
 * 
 * *NOTE*: This class should not be used directly; see example in joystick_test.
 */
class JoystickWindows
  : public JoystickBase<JoystickWindows, JAxisWindows, JButtonWindows>
{
public:
    //! Open default joystick device with (optionally) specified dead zone
    JoystickWindows(float deadZone = 0.0f)
      : JoystickBase(deadZone)
    {
        // Read XInput state
        read(m_State);

        // Set initial button states
        for (size_t i = toIndex(JButton::Start); i < toIndex(JButton::LENGTH); i++) {
            m_ButtonState[i] = (m_State.Gamepad.wButtons >> i) & 1;
        }

        // Set initial axis states
        updateAxes(m_State, true);
    }

    //! Block and keep updating the joystick on the current thread
    virtual void run() override
    {
        while (m_DoRun) {
            update();
            std::this_thread::sleep_for(50ms);
        }
    }

    virtual bool update() override
    {
        // Read XInput state
        read(m_NewState);

        // unset Pressed and Released bits for buttons
        for (auto &s : m_ButtonState) {
            s &= StateDown;
        }

        // Check that something has changed
        if (m_State.dwPacketNumber == m_NewState.dwPacketNumber) {
            return false;
        }

        // Check buttons for changes
        for (size_t i = toIndex(JButton::Start); i < toIndex(JButton::LENGTH); i++) {
            uint8_t &s = m_ButtonState[i];
            const uint8_t pressed = (m_NewState.Gamepad.wButtons >> i) & 1;
            if (pressed != (s & StateDown)) {
                // ... then this button has been pressed or released
                if (pressed) {
                    s |= (StateDown | StatePressed); // set StateDown and StatePressed
                } else {
                    s &= ~StateDown;    // clear StateDown
                    s |= StateReleased; // set StateReleased
                }

                // run button event handlers
                raiseButtonEvent(toButton(i), pressed);
            }
        }

        // Check axes for changes
        updateAxes(m_NewState, false);

        // Save the current controller state
        m_State = m_NewState;

        return true;
    }

    //! Convert a raw 16-bit int value for an axis to a float
    static constexpr float axisToFloat(JAxis axis, int16_t value) const override
    {
        switch (axis) {
        case JAxis::LeftStickHorizontal:
        case JAxis::RightStickHorizontal:
            return value < 0 ? static_cast<float>(value) / int16_absminf
                             : static_cast<float>(value) / int16_maxf;
        case JAxis::LeftStickVertical:
        case JAxis::RightStickVertical:
            return value < 0 ? static_cast<float>(-value) / int16_absminf
                             : static_cast<float>(-value) / int16_maxf;
        case JAxis::LeftTrigger:
        case JAxis::RightTrigger:
            return static_cast<float>(value) / 255.0f;
        default:
            return static_cast<float>(value);
        }
    }

private:
    XINPUT_STATE m_State, m_NewState;
    static const int ControllerNum = 0;

    void updateAxes(XINPUT_STATE &state, bool isInitial)
    {
        updateAxis(JAxis::LeftStickHorizontal, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, isInitial);
        updateAxis(JAxis::RightStickHorizontal, state.Gamepad.sThumbRX, state.Gamepad.sThumbRY, isInitial);
        updateAxis(JAxis::LeftTrigger, static_cast<int16_t>(state.Gamepad.bLeftTrigger), isInitial);
        updateAxis(JAxis::RightTrigger, static_cast<int16_t>(state.Gamepad.bRightTrigger), isInitial);

        // D-pad
        updateAxis(JAxis::DpadVertical, getDpadValue(state.Gamepad.wButtons & 3), isInitial);
        updateAxis(JAxis::DpadHorizontal, getDpadValue((state.Gamepad.wButtons >> 2) & 3), isInitial);
    }

    static float getDpadValue(WORD buttons)
    {
        return buttons ? (buttons == 1 ? -1.0f : 1.0f) : 0.0f;
    }

    static void read(XINPUT_STATE &state)
    {
        // Zeroise the state
        ZeroMemory(&state, sizeof(state));

        // Get the state
        DWORD result = XInputGetState(ControllerNum, &state);
        if (result != ERROR_SUCCESS) {
            throw std::runtime_error("Could not read from joystick");
        }
    }
}; // Joystick
} // HID
} // BoBRobotics
