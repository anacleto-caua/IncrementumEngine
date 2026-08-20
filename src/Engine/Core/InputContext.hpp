#pragma once

#include <vector>

#include "Engine/Core/Input.hpp"
#include "Core/EnumIndexedArray.hpp"

template <typename ActionEnum, ActionEnum Count>
class InputContext {
public:
    void BindKey(ActionEnum action, Input::Key key) {
        KeyBindings[action].push_back(key);
    }

    void BindButton(ActionEnum action, Input::MouseButton button) {
        ButtonBindings[action].push_back(button);
    }

    void OnPressed(ActionEnum action, Input::UserAction callback) {
        PressCallbacks[action] = std::move(callback);
    }

    void OnReleased(ActionEnum action, Input::UserAction callback) {
        ReleaseCallbacks[action] = std::move(callback);
    }

    void OnHeld(ActionEnum action, Input::UserAction callback) {
        HeldCallbacks[action] = std::move(callback);
    }

    bool IsActionDown(ActionEnum action) const {
        for (Input::Key key : KeyBindings[action]) if (Input::IsKeyDown(key)) return true;
        for (Input::MouseButton button : ButtonBindings[action]) if (Input::IsButtonDown(button)) return true;

        return false;
    }

    bool WasActionPressed(ActionEnum action) const {
        for (Input::Key key : KeyBindings[action]) if (Input::WasKeyPressed(key)) return true;
        for (Input::MouseButton button : ButtonBindings[action]) if (Input::WasButtonPressed(button)) return true;

        return false;
    }

    bool WasActionReleased(ActionEnum action) const {
        for (Input::Key key : KeyBindings[action]) if (Input::WasKeyReleased(key)) return true;
        for (Input::MouseButton button : ButtonBindings[action]) if (Input::WasButtonReleased(button)) return true;

        return false;
    }

    void Frame() {
        for (u64 i = 0; i < static_cast<u64>(Count); ++i) {
            ActionEnum action = static_cast<ActionEnum>(i);

            if (WasActionPressed(action) && PressCallbacks[action]) PressCallbacks[action]();
            if (WasActionReleased(action) && ReleaseCallbacks[action]) ReleaseCallbacks[action]();
            if (IsActionDown(action) && HeldCallbacks[action]) HeldCallbacks[action]();
        }
    }

private:
    EnumIndexedArray<std::vector<Input::Key>, ActionEnum, Count> KeyBindings;
    EnumIndexedArray<std::vector<Input::MouseButton>, ActionEnum, Count> ButtonBindings;

    EnumIndexedArray<Input::UserAction, ActionEnum, Count> PressCallbacks;
    EnumIndexedArray<Input::UserAction, ActionEnum, Count> ReleaseCallbacks;
    EnumIndexedArray<Input::UserAction, ActionEnum, Count> HeldCallbacks;
};
