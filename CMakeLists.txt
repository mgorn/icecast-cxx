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

if(ICECAST_CXX_ENABLE_TRANSPORT_CURL AND NOT ICECAST_CXX_ENABLE_STREAM)
    message(FATAL_ERROR "ICECAST_CXX_ENABLE_TRANSPORT_CURL requires ICECAST_CXX_ENABLE_STREAM")
endif()

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
set(_icecast_cxx_compiled_export_targets)
set(_icecast_cxx_interface_export_targets)

add_library(
    icecast_core
    src/core/capabilities.cxx
    src/core/credentials.cxx
    src/core/endpoint.cxx
    src/core/error.cxx
    src/core/headers.cxx
)
add_library(icecast::core ALIAS icecast_core)
set_target_properties(icecast_core PROPERTIES EXPORT_NAME core)
target_compile_features(icecast_core PUBLIC cxx_std_20)
target_include_directories(
    icecast_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_sources(
    icecast_core
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/icecast/core.hxx
            include/icecast/core/capabilities.hxx
            include/icecast/core/credentials.hxx
            include/icecast/core/endpoint.hxx
            include/icecast/core/error.hxx
            include/icecast/core/export.hxx
            include/icecast/core/headers.hxx
            include/icecast/core/result.hxx
)
get_target_property(_icecast_cxx_core_type icecast_core TYPE)
if(_icecast_cxx_core_type STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(icecast_core PRIVATE ICECAST_CXX_CORE_BUILDING PUBLIC ICECAST_CXX_CORE_SHARED)
endif()
unset(_icecast_cxx_core_type)
list(APPEND _icecast_cxx_export_targets icecast_core)
list(APPEND _icecast_cxx_compiled_export_targets icecast_core)

if(ICECAST_CXX_ENABLE_STREAM)
    add_library(
        icecast_stream
        src/stream/icy.cxx
        src/stream/models.cxx
        src/stream/reconnect.cxx
        src/stream/state.cxx
    )
    add_library(icecast::stream ALIAS icecast_stream)
    set_target_properties(icecast_stream PROPERTIES EXPORT_NAME stream)
    target_compile_features(icecast_stream PUBLIC cxx_std_20)
    target_include_directories(
        icecast_stream
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(icecast_stream PUBLIC icecast::core)
    target_sources(
        icecast_stream
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/icecast/stream.hxx
                include/icecast/stream/export.hxx
                include/icecast/stream/icy.hxx
                include/icecast/stream/models.hxx
                include/icecast/stream/reconnect.hxx
                include/icecast/stream/state.hxx
    )
    get_target_property(_icecast_cxx_stream_type icecast_stream TYPE)
    if(_icecast_cxx_stream_type STREQUAL "SHARED_LIBRARY")
        target_compile_definitions(icecast_stream PRIVATE ICECAST_CXX_STREAM_BUILDING PUBLIC ICECAST_CXX_STREAM_SHARED)
    endif()
    unset(_icecast_cxx_stream_type)
    list(APPEND _icecast_cxx_export_targets icecast_stream)
    list(APPEND _icecast_cxx_compiled_export_targets icecast_stream)
endif()

macro(icecast_cxx_add_interface_component component)
    set(_target "icecast_${component}")
    add_library(${_target} INTERFACE)
    add_library(icecast::${component} ALIAS ${_target})
    set_target_properties(${_target} PROPERTIES EXPORT_NAME ${component})
    target_compile_features(${_target} INTERFACE cxx_std_20)
    list(APPEND _icecast_cxx_export_targets ${_target})
    list(APPEND _icecast_cxx_interface_export_targets ${_target})
    unset(_target)
endmacro()

if(ICECAST_CXX_ENABLE_ADMIN)
    icecast_cxx_add_interface_component(admin)
    target_link_libraries(icecast_admin INTERFACE icecast::core)
endif()

if(ICECAST_CXX_ENABLE_TRANSPORT_CURL)
    include(cmake/icecast-cxx-curl.cmake)
    icecast_cxx_resolve_curl()

    add_library(
        icecast_transport_curl
        src/transport_curl/listener.cxx
    )
    add_library(icecast::transport_curl ALIAS icecast_transport_curl)
    set_target_properties(icecast_transport_curl PROPERTIES EXPORT_NAME transport_curl)
    target_compile_features(icecast_transport_curl PUBLIC cxx_std_20)
    target_include_directories(
        icecast_transport_curl
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(
        icecast_transport_curl
        PUBLIC
            icecast::stream
        PRIVATE
            $<BUILD_INTERFACE:CURL::libcurl>
            $<INSTALL_INTERFACE:CURL::libcurl>
    )
    target_sources(
        icecast_transport_curl
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/icecast/transport_curl.hxx
                include/icecast/transport_curl/export.hxx
                include/icecast/transport_curl/listener.hxx
    )
    get_target_property(_icecast_cxx_transport_curl_type icecast_transport_curl TYPE)
    if(_icecast_cxx_transport_curl_type STREQUAL "SHARED_LIBRARY")
        target_compile_definitions(icecast_transport_curl PRIVATE ICECAST_CXX_TRANSPORT_CURL_BUILDING PUBLIC ICECAST_CXX_TRANSPORT_CURL_SHARED)
    endif()
    unset(_icecast_cxx_transport_curl_type)
    list(APPEND _icecast_cxx_export_targets icecast_transport_curl)
    list(APPEND _icecast_cxx_compiled_export_targets icecast_transport_curl)
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

set(ICECAST_CXX_CONFIG_HAS_TRANSPORT_CURL OFF)
if(ICECAST_CXX_ENABLE_TRANSPORT_CURL)
    set(ICECAST_CXX_CONFIG_HAS_TRANSPORT_CURL ON)
endif()

if(ICECAST_CXX_INSTALL)
    set(_icecast_cxx_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/icecast-cxx")

    install(
        TARGETS ${_icecast_cxx_compiled_export_targets}
        EXPORT icecast-cxxTargets
        FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )

    if(_icecast_cxx_interface_export_targets)
        install(
            TARGETS ${_icecast_cxx_interface_export_targets}
            EXPORT icecast-cxxTargets
        )
    endif()

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

unset(_icecast_cxx_interface_export_targets)
unset(_icecast_cxx_compiled_export_targets)
unset(_icecast_cxx_export_targets)
