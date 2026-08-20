#pragma once

#if defined(_WIN32) && defined(ICECAST_CXX_STREAM_SHARED)
    #if defined(ICECAST_CXX_STREAM_BUILDING)
        #define ICECAST_CXX_STREAM_API __declspec(dllexport)
    #else
        #define ICECAST_CXX_STREAM_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) && defined(ICECAST_CXX_STREAM_SHARED)
    #define ICECAST_CXX_STREAM_API __attribute__((visibility("default")))
#else
    #define ICECAST_CXX_STREAM_API
#endif
