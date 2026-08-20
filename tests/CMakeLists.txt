if(NOT TARGET icecast::core)
    message(FATAL_ERROR "icecast::core must exist when tests are enabled")
endif()

add_executable(icecast_core_tests core.cxx)
target_link_libraries(icecast_core_tests PRIVATE icecast::core)
add_test(NAME icecast_cxx.core COMMAND icecast_core_tests)
