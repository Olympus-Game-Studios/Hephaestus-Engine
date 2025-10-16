cmake_minimum_required(VERSION 3.24)
project(HephaestusEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
add_subdirectory(extern/glfw)

add_executable(HephaestusEngine
    engine/Core/main.cpp
    engine/Gfx/VulkanDevice.cpp
    engine/Gfx/VulkanSwapchain.cpp
)

target_include_directories(HephaestusEngine PRIVATE engine)
target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw)
