#include "Window.h"
#include <stdexcept>
#include <iostream>

// Some build environments may include GLFW before Vulkan or without enabling
// the Vulkan-related declarations. Ensure the prototype is available here
// so glfwCreateWindowSurface can be used regardless of include ordering.
#ifdef __cplusplus
extern "C" {
#endif
VkResult glfwCreateWindowSurface(VkInstance instance, GLFWwindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);
#ifdef __cplusplus
}
#endif

int Window::s_instanceCount = 0;

// Static framebuffer callback implementation
void Window::framebuffer_size_callback(GLFWwindow* win, int /*width*/, int /*height*/)
{
	if (!win) return;
	void* ptr = glfwGetWindowUserPointer(win);
	if (!ptr) return;
	Window* w = static_cast<Window*>(ptr);
	w->m_resized.store(true);
}

Window::Window(uint32_t width, uint32_t height, const std::string& title, bool resizable)
{
	if (s_instanceCount == 0) {
		if (!glfwInit()) {
			throw std::runtime_error("Failed to initialize GLFW");
		}
	}

	// No OpenGL context; we will use Vulkan
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

	m_window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);
	if (!m_window) {
		if (s_instanceCount == 0) {
			glfwTerminate();
		}
		throw std::runtime_error("Failed to create GLFW window");
	}

	++s_instanceCount;

	// store pointer to this object so callbacks can access it
	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, Window::framebuffer_size_callback);
}

Window::~Window()
{
	if (m_window) {
		glfwDestroyWindow(m_window);
		m_window = nullptr;
	}

	--s_instanceCount;
	if (s_instanceCount == 0) {
		glfwTerminate();
	}
}

glm::vec2 Window::getSize() const
{
	int w = 0, h = 0;
	glfwGetWindowSize(m_window, &w, &h);
	return glm::vec2(static_cast<float>(w), static_cast<float>(h));
}

glm::vec2 Window::getPosition() const
{
	int x = 0, y = 0;
	glfwGetWindowPos(m_window, &x, &y);
	return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

glm::vec2 Window::getFramebufferSize() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(m_window, &w, &h);
	return glm::vec2(static_cast<float>(w), static_cast<float>(h));
}

void Window::getFramebufferSize(int& width, int& height) const
{
	glfwGetFramebufferSize(m_window, &width, &height);
}

bool Window::shouldClose() const
{
	return glfwWindowShouldClose(m_window) != 0;
}

void Window::pollEvents() const
{
	glfwPollEvents();
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkResult res = glfwCreateWindowSurface(instance, m_window, nullptr, &surface);
	if (res != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Vulkan surface from window");
	}
	return surface;
}

bool Window::wasResized() const
{
	return m_resized.load();
}

void Window::resetResized()
{
	m_resized.store(false);
}
