#include <precomp.h>
#include <GLFW/glfw3.h>
#include <glfw_monitor.h>

namespace veng {

gsl::span<GLFWmonitor*> GetMonitors() {
  std::int32_t monitor_count = 0;
  GLFWmonitor** monitor_pointers = glfwGetMonitors(&monitor_count);
  return gsl::span<GLFWmonitor*>(monitor_pointers, monitor_count);
}

glm::ivec2 GetMonitorPosition(gsl::not_null<GLFWmonitor*> monitor) {
  glm::ivec2 monitor_position;
  glfwGetMonitorPos(monitor, &monitor_position.x, &monitor_position.y);
  return monitor_position;
}

glm::ivec2 GetMonitorWorkAreaSize(gsl::not_null<GLFWmonitor*> monitor) {
  glm::ivec2 monitor_size;
  glfwGetMonitorWorkarea(monitor, nullptr, nullptr, &monitor_size.x, &monitor_size.y);
  return monitor_size;
}

void MoveWindowToMonitor(gsl::not_null<GLFWwindow*> window, gsl::not_null<GLFWmonitor*> monitor) {
  glm::ivec2 window_size;
  glfwGetWindowSize(window, &window_size.x, &window_size.y);

  const glm::ivec2 window_new_position = GetMonitorPosition(monitor) + (GetMonitorWorkAreaSize(monitor) / 2) - (window_size / 2);
  glfwSetWindowPos(window, window_new_position.x, window_new_position.y);
}

}  // namespace veng
