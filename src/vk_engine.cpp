//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>


#include <VkBootstrap.h>
#include <vk_initializers.h>
#include <vk_types.h>

#include <chrono>
#include <thread>

#include "events.hpp"
#include "window_info.hpp"
#include "vk_images.h"

#define VMA_IMPLEMENTATION

#include "vk_mem_alloc.h"
#include "vk_pipelines.h"

VulkanEngine *loadedEngine = nullptr;

VulkanEngine &VulkanEngine::Get() { return *loadedEngine; }

void VulkanEngine::init() {
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    window = SDL_CreateWindow(
            "Vulkan Engine",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            windowExtent.width,
            windowExtent.height,
            window_flags);

    init_vulkan();

    init_swapchain();

    init_commands();

    init_sync_structures();

    init_descriptors();

    init_pipelines();

    // everything went fine
    isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (isInitialized) {
        vkDeviceWaitIdle(device);

        global_deletion_queue.flush();

        destroy_swapchain();

        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);

        vkb::destroy_debug_utils_messenger(instance, debug_messenger);
        vkDestroyInstance(instance, nullptr);

        SDL_DestroyWindow(window);
    }

    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw() {
    VK_CHECK(vkWaitForFences(device, 1, &get_current_frame().render_fence, true, 1000000000));

    get_current_frame().deletion_queue.flush();

    VK_CHECK(vkResetFences(device, 1, &get_current_frame().render_fence));

    uint32_t swapchain_image_index;
    VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000, get_current_frame().swapchain_semaphore, nullptr,
                                   &swapchain_image_index));

    VkCommandBuffer command_buffer = get_current_frame().main_command_buffer;

    VK_CHECK(vkResetCommandBuffer(command_buffer, 0));

    VkCommandBufferBeginInfo command_buffer_begin_info = vkinit::command_buffer_begin_info(
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    draw_extent.width = draw_image.image_extent.width;
    draw_extent.height = draw_image.image_extent.height;

    VK_CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

    vkutil::transition_image(command_buffer, draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);

    draw_background(command_buffer);

    vkutil::transition_image(command_buffer, draw_image.image, VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(command_buffer, swapchain_images[swapchain_image_index], VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkutil::copy_image_to_image(command_buffer, draw_image.image, swapchain_images[swapchain_image_index], draw_extent,
                                swapchain_extent);

    vkutil::transition_image(command_buffer, swapchain_images[swapchain_image_index],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkCommandBufferSubmitInfo command_info = vkinit::command_buffer_submit_info(command_buffer);

    VkSemaphoreSubmitInfo wait_info = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
                                                                    get_current_frame().swapchain_semaphore);
    VkSemaphoreSubmitInfo signal_info = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                                                      get_current_frame().render_semaphore);

    VkSubmitInfo2 submit_info = vkinit::submit_info(&command_info, &signal_info, &wait_info);

    VK_CHECK(vkQueueSubmit2(graphics_queue, 1, &submit_info, get_current_frame().render_fence));

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.pSwapchains = &swapchain;
    present_info.swapchainCount = 1;
    present_info.pWaitSemaphores = &get_current_frame().render_semaphore;
    present_info.waitSemaphoreCount = 1;
    present_info.pImageIndices = &swapchain_image_index;
    VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));

    ++frame_number;


}

void VulkanEngine::draw_background(VkCommandBuffer command_buffer) {
    VkClearColorValue clear_value;
    float flash = std::abs(std::sin(frame_number / 720.0f));
    clear_value = {{0.0f, 0.0f, flash, 1.0f}};

    VkImageSubresourceRange clear_range = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

//    vkCmdClearColorImage(command_buffer, draw_image.image, VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &clear_range);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, gradient_pipeline);

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, gradient_pipeline_layout, 0, 1,
                            &draw_image_descriptors, 0, nullptr);

    vkCmdDispatch(command_buffer, std::ceil(draw_extent.width / 16.0), std::ceil(draw_extent.height / 16.0), 1);
}

