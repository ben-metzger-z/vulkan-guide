//
// Created by ben on 11/07/24.
//

#ifndef VULKAN_GUIDE_WINDOWING_HPP
#define VULKAN_GUIDE_WINDOWING_HPP

struct WindowInfo {

    bool quit = false;
    bool minimised = false;


    auto EventHandler() {
        return [this](SDL_Event event) {
            if (event.type == SDL_QUIT)
                quit = true;

            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    minimised = true;
                }
                if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
                    minimised = false;
                }
            }
        };
    }

};

#endif //VULKAN_GUIDE_WINDOWING_HPP
