cmake_minimum_required(VERSION 3.24)
project(HephaestusEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
add_subdirectory(extern/glfw)

# Lua scripting support - try to find it, but make it optional for now
# Check for vcpkg installation first
set(VCPKG_ROOT "$ENV{USERPROFILE}/vcpkg" CACHE PATH "Path to vcpkg installation")
if(EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" AND NOT CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Found vcpkg at ${VCPKG_ROOT}")
    # Vcpkg is available - it will handle finding Lua
    # This is particularly useful when building from scratch
endif()

# Try to find Lua via standard mechanisms (vcpkg, system paths, etc.)
find_package(Lua QUIET)
if(NOT Lua_FOUND)
    message(STATUS "Lua not found via standard search paths")
    
    # Try manual search in vcpkg directories
    set(VCPKG_TRIPLET "x64-windows")
    set(VCPKG_LUA_PATH "$ENV{USERPROFILE}/vcpkg/installed/${VCPKG_TRIPLET}")
    
    if(EXISTS "${VCPKG_LUA_PATH}/include/lua.h")
        message(STATUS "Found Lua in vcpkg at ${VCPKG_LUA_PATH}")
        set(LUA_INCLUDE_DIR "${VCPKG_LUA_PATH}/include")
        set(LUA_FOUND TRUE)
        
        # Find Lua library
        if(EXISTS "${VCPKG_LUA_PATH}/lib/lua.lib")
            set(LUA_LIBRARIES "${VCPKG_LUA_PATH}/lib/lua.lib")
        elseif(EXISTS "${VCPKG_LUA_PATH}/lib/lua54.lib")
            set(LUA_LIBRARIES "${VCPKG_LUA_PATH}/lib/lua54.lib")
        elseif(EXISTS "${VCPKG_LUA_PATH}/lib/lua53.lib")
            set(LUA_LIBRARIES "${VCPKG_LUA_PATH}/lib/lua53.lib")
        else()
            message(WARNING "Lua include found but library not found in ${VCPKG_LUA_PATH}/lib/")
            set(LUA_FOUND FALSE)
        endif()
    else()
        message(STATUS "Lua not found in vcpkg either")
        message(WARNING "Lua not found! Lua scripting will be disabled.")
        message(STATUS "To enable Lua support:")
        message(STATUS "  Option 1: vcpkg install lua:x64-windows")
        message(STATUS "  Option 2: Download from lua.org and install manually")
        message(STATUS "  Option 3: For CMake to find vcpkg Lua, use: cmake -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%/vcpkg/scripts/buildsystems/vcpkg.cmake ..")
        
        set(LUA_INCLUDE_DIR "")
        set(LUA_LIBRARIES "")
    endif()
else()
    message(STATUS "Found Lua: ${LUA_LIBRARIES}")
endif()

# Jolt Physics Engine
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Jolt as static library" FORCE)
set(JOLT_SANITIZE_UNDEFINED_BEHAVIOR OFF CACHE BOOL "" FORCE)
set(JOLT_CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "Enable deterministic behavior" FORCE)
# Disable static MSVC runtime to avoid linker conflicts
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use dynamic MSVC runtime" FORCE)
add_subdirectory(extern/JoltPhysics/Build)

add_executable(HephaestusEngine
    engine/Core/main.cpp
    engine/Assets/ModelImporter.cpp
    engine/Gfx/VulkanDevice.cpp
    engine/Gfx/VulkanSwapchain.cpp
    engine/Gfx/VulkanPipeline.cpp
    engine/Gfx/VulkanTexture.cpp
    engine/Gfx/VulkanGizmo.cpp
    engine/Physics/MeshCollisionSystem.cpp
    engine/Physics/JoltPhysicsEngine.cpp
    engine/Physics/JoltPhysicsSystem.cpp
    engine/Scene/Scene.cpp
    engine/Scene/Entity.cpp
    engine/Scene/SceneSerializer.cpp
    engine/Scripting/ScriptBase.cpp
    engine/Scripting/ScriptEngine.cpp
    engine/Scripting/LuaScriptEngine.cpp
    engine/UI/ProjectManager.cpp
    engine/UI/MaterialEditor/MaterialGraph.cpp
    engine/UI/MaterialEditor/MaterialGraphEvaluator.cpp
    engine/UI/MaterialEditor/MaterialEditorPanel.cpp
    engine/UI/MaterialEditor/TextureDiscovery.cpp
)

target_include_directories(HephaestusEngine PRIVATE engine)
if(LUA_FOUND)
    if(TARGET Lua::Lua)
        target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw Jolt Lua::Lua)
    else()
        # Manual vcpkg Lua
        target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw Jolt ${LUA_LIBRARIES})
    endif()
    target_include_directories(HephaestusEngine PRIVATE ${LUA_INCLUDE_DIR})
    target_compile_definitions(HephaestusEngine PRIVATE LUA_AVAILABLE=1)
    
    # Copy Lua DLL to output directory
    set(VCPKG_LUA_DLL_PATH "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/bin/lua.dll")
    if(EXISTS "${VCPKG_LUA_DLL_PATH}")
        add_custom_command(TARGET HephaestusEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_LUA_DLL_PATH}"
                "$<TARGET_FILE_DIR:HephaestusEngine>/lua.dll"
            COMMENT "Copying lua.dll to output directory"
        )
    endif()
else()
    target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw Jolt)
    # Lua disabled - LuaScriptEngine will be a stub
    target_compile_definitions(HephaestusEngine PRIVATE LUA_AVAILABLE=0)
endif()
target_include_directories(HephaestusEngine PRIVATE
    engine
    extern
    extern/VMA
    extern/entt/single_include
    extern/JoltPhysics
    extern/JoltPhysics/Build
)

# Set output directory for executable
set_target_properties(HephaestusEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.frag
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

