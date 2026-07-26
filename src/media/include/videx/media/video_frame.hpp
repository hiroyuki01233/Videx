#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace videx::media {

struct VideoFrame final {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int64_t timestampMicroseconds = 0;
    std::vector<std::uint8_t> rgba;
};

struct VideoFrameResult final {
    VideoFrame frame;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] VideoFrameResult decodeVideoFrame(const std::filesystem::path& path,
                                                std::int64_t timestampMicroseconds,
                                                std::int32_t maximumWidth = 1280,
                                                std::int32_t maximumHeight = 720);

} // namespace videx::media
