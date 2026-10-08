#include "VulkanRenderer.h"
#include "Window.h"

#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstring>
#include <array>
#include <fstream>
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

// Helper macros
#define VK_CHECK(res, msg) if ((res) != VK_SUCCESS) throw std::runtime_error(msg);

// forward declarations for shader helpers (definitions appear below)
static bool compileGLSLtoSPV(const std::string& glslPath, const std::string& spvPath, const std::string& stage);
static std::vector<char> readOrCompileSPV(const std::string& spvPath, const std::string& glslPath, const std::string& stage);

static std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();
	return buffer;
}

static bool compileGLSLtoSPV(const std::string& glslPath, const std::string& spvPath, const std::string& stage)
{
	std::string cmd = "glslangValidator -V ";
	if (!stage.empty()) {
		cmd += "-S " + stage + " ";
	}
	cmd += "\"" + glslPath + "\" -o \"" + spvPath + "\"";
	int res = std::system(cmd.c_str());
	return res == 0;
}

static std::vector<char> readOrCompileSPV(const std::string& spvPath, const std::string& glslPath, const std::string& stage)
{
	try {
		return readFile(spvPath);
	} catch (const std::exception&) {
		// locate GLSL source near executable or provided path
		auto getExeDir = []() -> std::string {
#ifdef _WIN32
			char buf[MAX_PATH];
			DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
			if (len == 0 || len == MAX_PATH) return std::string();
			std::string p(buf, buf + len);
			size_t pos = p.find_last_of("\\/");
			if (pos == std::string::npos) return std::string();
			return p.substr(0, pos);
#else
			return std::string();
#endif
		};

		auto pathJoin = [](const std::string& a, const std::string& b) {
			if (a.empty()) return b;
			std::string aa = a;
			char sep = '/';
			if (aa.back() == '/' || aa.back() == '\\') return aa + b;
			return aa + std::string(1, sep) + b;
		};

		std::string exeDir = getExeDir();
		std::vector<std::string> candidates;
		candidates.push_back(glslPath);
		if (!exeDir.empty()) {
			std::string cur = exeDir;
			for (int i = 0; i < 5; ++i) {
				candidates.push_back(pathJoin(cur, glslPath));
				candidates.push_back(pathJoin(cur, std::string("shaders/") + glslPath.substr(glslPath.find_last_of("/\\") + 1)));
				size_t pos = cur.find_last_of("/\\");
				if (pos == std::string::npos) break;
				cur = cur.substr(0, pos);
			}
		}

		std::string foundGlsl;
		for (auto &c : candidates) {
			std::ifstream f(c);
			if (f.good()) { foundGlsl = c; break; }
		}

		if (foundGlsl.empty()) {
			throw std::runtime_error(std::string("SPIR-V file '") + spvPath + "' not found and GLSL source '" + glslPath + "' could not be located.");
		}

		std::cerr << "SPIR-V file '" << spvPath << "' not found; compiling GLSL '" << foundGlsl << "' with glslangValidator...\n";

		std::string::size_type slash = foundGlsl.find_last_of("/\\");
		std::string dir = (slash == std::string::npos) ? std::string() : foundGlsl.substr(0, slash);
		std::string spvOut = pathJoin(dir, spvPath.substr(spvPath.find_last_of("/\\") + 1));

		if (!compileGLSLtoSPV(foundGlsl, spvOut, stage)) {
			throw std::runtime_error("failed to compile GLSL to SPIR-V; ensure glslangValidator is installed and in PATH");
		}

		return readFile(spvOut);
	}
}

// compileGLSLtoSPV and readOrCompileSPV helpers are defined earlier in the file (or will be used from other modules)

// Update uniform buffer (view/proj)
void VulkanRenderer::updateUniformBuffer()
{
	if (m_uniformBuffer == VK_NULL_HANDLE || m_uniformBufferMemory == VK_NULL_HANDLE) return;
	// compute view and proj from camera
	glm::vec3 forward;
	forward.x = cosf(m_camAngles.x) * cosf(m_camAngles.y);
	forward.y = sinf(m_camAngles.x);
	forward.z = cosf(m_camAngles.x) * sinf(m_camAngles.y);
	glm::vec3 target = m_camPos + forward;
	glm::mat4 view = glm::lookAt(m_camPos, target, glm::vec3(0.0f, 1.0f, 0.0f));

	int w = 0, h = 0;
	m_window->getFramebufferSize(w, h);
	float aspect = (h == 0) ? 1.0f : static_cast<float>(w) / static_cast<float>(h);
	glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
	// GLM was designed for OpenGL; invert Y for Vulkan NDC
	proj[1][1] *= -1;

	void* data;
	vkMapMemory(m_device, m_uniformBufferMemory, 0, sizeof(glm::mat4) * 2, 0, &data);
	memcpy(data, &view, sizeof(glm::mat4));
	memcpy(static_cast<char*>(data) + sizeof(glm::mat4), &proj, sizeof(glm::mat4));
	vkUnmapMemory(m_device, m_uniformBufferMemory);
}

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type");
}

