#include "VulkanRenderer.h"
#include "Window.h"

#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>

// Helper macros
#define VK_CHECK(res, msg) if ((res) != VK_SUCCESS) throw std::runtime_error(msg);

VulkanRenderer::~VulkanRenderer()
{
	cleanup();
}

void VulkanRenderer::init(VkInstance instance, VkSurfaceKHR surface, Window& window)
{
	m_instance = instance;
	m_surface = surface;
	m_window = &window;

	pickPhysicalDevice();
	createLogicalDevice();
	createSwapchain();
	createImageViews();
	createRenderPass();
	createFramebuffers();
	createCommandPoolAndBuffers();
	createSyncObjects();
}

void VulkanRenderer::cleanup()
{
	if (m_device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(m_device);
	}

	cleanupSwapchain();

	// Only destroy device-owned objects if device is valid
	if (m_device != VK_NULL_HANDLE) {
		if (m_imageAvailableSemaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
			m_imageAvailableSemaphore = VK_NULL_HANDLE;
		}
		if (m_renderFinishedSemaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
			m_renderFinishedSemaphore = VK_NULL_HANDLE;
		}
		if (m_inFlightFence != VK_NULL_HANDLE) {
			vkDestroyFence(m_device, m_inFlightFence, nullptr);
			m_inFlightFence = VK_NULL_HANDLE;
		}
		if (m_commandPool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(m_device, m_commandPool, nullptr);
			m_commandPool = VK_NULL_HANDLE;
		}

		vkDestroyDevice(m_device, nullptr);
		m_device = VK_NULL_HANDLE;
	}

	// Do not destroy instance or surface here (owned by main)
}

void VulkanRenderer::pickPhysicalDevice()
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
	if (deviceCount == 0) throw std::runtime_error("No Vulkan physical devices found");
	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

	// Pick first suitable device
	for (auto d : devices) {
		m_physicalDevice = d;
		break;
	}
	if (m_physicalDevice == VK_NULL_HANDLE) throw std::runtime_error("Failed to pick physical device");
}

void VulkanRenderer::createLogicalDevice()
{
	// Find a queue family that supports graphics and presentation
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

	int graphicsIndex = -1;
	for (uint32_t i = 0; i < queueFamilyCount; ++i) {
		VkBool32 presentSupport = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
		if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
			graphicsIndex = static_cast<int>(i);
			break;
		}
	}
	if (graphicsIndex < 0) throw std::runtime_error("Failed to find suitable queue family");

	m_graphicsQueueFamily = static_cast<uint32_t>(graphicsIndex);

	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfo{};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	VkPhysicalDeviceFeatures deviceFeatures{};

	const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = &queueCreateInfo;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = 1;
	createInfo.ppEnabledExtensionNames = deviceExtensions;

	VkResult res = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
	VK_CHECK(res, "Failed to create logical device");

	vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
}

VkExtent2D VulkanRenderer::chooseSwapExtent(int width, int height) const
{
	VkExtent2D actualExtent{};
	actualExtent.width = static_cast<uint32_t>(width);
	actualExtent.height = static_cast<uint32_t>(height);
	return actualExtent;
}

void VulkanRenderer::createSwapchain()
{
	// Query surface capabilities
	VkSurfaceCapabilitiesKHR caps{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

	int fbW = 0, fbH = 0;
	m_window->getFramebufferSize(fbW, fbH);
	if (fbW == 0 || fbH == 0) throw std::runtime_error("Framebuffer size is zero when creating swapchain");

	VkExtent2D extent = chooseSwapExtent(fbW, fbH);
	m_swapchainExtent = extent;

	// Choose format
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
	if (formatCount == 0) throw std::runtime_error("No surface formats");
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
	VkSurfaceFormatKHR surfaceFormat = formats[0];

	// Present mode: FIFO guaranteed

	uint32_t imageCount = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

	VkSwapchainCreateInfoKHR scInfo{};
	scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	scInfo.surface = m_surface;
	scInfo.minImageCount = imageCount;
	scInfo.imageFormat = surfaceFormat.format;
	scInfo.imageColorSpace = surfaceFormat.colorSpace;
	scInfo.imageExtent = extent;
	scInfo.imageArrayLayers = 1;
	scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	scInfo.preTransform = caps.currentTransform;
	scInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	scInfo.clipped = VK_TRUE;

	VkResult res = vkCreateSwapchainKHR(m_device, &scInfo, nullptr, &m_swapchain);
	VK_CHECK(res, "Failed to create swapchain");

	m_swapchainImageFormat = surfaceFormat.format;

	uint32_t actualCount = 0;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
	m_swapchainImages.resize(actualCount);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_swapchainImages.data());
}

