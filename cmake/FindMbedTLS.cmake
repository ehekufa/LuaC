# Locate mbedTLS 2.28+/3.x/4.x on Unix packages, vcpkg, MSYS2 and custom prefixes.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_MBEDTLS QUIET mbedtls)
    pkg_check_modules(PC_MBEDX509 QUIET mbedx509)
    pkg_check_modules(PC_MBEDCRYPTO QUIET mbedcrypto)
endif()
find_path(MbedTLS_INCLUDE_DIR NAMES mbedtls/ssl.h
    HINTS ${PC_MBEDTLS_INCLUDE_DIRS})
find_library(MbedTLS_LIBRARY NAMES mbedtls libmbedtls
    HINTS ${PC_MBEDTLS_LIBRARY_DIRS})
find_library(MbedX509_LIBRARY NAMES mbedx509 libmbedx509
    HINTS ${PC_MBEDX509_LIBRARY_DIRS})
find_library(MbedCrypto_LIBRARY NAMES mbedcrypto libmbedcrypto
    HINTS ${PC_MBEDCRYPTO_LIBRARY_DIRS})
find_library(MbedEverest_LIBRARY NAMES everest libeverest
    HINTS ${PC_MBEDCRYPTO_LIBRARY_DIRS})
find_library(MbedP256M_LIBRARY NAMES p256m libp256m
    HINTS ${PC_MBEDCRYPTO_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS REQUIRED_VARS
    MbedTLS_INCLUDE_DIR MbedTLS_LIBRARY MbedX509_LIBRARY MbedCrypto_LIBRARY
    VERSION_VAR PC_MBEDTLS_VERSION)

if(MbedTLS_FOUND)
    foreach(_target mbedtls mbedx509 mbedcrypto everest p256m)
        if(_target STREQUAL "mbedtls")
            set(_library "${MbedTLS_LIBRARY}")
        elseif(_target STREQUAL "mbedx509")
            set(_library "${MbedX509_LIBRARY}")
        elseif(_target STREQUAL "mbedcrypto")
            set(_library "${MbedCrypto_LIBRARY}")
        elseif(_target STREQUAL "everest")
            set(_library "${MbedEverest_LIBRARY}")
        else()
            set(_library "${MbedP256M_LIBRARY}")
        endif()
        if(_library AND NOT TARGET MbedTLS::${_target})
            string(TOUPPER "${_target}" _upper)
            add_library(MbedTLS::${_target} UNKNOWN IMPORTED)
            set_target_properties(MbedTLS::${_target} PROPERTIES
                IMPORTED_LOCATION "${_library}"
                INTERFACE_INCLUDE_DIRECTORIES "${MbedTLS_INCLUDE_DIR}"
            )
        endif()
    endforeach()
endif()
mark_as_advanced(MbedTLS_INCLUDE_DIR MbedTLS_LIBRARY MbedX509_LIBRARY
    MbedCrypto_LIBRARY MbedEverest_LIBRARY MbedP256M_LIBRARY)
