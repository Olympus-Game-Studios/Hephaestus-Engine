cmake_minimum_required(VERSION 3.24)
project(HephaestusEngine LANGUAGES CXX)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum OS X deployment version")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

if(APPLE)
    # On macOS, we might need to link against MoltenVK explicitly if not handled by the loader
    # But usually Vulkan::Vulkan is enough if the SDK is set up correctly.
    # We might need to find other libs like ObjC foundation if we do native stuff.
endif()

add_subdirectory(extern/glfw)

# Ultralight (optional)
option(HEPHAESTUS_USE_ULTRALIGHT "Enable Ultralight integration" ON)
if(HEPHAESTUS_USE_ULTRALIGHT)
    set(ULTRALIGHT_BUILD_SAMPLES OFF CACHE BOOL "Disable Ultralight samples" FORCE)
    add_subdirectory(extern/Ultralight)

    option(HEPHAESTUS_EDITOR_HTML_UI "Enable Ultralight-hosted HTML editor panels" ON)
endif()

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
    engine/Gfx/VulkanSkybox.cpp
    engine/Physics/MeshCollisionSystem.cpp
    engine/Physics/JoltPhysicsEngine.cpp
    engine/Physics/JoltPhysicsSystem.cpp
    engine/Scene/Scene.cpp
    engine/Scene/Entity.cpp
    engine/Scene/SceneSerializer.cpp
    engine/Scene/SceneExporter.cpp
    engine/Scripting/ScriptBase.cpp
    engine/Scripting/ScriptEngine.cpp
    engine/Scripting/LuaScriptEngine.cpp
    engine/UI/ProjectManager.cpp
    engine/UI/HtmlUI/UltralightPlatformSetup.cpp
    engine/UI/HtmlUI/EditorMessageBridge.cpp
    engine/UI/HtmlUI/HtmlPanelView.cpp
    engine/UI/HtmlUI/HtmlEditorUIHost.cpp
    engine/UI/MaterialEditor/MaterialGraph.cpp
    engine/UI/MaterialEditor/MaterialGraphEvaluator.cpp
    engine/UI/MaterialEditor/MaterialEditorPanel.cpp
    engine/UI/MaterialEditor/TextureDiscovery.cpp
)

if (WIN32)
    set(APP_ICON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/editor/Icons/logo")
    set(APP_ICON_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_app_icon.ps1")
    set(APP_ICON_OUTPUT "${CMAKE_BINARY_DIR}/HephaestusEngine.ico")

    add_custom_command(
        OUTPUT ${APP_ICON_OUTPUT}
        COMMAND powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "${APP_ICON_SCRIPT}"
                -SourceDir "${APP_ICON_SOURCE_DIR}"
                -OutputIcon "${APP_ICON_OUTPUT}"
        DEPENDS
            "${APP_ICON_SCRIPT}"
            "${APP_ICON_SOURCE_DIR}/256px.png"
            "${APP_ICON_SOURCE_DIR}/128px.png"
            "${APP_ICON_SOURCE_DIR}/64px.png"
            "${APP_ICON_SOURCE_DIR}/48px.png"
            "${APP_ICON_SOURCE_DIR}/32px.png"
            "${APP_ICON_SOURCE_DIR}/16px.png"
        COMMENT "Generating Windows application icon (.ico)"
        VERBATIM
    )

    add_custom_target(GenerateAppIcon ALL DEPENDS ${APP_ICON_OUTPUT})

    set(APP_ICON_RC "${CMAKE_BINARY_DIR}/HephaestusEngine_icon.rc")
    set(APP_ICON_PATH ${APP_ICON_OUTPUT})
    configure_file(engine/Core/HephaestusEngine.rc.in ${APP_ICON_RC} @ONLY)

    add_dependencies(HephaestusEngine GenerateAppIcon)
    target_sources(HephaestusEngine PRIVATE ${APP_ICON_RC})
    set_source_files_properties(${APP_ICON_RC} PROPERTIES GENERATED TRUE)
elseif(APPLE)
    # macOS App Bundle setup
    set_target_properties(HephaestusEngine PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Hephaestus Engine"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hephaestus.engine"
        MACOSX_BUNDLE_INFO_STRING "Hephaestus Engine Editor"
    )
endif()

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

if(HEPHAESTUS_USE_ULTRALIGHT)
    target_include_directories(HephaestusEngine PRIVATE
        extern/Ultralight/SDK/include
    )

    # Link Ultralight SDK libraries directly (Windows).
    target_link_libraries(HephaestusEngine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/lib/AppCore.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/lib/Ultralight.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/lib/UltralightCore.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/lib/WebCore.lib"
    )

    if(HEPHAESTUS_EDITOR_HTML_UI)
        target_compile_definitions(HephaestusEngine PRIVATE HEPHAESTUS_EDITOR_HTML_UI=1)
    else()
        target_compile_definitions(HephaestusEngine PRIVATE HEPHAESTUS_EDITOR_HTML_UI=0)
    endif()

    if(WIN32)
        # Copy Ultralight DLLs to output directory.
        add_custom_command(TARGET HephaestusEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/bin/AppCore.dll"
                "$<TARGET_FILE_DIR:HephaestusEngine>/AppCore.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/bin/Ultralight.dll"
                "$<TARGET_FILE_DIR:HephaestusEngine>/Ultralight.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/bin/UltralightCore.dll"
                "$<TARGET_FILE_DIR:HephaestusEngine>/UltralightCore.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/bin/WebCore.dll"
                "$<TARGET_FILE_DIR:HephaestusEngine>/WebCore.dll"
            COMMENT "Copying Ultralight SDK DLLs"
        )

        # Ultralight runtime resources (ICU data, cacert, etc.).
        # Ultralight loads these via the FileSystem interface using the relative path "resources/...".
        add_custom_command(TARGET HephaestusEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/extern/Ultralight/SDK/resources"
                "$<TARGET_FILE_DIR:HephaestusEngine>/resources"
            COMMENT "Copying Ultralight SDK resources"
        )
    endif()
else()
    target_compile_definitions(HephaestusEngine PRIVATE HEPHAESTUS_EDITOR_HTML_UI=0)
endif()

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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadowmap.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadowmap.frag
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

# --- Post-build: Copy required assets to build directory ---
# Copy assets folder
add_custom_command(TARGET HephaestusEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:HephaestusEngine>/assets"
    COMMENT "Copying assets to build directory"
)

# Copy editor folder (icons, fonts, etc.)
add_custom_command(TARGET HephaestusEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/editor"
        "$<TARGET_FILE_DIR:HephaestusEngine>/editor"
    COMMENT "Copying editor assets to build directory"
)

# --- Install target for distribution packaging ---
include(GNUInstallDirs)

# Install executable
install(TARGETS HephaestusEngine
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)

# Install shaders
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders/
    DESTINATION shaders
    FILES_MATCHING PATTERN "*.spv"
)

# Install assets
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/
    DESTINATION assets
)

# Install editor resources (icons, fonts)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/editor/
    DESTINATION editor
)

# Install Lua DLL if present
if(EXISTS "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/bin/lua.dll")
    install(FILES "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/bin/lua.dll"
        DESTINATION .
    )
endif()

# Create a runtime marker file for auto-detection
install(CODE "
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/PACKAGE_MANIFEST.txt\" 
         \"Hephaestus Engine Runtime Package\\nGenerated: ${CMAKE_CURRENT_TIMESTAMP}\\n\")
")

