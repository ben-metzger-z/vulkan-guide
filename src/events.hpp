//
// Created by ben on 11/07/24.
//

#ifndef VULKAN_GUIDE_EVENTS_HPP
#define VULKAN_GUIDE_EVENTS_HPP

#include <functional>

class Events {
    static inline std::vector<SDL_Event> events_ {};
    static inline std::vector<std::function<void(SDL_Event)>> callbacks {};

public:

    static void UpdateEvents() {
        std::vector<SDL_Event> events;

        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            events.push_back(e);

            for(auto & callback : callbacks) {
                callback(e);
            }
        }
    }

    static std::vector<SDL_Event> const& GetEvents() {
        return events_;
    }

    static void Connect(std::function<void(SDL_Event)> callback) {
        callbacks.push_back(callback);
    }

};

#endif //VULKAN_GUIDE_EVENTS_HPP
