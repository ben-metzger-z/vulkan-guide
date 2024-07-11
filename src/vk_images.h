
#pragma once

#include <vulkan/vulkan.h>

namespace vkutil {

    void transition_image(VkCommandBuffer command_buffer, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout);

    void copy_image_to_image(VkCommandBuffer command_buffer, VkImage source, VkImage destination, VkExtent2D source_size,
                             VkExtent2D destination_size);

};