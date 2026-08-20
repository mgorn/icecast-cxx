set(_icecast_cxx_top_level_default OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(_icecast_cxx_top_level_default ON)
endif()

set(_icecast_cxx_native_default OFF)
if(PROJECT_IS_TOP_LEVEL AND NOT EMSCRIPTEN)
    set(_icecast_cxx_native_default ON)
endif()

set(_icecast_cxx_web_default OFF)
if(PROJECT_IS_TOP_LEVEL AND EMSCRIPTEN)
    set(_icecast_cxx_web_default ON)
endif()

option(ICECAST_CXX_BUILD_TESTS "Build icecast-cxx tests" ${_icecast_cxx_top_level_default})
option(ICECAST_CXX_INSTALL "Generate icecast-cxx install and package-export rules" ${_icecast_cxx_top_level_default})
option(ICECAST_CXX_FETCH_DEPENDENCIES "Allow icecast-cxx to fetch missing dependencies" ON)

option(ICECAST_CXX_ENABLE_STREAM "Enable the icecast::stream component" ON)
option(ICECAST_CXX_ENABLE_ADMIN "Enable the icecast::admin component" ON)
option(ICECAST_CXX_ENABLE_TRANSPORT_CURL "Enable the native libcurl transport component" ${_icecast_cxx_native_default})
option(ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT "Enable the native libshout publishing component" ${_icecast_cxx_native_default})
option(ICECAST_CXX_ENABLE_TRANSPORT_WEB "Enable the Emscripten/browser transport component" ${_icecast_cxx_web_default})

set(
    ICECAST_CXX_DEPENDENCIES_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencies"
    CACHE PATH
    "Repository-local dependency source directory used before FetchContent"
)

unset(_icecast_cxx_top_level_default)
unset(_icecast_cxx_native_default)
unset(_icecast_cxx_web_default)
