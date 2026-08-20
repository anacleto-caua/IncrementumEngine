#include "Platform.hpp"

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "Game/Game.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/Window.hpp"
#include "Engine/Core/WindowSDL.hpp"

namespace Input {
    void ProcessEvent(const SDL_Event& event);
    void ClearFrameEdges();
}

namespace Window {
    ResizeCallback UserResizeCallback = nullptr;
}

namespace Platform {
    bool ToClose = false;
    bool SDLCALL EventWatcher([[maybe_unused]]void* userdata, SDL_Event* event);

    IncResult Initialize(i32 width, i32 height, const std::string title, ResizeCallback callback) {
        using namespace Window;

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return IncResult::FAIL;
        }

        SdlWindow = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

        if (!SdlWindow) {
            return IncResult::FAIL;
        }

        UserResizeCallback = callback;
        ToClose = false;

        SDL_AddEventWatch(EventWatcher, nullptr);

        return IncResult::SUCCESS;
    }

    void Update() {
        Input::ClearFrameEdges();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            Input::ProcessEvent(event);
            GImGuiPass.ProcessEvent(event); // Kinda janky dependency, but well...
        }

        f32 x, y;
        SDL_GetMouseState(&x, &y);
        Input::MouseXPos = x;
        Input::MouseYPos = y;

        // Get the raw, unbounded relative movement directly from SDL
        f32 x_delta, y_delta;
        SDL_GetRelativeMouseState(&x_delta, &y_delta);

        Input::MouseXDelta = x_delta;
        Input::MouseYDelta = y_delta;
    }

    void Shutdown() {
        using namespace Window;

        SDL_RemoveEventWatch(EventWatcher, nullptr);

        if (SdlWindow) {
            SDL_DestroyWindow(SdlWindow);
            SdlWindow = nullptr;
        }
        SDL_Quit();
    }

    bool ShouldClose() {
        return ToClose;
    }

    bool SDLCALL EventWatcher([[maybe_unused]]void* userdata, SDL_Event* event) {
        using namespace Window;

        if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            ToClose = true;
        } else if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            if (UserResizeCallback) {
                UserResizeCallback(event->window.data1, event->window.data2);
            }
        }

        return true;
    }
}

namespace Input {
    template <u64 Size>
    struct Callbacks {
        std::array<UserAction, Size> Presses;
        std::array<UserAction, Size> Releases;
    };

    template <u64 Size>
    struct EdgeState {
        std::array<bool, Size> JustPressed{};
        std::array<bool, Size> JustReleased{};

        void Clear() {
            JustPressed.fill(false);
            JustReleased.fill(false);
        }
    };

    static constexpr size_t BUTTON_COUNT = static_cast<size_t>(MouseButton::_BUTTON_COUNT_);
    static constexpr u8 BUTTON_MAP [BUTTON_COUNT] = {
        SDL_BUTTON_LEFT,
        SDL_BUTTON_RIGHT,
        SDL_BUTTON_MIDDLE
    };

    static constexpr size_t KEY_COUNT = static_cast<size_t>(Key::_KEY_COUNT_);
    static constexpr SDL_Scancode KEY_MAP [KEY_COUNT] = {
        // Letters
        SDL_SCANCODE_A, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E,
        SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_I, SDL_SCANCODE_J,
        SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O,
        SDL_SCANCODE_P, SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
        SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X, SDL_SCANCODE_Y, SDL_SCANCODE_Z,

        // Numbers (Top Row)
        SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
        SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9,

        // Function Keys
        SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6,
        SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,

        // Modifiers
        SDL_SCANCODE_LCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT, SDL_SCANCODE_LGUI,
        SDL_SCANCODE_RCTRL, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RALT, SDL_SCANCODE_RGUI,

        // Navigation / Control
        SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB,
        SDL_SCANCODE_INSERT, SDL_SCANCODE_DELETE, SDL_SCANCODE_HOME, SDL_SCANCODE_END, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_PAGEDOWN,

        // Arrows
        SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,

        // Punctuation / Math
        SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE,
        SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_BACKSLASH,
        SDL_SCANCODE_GRAVE, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS
    };

    static Callbacks<BUTTON_COUNT> MouseCallbacks;
    static Callbacks<KEY_COUNT> KeyCallbacks;

    static EdgeState<BUTTON_COUNT> MouseEdges;
    static EdgeState<KEY_COUNT> KeyEdges;

    bool IsButtonDown(MouseButton button) {
        u32 state = SDL_GetMouseState(nullptr, nullptr);
        return (state & SDL_BUTTON_MASK(BUTTON_MAP[static_cast<size_t>(button)])) != 0;
    }

