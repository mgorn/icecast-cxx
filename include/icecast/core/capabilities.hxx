#pragma once

#include <string>
#include <vector>

#include <icecast/core/export.hxx>

namespace icecast {

enum struct capability_support {
    unsupported,
    conditional,
    supported,
};

struct capability {
    capability_support support = capability_support::unsupported;
    std::string detail;
    std::vector<std::string> requirements;

    [[nodiscard]] bool is_supported() const noexcept {
        return support == capability_support::supported;
    }
};

struct platform_capabilities {
    capability stream_listening;
    capability stream_publishing;
    capability admin_operations;
};

struct server_capabilities {
    capability stream_listening;
    capability stream_publishing;
    capability admin_operations;
};

struct effective_capabilities {
    capability stream_listening;
    capability stream_publishing;
    capability admin_operations;
};

[[nodiscard]] ICECAST_CXX_CORE_API capability combine_capability(const capability& platform, const capability& server);
[[nodiscard]] ICECAST_CXX_CORE_API effective_capabilities combine_capabilities(const platform_capabilities& platform, const server_capabilities& server);

} // namespace icecast
