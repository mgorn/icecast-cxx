#pragma once

#include <string>

#include <icecast/core/export.hxx>
#include <icecast/core/result.hxx>

namespace icecast {

struct basic_credentials {
    std::string username;
    std::string password;
};

[[nodiscard]] ICECAST_CXX_CORE_API result<void> validate_basic_credentials(const basic_credentials& credentials);
[[nodiscard]] ICECAST_CXX_CORE_API std::string format_redacted_credentials(const basic_credentials& credentials);

} // namespace icecast
