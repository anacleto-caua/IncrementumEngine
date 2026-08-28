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

    enum class MouseButton {
        Left,
        Right,
        Middle,

        _BUTTON_COUNT_
    };

    enum class Key {
        // Letters
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        // Numbers (Top Row)
        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

        // Function Keys
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

        // Modifiers
        LCtrl, LShift, LAlt, LGUI,
        RCtrl, RShift, RAlt, RGUI,

        // Navigation / Control
        Space, Enter, Escape, Backspace, Tab,
        Insert, Delete, Home, End, PageUp, PageDown,

        // Arrows
        Left, Right, Up, Down,

        // Punctuation / Math
        LeftBracket, RightBracket, Semicolon, Apostrophe, Comma, Period, Slash, Backslash, Grave, Minus, Equals,

        _KEY_COUNT_
    };

    inline float MouseXPos = 0;
    inline float MouseYPos = 0;

    inline float MouseXDelta = 0;
    inline float MouseYDelta = 0;

    // MouseScrollX will only apply to weird stuff like trackpads and shift-scroll
    inline float MouseScrollX = 0;
    // Positive : away from user
    inline float MouseScrollY = 0;

    void CaptureMouse();
    void FreeMouse();

    bool IsButtonDown(MouseButton button);
    bool IsKeyDown(Key key);

    bool WasButtonPressed(MouseButton button);
    bool WasButtonReleased(MouseButton button);
    bool WasKeyPressed(Key key);
    bool WasKeyReleased(Key key);

    void RegisterCallback(MouseButton button, ActionType action, UserAction callback);
    void RegisterCallback(MouseButton button, UserAction callback);

    void RegisterCallback(Key key, ActionType action, UserAction callback);
    void RegisterCallback(Key key, UserAction callback);
}
