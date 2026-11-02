# Run with cmake -P after HephaestusGame.app links. Copies the player's non-system
# libraries (Lua, the Vulkan loader, MoltenVK) into the bundle so packaged games run
# on Macs without Homebrew or the Vulkan SDK.
#   APP          path to HephaestusGame.app
#   EXE          path to its executable
#   VULKAN       the Vulkan loader the player linked (@rpath, so not found by otool)
#   MOLTENVK     libMoltenVK.dylib
#   MOLTENVK_ICD the SDK's MoltenVK_icd.json

set(frameworks "${APP}/Contents/Frameworks")
file(MAKE_DIRECTORY "${frameworks}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGN} failed: ${error}")
    endif()
endfunction()

function(bundle_library library)
    file(REAL_PATH "${library}" real)
    execute_process(COMMAND otool -D "${real}" OUTPUT_VARIABLE id OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE ".*\n" "" id "${id}") # last line; universal binaries print one per arch
    get_filename_component(name "${id}" NAME)
    file(COPY_FILE "${real}" "${frameworks}/${name}")
    run(install_name_tool -id "@rpath/${name}" "${frameworks}/${name}")
    if(NOT id STREQUAL "@rpath/${name}")
        run(install_name_tool -change "${id}" "@rpath/${name}" "${EXE}")
    endif()
    run(codesign --force --sign - "${frameworks}/${name}")
endfunction()

bundle_library("${VULKAN}")
bundle_library("${MOLTENVK}")

# Absolute non-system dependencies, such as Homebrew's Lua.
execute_process(COMMAND otool -L "${EXE}" OUTPUT_VARIABLE dependencies)
string(REPLACE "\n" ";" dependencies "${dependencies}")
foreach(line IN LISTS dependencies)
    if(line MATCHES "^[ \t]+(/[^ ]+) \\(")
        set(dependency "${CMAKE_MATCH_1}")
        if(NOT dependency MATCHES "^/(System|usr/lib)/")
            bundle_library("${dependency}")
        endif()
    endif()
endforeach()

# The Vulkan loader looks for drivers in the bundle's Resources/vulkan/icd.d.
file(READ "${MOLTENVK_ICD}" icd)
string(REGEX REPLACE "\"library_path\"[ \t]*:[ \t]*\"[^\"]*\""
       "\"library_path\": \"../../../Frameworks/libMoltenVK.dylib\"" icd "${icd}")
file(WRITE "${APP}/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" "${icd}")

run(codesign --force --sign - "${APP}")