void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VK_CHECK(vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer), "failed to create buffer");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

	VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory), "failed to allocate buffer memory");

	vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
}

void VulkanRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = m_commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer), "failed to allocate command buffer for copy");

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	VkBufferCopy copyRegion{};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "failed to submit copy command buffer");
	vkQueueWaitIdle(m_graphicsQueue);

	vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanRenderer::drawFrameInternal()
{
	if (m_device == VK_NULL_HANDLE) return;

	uint32_t imageIndex;
	VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (res == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapchain();
		return;
	}
	VK_CHECK(res, "failed to acquire swapchain image");

	// Allocate a temporary command buffer
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = m_commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer), "failed to allocate command buffer");

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = nullptr;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo), "failed to begin command buffer");

	VkClearValue clearColor{};
	// Blue sky
	clearColor.color = {{0.529f, 0.808f, 0.922f, 1.0f}}; // skyblue

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = m_renderPass;
	renderPassInfo.framebuffer = m_framebuffers[imageIndex];
	renderPassInfo.renderArea.offset = {0, 0};
	renderPassInfo.renderArea.extent = m_swapchainExtent;
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

	// Bind descriptor set (UBO with view/proj)
	if (m_descriptorSet != VK_NULL_HANDLE) {
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
	}

	// Draw each scene object using push-constant model matrix
	for (const auto &obj : m_sceneObjects) {
		glm::mat4 model = glm::mat4(1.0f);
		// rotation stored as vec4(x,y,z,w)
		glm::quat rot(obj.rotation.w, obj.rotation.x, obj.rotation.y, obj.rotation.z);
		model = glm::translate(glm::mat4(1.0f), obj.position) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), obj.scale);

		vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &model);
		vkCmdDraw(commandBuffer, obj.vertexCount, 1, obj.vertexOffset, 0);
	}

	// Draw player cube (visible)
	// Find a mesh in scene objects that corresponds to player cube vertex offset if present; otherwise draw a generic cube at player
	glm::mat4 pmodel = glm::translate(glm::mat4(1.0f), m_playerPos) * glm::scale(glm::mat4(1.0f), m_playerHalfExtents * 2.0f);
	vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &pmodel);
	// If we appended a cube as the last object, draw it via m_sceneObjects back-index if available
	if (!m_sceneObjects.empty()) {
		const SceneObject &maybeCube = m_sceneObjects.back();
		vkCmdDraw(commandBuffer, maybeCube.vertexCount, 1, maybeCube.vertexOffset, 0);
	}

	VkDeviceSize offsets[] = {0};
	if (m_vertexBuffer != VK_NULL_HANDLE) {
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer, offsets);
	}
	if (m_indexBuffer != VK_NULL_HANDLE) {
		vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
	}
	if (m_descriptorSet != VK_NULL_HANDLE) {
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
	}

	// Draw ground first (non-indexed). Then draw triangle on top.
	if (m_groundVertexCount > 0) {
		vkCmdDraw(commandBuffer, m_groundVertexCount, 1, 0, 0);
	}
	if (m_triangleVertexCount > 0) {
		vkCmdDraw(commandBuffer, m_triangleVertexCount, 1, m_triangleVertexOffset, 0);
	} else if (m_vertexCount > 0 && m_indexCount == 0) {
		// Fallback: draw everything
		vkCmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
	}

	vkCmdEndRenderPass(commandBuffer);

	VK_CHECK(vkEndCommandBuffer(commandBuffer), "failed to record command buffer");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphore };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFence), "failed to submit draw command buffer");

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapchains[] = { m_swapchain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapchains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = nullptr;

	VkResult presRes = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
	if (presRes == VK_ERROR_OUT_OF_DATE_KHR || presRes == VK_SUBOPTIMAL_KHR) {
		recreateSwapchain();
	} else {
		VK_CHECK(presRes, "failed to present swapchain image");
	}

	vkQueueWaitIdle(m_graphicsQueue);

	vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}


VulkanRenderer::~VulkanRenderer()
{
	cleanup();
}

// Placeholder simple vertex format
struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
};

