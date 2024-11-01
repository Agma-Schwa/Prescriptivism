module;
#include <chrono>
#include <SDL3/SDL.h>
#include <thread>
module pr.client;

import pr.utils;

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Main Menu Screen
// =============================================================================
void MenuScreen::render(Renderer& renderer) {
    renderer.clear(Colour{45, 42, 46, 255});
}

// =============================================================================
//  API
// =============================================================================
Client::Client(): renderer(Renderer::Create(800, 600)) {}

void Client::Run() {
    constexpr chr::milliseconds ClientTickDuration = 16ms;
    bool quit = false;
    while (not quit) {
        Renderer::Frame _ = renderer.frame();
        auto start_of_tick = chr::system_clock::now();

        // Process events.
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                default: break;
                case SDL_EVENT_QUIT:
                    quit = true;
                    break;
            }
        }

        // Draw the current screen.
        current_screen->render(renderer);

        const auto end_of_tick = chr::system_clock::now();
        const auto tick_duration = chr::duration_cast<chr::milliseconds>(end_of_tick - start_of_tick);
        if (tick_duration < ClientTickDuration) {
            SDL_WaitEventTimeout(
                nullptr,
                i32((ClientTickDuration - tick_duration).count())
            );
        } else {
            Log("Client tick took too long: {}ms", tick_duration.count());
        }
    }
}

