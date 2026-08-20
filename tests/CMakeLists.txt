if(NOT TARGET icecast::core)
    message(FATAL_ERROR "icecast::core must exist when tests are enabled")
endif()

add_test(
    NAME icecast_cxx.scaffold
    COMMAND "${CMAKE_COMMAND}" -E echo "icecast-cxx scaffold configured successfully"
)