// Create minimal GPU resources for a plane and a cube (host visible staging omitted for brevity)
void VulkanRenderer::createSceneResources()
{
	// NOTE: This is a high-level placeholder. Proper Vulkan resource creation requires many helper functions.
	// Here we only show intent: create vertex/index buffers, a uniform buffer, descriptor set, and a simple pipeline.

	// For a real implementation: create staging buffers, copy to device local buffers, create UBO, descriptor sets, shaders, and pipeline.
	// We'll implement a straightforward path using helper functions below:

	// Create a ground plane (white) composed of two triangles, then a 3D triangle in front
	std::vector<Vertex> vertices;

	// Ground (two triangles) on y = 0, large quad
	std::vector<Vertex> ground = {
		{{-50.0f, 0.0f, -50.0f}, {1.0f, 1.0f, 1.0f}},
		{{50.0f, 0.0f, -50.0f}, {1.0f, 1.0f, 1.0f}},
		{{50.0f, 0.0f, 50.0f}, {1.0f, 1.0f, 1.0f}},
		{{-50.0f, 0.0f, 50.0f}, {1.0f, 1.0f, 1.0f}},
	};
	// two triangles: 0,1,2 and 2,3,0
	vertices.insert(vertices.end(), ground.begin(), ground.end());

	m_groundVertexCount = 6; // we'll emit two triangles using first 4 verts (we'll draw 6 vertices via indexing with no index by duplicating)
	// Construct draw sequence without index buffer by duplicating
	std::vector<Vertex> groundDraw = { ground[0], ground[1], ground[2], ground[2], ground[3], ground[0] };

	// Build a triangular-base pyramid (base + 3 side triangles).
	// Position it roughly above the ground near z = -3.
	float baseY = 0.2f;
	glm::vec3 baseCenter(0.0f, baseY, -3.0f);
	float baseRadius = 0.5f;
	float apexY = 1.8f;
	std::array<glm::vec3, 3> baseVerts;
	for (int i = 0; i < 3; ++i) {
		float ang = i * (2.0f * 3.14159265358979323846f) / 3.0f;
		baseVerts[i] = baseCenter + glm::vec3(cosf(ang) * baseRadius, 0.0f, sinf(ang) * baseRadius);
	}

	// Build final vertex array: ground (6 verts) then pyramid (base + 3 sides)
	vertices.clear();
	vertices.insert(vertices.end(), groundDraw.begin(), groundDraw.end());
	m_triangleVertexOffset = static_cast<uint32_t>(vertices.size());

	// base triangle (slightly gray)
	vertices.push_back({{baseVerts[0].x, baseVerts[0].y, baseVerts[0].z}, {0.8f, 0.8f, 0.8f}});
	vertices.push_back({{baseVerts[1].x, baseVerts[1].y, baseVerts[1].z}, {0.8f, 0.8f, 0.8f}});
	vertices.push_back({{baseVerts[2].x, baseVerts[2].y, baseVerts[2].z}, {0.8f, 0.8f, 0.8f}});

	// three side triangles (each uses two base verts + apex)
	for (int i = 0; i < 3; ++i) {
		int a = i;
		int b = (i + 1) % 3;
		vertices.push_back({{baseVerts[a].x, baseVerts[a].y, baseVerts[a].z}, {0.7f, 0.2f, 0.2f}});
		vertices.push_back({{baseVerts[b].x, baseVerts[b].y, baseVerts[b].z}, {0.2f, 0.7f, 0.2f}});
		vertices.push_back({{baseCenter.x, apexY, baseCenter.z}, {0.2f, 0.2f, 0.7f}});
	}

	m_groundVertexCount = 6;
	m_triangleVertexCount = static_cast<uint32_t>(vertices.size()) - m_triangleVertexOffset;
	m_vertexCount = static_cast<uint32_t>(vertices.size());
	m_indexCount = 0; // still using non-indexed draws

	VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();

	// Create staging buffer
	VkBuffer stagingVertexBuffer;
	VkDeviceMemory stagingVertexMemory;
	createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingVertexBuffer, stagingVertexMemory);

	void* data;
	vkMapMemory(m_device, stagingVertexMemory, 0, vertexBufferSize, 0, &data);
	memcpy(data, vertices.data(), (size_t)vertexBufferSize);
	vkUnmapMemory(m_device, stagingVertexMemory);

	createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_vertexBuffer, m_vertexBufferMemory);

	copyBuffer(stagingVertexBuffer, m_vertexBuffer, vertexBufferSize);

	vkDestroyBuffer(m_device, stagingVertexBuffer, nullptr);
	vkFreeMemory(m_device, stagingVertexMemory, nullptr);

	// Create scene objects: base pyramid object
	SceneObject pyramid{};
	pyramid.position = glm::vec3(0.0f, 0.0f, -3.0f);
	pyramid.scale = glm::vec3(1.0f);
	pyramid.rotation = glm::vec4(0,0,0,1); // quaternion as vec4
	pyramid.aabbHalfExtents = glm::vec3(0.8f, 1.0f, 0.8f);
	pyramid.vertexOffset = m_triangleVertexOffset;
	pyramid.vertexCount = m_triangleVertexCount;
	m_sceneObjects.push_back(pyramid);

	// Add a 3-colored cube near origin
	// Create cube geometry appended to vertex buffer (simple colored cube)
	size_t cubeOffset = vertices.size();
	float hs = 1.5f; // half-size
	std::array<glm::vec3,8> cubeVerts = {
		glm::vec3(-hs, -hs, -hs), glm::vec3(hs, -hs, -hs), glm::vec3(hs, hs, -hs), glm::vec3(-hs, hs, -hs),
		glm::vec3(-hs, -hs, hs),  glm::vec3(hs, -hs, hs),  glm::vec3(hs, hs, hs),  glm::vec3(-hs, hs, hs)
	};
	std::array<glm::vec3,6> faceColors = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1), glm::vec3(1,1,0), glm::vec3(1,0,1), glm::vec3(0,1,1) };
	std::vector<Vertex> cubeVertices;
	auto pushFace = [&](int a, int b, int c, int d, glm::vec3 col) {
		cubeVertices.push_back({{cubeVerts[a].x, cubeVerts[a].y, cubeVerts[a].z}, col});
		cubeVertices.push_back({{cubeVerts[b].x, cubeVerts[b].y, cubeVerts[b].z}, col});
		cubeVertices.push_back({{cubeVerts[c].x, cubeVerts[c].y, cubeVerts[c].z}, col});
		cubeVertices.push_back({{cubeVerts[a].x, cubeVerts[a].y, cubeVerts[a].z}, col});
		cubeVertices.push_back({{cubeVerts[c].x, cubeVerts[c].y, cubeVerts[c].z}, col});
		cubeVertices.push_back({{cubeVerts[d].x, cubeVerts[d].y, cubeVerts[d].z}, col});
	};
	// -Z face
	pushFace(0,1,2,3, faceColors[0]);
	// +Z face
	pushFace(5,4,7,6, faceColors[1]);
	// -X face
	pushFace(4,0,3,7, faceColors[2]);
	// +X face
	pushFace(1,5,6,2, faceColors[3]);
	// -Y face
	pushFace(4,5,1,0, faceColors[4]);
	// +Y face
	pushFace(3,2,6,7, faceColors[5]);

	// Recreate full vertex buffer with cube appended
	std::vector<Vertex> fullVerts = vertices;
	fullVerts.insert(fullVerts.end(), cubeVertices.begin(), cubeVertices.end());

	// Destroy old vertex buffer and re-create with new size
	if (m_vertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
	}
	if (m_vertexBufferMemory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
	}

	VkDeviceSize newSize = sizeof(fullVerts[0]) * fullVerts.size();

	// staging
	VkBuffer staging2;
	VkDeviceMemory staging2Mem;
	createBuffer(newSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		staging2, staging2Mem);
	void* mapped2 = nullptr;
	vkMapMemory(m_device, staging2Mem, 0, newSize, 0, &mapped2);
	memcpy(mapped2, fullVerts.data(), (size_t)newSize);
	vkUnmapMemory(m_device, staging2Mem);

	// device local buffer
	createBuffer(newSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_vertexBuffer, m_vertexBufferMemory);

	// copy staging2 -> m_vertexBuffer
	copyBuffer(staging2, m_vertexBuffer, newSize);

	// cleanup staging2
	vkDestroyBuffer(m_device, staging2, nullptr);
	vkFreeMemory(m_device, staging2Mem, nullptr);

	// Update counts and create cube scene object
	m_vertexCount = static_cast<uint32_t>(fullVerts.size());
	uint32_t cubeVertCount = static_cast<uint32_t>(cubeVertices.size());
	SceneObject cubeObj{};
	cubeObj.position = glm::vec3(0.0f, hs, 0.0f); // center the cube so pyramid sits inside
	// Make the cube larger in X and Z (non-uniform scale)
	cubeObj.scale = glm::vec3(3.0f, 1.0f, 4.0f);
	cubeObj.rotation = glm::vec4(0,0,0,1);
	// Match AABB to scaled dimensions so collisions align with visual scale
	cubeObj.aabbHalfExtents = glm::vec3(hs * cubeObj.scale.x, hs * cubeObj.scale.y, hs * cubeObj.scale.z);
	cubeObj.vertexOffset = static_cast<uint32_t>(cubeOffset);
	cubeObj.vertexCount = cubeVertCount;
	m_sceneObjects.push_back(cubeObj);

	// Add a small sphere logical object (octahedron approximation). Not re-uploaded to GPU in this simplified patch.
	uint32_t sphereOffset = static_cast<uint32_t>(fullVerts.size());
	std::vector<Vertex> sphereVerts;
	float sr = 0.5f;
	sphereVerts.push_back({{0, sr, 0}, {0.9f,0.6f,0.1f}});
	sphereVerts.push_back({{sr,0,0}, {0.9f,0.6f,0.1f}});
	sphereVerts.push_back({{0,0,sr}, {0.9f,0.6f,0.1f}});
	sphereVerts.push_back({{-sr,0,0}, {0.9f,0.6f,0.1f}});
	sphereVerts.push_back({{0,0,-sr}, {0.9f,0.6f,0.1f}});
	sphereVerts.push_back({{0,-sr,0}, {0.9f,0.6f,0.1f}});
	SphereObject sph{}; sph.pos = glm::vec3(-2.0f, 0.5f, -2.0f); sph.radius = sr; sph.vertexOffset = sphereOffset; sph.vertexCount = static_cast<uint32_t>(sphereVerts.size());
	m_spheres.push_back(sph);

	// Uniform buffer (view/proj) - host visible for simplicity
	VkDeviceSize uboSize = sizeof(glm::mat4) * 2; // view + proj
	createBuffer(uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		m_uniformBuffer, m_uniformBufferMemory);

	// Descriptor set layout
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &uboLayoutBinding;

	VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout), "failed to create descriptor set layout");

	// Descriptor pool
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize.descriptorCount = 1;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;

	VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "failed to create descriptor pool");

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_descriptorSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet), "failed to allocate descriptor set");

	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = m_uniformBuffer;
	bufferInfo.offset = 0;
	bufferInfo.range = uboSize;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = m_descriptorSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

	// Load SPIR-V; if .spv missing try to compile GLSL to SPV using glslangValidator
	// Wrap in try/catch to provide a clear error and guidance rather than throwing raw runtime_error
	std::vector<char> vertShaderCode;
	std::vector<char> fragShaderCode;
	try {
		vertShaderCode = readOrCompileSPV("shaders/vert.spv", "shaders/vert.glsl", "vert");
		fragShaderCode = readOrCompileSPV("shaders/frag.spv", "shaders/frag.glsl", "frag");
	} catch (const std::exception& e) {
		std::string msg = std::string("Failed to initialize Vulkan renderer: ") + e.what() +
			"\nEnsure shaders/vert.spv and shaders/frag.spv exist or that glslangValidator is installed and in PATH.\n" +
			"You can compile manually: glslangValidator -V shaders/vert.glsl -o shaders/vert.spv\n" +
			"and glslangValidator -V shaders/frag.glsl -o shaders/frag.spv\n";
		throw std::runtime_error(msg);
	}

	VkShaderModule vertShaderModule;
	VkShaderModule fragShaderModule;

	VkShaderModuleCreateInfo vertCreateInfo{};
	vertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vertCreateInfo.codeSize = vertShaderCode.size();
	vertCreateInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
	VK_CHECK(vkCreateShaderModule(m_device, &vertCreateInfo, nullptr, &vertShaderModule), "failed to create vertex shader module");

	VkShaderModuleCreateInfo fragCreateInfo{};
	fragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	fragCreateInfo.codeSize = fragShaderCode.size();
	fragCreateInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
	VK_CHECK(vkCreateShaderModule(m_device, &fragCreateInfo, nullptr, &fragShaderModule), "failed to create fragment shader module");

	// Pipeline: create minimal pipeline layout and graphics pipeline
	VkPipelineShaderStageCreateInfo vertStageInfo{};
	vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStageInfo.module = vertShaderModule;
	vertStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragStageInfo{};
	fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragStageInfo.module = fragShaderModule;
	fragStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

	// Vertex input
	VkVertexInputBindingDescription bindingDesc{};
	bindingDesc.binding = 0;
	bindingDesc.stride = sizeof(Vertex);
	bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription,2> attrDesc{};
	attrDesc[0].binding = 0;
	attrDesc[0].location = 0;
	attrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrDesc[0].offset = offsetof(Vertex, pos);
	attrDesc[1].binding = 0;
	attrDesc[1].location = 1;
	attrDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrDesc[1].offset = offsetof(Vertex, color);

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDesc.size());
	vertexInputInfo.pVertexAttributeDescriptions = attrDesc.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(m_swapchainExtent.width);
	viewport.height = static_cast<float>(m_swapchainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = {0,0};
	scissor.extent = m_swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	// Enable depth testing/writes so 3D occlusion works
	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	// Add push constant range for model matrix (mat4)
	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(glm::mat4);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushRange;

	VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout), "failed to create pipeline layout");

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = m_pipelineLayout;
	pipelineInfo.renderPass = m_renderPass;
	pipelineInfo.subpass = 0;

	VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline), "failed to create graphics pipeline");

	vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
	vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

