#include <vk_pipelines.h>
#include <fstream>
#include <vk_initializers.h>

// TODO: Out as parameter is yuck
std::optional<VkShaderModule> vkutil::load_shader_module(const char *filepath, VkDevice device) {

    std::ifstream file(filepath, std::ios::ate | std::ios::binary);

    if(!file.is_open()) {
        return {};
    }

    size_t file_size = static_cast<size_t>(file.tellg());

    std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

    file.seekg(0);

    file.read(reinterpret_cast<char*>(buffer.data()), file_size);

    file.close();

    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.pNext = nullptr;

    create_info.codeSize = buffer.size() * sizeof(uint32_t);
    create_info.pCode = buffer.data();

    VkShaderModule shader_module;
    if(vkCreateShaderModule(device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
        return {};
    }

    return shader_module;
}