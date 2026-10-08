#pragma once

#include <vulkan/vulkan.h>

namespace veng {

struct TextureHandle {
  VkImage image = VK_NULL_HANDLE;
  VkImageView image_view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
};

}  // namespace veng
