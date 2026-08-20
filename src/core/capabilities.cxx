#include <icecast/core/capabilities.hxx>

#include <algorithm>

namespace icecast {
namespace {

[[nodiscard]] capability_support combine_support(capability_support platform, capability_support server) noexcept {
    if ((platform == capability_support::unsupported) or (server == capability_support::unsupported)) {
        return capability_support::unsupported;
    }

    if ((platform == capability_support::conditional) or (server == capability_support::conditional)) {
        return capability_support::conditional;
    }

    return capability_support::supported;
}

void append_unique(std::vector<std::string>& destination, const std::vector<std::string>& source) {
    for (const auto& requirement : source) {
        if (std::find(destination.begin(), destination.end(), requirement) == destination.end()) {
            destination.push_back(requirement);
        }
    }
}

[[nodiscard]] std::string combine_detail(const std::string& platform, const std::string& server) {
    if (platform.empty()) {
        return server;
    }

    if (server.empty() or (platform == server)) {
        return platform;
    }

    return platform + "; " + server;
}

} // namespace

capability combine_capability(const capability& platform, const capability& server) {
    capability combined{
        .support = combine_support(platform.support, server.support),
        .detail = combine_detail(platform.detail, server.detail),
        .requirements = platform.requirements,
    };
    append_unique(combined.requirements, server.requirements);
    return combined;
}

effective_capabilities combine_capabilities(const platform_capabilities& platform, const server_capabilities& server) {
    return {
        .stream_listening = combine_capability(platform.stream_listening, server.stream_listening),
        .stream_publishing = combine_capability(platform.stream_publishing, server.stream_publishing),
        .admin_operations = combine_capability(platform.admin_operations, server.admin_operations),
    };
}

} // namespace icecast