void VulkanRenderer::destroySceneResources()
{
	if (m_device == VK_NULL_HANDLE) return;
	if (m_graphicsPipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
		m_graphicsPipeline = VK_NULL_HANDLE;
	}
	if (m_pipelineLayout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
		m_pipelineLayout = VK_NULL_HANDLE;
	}
	if (m_descriptorPool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
		m_descriptorPool = VK_NULL_HANDLE;
	}
	if (m_descriptorSetLayout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
		m_descriptorSetLayout = VK_NULL_HANDLE;
	}
	if (m_uniformBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device, m_uniformBuffer, nullptr);
		vkFreeMemory(m_device, m_uniformBufferMemory, nullptr);
		m_uniformBuffer = VK_NULL_HANDLE;
		m_uniformBufferMemory = VK_NULL_HANDLE;
	}
	if (m_indexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
		vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
		m_indexBuffer = VK_NULL_HANDLE;
		m_indexBufferMemory = VK_NULL_HANDLE;
	}
	if (m_vertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
		vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
		m_vertexBuffer = VK_NULL_HANDLE;
		m_vertexBufferMemory = VK_NULL_HANDLE;
	}
}

void VulkanRenderer::update(float dt)
{
	if (!m_window) return;

	// Tunables (local to keep renderer-agnostic)
	const float MOUSE_SENSITIVITY = 0.0025f;
	const float MOVE_SPEED = 3.0f;
	const float GRAVITY = -9.81f;
	const float JUMP_VELOCITY = 5.0f;

	// Mouse look: use GLFW via Window::handle()
	GLFWwindow* win = m_window->handle();
	double mx = 0.0, my = 0.0;
	glfwGetCursorPos(win, &mx, &my);
	static bool firstMouse = true;
	static double lastX = 0.0, lastY = 0.0;
	if (firstMouse) { lastX = mx; lastY = my; firstMouse = false; }
	double dx = mx - lastX;
	double dy = my - lastY;
	lastX = mx; lastY = my;

	// Update camera angles (yaw = y, pitch = x)
	// Yaw: add dx to rotate horizontally (no left/right inversion)
	m_camAngles.y += static_cast<float>(dx) * MOUSE_SENSITIVITY;
	m_camAngles.x += static_cast<float>(-dy) * MOUSE_SENSITIVITY;
	// clamp pitch to avoid flipping
	if (m_camAngles.x > 1.5f) m_camAngles.x = 1.5f;
	if (m_camAngles.x < -1.5f) m_camAngles.x = -1.5f;

	// Construct forward/right vectors from spherical angles
	glm::vec3 forward;
	forward.x = cosf(m_camAngles.x) * cosf(m_camAngles.y);
	forward.y = sinf(m_camAngles.x);
	forward.z = cosf(m_camAngles.x) * sinf(m_camAngles.y);
	// Compute right vector so A/D move left/right without inversion
	glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));

	// Movement input via GLFW
	glm::vec3 moveDir(0.0f);
	if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) moveDir += forward;
	if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) moveDir -= forward;
	// Inverted lateral mapping per user request: A strafes right, D strafes left
	if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) moveDir += right;
	if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) moveDir -= right;
	if (glm::length(moveDir) > 0.0001f) moveDir = glm::normalize(moveDir);

	// Apply horizontal movement
	m_camVel.x = moveDir.x * MOVE_SPEED;
	m_camVel.z = moveDir.z * MOVE_SPEED;

	// Jump
	if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS && m_onGround) {
		m_camVel.y = JUMP_VELOCITY;
		m_onGround = false;
	}

	// Gravity and integrate velocity -> position
	m_camVel.y += GRAVITY * dt;
	m_camPos += m_camVel * dt;

	// Simple ground collision at y = 0
	if (m_camPos.y <= 0.0f) {
		m_camPos.y = 0.0f;
		m_camVel.y = 0.0f;
		m_onGround = true;
	}

	// Note: this renderer is agnostic about uniform uploads.
	// The application should call update() from the main loop and
	// write view/proj/uniforms in drawFrame using m_camPos / m_camAngles.
}

