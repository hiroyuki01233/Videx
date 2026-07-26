#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace videx::media {

enum class StreamKind {
    Unknown,
    Video,
    Audio,
    Subtitle,
    Data,
    Attachment,
};

struct Rational final {
    std::int32_t numerator = 0;
    std::int32_t denominator = 1;
};

struct StreamInfo final {
    std::int32_t index = -1;
    StreamKind kind = StreamKind::Unknown;
    std::string codecName;
    Rational timeBase;
    std::int64_t durationTicks = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t sampleRate = 0;
    std::int32_t channelCount = 0;
};

struct MediaInfo final {
    std::string formatName;
    std::int64_t durationMicroseconds = 0;
    std::vector<StreamInfo> streams;
};

struct ProbeResult final {
    MediaInfo media;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] ProbeResult probeMedia(const std::filesystem::path& path);
[[nodiscard]] const char* streamKindName(StreamKind kind) noexcept;

} // namespace videx::media
