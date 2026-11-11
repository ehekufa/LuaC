cmake_minimum_required(VERSION 3.16)
project(LuaC VERSION 0.4.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(LUAC_BUILD_CLI "Build the native JIT/AOT compiler executable" ON)
option(LUAC_BUILD_UI_TESTS "Build the headless Nuklear command adapter test" ON)

add_library(luac_library src/luac.c)
add_library(LuaC::luac ALIAS luac_library)
add_library(luac_engine INTERFACE)
add_library(LuaC::engine ALIAS luac_engine)
set_target_properties(luac_engine PROPERTIES EXPORT_NAME engine)
target_include_directories(luac_engine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(luac_engine INTERFACE cxx_std_17)

set_target_properties(luac_library PROPERTIES
    OUTPUT_NAME luac
    EXPORT_NAME luac
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_compile_definitions(luac_library PRIVATE
    LUAC_LIBRARY_ONLY
    LUAC_RUNTIME_PATH="runtime.h"
)
if(BUILD_SHARED_LIBS AND WIN32)
    target_compile_definitions(luac_library PRIVATE LUAC_BUILD_SHARED)
endif()
target_include_directories(luac_library PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(CMAKE_DL_LIBS)
    target_link_libraries(luac_library PRIVATE ${CMAKE_DL_LIBS})
endif()

if(LUAC_BUILD_CLI)
    add_executable(luac_compiler src/luac.c)
    add_executable(LuaC::compiler ALIAS luac_compiler)
    set_target_properties(luac_compiler PROPERTIES
        OUTPUT_NAME luac
        EXPORT_NAME compiler
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS YES
    )
    target_compile_definitions(luac_compiler PRIVATE
        LUAC_RUNTIME_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/runtime.h"
    )
    target_include_directories(luac_compiler PRIVATE src)
    if(CMAKE_DL_LIBS)
        target_link_libraries(luac_compiler PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()

if(BUILD_TESTING)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/.tmp")
    add_executable(luac-api-test tests/api_test.c)
    target_link_libraries(luac-api-test PRIVATE luac_library)
    add_test(NAME luac-c-api COMMAND luac-api-test)
    add_executable(luac-engine-test tests/engine_test.cpp)
    target_link_libraries(luac-engine-test PRIVATE luac_engine)
    add_test(NAME luac-native-engine COMMAND luac-engine-test)

    if(LUAC_BUILD_CLI)
        add_test(NAME luac-compat-jit COMMAND luac_compiler
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compat.lua")
        set_tests_properties(luac-compat-jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            PASS_REGULAR_EXPRESSION "LuaC compatibility: OK"
        )
        add_test(NAME luac-compat-aot
            COMMAND ${CMAKE_COMMAND}
                -DLUAC=$<TARGET_FILE:luac_compiler>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat.lua
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/luac-compat-aot
                -DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAotTest.cmake
        )
        add_test(NAME luac-libraries-jit COMMAND luac_compiler
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries.lua")
        set_tests_properties(luac-libraries-jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            PASS_REGULAR_EXPRESSION "LuaC extended libraries: OK"
        )
        add_test(NAME luac-libraries-aot
            COMMAND ${CMAKE_COMMAND}
                -DLUAC=$<TARGET_FILE:luac_compiler>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries.lua
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/luac-libraries-aot
                -DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DEXPECTED=LuaC.extended.libraries:.OK
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAotTest.cmake
        )
    endif()

    if(LUAC_BUILD_UI_TESTS)
        add_executable(luac-ui-test gui/nuklear_impl.c tests/ui_test.cpp)
        target_include_directories(luac-ui-test PRIVATE gui)
        target_compile_features(luac-ui-test PRIVATE cxx_std_17)
        add_test(NAME luac-nuklear-ui COMMAND luac-ui-test)
    endif()
endif()

configure_file(cmake/luac.pc.in "${CMAKE_CURRENT_BINARY_DIR}/luac.pc" @ONLY)
configure_package_config_file(cmake/LuaCConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/LuaCConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LuaC"
    PATH_VARS CMAKE_INSTALL_DATADIR
)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/LuaCConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS luac_library luac_engine
    EXPORT LuaCTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(LUAC_BUILD_CLI)
    install(TARGETS luac_compiler EXPORT LuaCTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(EXPORT LuaCTargets
    FILE LuaCTargets.cmake
    NAMESPACE LuaC::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LuaC
)
install(FILES src/luac.h src/engine.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES src/runtime.h src/luac_libraries.h DESTINATION ${CMAKE_INSTALL_DATADIR}/luac)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LuaCConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LuaCConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LuaC
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/luac.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
