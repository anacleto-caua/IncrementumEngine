#pragma once

#include <functional>

#include <SDL3/SDL.h>

namespace Input {
    enum class ActionType {
        Press,
        Release
    };

    using UserAction = std::function<void()>;

    inline bool IsActive = true;

    namespace Mouse {
        enum class Button {
            Left,
            Right,
            Middle,

            _BUTTON_COUNT_
        };

        void Free();
        void Capture();

        bool IsButtonDown(Button button);

        inline float XPos = 0;
        inline float YPos = 0;

        inline float XDelta = 0;
        inline float YDelta = 0;

        void RegisterCallback(Button Button, ActionType ActionType, UserAction Callback);
        void RegisterCallback(Button Button, UserAction Callback);
    }

    namespace Keyboard {
        enum class Key {
            Forward,
            Backward,
            Right,
            Left,
            Up,
            Down,
            Interact,
            Space,
            Escape,
            Ctrl,
            Shift,

            _KEY_COUNT_
        };

        bool IsKeyDown(Key key);

        void RegisterCallback(Key Key, ActionType ActionType, UserAction Callback);
        void RegisterCallback(Key Key, UserAction Callback);
    }
}