void VulkanRenderer::init(VkInstance instance, VkSurfaceKHR surface, Window& window)
{
	m_instance = instance;
	m_surface = surface;
	m_window = &window;

	pickPhysicalDevice();
	createLogicalDevice();
	createSwapchain();
	createImageViews();
	createRenderPass();
	createFramebuffers();
	createCommandPoolAndBuffers();
	createSyncObjects();
	// Create scene GPU resources (buffers, descriptors, pipeline)
	createSceneResources();
}

void VulkanRenderer::cleanup()
{
	if (m_device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(m_device);
	}

	cleanupSwapchain();

	// Destroy scene GPU resources
	destroySceneResources();

	// Only destroy device-owned objects if device is valid
	if (m_device != VK_NULL_HANDLE) {
		if (m_imageAvailableSemaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
			m_imageAvailableSemaphore = VK_NULL_HANDLE;
		}
		if (m_renderFinishedSemaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
			m_renderFinishedSemaphore = VK_NULL_HANDLE;
		}
		if (m_inFlightFence != VK_NULL_HANDLE) {
			vkDestroyFence(m_device, m_inFlightFence, nullptr);
			m_inFlightFence = VK_NULL_HANDLE;
		}
		if (m_commandPool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(m_device, m_commandPool, nullptr);
			m_commandPool = VK_NULL_HANDLE;
		}

		vkDestroyDevice(m_device, nullptr);
		m_device = VK_NULL_HANDLE;
	}

	// Do not destroy instance or surface here (owned by main)
}

