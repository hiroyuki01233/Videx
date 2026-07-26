#include <videx/core/application_info.hpp>

#include <iostream>
#include <string_view>

int runTimelineTests();

int main() {
    using videx::core::ApplicationInfo;

    if (ApplicationInfo::name() != std::string_view{"Videx"}) {
        std::cerr << "unexpected application name\n";
        return 1;
    }

    if (ApplicationInfo::version().empty()) {
        std::cerr << "application version must not be empty\n";
        return 1;
    }

    return runTimelineTests() == 0 ? 0 : 1;
}
