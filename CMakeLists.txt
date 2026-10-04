cmake_minimum_required(VERSION 3.22)
project(Vulkan-Project VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_WITH_VULKAN "Enable Vulkan support" ON)

if(BUILD_WITH_VULKAN)
	find_package(Vulkan)
	if(NOT Vulkan_FOUND AND NOT TARGET Vulkan::Vulkan)
		message(FATAL_ERROR "Vulkan SDK not found. Install the LunarG Vulkan SDK or set -DBUILD_WITH_VULKAN=OFF to disable Vulkan support.")
	endif()
endif()

include(FetchContent)

FetchContent_Declare(
	glm
	GIT_REPOSITORY "https://github.com/g-truc/glm.git"
	GIT_TAG "0.9.9.8"
	GIT_SHALLOW TRUE
)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY "https://github.com/glfw/glfw.git"
	GIT_TAG "3.3.8"
	GIT_SHALLOW TRUE
)


# Populate glm source without executing its CMakeLists (some glm CMakeLists require
# top-level project policies which can break when added as a subproject). We'll
# create a lightweight INTERFACE target that exposes its headers only.
FetchContent_Populate(glm)

add_library(glm_headers INTERFACE)
target_include_directories(glm_headers INTERFACE ${glm_SOURCE_DIR})

if(NOT TARGET glm::glm)
	add_library(glm::glm ALIAS glm_headers)
endif()

# Optionally enable GLFW
option(BUILD_WITH_GLFW "Enable GLFW (windowing) support" ON)

if(BUILD_WITH_GLFW)
	# Try to find an installed GLFW first
	find_package(glfw3 QUIET)
	if(NOT glfw3_FOUND)
		# Configure GLFW build options to avoid extra build overhead
		set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
		set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
		set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

		FetchContent_Declare(
			glfw
			GIT_REPOSITORY "https://github.com/glfw/glfw.git"
			GIT_TAG "3.3.8"
			GIT_SHALLOW TRUE
		)
		FetchContent_MakeAvailable(glfw)
	endif()
endif()

file(GLOB_RECURSE VulkanEngineSources CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(Vulkan-Project ${VulkanEngineSources})

# Project headers
target_include_directories(Vulkan-Project PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Prefer the imported Vulkan target when available, fall back to variables
if(BUILD_WITH_VULKAN)
	if(TARGET Vulkan::Vulkan)
		target_link_libraries(Vulkan-Project PRIVATE Vulkan::Vulkan)
	else()
		target_include_directories(Vulkan-Project PRIVATE ${Vulkan_INCLUDE_DIRS})
		target_link_libraries(Vulkan-Project PRIVATE ${Vulkan_LIBRARIES})
	endif()
endif()

# GLM is header-only; expose its include directory to the project
if(DEFINED glm_SOURCE_DIR)
	target_include_directories(Vulkan-Project PRIVATE ${glm_SOURCE_DIR})
endif()

# Link GLFW if requested and available
if(BUILD_WITH_GLFW)
	if(TARGET glfw)
		target_link_libraries(Vulkan-Project PRIVATE glfw)
	elseif(TARGET GLFW::GLFW)
		target_link_libraries(Vulkan-Project PRIVATE GLFW::GLFW)
	else()
		message(WARNING "GLFW requested but no GLFW target found; includes may fail.")
	endif()
endif()

target_compile_features(Vulkan-Project PRIVATE cxx_std_17)