void VulkanEngine::run() {

    WindowInfo window_info;
    Events::Connect(window_info.EventHandler());

    // main loop
    while (!window_info.quit) {
        Events::UpdateEvents();

        // do not draw if we are minimized
        if (window_info.minimised) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}

void VulkanEngine::init_vulkan() {
    vkb::InstanceBuilder builder;

    auto instance_result = builder.set_app_name("Vulkan Application")
            .request_validation_layers(true)
            .use_default_debug_messenger()
            .require_api_version(1, 3, 0)
            .build();

    vkb::Instance vkb_instance = instance_result.value();

    instance = vkb_instance.instance;
    debug_messenger = vkb_instance.debug_messenger;

    SDL_Vulkan_CreateSurface(window, instance, &surface);

    VkPhysicalDeviceVulkan13Features features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = true;
    features.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    vkb::PhysicalDeviceSelector selector{vkb_instance};
    vkb::PhysicalDevice physical_device = selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_surface(surface)
            .select()
            .value();

    vkb::DeviceBuilder device_builder{physical_device};

    vkb::Device vkb_device = device_builder.build().value();

    chosen_gpu = physical_device.physical_device;
    device = vkb_device.device;

    graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocator_create_info = {};
    allocator_create_info.physicalDevice = chosen_gpu;
    allocator_create_info.device = device;
    allocator_create_info.instance = instance;
    allocator_create_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocator_create_info, &allocator);

    global_deletion_queue.push_function([&]() {
        vmaDestroyAllocator(allocator);
    });

}

void VulkanEngine::init_swapchain() {
    create_swapchain(windowExtent.width, windowExtent.height);

    VkExtent3D draw_image_extent = {
            windowExtent.width, windowExtent.height, 1
    };

    draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    draw_image.image_extent = draw_image_extent;

    VkImageUsageFlags draw_image_usages{};
    draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_STORAGE_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo image_info = vkinit::image_create_info(draw_image.image_format, draw_image_usages,
                                                             draw_image_extent);

    VmaAllocationCreateInfo image_allocation_info = {};
    image_allocation_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    image_allocation_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(allocator, &image_info, &image_allocation_info, &draw_image.image, &draw_image.allocation, nullptr);

    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(draw_image.image_format, draw_image.image,
                                                                    VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &draw_image.image_view));

    global_deletion_queue.push_function([=]() {
        vkDestroyImageView(device, draw_image.image_view, nullptr);
        vmaDestroyImage(allocator, draw_image.image, draw_image.allocation);
    });
}

void VulkanEngine::init_commands() {
    VkCommandPoolCreateInfo command_pool_create_info = vkinit::command_pool_create_info(graphics_queue_family,
                                                                                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);


    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &frames[i].command_pool));

        VkCommandBufferAllocateInfo command_buffer_allocate_info = vkinit::command_buffer_allocate_info(
                frames[i].command_pool, 1);

        VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &frames[i].main_command_buffer));
    }

    global_deletion_queue.push_function([&]() {
        for (int i = 0; i < FRAME_OVERLAP; ++i) {
            vkDestroyCommandPool(device, frames[i].command_pool, nullptr);
        }
    });

    // immediate

    VK_CHECK(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &immediate_command_pool));

    VkCommandBufferAllocateInfo command_buffer_allocate_info = vkinit::command_buffer_allocate_info(
            immediate_command_pool, 1);

    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &immediate_command_buffer));

    global_deletion_queue.push_function([&]() {
        vkDestroyCommandPool(device, immediate_command_pool, nullptr);
    });
}

