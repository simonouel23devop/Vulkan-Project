#pragma once

namespace veng {

struct GlfwInitialization {
 public:
  GlfwInitialization();
  ~GlfwInitialization();

  GlfwInitialization(const GlfwInitialization&) = delete;
  GlfwInitialization& operator=(const GlfwInitialization&) = delete;
};

}  // namespace veng