    bool IsKeyDown(Key key) {
        const bool* state = SDL_GetKeyboardState(nullptr);
        return state[KEY_MAP[static_cast<size_t>(key)]];
    }

    bool WasButtonPressed(MouseButton button) {
        return MouseEdges.JustPressed[static_cast<size_t>(button)];
    }

    bool WasButtonReleased(MouseButton button) {
        return MouseEdges.JustReleased[static_cast<size_t>(button)];
    }

    bool WasKeyPressed(Key key) {
        return KeyEdges.JustPressed[static_cast<size_t>(key)];
    }

    bool WasKeyReleased(Key key) {
        return KeyEdges.JustReleased[static_cast<size_t>(key)];
    }

    void CaptureMouse() {
        SDL_SetWindowRelativeMouseMode(Window::SdlWindow, true);
    }

    void FreeMouse() {
        SDL_SetWindowRelativeMouseMode(Window::SdlWindow, false);
    }

    void RegisterCallback(MouseButton button, ActionType type, UserAction callback) {
        size_t idx = static_cast<size_t>(button);
        if (type == ActionType::Press) MouseCallbacks.Presses[idx] = callback;
        else MouseCallbacks.Releases[idx] = callback;
    }

    void RegisterCallback(MouseButton button, UserAction callback) {
        RegisterCallback(button, ActionType::Press, callback);
    }

    void RegisterCallback(Key key, ActionType type, UserAction callback) {
        size_t idx = static_cast<size_t>(key);
        if (type == ActionType::Press) KeyCallbacks.Presses[idx] = callback;
        else KeyCallbacks.Releases[idx] = callback;
    }

    void RegisterCallback(Key key, UserAction callback) {
        RegisterCallback(key, ActionType::Press, callback);
    }

    void ClearFrameEdges() {
        MouseEdges.Clear();
        KeyEdges.Clear();
    }

    void ProcessEvent(const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            if (event.key.repeat) return;

            for (size_t i = 0; i < KEY_COUNT; ++i) {
                if (event.key.scancode == KEY_MAP[i]) {
                    bool pressed = event.type == SDL_EVENT_KEY_DOWN;
                    if (pressed) KeyEdges.JustPressed[i] = true;
                    else KeyEdges.JustReleased[i] = true;

                    auto& actionList = pressed ? KeyCallbacks.Presses : KeyCallbacks.Releases;
                    if (actionList[i]) actionList[i]();
                    break;
                }
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            for (size_t i = 0; i < BUTTON_COUNT; ++i) {
                if (event.button.button == BUTTON_MAP[i]) {
                    bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if (pressed) MouseEdges.JustPressed[i] = true;
                    else MouseEdges.JustReleased[i] = true;

                    auto& actionList = pressed ? MouseCallbacks.Presses : MouseCallbacks.Releases;
                    if (actionList[i]) actionList[i]();
                    break;
                }
            }
        }
    }
}

namespace Window {
    std::vector<const char*> GetRequiredExtensions() {
        u32 count = 0;
        char const* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        return std::vector<const char*>(extensions, extensions + count);
    }

    bool CreateSurface(VkInstance instance, VkSurfaceKHR &surface) {
        return SDL_Vulkan_CreateSurface(SdlWindow, instance, nullptr, &surface);
    }

    void GetFramebufferSize(i32 &width, i32 &height) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(SdlWindow, &w, &h);
        width = w;
        height = h;
    }

    void SetSize(u32 width, u32 height) {
        SDL_SetWindowSize(SdlWindow, static_cast<i32>(width), static_cast<i32>(height));
    }

    void SetTitle(const char* title) {
        SDL_SetWindowTitle(SdlWindow, title);
    }

    void SetWindowMode(WindowMode mode) {
        CurrentMode = mode;
        switch(mode) {
            case WindowMode::Windowed:
                SDL_SetWindowFullscreen(SdlWindow, 0);
                break;
            case WindowMode::Borderless:
                SDL_SetWindowFullscreen(SdlWindow, SDL_WINDOW_BORDERLESS);
                break;
            case WindowMode::Fullscreen:
                SDL_SetWindowFullscreen(SdlWindow, SDL_WINDOW_FULLSCREEN);
                break;
        }
    }

    void ToggleFullscreen() {
        if(CurrentMode == WindowMode::Windowed) {
            SetWindowMode(WindowMode::Fullscreen);
        } else {
            SetWindowMode(WindowMode::Windowed);
         }
    }

    void CenterPositionOnCurrentMonitor() {
        SDL_SetWindowPosition(SdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}
