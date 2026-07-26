#pragma once

#include <string_view>

namespace videx::core {

class ApplicationInfo final {
  public:
    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "Videx";
    }
    [[nodiscard]] static std::string_view version() noexcept;
};

} // namespace videx::core
