#include <videx/core/application_info.hpp>

namespace videx::core {

std::string_view ApplicationInfo::version() noexcept {
    return "0.1.0-dev";
}

} // namespace videx::core
