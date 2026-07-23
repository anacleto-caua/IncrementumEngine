#include "Platform.hpp"

#include <string>

#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "Engine/Core/Input.hpp"
#include "Engine/Core/Window.hpp"
#include "Engine/Core/WindowSDL.hpp"
#include "Renderer/Passes/ImGuiPass.hpp"

namespace Input {
    void Create();
    void ProcessEvent(const SDL_Event& event);
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

        Input::Create();

        return IncResult::SUCCESS;
    }

    void Update() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            Input::ProcessEvent(event);
            ImGuiPass::ProcessEvent(event); // Kinda janky dependency, but well...
        }

        f32 x, y;
        SDL_GetMouseState(&x, &y);
        Input::Mouse::XPos = x;
        Input::Mouse::YPos = y;

        // Get the raw, unbounded relative movement directly from SDL
        f32 x_delta, y_delta;
        SDL_GetRelativeMouseState(&x_delta, &y_delta);

        Input::Mouse::XDelta = x_delta;
        Input::Mouse::YDelta = y_delta;
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
    struct Callbacks {
        std::vector<UserAction> Presses;
        std::vector<UserAction> Releases;
    };

    namespace Mouse {
        static constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::_BUTTON_COUNT_);
        static constexpr u8 BUTTON_MAP [BUTTON_COUNT] = {
            SDL_BUTTON_LEFT,
            SDL_BUTTON_RIGHT,
            SDL_BUTTON_MIDDLE
        };

        static Callbacks MouseCallbacks;

        bool IsButtonDown(Button button) {
            u32 state = SDL_GetMouseState(nullptr, nullptr);
            return (state & SDL_BUTTON_MASK(BUTTON_MAP[static_cast<size_t>(button)])) != 0;
        }

        void Capture() {
            SDL_SetWindowRelativeMouseMode(Window::SdlWindow, true);
        }

        void Free() {
            SDL_SetWindowRelativeMouseMode(Window::SdlWindow, false);
        }

        void RegisterCallback(Button button, ActionType type, UserAction callback) {
            size_t idx = static_cast<size_t>(button);
            if (type == ActionType::Press) MouseCallbacks.Presses[idx] = callback;
            else MouseCallbacks.Releases[idx] = callback;
        }

        void RegisterCallback(Button button, UserAction callback) {
            RegisterCallback(button, ActionType::Press, callback);
        }
    }

    namespace Keyboard {
        static constexpr size_t KEY_COUNT = static_cast<size_t>(Key::_KEY_COUNT_);
        static constexpr SDL_Scancode KEY_MAP [KEY_COUNT] = {
            SDL_SCANCODE_W,
            SDL_SCANCODE_S,
            SDL_SCANCODE_D,
            SDL_SCANCODE_A,
            SDL_SCANCODE_Q,
            SDL_SCANCODE_E,
            SDL_SCANCODE_F,
            SDL_SCANCODE_SPACE,
            SDL_SCANCODE_ESCAPE,
            SDL_SCANCODE_LCTRL,
            SDL_SCANCODE_LSHIFT
        };

        static Callbacks KeyCallbacks;

        bool IsKeyDown(Key key) {
            const bool* state = SDL_GetKeyboardState(nullptr);
            return state[KEY_MAP[static_cast<size_t>(key)]];
        }

        void RegisterCallback(Key key, ActionType type, UserAction callback) {
            size_t idx = static_cast<size_t>(key);
            if (type == ActionType::Press) KeyCallbacks.Presses[idx] = callback;
            else KeyCallbacks.Releases[idx] = callback;
        }

        void RegisterCallback(Key key, UserAction callback) {
            RegisterCallback(key, ActionType::Press, callback);
        }
    }

    void Create() {
        {
            using namespace Keyboard;
            KeyCallbacks.Presses.resize(KEY_COUNT);
            KeyCallbacks.Releases.resize(KEY_COUNT);
            RegisterCallback(Key::Escape, ActionType::Press, []() { Mouse::Free(); });
        }
        {
            using namespace Mouse;
            MouseCallbacks.Presses.resize(BUTTON_COUNT);
            MouseCallbacks.Releases.resize(BUTTON_COUNT);
            RegisterCallback(Button::Right, ActionType::Press, []() { Capture(); });
        }
    }

    void ProcessEvent(const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            if (event.key.repeat) return;
            using namespace Keyboard;

            for (size_t i = 0; i < KEY_COUNT; ++i) {
                if (event.key.scancode == KEY_MAP[i]) {
                    auto& actionList = (event.type == SDL_EVENT_KEY_DOWN) ? KeyCallbacks.Presses : KeyCallbacks.Releases;
                    if (actionList[i]) actionList[i]();
                    break;
                }
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            using namespace Mouse;

            for (size_t i = 0; i < BUTTON_COUNT; ++i) {
                if (event.button.button == BUTTON_MAP[i]) {
                    auto& actionList = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? MouseCallbacks.Presses : MouseCallbacks.Releases;
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
}

