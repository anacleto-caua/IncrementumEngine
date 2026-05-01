#include "Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace Window {
    bool ToClose = false;
    SDL_Window* SdlWindow = nullptr;
    ResizeCallback UserResizeCallback = nullptr;

    bool SDLCALL EventWatcher([[maybe_unused]]void* userdata, SDL_Event* event) {
        if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            ToClose = true;
        }
        else if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            if (UserResizeCallback) {
                UserResizeCallback(event->window.data1, event->window.data2);
            }
        }
        return true;
    }

    IncResult Create(u32 width, u32 height, const std::string &title, ResizeCallback callback) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return IncResult::FAIL;
        }

        SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
        SdlWindow = SDL_CreateWindow(title.c_str(), width, height, flags);

        if (!SdlWindow) {
            return IncResult::FAIL;
        }

        UserResizeCallback = callback;
        ToClose = false;

        SDL_AddEventWatch(EventWatcher, nullptr);

        return IncResult::SUCCESS;
    }

    void Destroy() {
        SDL_RemoveEventWatch(EventWatcher, nullptr);

        if (SdlWindow) {
            SDL_DestroyWindow(SdlWindow);
            SdlWindow = nullptr;
        }
        SDL_Quit();
    }

    std::vector<const char*> GetRequiredExtensions() {
        u32 count = 0;
        char const* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        return std::vector<const char*>(extensions, extensions + count);
    }

    bool CreateSurface(VkInstance instance, VkSurfaceKHR &surface) {
        return SDL_Vulkan_CreateSurface(SdlWindow, instance, nullptr, &surface);
    }

    void GetFramebufferSize(u32 &width, u32 &height) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(SdlWindow, &w, &h);
        width = static_cast<u32>(w);
        height = static_cast<u32>(h);
    }

    void Update() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // ...
        }
    }

    bool ShouldClose() {
        return ToClose;
    }
}
