#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace videx::media {

struct AudioBuffer final {
    std::int32_t sampleRate = 48'000;
    std::int32_t channelCount = 2;
    std::int64_t timestampMicroseconds = 0;
    std::vector<float> interleavedSamples;
};

struct AudioBufferResult final {
    AudioBuffer buffer;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] AudioBufferResult decodeAudio(const std::filesystem::path& path,
                                            std::int64_t timestampMicroseconds,
                                            std::int64_t durationMicroseconds);

} // namespace videx::media
