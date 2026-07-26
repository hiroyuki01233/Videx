#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace videx::media {

struct WaveformResult final {
    std::vector<float> peaks;
    std::string error;
    [[nodiscard]] bool succeeded() const noexcept { return error.empty(); }
};

[[nodiscard]] WaveformResult generateWaveform(const std::filesystem::path& path,
                                              std::int64_t durationMicroseconds,
                                              std::int32_t bucketCount);

} // namespace videx::media
