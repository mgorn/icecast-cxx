#pragma once

#if defined(_WIN32) && defined(ICECAST_CXX_TRANSPORT_CURL_SHARED)
    #if defined(ICECAST_CXX_TRANSPORT_CURL_BUILDING)
        #define ICECAST_CXX_TRANSPORT_CURL_API __declspec(dllexport)
    #else
        #define ICECAST_CXX_TRANSPORT_CURL_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) && defined(ICECAST_CXX_TRANSPORT_CURL_SHARED)
    #define ICECAST_CXX_TRANSPORT_CURL_API __attribute__((visibility("default")))
#else
    #define ICECAST_CXX_TRANSPORT_CURL_API
#endif
