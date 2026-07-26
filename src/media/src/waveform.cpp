#include <videx/media/audio_buffer.hpp>
#include <videx/media/waveform.hpp>

#include <algorithm>
#include <cmath>

namespace videx::media {

WaveformResult generateWaveform(const std::filesystem::path& path,
                                const std::int64_t durationMicroseconds,
                                const std::int32_t bucketCount) {
    if (durationMicroseconds <= 0 || bucketCount <= 0 || bucketCount > 100'000) {
        return {.error = "waveform request is invalid"};
    }
    std::vector<float> peaks(static_cast<std::size_t>(bucketCount), 0.0F);
    constexpr std::int64_t chunkDuration = 20'000'000;
    for (std::int64_t chunkStart = 0; chunkStart < durationMicroseconds;
         chunkStart += chunkDuration) {
        const std::int64_t duration =
            std::min(chunkDuration, durationMicroseconds - chunkStart);
        const AudioBufferResult decoded = decodeAudio(path, chunkStart, duration);
        if (!decoded.succeeded()) {
            return {.error = decoded.error};
        }
        const std::int64_t frameCount = static_cast<std::int64_t>(
            decoded.buffer.interleavedSamples.size() / decoded.buffer.channelCount);
        for (std::int64_t frame = 0; frame < frameCount; ++frame) {
            const std::int64_t absoluteMicroseconds =
                chunkStart + frame * 1'000'000 / decoded.buffer.sampleRate;
            const std::size_t bucket = static_cast<std::size_t>(std::clamp<std::int64_t>(
                absoluteMicroseconds * bucketCount / durationMicroseconds, 0,
                bucketCount - 1));
            float magnitude = 0.0F;
            for (std::int32_t channel = 0; channel < decoded.buffer.channelCount; ++channel) {
                magnitude = std::max(
                    magnitude,
                    std::abs(decoded.buffer.interleavedSamples[
                        static_cast<std::size_t>(frame * decoded.buffer.channelCount + channel)]));
            }
            peaks[bucket] = std::max(peaks[bucket], std::min(magnitude, 1.0F));
        }
    }
    return {.peaks = std::move(peaks)};
}

} // namespace videx::media
