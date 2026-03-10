#include "Input.hpp"

#include <array>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Engine/Core/Window.hpp"

namespace Input {
    namespace Mouse {
        static constexpr float MOUSE_DELTA_CAP = .000001f;

        static constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::_BUTTON_COUNT_);
        static constexpr std::array<int, BUTTON_COUNT>GlfwMouseButtonMap = {
           GLFW_MOUSE_BUTTON_LEFT,
           GLFW_MOUSE_BUTTON_RIGHT,
        };

        struct ButtonState {
            bool IsPressed = false;
        };
        std::vector<ButtonState> KeyStates {};

        std::vector<UserAction> PressActions {};
        std::vector<UserAction> ReleaseActions {};

        std::vector<UserAction> RepeatActions {};
        std::vector<size_t> PollingKeys {}; // It's carries glfw keys which map to the repeat actions array

        void MouseCallbackStrain(
            [[maybe_unused]]GLFWwindow* window,
            int button,
            int action,
            [[maybe_unused]]int mods)
        {
            if (action == GLFW_PRESS) {
                if (static_cast<size_t>(button) < PressActions.size()) {
                    if (PressActions[button]) {
                        PressActions[button]();
                    }
                    KeyStates[button].IsPressed = true;
                }
                return;
            }
            if (action == GLFW_RELEASE) {
                if (static_cast<size_t>(button) < ReleaseActions.size()) {
                    if (ReleaseActions[button]) {
                        ReleaseActions[button]();
                    }
                    KeyStates[button].IsPressed = false;
                }
                return;
            }
        }

        void RegisterCallback(ActionType ActionType, Button Button, UserAction Callback) {
            size_t ButtonIndex = static_cast<size_t>(GlfwMouseButtonMap[static_cast<size_t>(Button)]);

            if (ButtonIndex >= KeyStates.size()) {
                KeyStates.resize(ButtonIndex + 1);
            }

            auto PushButtonAction = [&ButtonIndex, &Callback](std::vector<UserAction>& vec) {
                if (ButtonIndex >= vec.size()) {
                    vec.resize(ButtonIndex + 1);
                }

                if (vec[ButtonIndex]) {
                    spdlog::warn("Input action is being overwriten");
                }
                vec[ButtonIndex] = Callback;
            };

            switch (ActionType) {
                case ActionType::Press:
                    PushButtonAction(PressActions);
                    break;
                case ActionType::Repeat:
                    PushButtonAction(RepeatActions);
                    PollingKeys.push_back(ButtonIndex);
                    break;
                case ActionType::Release:
                    PushButtonAction(ReleaseActions);
                    break;
            }
        }

        void RegisterCallback(Button Button, UserAction Callback) {
            RegisterCallback(ActionType::Press, Button, Callback);
            RegisterCallback(ActionType::Repeat, Button, Callback);
            RegisterCallback(ActionType::Release, Button, Callback);
        }

        double LastFrameXPos = 0;
        double LastFrameYPos = 0;
        void Poll() {
            glfwGetCursorPos(Window::glfwWindow, &XPos, &YPos);

            XDelta = XPos - LastFrameXPos;
            YDelta = LastFrameYPos - YPos;
            if (XDelta < MOUSE_DELTA_CAP && XDelta > MOUSE_DELTA_CAP) {
                XDelta = 0;
            }
            if (YDelta < MOUSE_DELTA_CAP && YDelta > MOUSE_DELTA_CAP) {
                YDelta = 0;
            }

            LastFrameXPos = XPos;
            LastFrameYPos = YPos;
        }

