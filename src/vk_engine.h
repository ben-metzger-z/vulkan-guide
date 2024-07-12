// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_deletion_queue.hpp>
#include "vk_descriptors.h"

struct FrameData {
    VkCommandPool command_pool;
    VkCommandBuffer main_command_buffer;

    VkSemaphore swapchain_semaphore, render_semaphore;
    VkFence render_fence;

    DeletionQueue deletion_queue;
};

struct AllocatedImage {
    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;
    VkExtent3D image_extent;
    VkFormat image_format;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice chosen_gpu;
    VkDevice device;
    VkSurfaceKHR surface;

    // TODO: Abstract swapchain
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkExtent2D swapchain_extent;

    std::array<FrameData, FRAME_OVERLAP> frames;

    // TODO: Abstract commands
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;

    DeletionQueue global_deletion_queue;

    VmaAllocator allocator;

    AllocatedImage draw_image;
    VkExtent2D draw_extent;

    DescriptorAllocator global_descriptor_allocator;

    VkDescriptorSet  draw_image_descriptors;
    VkDescriptorSetLayout draw_image_descriptor_layout;

    VkPipeline gradient_pipeline;
    VkPipelineLayout  gradient_pipeline_layout;

	bool isInitialized{ false };
	int frame_number {0};
	bool stop_rendering{ false };
	VkExtent2D windowExtent{ 1700 , 900 };

	struct SDL_Window* window{ nullptr };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();
    void draw_background(VkCommandBuffer command_buffer);

    FrameData& get_current_frame() {
        return frames[frame_number % FRAME_OVERLAP];
    }

	//run main loop
	void run();


private:
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync_structures();
    void init_descriptors();
    void init_pipelines();
    void init_background_pipelines();

    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();
};