void VulkanRenderer::createImageViews()
{
	m_swapchainImageViews.resize(m_swapchainImages.size());
	for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
		VkImageViewCreateInfo ivInfo{};
		ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivInfo.image = m_swapchainImages[i];
		ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivInfo.format = m_swapchainImageFormat;
		ivInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ivInfo.subresourceRange.baseMipLevel = 0;
		ivInfo.subresourceRange.levelCount = 1;
		ivInfo.subresourceRange.baseArrayLayer = 0;
		ivInfo.subresourceRange.layerCount = 1;

		VkResult res = vkCreateImageView(m_device, &ivInfo, nullptr, &m_swapchainImageViews[i]);
		VK_CHECK(res, "Failed to create image view");
	}
}

void VulkanRenderer::createRenderPass()
{
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = m_swapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkRenderPassCreateInfo rpInfo{};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpInfo.attachmentCount = 1;
	rpInfo.pAttachments = &colorAttachment;
	rpInfo.subpassCount = 1;
	rpInfo.pSubpasses = &subpass;

	VkResult res = vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass);
	VK_CHECK(res, "Failed to create render pass");
}

void VulkanRenderer::createFramebuffers()
{
	m_framebuffers.resize(m_swapchainImageViews.size());
	for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
		VkImageView attachments[] = { m_swapchainImageViews[i] };
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = attachments;
		fbInfo.width = m_swapchainExtent.width;
		fbInfo.height = m_swapchainExtent.height;
		fbInfo.layers = 1;

		VkResult res = vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffers[i]);
		VK_CHECK(res, "Failed to create framebuffer");
	}
}

void VulkanRenderer::createCommandPoolAndBuffers()
{
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
	poolInfo.flags = 0;
	VkResult res = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
	VK_CHECK(res, "Failed to create command pool");

	m_commandBuffers.resize(m_framebuffers.size());
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
	res = vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data());
	VK_CHECK(res, "Failed to allocate command buffers");

	// Record simple clear commands
	for (size_t i = 0; i < m_commandBuffers.size(); ++i) {
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(m_commandBuffers[i], &beginInfo);

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = m_renderPass;
		rpBegin.framebuffer = m_framebuffers[i];
		rpBegin.renderArea.offset = {0,0};
		rpBegin.renderArea.extent = m_swapchainExtent;

		VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues = &clearColor;

		vkCmdBeginRenderPass(m_commandBuffers[i], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdEndRenderPass(m_commandBuffers[i]);

		vkEndCommandBuffer(m_commandBuffers[i]);
	}
}

void VulkanRenderer::createSyncObjects()
{
	VkSemaphoreCreateInfo semInfo{};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailableSemaphore);
	vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinishedSemaphore);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFence);
}

void VulkanRenderer::cleanupSwapchain()
{
	for (auto fb : m_framebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
	m_framebuffers.clear();
	for (auto iv : m_swapchainImageViews) if (iv) vkDestroyImageView(m_device, iv, nullptr);
	m_swapchainImageViews.clear();
	if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	m_swapchain = VK_NULL_HANDLE;
	if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
	m_renderPass = VK_NULL_HANDLE;
	m_commandBuffers.clear();
}

void VulkanRenderer::recreateSwapchain()
{
	vkDeviceWaitIdle(m_device);
	cleanupSwapchain();
	createSwapchain();
	createImageViews();
	createRenderPass();
	createFramebuffers();
	createCommandPoolAndBuffers();
}

void VulkanRenderer::drawFrame()
{
	vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
	vkResetFences(m_device, 1, &m_inFlightFence);

	uint32_t imageIndex = 0;
	VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
	if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) VK_CHECK(res, "Failed to acquire swapchain image");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffers[imageIndex];
	VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphore };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	res = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFence);
	if (res != VK_SUCCESS) {
		if (res == VK_ERROR_DEVICE_LOST) throw std::runtime_error("Device lost during vkQueueSubmit");
		VK_CHECK(res, "Failed to submit draw command buffer");
	}

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapchains[] = { m_swapchain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapchains;
	presentInfo.pImageIndices = &imageIndex;

	res = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
	if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
		recreateSwapchain();
	} else if (res != VK_SUCCESS) {
		VK_CHECK(res, "Failed to present swapchain image");
	}
}
