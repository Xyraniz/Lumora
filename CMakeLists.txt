cmake_minimum_required(VERSION 3.20)
project(Lumora VERSION 0.1.0 LANGUAGES C CXX)

# SDL is only needed by the optional visual laboratory.  The headless runtime
# is useful on CI, servers, and minimal Windows installations where SDL is not
# available, so do not make the visual dependencies a prerequisite for the
# core executable.
find_package(SDL2 QUIET)
find_package(SDL2_ttf QUIET)
find_package(SDL2_image QUIET)

set(LUMORA_VISUAL_AVAILABLE OFF)
if(SDL2_FOUND AND SDL2_ttf_FOUND AND SDL2_image_FOUND)
    set(LUMORA_VISUAL_AVAILABLE ON)
    message(STATUS "Lumora visual laboratory: enabled")
else()
    message(STATUS "Lumora visual laboratory: disabled (SDL2, SDL2_ttf, and SDL2_image are required)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(LUAU_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/luau EXCLUDE_FROM_ALL)

add_executable(lumora
    src/main.cpp
    src/prelude.cpp
    src/runtime.cpp
    src/paths.cpp
    src/require.cpp
    src/json.cpp
    src/host_api.cpp
)
if(LUMORA_VISUAL_AVAILABLE)
    target_sources(lumora PRIVATE src/visual.cpp)
else()
    target_sources(lumora PRIVATE src/visual_stub.cpp)
endif()
target_link_libraries(lumora PRIVATE Luau.Compiler Luau.VM Luau.Common Luau.Require Luau.CLI.lib)
target_include_directories(lumora PRIVATE vendor/luau/VM/include vendor/luau/Compiler/include vendor/luau/CLI/include src)

if(LUMORA_VISUAL_AVAILABLE)
    target_link_libraries(lumora PRIVATE SDL2::SDL2 SDL2_ttf::SDL2_ttf SDL2_image::SDL2_image)
endif()

if(UNIX)
    target_link_libraries(lumora PRIVATE pthread dl m)
endif()

add_custom_command(TARGET lumora POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/bin/lumora$<TARGET_FILE_SUFFIX:lumora>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/bin/luau-vm$<TARGET_FILE_SUFFIX:lumora>
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME roblox_api COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/roblox_api.lua)
    add_test(NAME roblox_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/roblox_contract.lua)
    add_test(NAME luau_modern COMMAND $<TARGET_FILE:lumora> --no-roblox ${CMAKE_SOURCE_DIR}/tests/luau_modern.lua)
    add_test(NAME datatypes_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/datatypes_contract.lua)
    add_test(NAME properties_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/properties_contract.lua)
    add_test(NAME events_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/events_contract.lua)
    add_test(NAME negative_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/negative_contract.lua)
    add_test(NAME host_api_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/host_api_contract.lua)
    add_test(NAME simulated_players_esp COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/simulated_players_esp.lua)
    add_test(NAME functional_apis_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/functional_apis_contract.lua)
    add_test(NAME require_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/require_contract.lua)
    add_test(NAME require_pure_contract COMMAND $<TARGET_FILE:lumora> --no-roblox ${CMAKE_SOURCE_DIR}/tests/require_pure_contract.lua)
    add_test(NAME lune_compat_contract COMMAND $<TARGET_FILE:lumora> run ${CMAKE_SOURCE_DIR}/tests/lune_compat_contract.lua)
    add_test(NAME sandbox_require_contract COMMAND $<TARGET_FILE:lumora> --sandbox ${CMAKE_SOURCE_DIR}/tests/sandbox_require_contract.lua)
    add_test(NAME scheduler_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/scheduler_contract.lua)
    add_test(NAME attached_library_load COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/examples/attached_library.lua)
    add_test(NAME roblox_parity COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/roblox_parity.lua)

    # These contracts are POSIX shell programs. Register them only where the
    # test runner can execute them; otherwise CTest reports misleading
    # BAD_COMMAND failures before any Lumora code is run.
    if(UNIX)
        add_test(NAME smoke COMMAND ${CMAKE_SOURCE_DIR}/tests/smoke.sh $<TARGET_FILE:lumora>)
        add_test(NAME cli_flags COMMAND ${CMAKE_SOURCE_DIR}/tests/cli_flags.sh $<TARGET_FILE:lumora>)
        add_test(NAME cli_contract COMMAND ${CMAKE_SOURCE_DIR}/tests/cli_contract.sh $<TARGET_FILE:lumora>)
        add_test(NAME sandbox_contract COMMAND ${CMAKE_SOURCE_DIR}/tests/sandbox_contract.sh $<TARGET_FILE:lumora>)
        add_test(NAME json_schema COMMAND ${CMAKE_SOURCE_DIR}/tests/json_schema.sh $<TARGET_FILE:lumora>)
        add_test(NAME differential_contract COMMAND ${CMAKE_SOURCE_DIR}/tests/differential_contract.sh $<TARGET_FILE:lumora>)
        add_test(NAME lune_compat COMMAND ${CMAKE_SOURCE_DIR}/tests/lune_compat.sh $<TARGET_FILE:lumora>)
    endif()
endif()
