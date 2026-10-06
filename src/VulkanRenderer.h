#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <memory>
#include <cstdint>

class Window;

class VulkanRenderer {
public:
	VulkanRenderer() = default;
	~VulkanRenderer();

	// Initialize renderer with created VkInstance and GLFW window surface
	void init(VkInstance instance, VkSurfaceKHR surface, Window& window);
	void cleanup();

	// Draw a frame (acquire, submit, present)
	void drawFrame();

	// Recreate swapchain (call when resized or out-of-date)
	void recreateSwapchain();

private:
	VkInstance m_instance = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	uint32_t m_graphicsQueueFamily = UINT32_MAX;

	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D m_swapchainExtent{};
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> m_framebuffers;

	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;

	VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence m_inFlightFence = VK_NULL_HANDLE;

	Window* m_window = nullptr;

	// Helpers
	void pickPhysicalDevice();
	void createLogicalDevice();
	void createSwapchain();
	void cleanupSwapchain();
	void createImageViews();
	void createRenderPass();
	void createFramebuffers();
	void createCommandPoolAndBuffers();
	void createSyncObjects();
	VkExtent2D chooseSwapExtent(int width, int height) const;
};
