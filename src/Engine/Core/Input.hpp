// One must be carefull since this class utility is tied to the glfw
// library that has it's context initializated at the Window class

#pragma once

#include <functional>

#include <GLFW/glfw3.h>

namespace Input {
    enum class ActionType {
        Press,
        Repeat,
        Release
    };

    using UserAction = std::function<void()>;

    inline bool IsActive = true;

    void Create();
    void Destroy();

    void PollInput();

    namespace Mouse {
        enum class Button {
            Left,
            Right,

            _BUTTON_COUNT_
        };

        void RegisterCallback(ActionType ActionType, Button Button, UserAction Callback);
        void RegisterCallback(Button Button, UserAction Callback);

        inline double XPos = 0;
        inline double YPos = 0;

        inline double XDelta = 0;
        inline double YDelta = 0;

        void Poll();
    };

    namespace Keyboard {
        enum class Key {
            Forward,
            Backward,
            Right,
            Left,
            Up,
            Down,
            Interact,
            Escape,
            Ctrl,
            Shift,

            _KEY_COUNT_
        };

        void RegisterCallback(ActionType ActionType, Key Key, UserAction Callback);
        void RegisterCallback(Key Key, UserAction Callback);
    };
};
