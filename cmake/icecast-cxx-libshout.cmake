include_guard(GLOBAL)

include(ExternalProject)

set(ICECAST_CXX_LIBSHOUT_MINIMUM_VERSION "2.4.4")
set(_ICECAST_CXX_DEPENDENCY_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/../dependencies.json")

function(_icecast_cxx_read_libshout_manifest)
    if(NOT EXISTS "${_ICECAST_CXX_DEPENDENCY_MANIFEST}")
        message(FATAL_ERROR "Missing icecast-cxx dependency manifest: ${_ICECAST_CXX_DEPENDENCY_MANIFEST}")
    endif()

    file(READ "${_ICECAST_CXX_DEPENDENCY_MANIFEST}" _manifest)
    string(JSON _schema ERROR_VARIABLE _schema_error GET "${_manifest}" schema_version)
    if(_schema_error OR NOT _schema EQUAL 1)
        message(FATAL_ERROR "Unsupported or invalid icecast-cxx dependencies.json schema")
    endif()

    string(JSON _count ERROR_VARIABLE _count_error LENGTH "${_manifest}" dependencies)
    if(_count_error OR _count EQUAL 0)
        message(FATAL_ERROR "icecast-cxx dependencies.json does not contain the libshout dependency")
    endif()

    math(EXPR _last "${_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _name GET "${_manifest}" dependencies ${_index} name)
        if(_name STREQUAL "libshout")
            string(JSON _url GET "${_manifest}" dependencies ${_index} url)
            string(JSON _sha256 GET "${_manifest}" dependencies ${_index} sha256)
            string(JSON _directory GET "${_manifest}" dependencies ${_index} directory)
            set(ICECAST_CXX_LIBSHOUT_URL "${_url}" PARENT_SCOPE)
            set(ICECAST_CXX_LIBSHOUT_SHA256 "${_sha256}" PARENT_SCOPE)
            set(ICECAST_CXX_LIBSHOUT_DIRECTORY "${_directory}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    message(FATAL_ERROR "icecast-cxx dependencies.json does not contain the libshout dependency")
endfunction()

function(_icecast_cxx_add_libshout_external source_dir url sha256)
    if(WIN32)
        message(FATAL_ERROR
            "Automatic libshout source builds currently require an autotools-capable Unix-like environment. "
            "On Windows, provide a compatible Shout::shout target or installed pkg-config package."
        )
    endif()

    find_program(ICECAST_CXX_MAKE_PROGRAM NAMES gmake make REQUIRED)

    set(_prefix "${CMAKE_BINARY_DIR}/_icecast-cxx/libshout")
    set(_include_dir "${_prefix}/include")
    set(_library_dir "${_prefix}/lib")
    set(_library "${_library_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}shout${CMAKE_SHARED_LIBRARY_SUFFIX}")
    file(MAKE_DIRECTORY "${_include_dir}")
    file(MAKE_DIRECTORY "${_library_dir}")

    set(_external_arguments)
    if(source_dir)
        if(NOT EXISTS "${source_dir}/configure")
            message(FATAL_ERROR "ICECAST_CXX_LIBSHOUT_SOURCE_DIR must contain a prepared libshout release source tree with configure: ${source_dir}")
        endif()
        list(APPEND _external_arguments SOURCE_DIR "${source_dir}" DOWNLOAD_COMMAND "" UPDATE_COMMAND "")
    else()
        list(APPEND _external_arguments URL "${url}" URL_HASH "SHA256=${sha256}" UPDATE_COMMAND "")
    endif()

    ExternalProject_Add(
        icecast_cxx_libshout_external
        ${_external_arguments}
        PREFIX "${CMAKE_BINARY_DIR}/_icecast-cxx/libshout-external"
        INSTALL_DIR "${_prefix}"
        CONFIGURE_COMMAND
            <SOURCE_DIR>/configure
            --prefix=<INSTALL_DIR>
            --libdir=<INSTALL_DIR>/lib
            --includedir=<INSTALL_DIR>/include
            --enable-shared
            --disable-static
            --disable-examples
            --disable-tools
        BUILD_COMMAND "${ICECAST_CXX_MAKE_PROGRAM}"
        INSTALL_COMMAND "${ICECAST_CXX_MAKE_PROGRAM}" install
        BUILD_BYPRODUCTS "${_library}"
    )

    add_library(Shout::shout SHARED IMPORTED GLOBAL)
    set_target_properties(
        Shout::shout
        PROPERTIES
            IMPORTED_LOCATION "${_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_include_dir}"
    )
    add_dependencies(Shout::shout icecast_cxx_libshout_external)
endfunction()

function(icecast_cxx_resolve_libshout)
    if(TARGET Shout::shout)
        return()
    endif()

    _icecast_cxx_read_libshout_manifest()

    set(_source_dir "")
    if(ICECAST_CXX_LIBSHOUT_SOURCE_DIR)
        set(_source_dir "${ICECAST_CXX_LIBSHOUT_SOURCE_DIR}")
    elseif(EXISTS "${ICECAST_CXX_DEPENDENCIES_DIR}/${ICECAST_CXX_LIBSHOUT_DIRECTORY}/configure")
        set(_source_dir "${ICECAST_CXX_DEPENDENCIES_DIR}/${ICECAST_CXX_LIBSHOUT_DIRECTORY}")
    endif()

    if(_source_dir)
        _icecast_cxx_add_libshout_external("${_source_dir}" "${ICECAST_CXX_LIBSHOUT_URL}" "${ICECAST_CXX_LIBSHOUT_SHA256}")
        return()
    endif()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ICECAST_CXX_SYSTEM_SHOUT QUIET IMPORTED_TARGET GLOBAL "shout>=${ICECAST_CXX_LIBSHOUT_MINIMUM_VERSION}")
        if(TARGET PkgConfig::ICECAST_CXX_SYSTEM_SHOUT)
            add_library(Shout::shout INTERFACE IMPORTED GLOBAL)
            set_property(TARGET Shout::shout PROPERTY INTERFACE_LINK_LIBRARIES PkgConfig::ICECAST_CXX_SYSTEM_SHOUT)
            return()
        endif()
    endif()

    if((NOT ICECAST_CXX_FETCH_DEPENDENCIES) OR (NOT ICECAST_CXX_FETCH_LIBSHOUT))
        message(FATAL_ERROR
            "icecast::publish_libshout requires libshout >= ${ICECAST_CXX_LIBSHOUT_MINIMUM_VERSION}. "
            "Provide Shout::shout, set ICECAST_CXX_LIBSHOUT_SOURCE_DIR, place libshout under "
            "ICECAST_CXX_DEPENDENCIES_DIR, install the shout pkg-config package, or enable automatic dependency fetching."
        )
    endif()

    _icecast_cxx_add_libshout_external("" "${ICECAST_CXX_LIBSHOUT_URL}" "${ICECAST_CXX_LIBSHOUT_SHA256}")
endfunction()
