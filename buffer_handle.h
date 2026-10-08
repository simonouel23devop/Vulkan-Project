#pragma once

#include <vulkan/vulkan.h>

namespace veng {

struct BufferHandle {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

}  // namespace veng