void VulkanEngine::init_sync_structures() {
    VkFenceCreateInfo fence_create_info = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphore_create_info = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; ++i) {
        VK_CHECK(vkCreateFence(device, &fence_create_info, nullptr, &frames[i].render_fence));

        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &frames[i].swapchain_semaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &frames[i].render_semaphore));
    }

    global_deletion_queue.push_function([&]() {
        for (int i = 0; i < FRAME_OVERLAP; ++i) {
            vkDestroyFence(device, frames[i].render_fence, nullptr);

            vkDestroySemaphore(device, frames[i].swapchain_semaphore, nullptr);
            vkDestroySemaphore(device, frames[i].render_semaphore, nullptr);
        }
    });

    VK_CHECK(vkCreateFence(device, &fence_create_info, nullptr, &immediate_fence));

    global_deletion_queue.push_function([&]() {
        vkDestroyFence(device, immediate_fence, nullptr);
    });
}

void VulkanEngine::init_descriptors() {
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
            {
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
            };

    global_descriptor_allocator.init_pool(device, 10, sizes);

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        draw_image_descriptor_layout = builder.build(device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    draw_image_descriptors = global_descriptor_allocator.allocate(device, draw_image_descriptor_layout);

    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_info.imageView = draw_image.image_view;

    VkWriteDescriptorSet draw_image_write = {};
    draw_image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    draw_image_write.pNext = nullptr;

    draw_image_write.dstBinding = 0;
    draw_image_write.dstSet = draw_image_descriptors;
    draw_image_write.descriptorCount = 1;
    draw_image_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    draw_image_write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(device, 1, &draw_image_write, 0, nullptr);

    global_deletion_queue.push_function(
            [&]() {
                global_descriptor_allocator.destroy_pool(device);
                vkDestroyDescriptorSetLayout(device, draw_image_descriptor_layout, nullptr);
            }
    );
}

void VulkanEngine::init_pipelines() {
    init_background_pipelines();
}

void VulkanEngine::init_background_pipelines() {
    VkPipelineLayoutCreateInfo compute_layout = {};
    compute_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compute_layout.pNext = nullptr;
    compute_layout.pSetLayouts = &draw_image_descriptor_layout;
    compute_layout.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(device, &compute_layout, nullptr, &gradient_pipeline_layout));

    VkShaderModule compute_draw_shader;
    if (auto opt_shader_module = vkutil::load_shader_module("../shaders/gradient.comp.spv",
                                                            device); opt_shader_module.has_value()) {
        compute_draw_shader = opt_shader_module.value();
    } else {
        fmt::print("Error when building the compute shader \n");
    }

    VkPipelineShaderStageCreateInfo stage_create_info = {};
    stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_create_info.pNext = nullptr;
    stage_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_create_info.module = compute_draw_shader;
    stage_create_info.pName = "main";

    VkComputePipelineCreateInfo compute_pipeline_create_info = {};
    compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute_pipeline_create_info.pNext = nullptr;
    compute_pipeline_create_info.layout = gradient_pipeline_layout;
    compute_pipeline_create_info.stage = stage_create_info;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info, nullptr,
                                      &gradient_pipeline));

    vkDestroyShaderModule(device, compute_draw_shader, nullptr);

    global_deletion_queue.push_function([&]() {
        vkDestroyPipelineLayout(device, gradient_pipeline_layout, nullptr);
        vkDestroyPipeline(device, gradient_pipeline, nullptr);
    });
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height) {
    vkb::SwapchainBuilder swapchain_builder(chosen_gpu, device, surface);

    swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkb_swapchain = swapchain_builder
            .set_desired_format(
                    VkSurfaceFormatKHR{.format = swapchain_image_format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

    swapchain_extent = vkb_swapchain.extent;

    swapchain = vkb_swapchain.swapchain;
    swapchain_images = vkb_swapchain.get_images().value();
    swapchain_image_views = vkb_swapchain.get_image_views().value();

}

void VulkanEngine::destroy_swapchain() {
    vkDestroySwapchainKHR(device, swapchain, nullptr);

    for (int i = 0; i < swapchain_images.size(); ++i) {
        vkDestroyImageView(device, swapchain_image_views[i], nullptr);
    }
}