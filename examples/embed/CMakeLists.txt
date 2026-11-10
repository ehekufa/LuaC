cmake_minimum_required(VERSION 3.16)
project(LuaCEmbedExample LANGUAGES C)

# Install LuaC first, then configure with:
# cmake -S examples/embed -B build-embed -DCMAKE_PREFIX_PATH=/your/prefix
find_package(LuaC 0.3 CONFIG REQUIRED)

add_executable(luac-embed-compiler main.c)
target_link_libraries(luac-embed-compiler PRIVATE LuaC::luac)

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/embedded_program.c")
add_custom_command(
    OUTPUT "${GENERATED}"
    COMMAND LuaC::compiler --emit-c "${CMAKE_CURRENT_SOURCE_DIR}/program.lua" -o "${GENERATED}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/program.lua"
    COMMENT "Compiling Lua syntax to independent native C without a VM"
)
add_executable(luac-embed-native native_main.c "${GENERATED}")
target_include_directories(luac-embed-native PRIVATE "${LuaC_RUNTIME_DIR}")
if(UNIX)
    target_link_libraries(luac-embed-native PRIVATE m)
endif()
