cmake_minimum_required(VERSION 3.24)

project(
    icecast-cxx
    VERSION 0.0.0
    DESCRIPTION "Modern modular C++ interfaces for Icecast"
    HOMEPAGE_URL "https://github.com/mgorn/icecast-cxx"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/icecast-cxx-options.cmake)

if(ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT AND NOT ICECAST_CXX_ENABLE_STREAM)
    message(FATAL_ERROR "ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT requires ICECAST_CXX_ENABLE_STREAM")
endif()

if(ICECAST_CXX_ENABLE_TRANSPORT_WEB AND NOT ICECAST_CXX_ENABLE_STREAM)
    message(FATAL_ERROR "ICECAST_CXX_ENABLE_TRANSPORT_WEB requires ICECAST_CXX_ENABLE_STREAM")
endif()

if(EMSCRIPTEN AND (ICECAST_CXX_ENABLE_TRANSPORT_CURL OR ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT))
    message(FATAL_ERROR "Native curl/libshout backends are not supported in Emscripten builds")
endif()

if(ICECAST_CXX_ENABLE_TRANSPORT_WEB AND NOT EMSCRIPTEN)
    message(FATAL_ERROR "ICECAST_CXX_ENABLE_TRANSPORT_WEB requires an Emscripten build")
endif()

set(_icecast_cxx_export_targets)

macro(icecast_cxx_add_interface_component component)
    set(_target "icecast_${component}")
    add_library(${_target} INTERFACE)
    add_library(icecast::${component} ALIAS ${_target})
    set_target_properties(${_target} PROPERTIES EXPORT_NAME ${component})
    target_compile_features(${_target} INTERFACE cxx_std_20)
    list(APPEND _icecast_cxx_export_targets ${_target})
    unset(_target)
endmacro()

# These targets intentionally contain no protocol implementation yet. They establish
# the consumer-facing CMake surface while the project is still being scaffolded.
icecast_cxx_add_interface_component(core)

if(ICECAST_CXX_ENABLE_STREAM)
    icecast_cxx_add_interface_component(stream)
    target_link_libraries(icecast_stream INTERFACE icecast::core)
endif()

if(ICECAST_CXX_ENABLE_ADMIN)
    icecast_cxx_add_interface_component(admin)
    target_link_libraries(icecast_admin INTERFACE icecast::core)
endif()

if(ICECAST_CXX_ENABLE_TRANSPORT_CURL)
    icecast_cxx_add_interface_component(transport_curl)
    target_link_libraries(icecast_transport_curl INTERFACE icecast::core)
endif()

if(ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT)
    icecast_cxx_add_interface_component(publish_libshout)
    target_link_libraries(icecast_publish_libshout INTERFACE icecast::stream)
endif()

if(ICECAST_CXX_ENABLE_TRANSPORT_WEB)
    icecast_cxx_add_interface_component(transport_web)
    target_link_libraries(icecast_transport_web INTERFACE icecast::stream)
endif()

if(ICECAST_CXX_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()

if(ICECAST_CXX_INSTALL)
    set(_icecast_cxx_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/icecast-cxx")

    install(
        TARGETS ${_icecast_cxx_export_targets}
        EXPORT icecast-cxxTargets
    )

    install(
        EXPORT icecast-cxxTargets
        FILE icecast-cxxTargets.cmake
        NAMESPACE icecast::
        DESTINATION "${_icecast_cxx_package_dir}"
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/icecast-cxx-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/icecast-cxxConfig.cmake"
        INSTALL_DESTINATION "${_icecast_cxx_package_dir}"
        NO_SET_AND_CHECK_MACRO
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/icecast-cxxConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMinorVersion
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/icecast-cxxConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/icecast-cxxConfigVersion.cmake"
        DESTINATION "${_icecast_cxx_package_dir}"
    )

    unset(_icecast_cxx_package_dir)
endif()

unset(_icecast_cxx_export_targets)
