#pragma once 
#include <vk_types.h>

namespace vkutil {

    std::optional<VkShaderModule> load_shader_module(const char* filepath, VkDevice device);

};