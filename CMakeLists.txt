cmake_minimum_required(VERSION 3.16)
project(LuaC VERSION 0.5.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(LUAC_BUILD_CLI "Build the native JIT/AOT compiler executable" ON)
option(LUAC_BUILD_UI_TESTS "Build the headless Nuklear command adapter test" ON)
option(LUAC_WITH_MBEDTLS "Enable verified HTTPS with mbedTLS" OFF)
option(LUAC_WITH_MINIAUDIO "Enable native audio playback with bundled miniaudio" OFF)

add_library(luac_runtime INTERFACE)
add_library(LuaC::runtime ALIAS luac_runtime)
set_target_properties(luac_runtime PROPERTIES EXPORT_NAME runtime)
target_include_directories(luac_runtime INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set(LUAC_NATIVE_CFLAGS "")
set(LUAC_NATIVE_LDFLAGS "")
if(LUAC_WITH_MBEDTLS)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    find_package(MbedTLS REQUIRED)
    target_compile_definitions(luac_runtime INTERFACE LUAC_WITH_MBEDTLS)
    target_link_libraries(luac_runtime INTERFACE MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
    string(APPEND LUAC_NATIVE_CFLAGS " -DLUAC_WITH_MBEDTLS")
    get_target_property(_mbedtls_includes MbedTLS::mbedtls INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_include IN LISTS _mbedtls_includes)
        if(NOT _include MATCHES "^\\$<")
            string(APPEND LUAC_NATIVE_CFLAGS " -I\"${_include}\"")
        endif()
    endforeach()
    foreach(_target mbedtls mbedx509 mbedcrypto everest p256m)
        if(TARGET MbedTLS::${_target})
            get_target_property(_location MbedTLS::${_target} IMPORTED_LOCATION)
            if(NOT _location)
                get_target_property(_location MbedTLS::${_target} IMPORTED_LOCATION_RELEASE)
            endif()
            if(_location)
                string(APPEND LUAC_NATIVE_LDFLAGS " \"${_location}\"")
            else()
                string(APPEND LUAC_NATIVE_LDFLAGS " -l${_target}")
            endif()
        endif()
    endforeach()
endif()
if(LUAC_WITH_MINIAUDIO)
    find_package(Threads REQUIRED)
    target_compile_definitions(luac_runtime INTERFACE LUAC_WITH_MINIAUDIO)
    target_link_libraries(luac_runtime INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(luac_runtime INTERFACE m ${CMAKE_DL_LIBS})
    endif()
    string(APPEND LUAC_NATIVE_CFLAGS " -DLUAC_WITH_MINIAUDIO")
    string(APPEND LUAC_NATIVE_LDFLAGS " -pthread")
    if(CMAKE_DL_LIBS)
        string(APPEND LUAC_NATIVE_LDFLAGS " -l${CMAKE_DL_LIBS}")
    endif()
endif()
if(WIN32)
    target_link_libraries(luac_runtime INTERFACE ws2_32 crypt32)
    if(LUAC_WITH_MBEDTLS)
        string(APPEND LUAC_NATIVE_LDFLAGS " -lcrypt32")
    endif()
elseif(UNIX)
    target_link_libraries(luac_runtime INTERFACE m ${CMAKE_DL_LIBS})
    if(CMAKE_DL_LIBS)
        string(APPEND LUAC_NATIVE_LDFLAGS " -l${CMAKE_DL_LIBS}")
    endif()
endif()

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

set(LUAC_DEFAULT_CFLAGS_VALUE "${LUAC_NATIVE_CFLAGS}")
set(LUAC_DEFAULT_LDFLAGS_VALUE "${LUAC_NATIVE_LDFLAGS}")
foreach(_flag_var LUAC_DEFAULT_CFLAGS_VALUE LUAC_DEFAULT_LDFLAGS_VALUE)
    string(REPLACE "\\" "\\\\" ${_flag_var} "${${_flag_var}}")
    string(REPLACE "\"" "\\\"" ${_flag_var} "${${_flag_var}}")
endforeach()
configure_file(cmake/luac_default_flags.h.in "${CMAKE_CURRENT_BINARY_DIR}/luac_default_flags.h" @ONLY)

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
    target_compile_definitions(luac_compiler PRIVATE LUAC_HAS_BUILD_CONFIG)
    target_include_directories(luac_compiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
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
    add_executable(luac-embedded-test tests/embedded_test.c src/luac.c)
    target_compile_definitions(luac-embedded-test PRIVATE
        LUAC_LIBRARY_ONLY LUAC_EMBEDDED_RUNTIME LUAC_RUNTIME_PATH="runtime.h"
        LUAC_TEST_PROJECT="${CMAKE_CURRENT_SOURCE_DIR}/tests/embedded_project"
    )
    target_include_directories(luac-embedded-test PRIVATE src)
    target_link_libraries(luac-embedded-test PRIVATE luac_runtime)
    add_test(NAME luac-embedded-project COMMAND luac-embedded-test)
    add_executable(luac-android-samples-test tests/android_samples_test.c src/luac.c)
    target_compile_definitions(luac-android-samples-test PRIVATE
        LUAC_LIBRARY_ONLY LUAC_EMBEDDED_RUNTIME LUAC_RUNTIME_PATH="runtime.h"
        LUAC_SAMPLE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/assets/samples"
    )
    target_include_directories(luac-android-samples-test PRIVATE src)
    target_link_libraries(luac-android-samples-test PRIVATE luac_runtime)
    add_test(NAME luac-android-samples COMMAND luac-android-samples-test)


    if(LUAC_BUILD_CLI)
        add_test(NAME luac-pack-jit COMMAND luac_compiler
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack.lua")
        set_tests_properties(luac-pack-jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            PASS_REGULAR_EXPRESSION "LuaC binary pack: OK"
        )
        add_test(NAME luac-pack-aot
            COMMAND ${CMAKE_COMMAND}
                -DLUAC=$<TARGET_FILE:luac_compiler>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/pack.lua
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/luac-pack-aot
                -DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DEXPECTED=LuaC.binary.pack:.OK
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAotTest.cmake
        )
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
        add_test(NAME luac-runtime-jit COMMAND luac_compiler
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime.lua")
        set_tests_properties(luac-runtime-jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            PASS_REGULAR_EXPRESSION "LuaC runtime roadmap: OK"
        )
        add_test(NAME luac-runtime-aot
            COMMAND ${CMAKE_COMMAND}
                -DLUAC=$<TARGET_FILE:luac_compiler>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime.lua
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/luac-runtime-aot
                -DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DEXPECTED=LuaC.runtime.roadmap:.OK
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAotTest.cmake
        )
        add_library(luac-ffi-fixture SHARED tests/ffi_fixture.c)
        add_test(NAME luac-ffi-jit COMMAND luac_compiler
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ffi.lua")
        set_tests_properties(luac-ffi-jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            ENVIRONMENT "LUAC_FFI_FIXTURE=$<TARGET_FILE:luac-ffi-fixture>"
            PASS_REGULAR_EXPRESSION "LuaC FFI: OK"
        )
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME luac-websocket
                COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/websocket_test.py")
            set_tests_properties(luac-websocket PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                ENVIRONMENT "LUAC=$<TARGET_FILE:luac_compiler>"
                PASS_REGULAR_EXPRESSION "WebSocket independent server: OK"
            )
        endif()
        add_test(NAME luac-game-jit COMMAND luac_compiler
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/game.lua")
        set_tests_properties(luac-game-jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            PASS_REGULAR_EXPRESSION "LuaC game assets physics input: OK"
        )
        add_test(NAME luac-audio-jit COMMAND luac_compiler
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio.lua")
        set_tests_properties(luac-audio-jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            PASS_REGULAR_EXPRESSION "LuaC native audio: OK"
        )
        add_test(NAME luac-audio-aot
            COMMAND ${CMAKE_COMMAND}
                -DLUAC=$<TARGET_FILE:luac_compiler>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/audio.lua
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/luac-audio-aot
                -DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DEXPECTED=LuaC.native.audio:.OK
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAotTest.cmake
        )
        if(LUAC_WITH_MBEDTLS AND UNIX)
            add_test(NAME luac-https-verification
                COMMAND ${CMAKE_COMMAND} -E env
                    PYTHON=${Python3_EXECUTABLE}
                    LUAC=$<TARGET_FILE:luac_compiler>
                    bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_https.sh
            )
            set_tests_properties(luac-https-verification PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                PASS_REGULAR_EXPRESSION "certificate and hostname verification: OK"
            )
        endif()
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

install(TARGETS luac_library luac_engine luac_runtime
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
install(FILES src/runtime.h src/luac_interpreter.h src/luac_pack.h src/luac_libraries.h src/luac_ffi.h src/luac_websocket.h src/luac_game.h src/luac_tls.h src/luac_audio.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES src/runtime.h src/luac_interpreter.h src/luac_pack.h src/luac_libraries.h src/luac_ffi.h src/luac_websocket.h src/luac_game.h src/luac_tls.h src/luac_audio.h
    DESTINATION ${CMAKE_INSTALL_DATADIR}/luac)
install(DIRECTORY src/third_party DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY src/third_party DESTINATION ${CMAKE_INSTALL_DATADIR}/luac)
install(FILES cmake/FindMbedTLS.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LuaC)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LuaCConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LuaCConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LuaC
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/luac.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
