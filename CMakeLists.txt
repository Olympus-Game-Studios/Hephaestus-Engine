cmake_minimum_required(VERSION 3.24)
project(HephaestusEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
add_subdirectory(extern/glfw)

add_executable(HephaestusEngine
    engine/Core/main.cpp
    engine/Assets/ModelImporter.cpp
    engine/Gfx/VulkanDevice.cpp
    engine/Gfx/VulkanSwapchain.cpp
    engine/Gfx/VulkanPipeline.cpp
    engine/Gfx/VulkanTexture.cpp
    engine/Gfx/VulkanGizmo.cpp
    engine/Physics/MeshCollisionSystem.cpp
    engine/Scene/Scene.cpp
    engine/Scene/Entity.cpp
    engine/Scene/SceneSerializer.cpp
    engine/UI/MaterialEditor/MaterialGraph.cpp
    engine/UI/MaterialEditor/MaterialGraphEvaluator.cpp
    engine/UI/MaterialEditor/MaterialEditorPanel.cpp
    engine/UI/MaterialEditor/TextureDiscovery.cpp
)

target_include_directories(HephaestusEngine PRIVATE engine)
target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw)
target_include_directories(HephaestusEngine PRIVATE
    engine
    extern
    extern/VMA
    extern/entt/single_include
)


# Dear ImGui
add_library(imgui STATIC
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
    extern/imgui/backends/imgui_impl_glfw.cpp
    extern/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    extern/imgui
    extern/imgui/backends
)

target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
target_link_libraries(HephaestusEngine PRIVATE imgui)

# ImGuizmo
add_library(imguizmo STATIC
    extern/ImGuizmo/ImGuizmo.cpp
)

target_include_directories(imguizmo PUBLIC
    extern/ImGuizmo
)

target_link_libraries(imguizmo PUBLIC imgui)
target_link_libraries(HephaestusEngine PRIVATE imguizmo)

set(IMNODES_IMGUI_TARGET_NAME imgui)
set(IMNODES_EXAMPLES OFF CACHE BOOL "Disable ImNodes examples" FORCE)
add_subdirectory(extern/imnodes)
target_link_libraries(HephaestusEngine PRIVATE imnodes)



# --- Compile GLSL shaders to SPIR-V if glslc is available ---
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)
if (GLSLC_EXECUTABLE)
    message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")
    set(SHADER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/billboard.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/billboard.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo.frag
    )

    set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/shaders)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/Release/shaders)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/Debug/shaders)

    set(SHADER_OUTPUTS)
    foreach(SRC ${SHADER_SOURCES})
        get_filename_component(FN ${SRC} NAME)
        set(OUT ${SHADER_OUTPUT_DIR}/${FN}.spv)
        add_custom_command(
            OUTPUT ${OUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLC_EXECUTABLE} ${SRC} -o ${OUT}
            DEPENDS ${SRC}
            COMMENT "Compiling ${FN} -> ${OUT}"
            VERBATIM
        )
        list(APPEND SHADER_OUTPUTS ${OUT})
    endforeach()

    add_custom_target(Shaders ALL DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(HephaestusEngine Shaders)
else()
    message(WARNING "glslc not found; expecting precompiled .spv under build/<config>/shaders")
endif()

