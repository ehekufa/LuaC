cmake_minimum_required(VERSION 3.22.1)
project(LuaCStudio LANGUAGES C CXX)
set(LUAC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

add_library(luac_compiler STATIC "${LUAC_ROOT}/src/luac.c")
target_include_directories(luac_compiler PRIVATE "${LUAC_ROOT}/src")
target_compile_definitions(luac_compiler PRIVATE LUAC_LIBRARY_ONLY LUAC_EMBEDDED_RUNTIME LUAC_RUNTIME_PATH="runtime.h")
set_target_properties(luac_compiler PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)

add_library(luac_native SHARED native.cpp vulkan_android.cpp)
target_include_directories(luac_native PRIVATE "${LUAC_ROOT}/src")
target_compile_features(luac_native PRIVATE cxx_std_17)
target_link_libraries(luac_native PRIVATE luac_compiler android log vulkan m dl)
