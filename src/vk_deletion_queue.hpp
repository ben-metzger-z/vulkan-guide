//
// Created by ben on 11/07/24.
//

#ifndef VULKAN_GUIDE_VK_DELETION_QUEUE_HPP
#define VULKAN_GUIDE_VK_DELETION_QUEUE_HPP

#include <deque>
#include <functional>

struct DeletionQueue {

    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()>&& function) {
        deletors.push_back(function);
    }

    void flush() {
        for(auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
            (*it)();
        }

        deletors.clear();
    }

};

#endif //VULKAN_GUIDE_VK_DELETION_QUEUE_HPP
