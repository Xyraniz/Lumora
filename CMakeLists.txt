cmake_minimum_required(VERSION 3.20)
project(Lumora VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(LUAU_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/luau EXCLUDE_FROM_ALL)

add_executable(lumora src/main.cpp)
target_link_libraries(lumora PRIVATE Luau.Compiler Luau.VM Luau.Common)
target_include_directories(lumora PRIVATE vendor/luau/VM/include vendor/luau/Compiler/include)

if(UNIX)
    target_link_libraries(lumora PRIVATE pthread dl m)
endif()

add_custom_command(TARGET lumora POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/bin/lumora
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/bin/luau-vm
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME smoke COMMAND ${CMAKE_SOURCE_DIR}/tests/smoke.sh $<TARGET_FILE:lumora>)
    add_test(NAME roblox_api COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/roblox_api.lua)
    add_test(NAME roblox_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/roblox_contract.lua)
    add_test(NAME cli_flags COMMAND ${CMAKE_SOURCE_DIR}/tests/cli_flags.sh $<TARGET_FILE:lumora>)
    add_test(NAME cli_contract COMMAND ${CMAKE_SOURCE_DIR}/tests/cli_contract.sh $<TARGET_FILE:lumora>)
    add_test(NAME luau_modern COMMAND $<TARGET_FILE:lumora> --no-roblox ${CMAKE_SOURCE_DIR}/tests/luau_modern.lua)
    add_test(NAME datatypes_contract COMMAND $<TARGET_FILE:lumora> ${CMAKE_SOURCE_DIR}/tests/datatypes_contract.lua)
endif()
