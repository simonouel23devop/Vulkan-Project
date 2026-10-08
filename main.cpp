#include <precomp.h>
#include <GLFW/glfw3.h>
#include <glfw_initialization.h>
#include <glfw_monitor.h>
#include <glfw_window.h>
#include <graphics.h>
#include <glm/gtc/matrix_transform.hpp>

std::int32_t main(std::int32_t argc, gsl::zstring* argv) {
  const veng::GlfwInitialization _glfw;

  veng::Window window("Vulkan Engine", {800, 600});
  window.TryMoveToMonitor(1);  // if you want to change monitor

  veng::Graphics graphics(&window);

  std::array<veng::Vertex, 4> vertices = {
    veng::Vertex({-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}), // top - left
    veng::Vertex({0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}), // top - right
    veng::Vertex({-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}), // bottom - left
    veng::Vertex({0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}), // bottom - right
  };

  veng::BufferHandle buffer = graphics.CreateVertexBuffer(vertices);

  std::array<std::uint32_t, 6> indices = {
    0, 3, 2, 0, 1, 3
  };

  veng::BufferHandle index_buffer = graphics.CreateIndexBuffer(indices);

  glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f));
  glm::mat4 projection = glm::perspective(glm::radians(60.0f), 800.0f / 600.0f, 0.1f, 100.0f);
  graphics.SetViewProjection(view, projection);

  veng::TextureHandle texture = graphics.CreateTexture("paving-stones.jpg");

  while (!window.ShouldClose()) {
    glfwPollEvents();  // not window specific
    if(graphics.BeginFrame()) {
      graphics.SetTexture(texture);
      graphics.RenderIndexedBuffer(buffer, index_buffer, indices.size());
      graphics.EndFrame();
    }
  }

  graphics.DestroyTexture(texture);
  graphics.DestroyBuffer(buffer);
  graphics.DestroyBuffer(index_buffer);

  return EXIT_SUCCESS;
}
