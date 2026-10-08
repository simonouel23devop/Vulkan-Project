#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <memory>
#include <cstdint>

class Window;

class VulkanRenderer {
public:
	VulkanRenderer() = default;
	~VulkanRenderer();

	// Initialize renderer with created VkInstance and GLFW window surface
	void init(VkInstance instance, VkSurfaceKHR surface, Window& window);
	void cleanup();
	// New: update and camera input
	void update(float dt);
	void setWindow(Window* window) { m_window = window; }

	// New: basic scene resources lifecycle
	void createSceneResources();
	void destroySceneResources();

	// Update uniform buffer (view/projection) from camera
	void updateUniformBuffer();

	// Draw a frame (acquire, submit, present)
	void drawFrame();
	// Internal draw used by the new scene implementation (kept separate to avoid duplicate definitions)
	void drawFrameInternal();

	// Recreate swapchain (call when resized or out-of-date)
	void recreateSwapchain();

private:
	VkInstance m_instance = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	uint32_t m_graphicsQueueFamily = UINT32_MAX;

	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D m_swapchainExtent{};
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> m_framebuffers;

	// Depth resources
	VkImage m_depthImage = VK_NULL_HANDLE;
	VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
	VkImageView m_depthImageView = VK_NULL_HANDLE;

	// Scene objects
	struct SceneObject {
		glm::vec3 position;
		glm::vec3 scale;
		// store rotation as a vec4 (x,y,z,w) to avoid requiring quaternion completeness in all translation units
		glm::vec4 rotation;
		// simple AABB extents (half-extents)
		glm::vec3 aabbHalfExtents;
		uint32_t vertexOffset;
		uint32_t vertexCount;
	};

	std::vector<SceneObject> m_sceneObjects;

	// Player representation (visible cube + physics)
	glm::vec3 m_playerPos = glm::vec3(0.0f, 1.0f, 3.0f);
	glm::vec3 m_playerVel = glm::vec3(0.0f);
	glm::vec3 m_playerHalfExtents = glm::vec3(0.5f, 1.0f, 0.5f);
	bool m_playerOnGround = false;

	// Sphere object (for rendering, simple bounding sphere used for collision check if needed)
	struct SphereObject { glm::vec3 pos; float radius; uint32_t vertexOffset; uint32_t vertexCount; };
	std::vector<SphereObject> m_spheres;

	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;

	VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence m_inFlightFence = VK_NULL_HANDLE;

	Window* m_window = nullptr;

	// Camera / player state
	// Place camera slightly back and looking toward -Z by default
	glm::vec3 m_camPos{0.0f, 0.0f, 3.0f};
	glm::vec2 m_camAngles{0.0f, -3.14159265f / 2.0f}; // pitch, yaw
	glm::vec3 m_camVel{0.0f};
	bool m_onGround = false;

	// Mouse input tracking
	double m_lastMouseX = 0.0;
	double m_lastMouseY = 0.0;
	bool m_firstMouse = true;

	// Gameplay constants
	const float PLAYER_SPEED = 4.0f; // units per second
	const float PLAYER_ACCEL = 20.0f; // accel for smoothing
	const float PLAYER_JUMP_V = 6.0f;
	const float GRAVITY = -9.81f;

	// Scene GPU resources (simple)
	VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
	VkBuffer m_indexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;

	VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_uniformBufferMemory = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
	uint32_t m_indexCount = 0;
	uint32_t m_vertexCount = 0;
	uint32_t m_groundVertexCount = 0;
	uint32_t m_triangleVertexCount = 0;
	uint32_t m_triangleVertexOffset = 0;

	// Helpers
	// Buffer and memory helpers
	uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
	void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

	void pickPhysicalDevice();
	void createLogicalDevice();
	void createSwapchain();
	void cleanupSwapchain();
	void createImageViews();
	void createRenderPass();
	void createFramebuffers();
	void createCommandPoolAndBuffers();
	void createSyncObjects();
	VkExtent2D chooseSwapExtent(int width, int height) const;
};
