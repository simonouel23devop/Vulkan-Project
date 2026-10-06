#include <iostream>
#include <vector>
#include <cstdlib>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h> // Include the Vulkan header
#include "Window.h"
#include "VulkanRenderer.h"
#include <thread>
#include <chrono>
// Utility: check a condition and exit with an error message if false
static void check(bool condition, const char* msg)
{
	if (!condition) {
		std::cerr << "Fatal: " << msg << "\n";
		std::exit(EXIT_FAILURE);
	}
}

// Utility: return window size as glm::vec2 (x = width, y = height)
static glm::vec2 getWindowSizeVec2(GLFWwindow* window)
{
	int w = 0, h = 0;
	glfwGetWindowSize(window, &w, &h);
	return glm::vec2(static_cast<float>(w), static_cast<float>(h));
}

// Utility: return window position as glm::vec2 (x = xpos, y = ypos)
static glm::vec2 getWindowPosVec2(GLFWwindow* window)
{
	int x = 0, y = 0;
	glfwGetWindowPos(window, &x, &y);
	return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

int main()
{
const uint32_t width = 1024, height = 768;
// Allow the user to resize the window
Window window(width, height, "Vulkan project 1.0", true);

	// Create Vulkan instance
	VkInstance instance = VK_NULL_HANDLE;

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkan-Project 1.0";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	// Get required extensions from GLFW
	uint32_t glfwExtCount = 0;
	const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
	check(glfwExts != nullptr && glfwExtCount > 0, "Could not get required GLFW Vulkan extensions");

	std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	VkResult res = vkCreateInstance(&createInfo, nullptr, &instance);
	check(res == VK_SUCCESS, "Failed to create Vulkan instance");

// Create a surface for the GLFW window
VkSurfaceKHR surface = VK_NULL_HANDLE;
try {
	surface = window.createSurface(instance);
} catch (const std::exception& e) {
	std::cerr << "Failed to create window surface: " << e.what() << std::endl;
	return EXIT_FAILURE;
}
	std::cout << "Vulkan instance and surface created successfully." << std::endl;

	// Create renderer and initialize swapchain/resources
	VulkanRenderer renderer;
	try {
		renderer.init(instance, surface, window);
	} catch (const std::exception& e) {
		std::cerr << "Failed to initialize Vulkan renderer: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	// Main loop (draw each frame and handle resize)
	while (!window.shouldClose()) {
		window.pollEvents();

		if (window.wasResized()) {
			std::cout << "Window resized, recreating swapchain..." << std::endl;
			try {
				renderer.recreateSwapchain();
			} catch (const std::exception& e) {
				std::cerr << "Failed to recreate swapchain: " << e.what() << std::endl;
				return EXIT_FAILURE;
			}
			window.resetResized();
		}

		// Draw a frame (clears to black)
		try {
			renderer.drawFrame();
		} catch (const std::exception& e) {
			std::cerr << "Render error: " << e.what() << std::endl;
			break;
		}
	}

	renderer.cleanup();

	// Cleanup
	if (surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(instance, surface, nullptr);
	}
	if (instance != VK_NULL_HANDLE) {
		vkDestroyInstance(instance, nullptr);
	}
	// Window destructor will cleanup GLFW

	return 0;
}
