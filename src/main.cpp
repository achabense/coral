#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "gui.hpp"

static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;

[[noreturn]] static void exit_failure() {
    assert(false);
    std::exit(EXIT_FAILURE);
}

static void update_texture(SDL_Texture* texture, const tileT& tile) {
    assert(window && renderer && texture);
    const auto size = tile.size();
#ifndef NDEBUG
    float w = 0, h = 0;
    SDL_GetTextureSize(texture, &w, &h);
    assert(w == size.x && h == size.y);
#endif

    constexpr int pixel_size = sizeof(Uint32);
    void* pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(texture, nullptr, &pixels, &pitch) || (pitch % pixel_size) != 0) {
        exit_failure();
    }

    const cellT* c = tile.data().data();
    const int dp = pitch / pixel_size - size.x;
    assert(dp >= 0);
    Uint32* p = (Uint32*)pixels;
    for (int y = 0; y < size.y; ++y) {
        for (int x = 0; x < size.x; ++x) {
            *p++ = color_for(*c++);
        }
        p += dp;
    }

    SDL_UnlockTexture(texture);
}

ImTextureID texture_create(const tileT& tile) {
    assert(window && renderer && !tile.empty());
    const auto size = tile.size();
    constexpr SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32; // For compatibility with `IM_COL32(...)`.
    SDL_Texture* texture = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING, size.x, size.y);
    if (!texture) {
        exit_failure();
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    update_texture(texture, tile);
    return (ImTextureID)(intptr_t)texture;
}
void texture_update(ImTextureID texture, const tileT& tile) {
    assert(window && renderer && texture && !tile.empty());
    update_texture((SDL_Texture*)(intptr_t)texture, tile);
}
void texture_destroy(ImTextureID texture) {
    assert(window && renderer && texture);
    SDL_DestroyTexture((SDL_Texture*)(intptr_t)texture);
}

int main(int, char**) {
    assert(!window && !renderer);

    // Setup SDL.
    {
        SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "unaware");
        // SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1"); // Enabled by default.
        if (!SDL_Init(SDL_INIT_VIDEO /*| SDL_INIT_GAMEPAD*/)) {
            exit_failure();
        }

        constexpr SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN; // | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (!SDL_CreateWindowAndRenderer("Nullberry", 1280, 720, flags, &window, &renderer)) {
            exit_failure();
        }

        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        // SDL_MaximizeWindow(window);

        SDL_SetRenderVSync(renderer, 1); // Enable vsync.
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_ShowWindow(window); // Guaranteed to be all-black.
    }

    // Setup Dear ImGui.
    {
        ImGui::CreateContext();

        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // No keyboard nav.
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        ImGui::GetIO().IniFilename = nullptr; // No "imgui.ini".
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::GetIO().ConfigDebugHighlightIdConflicts = true;

        ImGui::StyleColorsDark();

        // Setup Platform/Renderer backends.
        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);
        ImGui::GetPlatformIO().Platform_OpenInShellFn = [](ImGuiContext*, const char* u8path) {
            return SDL_OpenURL(u8path);
        };
    }

    for (auto data = main_data::make_unique();;) {
        bool quit = false;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
                break;
            } else if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                       event.key.key == SDLK_TAB) {
                continue; // To disable tab-related controls (especially ctrl+tab -> nav menu).
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        if (quit) {
            break;
        } else if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            // Related: https://github.com/ocornut/imgui/issues/7844
            SDL_Delay(10 /*ms*/);
            continue;
        } else {
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
        }

        frame_main(*data);

        {
            const auto& io = ImGui::GetIO();
            ImGui::Render();
            SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }

        constexpr int max_fps = 100; // May be further limited by vsync (like 60fps).
        static Uint64 last = 0;      // Don't have to be static, but moving out of loop makes no actual benefit.
        const Uint64 now = SDL_GetTicksNS();
        const Uint64 until = last + (1000 * 1000 * 1000) /*ns*/ / max_fps;
        if (now < until) {
            SDL_DelayNS(until - now);
            last = until; // Instead of another `SDL_GetTicksNs()` call.
        } else {
            last = now;
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    window = nullptr;
    renderer = nullptr;

    return 0;
}