void VulkanRenderer::pickPhysicalDevice()
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
	if (deviceCount == 0) throw std::runtime_error("No Vulkan physical devices found");
	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

	// Pick first suitable device
	for (auto d : devices) {
		m_physicalDevice = d;
		break;
	}
	if (m_physicalDevice == VK_NULL_HANDLE) throw std::runtime_error("Failed to pick physical device");
}

void VulkanRenderer::createLogicalDevice()
{
	// Find a queue family that supports graphics and presentation
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

	int graphicsIndex = -1;
	for (uint32_t i = 0; i < queueFamilyCount; ++i) {
		VkBool32 presentSupport = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
		if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
			graphicsIndex = static_cast<int>(i);
			break;
		}
	}
	if (graphicsIndex < 0) throw std::runtime_error("Failed to find suitable queue family");

	m_graphicsQueueFamily = static_cast<uint32_t>(graphicsIndex);

	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfo{};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	VkPhysicalDeviceFeatures deviceFeatures{};

	const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = &queueCreateInfo;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = 1;
	createInfo.ppEnabledExtensionNames = deviceExtensions;

	VkResult res = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
	VK_CHECK(res, "Failed to create logical device");

	vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
}

VkExtent2D VulkanRenderer::chooseSwapExtent(int width, int height) const
{
	VkExtent2D actualExtent{};
	actualExtent.width = static_cast<uint32_t>(width);
	actualExtent.height = static_cast<uint32_t>(height);
	return actualExtent;
}

