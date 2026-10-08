#include <precomp.h>
#include <GLFW/glfw3.h>
#include <glfw_monitor.h>
#include <glfw_window.h>

namespace veng {

Window::Window(gsl::czstring name, glm::ivec2 size) {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(size.x, size.y, name, nullptr, nullptr);

  if (window_ == nullptr) {
    std::exit(EXIT_FAILURE);
  }
}
Window::~Window() {
  glfwDestroyWindow(window_);
}

glm::ivec2 Window::GetWindowSize() const {
  glm::ivec2 window_size;
  glfwGetWindowSize(window_, &window_size.x, &window_size.y);
  return window_size;
}

glm::ivec2 Window::GetFramebufferSize() const {
  glm::ivec2 framebuffer_size;
  glfwGetFramebufferSize(window_, &framebuffer_size.x, &framebuffer_size.y);
  return framebuffer_size;
}

bool Window::ShouldClose() const {
  return glfwWindowShouldClose(window_);
}

GLFWwindow* Window::GetHandle() const {
  return window_;
}

bool Window::TryMoveToMonitor(std::uint16_t monitor_number) {
  gsl::span<GLFWmonitor*> monitors = veng::GetMonitors();

  if (monitor_number < monitors.size()) {
    veng::MoveWindowToMonitor(window_, monitors[monitor_number]);
    return true;
  }

  return false;
}

}  // namespace veng