        bool IsMouseFreed = true;
        void FreeMouseCursor() {
            IsMouseFreed = true;
            glfwSetInputMode(Window::glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        void CaptureMouseCursor() {
            IsMouseFreed = false;
            glfwSetInputMode(Window::glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }

    namespace Keyboard {
        static constexpr size_t KEY_COUNT = static_cast<size_t>(Key::_KEY_COUNT_);
        static constexpr std::array<int, KEY_COUNT>GlfwKeyMap = {
            GLFW_KEY_W,
            GLFW_KEY_S,
            GLFW_KEY_D,
            GLFW_KEY_A,
            GLFW_KEY_Q,
            GLFW_KEY_E,
            GLFW_KEY_F,
            GLFW_KEY_ESCAPE,
            GLFW_KEY_LEFT_CONTROL,
            GLFW_KEY_LEFT_SHIFT
        };

        struct KeyState {
            bool IsPressed = false;
        };
        std::vector<KeyState> KeyStates {};

        std::vector<UserAction> PressActions {};
        std::vector<UserAction> ReleaseActions {};

        std::vector<UserAction> RepeatActions {};
        std::vector<size_t> PollingKeys {}; // It's carries glfw keys which map to the repeat actions array

        // Map to find the InfKey
        static const std::unordered_map<int, Key> ReverseKeyMap = []() {
            std::unordered_map<int, Key> map;
            for (size_t i = 0; i < KEY_COUNT; ++i) {
                map[GlfwKeyMap[i]] = static_cast<Key>(i);
            }
            return map;
        }();

        Key ReverseMapInfKey(int glfwKey) {
            return ReverseKeyMap.find(glfwKey)->second;
        }

        void KeyCallbackStrain(
            [[maybe_unused]]GLFWwindow* window,
            int key,
            [[maybe_unused]]int scancode,
            int action,
            [[maybe_unused]]int mods)
        {
            if (action == GLFW_PRESS) {
                if (static_cast<size_t>(key) < PressActions.size()) {
                    if (PressActions[key]) {
                        PressActions[key]();
                    }
                    KeyStates[key].IsPressed = true;
                }
                return;
            }
            if (action == GLFW_RELEASE) {
                if (static_cast<size_t>(key) < ReleaseActions.size()) {
                    if (ReleaseActions[key]) {
                        ReleaseActions[key]();
                    }
                    KeyStates[key].IsPressed = false;
                }
                return;
            }
        }

        void RegisterCallback(ActionType ActionType, Key Key, UserAction Callback) {
            size_t KeyIndex = static_cast<size_t>(GlfwKeyMap[static_cast<size_t>(Key)]);

            if (KeyIndex >= KeyStates.size()) {
                KeyStates.resize(KeyIndex + 1);
            }

            auto PushKeyAction = [&KeyIndex, &Callback](std::vector<UserAction>& vec) {
                if (KeyIndex >= vec.size()) {
                    vec.resize(KeyIndex + 1);
                }

                if (vec[KeyIndex]) {
                    spdlog::warn("Input action is being overwriten");
                }
                vec[KeyIndex] = Callback;
            };

            switch (ActionType) {
                case ActionType::Press:
                    PushKeyAction(PressActions);
                    break;
                case ActionType::Repeat:
                    PushKeyAction(RepeatActions);
                    PollingKeys.push_back(KeyIndex);
                    break;
                case ActionType::Release:
                    PushKeyAction(ReleaseActions);
                    break;
            }
        }

        void RegisterCallback(Key Key, UserAction Callback) {
            RegisterCallback(ActionType::Press, Key, Callback);
            RegisterCallback(ActionType::Repeat, Key, Callback);
            RegisterCallback(ActionType::Release, Key, Callback);
        }
    }

    void Create() {
        glfwSetKeyCallback(Window::glfwWindow, Keyboard::KeyCallbackStrain);
        glfwSetMouseButtonCallback(Window::glfwWindow, Mouse::MouseCallbackStrain);

        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(Window::glfwWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        } else {
            spdlog::warn("Raw mouse motion not supported.");
        }

        Mouse::CaptureMouseCursor();
        Keyboard::RegisterCallback(Keyboard::Key::Escape, [](void){ Mouse::FreeMouseCursor(); });
        Mouse::RegisterCallback(Mouse::Button::Right, [](void){ Mouse::CaptureMouseCursor(); });
    }

    void Destroy() {
        // ...
    }

    void PollInput() {
        for(size_t glfwKey : Keyboard::PollingKeys) {
            if (Keyboard::KeyStates[glfwKey].IsPressed) {
                Keyboard::RepeatActions[glfwKey]();
            }
        }

        for(size_t glfwButton : Mouse::PollingKeys) {
            if (Mouse::KeyStates[glfwButton].IsPressed) {
                Mouse::RepeatActions[glfwButton]();
            }
        }

        Mouse::Poll();
    }
};