void VulkanRenderer::createSwapchain()
{
	// Query surface capabilities
	VkSurfaceCapabilitiesKHR caps{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

	int fbW = 0, fbH = 0;
	m_window->getFramebufferSize(fbW, fbH);
	if (fbW == 0 || fbH == 0) throw std::runtime_error("Framebuffer size is zero when creating swapchain");

	VkExtent2D extent = chooseSwapExtent(fbW, fbH);
	m_swapchainExtent = extent;

	// Choose format
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
	if (formatCount == 0) throw std::runtime_error("No surface formats");
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
	VkSurfaceFormatKHR surfaceFormat = formats[0];

	// Present mode: FIFO guaranteed

	uint32_t imageCount = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

	VkSwapchainCreateInfoKHR scInfo{};
	scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	scInfo.surface = m_surface;
	scInfo.minImageCount = imageCount;
	scInfo.imageFormat = surfaceFormat.format;
	scInfo.imageColorSpace = surfaceFormat.colorSpace;
	scInfo.imageExtent = extent;
	scInfo.imageArrayLayers = 1;
	scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	scInfo.preTransform = caps.currentTransform;
	scInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	scInfo.clipped = VK_TRUE;

	VkResult res = vkCreateSwapchainKHR(m_device, &scInfo, nullptr, &m_swapchain);
	VK_CHECK(res, "Failed to create swapchain");

	m_swapchainImageFormat = surfaceFormat.format;

	uint32_t actualCount = 0;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
	m_swapchainImages.resize(actualCount);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_swapchainImages.data());
}

