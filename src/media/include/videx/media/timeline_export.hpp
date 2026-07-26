#pragma once

#include <videx/media/video_frame.hpp>
#include <videx/media/audio_buffer.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace videx::media {

enum class ExportClipKind { Video, Audio };
enum class ExportEffectType { Brightness, Contrast, Saturation, Blur, Vignette };
enum class ExportInterpolation { Linear, Hold, Ease, EaseIn, EaseOut, EaseInOut };
enum class ExportMaskShape { None, Rectangle, Ellipse };

// Maps a linear 0..1 ratio to interpolated progress. Must stay byte-identical
// with videx::core::interpolationProgress so preview and export match.
[[nodiscard]] constexpr double exportInterpolationProgress(
    const ExportInterpolation interpolation, const double ratio) noexcept {
    switch (interpolation) {
    case ExportInterpolation::Hold:
        return 0.0;
    case ExportInterpolation::Ease:
        return ratio * ratio * (3.0 - 2.0 * ratio);
    case ExportInterpolation::EaseIn:
        return ratio * ratio * ratio;
    case ExportInterpolation::EaseOut: {
        const double inverse = 1.0 - ratio;
        return 1.0 - inverse * inverse * inverse;
    }
    case ExportInterpolation::EaseInOut: {
        if (ratio < 0.5) {
            return 4.0 * ratio * ratio * ratio;
        }
        const double inverse = 1.0 - ratio;
        return 1.0 - 4.0 * inverse * inverse * inverse;
    }
    case ExportInterpolation::Linear:
    default:
        return ratio;
    }
}

// Antiderivative of exportInterpolationProgress over [0, ratio]; used to
// integrate speed ramps analytically. Hold integrates the constant 0 curve
// contribution (the caller adds the previous keyframe's constant rate).
[[nodiscard]] constexpr double exportInterpolationIntegral(
    const ExportInterpolation interpolation, const double ratio) noexcept {
    switch (interpolation) {
    case ExportInterpolation::Hold:
        return 0.0;
    case ExportInterpolation::Ease:
        return ratio * ratio * ratio - 0.5 * ratio * ratio * ratio * ratio;
    case ExportInterpolation::EaseIn:
        return 0.25 * ratio * ratio * ratio * ratio;
    case ExportInterpolation::EaseOut: {
        const double inverse = 1.0 - ratio;
        return ratio + (inverse * inverse * inverse * inverse - 1.0) * 0.25;
    }
    case ExportInterpolation::EaseInOut: {
        if (ratio < 0.5) {
            return ratio * ratio * ratio * ratio;
        }
        const double inverse = 1.0 - ratio;
        return ratio - 0.5 + inverse * inverse * inverse * inverse;
    }
    case ExportInterpolation::Linear:
    default:
        return 0.5 * ratio * ratio;
    }
}

struct ExportEffectKeyframe final {
    std::int64_t frameOffset = 0;
    double value = 0.0;
    ExportInterpolation interpolation = ExportInterpolation::Linear;
};

struct ExportEffect final {
    ExportEffectType type = ExportEffectType::Brightness;
    bool enabled = true;
    double amount = 0.0;
    std::vector<ExportEffectKeyframe> keyframes;
};
struct ExportSpeedKeyframe final {
    std::int64_t frameOffset = 0;
    double rate = 1.0;
    ExportInterpolation interpolation = ExportInterpolation::Linear;
};
struct ExportMotionKeyframe final {
    std::int64_t frameOffset = 0;
    double opacity = 1.0;
    double positionX = 0.0;
    double positionY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDegrees = 0.0;
    double anchorX = 0.5;
    double anchorY = 0.5;
    ExportInterpolation interpolation = ExportInterpolation::Linear;
};
struct ExportGainKeyframe final {
    std::int64_t frameOffset = 0;
    double gainDb = 0.0;
    ExportInterpolation interpolation = ExportInterpolation::Linear;
};

struct ExportClip final {
    ExportClipKind kind = ExportClipKind::Video;
    std::filesystem::path path;
    // Stacking order within the clip's kind group: video order 0 composites at
    // the back (V1) and higher orders in front. Transitions only pair clips
    // that share the same track order.
    std::int64_t trackOrder = 0;
    std::int64_t timelineStart = 0;
    std::int64_t duration = 0;
    std::int64_t sourceStart = 0;
    double opacity = 1.0;
    double audioGainDb = 0.0;
    double playbackRate = 1.0;
    std::int64_t fadeInFrames = 0;
    std::int64_t fadeOutFrames = 0;
    std::int64_t videoTransitionInFrames = 0;
    std::int64_t audioTransitionInFrames = 0;
    double positionX = 0.0;
    double positionY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDegrees = 0.0;
    double anchorX = 0.5;
    double anchorY = 0.5;
    double cropLeft = 0.0;
    double cropRight = 0.0;
    double cropTop = 0.0;
    double cropBottom = 0.0;
    std::vector<ExportSpeedKeyframe> speedKeyframes;
    std::vector<ExportMotionKeyframe> motionKeyframes;
    std::vector<ExportGainKeyframe> gainKeyframes;
    ExportMaskShape maskShape = ExportMaskShape::None;
    double maskCenterX = 0.5;
    double maskCenterY = 0.5;
    double maskWidth = 1.0;
    double maskHeight = 1.0;
    double maskFeather = 0.0;
    bool maskInverted = false;
    std::vector<ExportEffect> effects;
};

struct ExportOverlay final {
    std::filesystem::path path;
    std::int64_t timelineStart = 0;
    std::int64_t duration = 0;
};

struct TimelineExportRequest final {
    std::filesystem::path outputPath;
    std::int32_t frameRateNumerator = 24;
    std::int32_t frameRateDenominator = 1;
    std::int32_t width = 1280;
    std::int32_t height = 720;
    std::int64_t durationFrames = 0;
    std::int64_t videoBitrate = 5'000'000;
    std::vector<ExportClip> clips;
    std::vector<ExportOverlay> overlays;
};

struct TimelineExportResult final {
    std::string videoEncoder;
    std::string audioEncoder;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept { return error.empty(); }
};

using ExportProgress = std::function<bool(std::int64_t completed, std::int64_t total)>;

[[nodiscard]] TimelineExportResult exportTimeline(const TimelineExportRequest& request,
                                                  const ExportProgress& progress = {});
[[nodiscard]] VideoFrameResult renderTimelineFrame(const TimelineExportRequest& request,
                                                   std::int64_t timelineFrame);
[[nodiscard]] AudioBufferResult renderTimelineAudio(const TimelineExportRequest& request,
                                                    std::int64_t timelineStartFrame,
                                                    std::int64_t durationFrames);

} // namespace videx::media
