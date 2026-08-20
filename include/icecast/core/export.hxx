#pragma once

#if defined(_WIN32) && defined(ICECAST_CXX_CORE_SHARED)
    #if defined(ICECAST_CXX_CORE_BUILDING)
        #define ICECAST_CXX_CORE_API __declspec(dllexport)
    #else
        #define ICECAST_CXX_CORE_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) && defined(ICECAST_CXX_CORE_SHARED)
    #define ICECAST_CXX_CORE_API __attribute__((visibility("default")))
#else
    #define ICECAST_CXX_CORE_API
#endif