void VulkanRenderer::createImageViews()
{
	m_swapchainImageViews.resize(m_swapchainImages.size());
	for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
		VkImageViewCreateInfo ivInfo{};
		ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivInfo.image = m_swapchainImages[i];
		ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivInfo.format = m_swapchainImageFormat;
		ivInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ivInfo.subresourceRange.baseMipLevel = 0;
		ivInfo.subresourceRange.levelCount = 1;
		ivInfo.subresourceRange.baseArrayLayer = 0;
		ivInfo.subresourceRange.layerCount = 1;

		VkResult res = vkCreateImageView(m_device, &ivInfo, nullptr, &m_swapchainImageViews[i]);
		VK_CHECK(res, "Failed to create image view");
	}
}

void VulkanRenderer::createRenderPass()
{
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = m_swapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkRenderPassCreateInfo rpInfo{};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpInfo.attachmentCount = 1;
	rpInfo.pAttachments = &colorAttachment;
	rpInfo.subpassCount = 1;
	rpInfo.pSubpasses = &subpass;

	VkResult res = vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass);
	VK_CHECK(res, "Failed to create render pass");
}

void VulkanRenderer::createFramebuffers()
{
	m_framebuffers.resize(m_swapchainImageViews.size());
	for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
		VkImageView attachments[] = { m_swapchainImageViews[i] };
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = attachments;
		fbInfo.width = m_swapchainExtent.width;
		fbInfo.height = m_swapchainExtent.height;
		fbInfo.layers = 1;

		VkResult res = vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffers[i]);
		VK_CHECK(res, "Failed to create framebuffer");
	}
}

void VulkanRenderer::createCommandPoolAndBuffers()
{
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
	poolInfo.flags = 0;
	VkResult res = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
	VK_CHECK(res, "Failed to create command pool");

	m_commandBuffers.resize(m_framebuffers.size());
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
	res = vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data());
	VK_CHECK(res, "Failed to allocate command buffers");

	// Record simple clear commands
	for (size_t i = 0; i < m_commandBuffers.size(); ++i) {
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(m_commandBuffers[i], &beginInfo);

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = m_renderPass;
		rpBegin.framebuffer = m_framebuffers[i];
		rpBegin.renderArea.offset = {0,0};
		rpBegin.renderArea.extent = m_swapchainExtent;

		VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues = &clearColor;

		vkCmdBeginRenderPass(m_commandBuffers[i], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdEndRenderPass(m_commandBuffers[i]);

		vkEndCommandBuffer(m_commandBuffers[i]);
	}
}

void VulkanRenderer::createSyncObjects()
{
	VkSemaphoreCreateInfo semInfo{};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailableSemaphore);
	vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinishedSemaphore);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFence);
}

void VulkanRenderer::cleanupSwapchain()
{
	// Only destroy swapchain-related resources if the device is valid.
	if (m_device != VK_NULL_HANDLE) {
		for (size_t i = 0; i < m_framebuffers.size(); ++i) {
			if (m_framebuffers[i]) {
				vkDestroyFramebuffer(m_device, m_framebuffers[i], nullptr);
				m_framebuffers[i] = VK_NULL_HANDLE;
			}
		}
		m_framebuffers.clear();

		for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
			if (m_swapchainImageViews[i]) {
				vkDestroyImageView(m_device, m_swapchainImageViews[i], nullptr);
				m_swapchainImageViews[i] = VK_NULL_HANDLE;
			}
		}
		m_swapchainImageViews.clear();

		if (m_swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
			m_swapchain = VK_NULL_HANDLE;
		}

		if (m_renderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(m_device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}
	}

	m_commandBuffers.clear();
}

void VulkanRenderer::recreateSwapchain()
{
	vkDeviceWaitIdle(m_device);
	cleanupSwapchain();
	createSwapchain();
	createImageViews();
	createRenderPass();
	createFramebuffers();
	createCommandPoolAndBuffers();
}

void VulkanRenderer::drawFrame()
{
	vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
	vkResetFences(m_device, 1, &m_inFlightFence);

	uint32_t imageIndex = 0;
	VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
	if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) VK_CHECK(res, "Failed to acquire swapchain image");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffers[imageIndex];
	VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphore };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	res = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFence);
	if (res != VK_SUCCESS) {
		if (res == VK_ERROR_DEVICE_LOST) throw std::runtime_error("Device lost during vkQueueSubmit");
		VK_CHECK(res, "Failed to submit draw command buffer");
	}

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapchains[] = { m_swapchain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapchains;
	presentInfo.pImageIndices = &imageIndex;

	res = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
	if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
		recreateSwapchain();
	} else if (res != VK_SUCCESS) {
		VK_CHECK(res, "Failed to present swapchain image");
	}
}
