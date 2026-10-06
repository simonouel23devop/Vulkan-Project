#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <string>
#include <atomic>

class Window {
public:
	// resizable: whether the window should be resizable by the user
	Window(uint32_t width, uint32_t height, const std::string& title, bool resizable = true);
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;
	Window(Window&&) = delete;
	Window& operator=(Window&&) = delete;

	glm::vec2 getSize() const;
	glm::vec2 getPosition() const;
	// Return framebuffer size in pixels (useful for Vulkan extents)
	glm::vec2 getFramebufferSize() const;
	void getFramebufferSize(int& width, int& height) const;

	// Resize helpers
	bool wasResized() const;
	void resetResized();

	bool shouldClose() const;
	void pollEvents() const;

	GLFWwindow* handle() const { return m_window; }

	// Create a Vulkan surface for this window. Throws std::runtime_error on failure.
	VkSurfaceKHR createSurface(VkInstance instance) const;

	// GLFW framebuffer size callback (static so it can be passed to GLFW)
	static void framebuffer_size_callback(GLFWwindow* win, int width, int height);

private:
	GLFWwindow* m_window = nullptr;
	static int s_instanceCount;
	std::atomic<bool> m_resized{false};
};
