#include <videx/media/audio_buffer.hpp>
#include <videx/media/timeline_export.hpp>
#include <videx/media/video_frame.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <system_error>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr int audioSampleRate = 48'000;
constexpr int audioChannels = 2;
constexpr std::int64_t audioCacheSamples = audioSampleRate * 2LL;

struct CodecContextDeleter final {
    void operator()(AVCodecContext* context) const noexcept { avcodec_free_context(&context); }
};
struct FrameDeleter final {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
struct PacketDeleter final {
    void operator()(AVPacket* packet) const noexcept { av_packet_free(&packet); }
};
struct ScaleContextDeleter final {
    void operator()(SwsContext* context) const noexcept { sws_freeContext(context); }
};
struct ResampleContextDeleter final {
    void operator()(SwrContext* context) const noexcept { swr_free(&context); }
};

using CodecContext = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using ScaleContext = std::unique_ptr<SwsContext, ScaleContextDeleter>;
using ResampleContext = std::unique_ptr<SwrContext, ResampleContextDeleter>;

std::string ffmpegError(const int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    return av_strerror(errorCode, buffer, sizeof(buffer)) < 0 ? "unknown FFmpeg error"
                                                              : std::string(buffer);
}

std::string utf8Path(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

const AVCodec* findVideoEncoder() {
#if defined(_WIN32)
    constexpr std::array names{"h264_mf", "h264_d3d12va", "libx264", "libopenh264"};
#elif defined(__APPLE__)
    constexpr std::array names{"h264_videotoolbox", "libx264", "libopenh264"};
#else
    constexpr std::array names{"libx264", "libopenh264"};
#endif
    for (const char* name : names) {
        if (const AVCodec* codec = avcodec_find_encoder_by_name(name)) {
            return codec;
        }
    }
    return avcodec_find_encoder(AV_CODEC_ID_H264);
}

AVPixelFormat selectPixelFormat(const AVCodec* codec) {
    const void* configurations = nullptr;
    int configurationCount = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                     &configurations, &configurationCount) < 0 ||
        configurations == nullptr || configurationCount <= 0) {
        return AV_PIX_FMT_YUV420P;
    }
    const auto* formats = static_cast<const AVPixelFormat*>(configurations);
    for (const AVPixelFormat preferred : {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12}) {
        for (int index = 0; index < configurationCount; ++index) {
            if (formats[index] == preferred) {
                return preferred;
            }
        }
    }
    return formats[0];
}

AVSampleFormat selectSampleFormat(const AVCodec* codec) {
    const void* configurations = nullptr;
    int configurationCount = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                     &configurations, &configurationCount) < 0 ||
        configurations == nullptr || configurationCount <= 0) {
        return AV_SAMPLE_FMT_FLTP;
    }
    const auto* formats = static_cast<const AVSampleFormat*>(configurations);
    for (int index = 0; index < configurationCount; ++index) {
        if (formats[index] == AV_SAMPLE_FMT_FLTP) {
            return formats[index];
        }
    }
    return formats[0];
}

int writeAvailablePackets(AVCodecContext* encoder, AVFormatContext* output, AVStream* stream,
                          AVPacket* packet) {
    int result = 0;
    while ((result = avcodec_receive_packet(encoder, packet)) >= 0) {
        av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
        packet->stream_index = stream->index;
        result = av_interleaved_write_frame(output, packet);
        av_packet_unref(packet);
        if (result < 0) {
            return result;
        }
    }
    return result == AVERROR(EAGAIN) || result == AVERROR_EOF ? 0 : result;
}

struct AudioCache final {
    std::size_t clipIndex = std::numeric_limits<std::size_t>::max();
    std::int64_t timelineSampleStart = 0;
    std::int64_t timelineSampleEnd = 0;
    std::int64_t sourceSampleStart = 0;
    std::vector<float> samples;
};

double effectValueAt(const videx::media::ExportEffect& effect, const std::int64_t frame) {
    if (effect.keyframes.empty()) {
        return effect.amount;
    }
    const auto next = std::ranges::lower_bound(
        effect.keyframes, frame, {}, &videx::media::ExportEffectKeyframe::frameOffset);
    if (next == effect.keyframes.begin()) return next->value;
    if (next == effect.keyframes.end()) return effect.keyframes.back().value;
    const auto previous = std::prev(next);
    if (previous->interpolation == videx::media::ExportInterpolation::Hold) {
        return previous->value;
    }
    double ratio = static_cast<double>(frame - previous->frameOffset) /
                   static_cast<double>(next->frameOffset - previous->frameOffset);
    ratio = videx::media::exportInterpolationProgress(previous->interpolation, ratio);
    return previous->value + (next->value - previous->value) * ratio;
}

videx::media::ExportMotionKeyframe motionAt(
    const videx::media::ExportClip& clip, const std::int64_t frame) {
    using videx::media::ExportMotionKeyframe;
    ExportMotionKeyframe base{.opacity = clip.opacity,
                              .positionX = clip.positionX,
                              .positionY = clip.positionY,
                              .scaleX = clip.scaleX,
                              .scaleY = clip.scaleY,
                              .rotationDegrees = clip.rotationDegrees,
                              .anchorX = clip.anchorX,
                              .anchorY = clip.anchorY};
    if (clip.motionKeyframes.empty()) return base;
    const auto next = std::ranges::lower_bound(
        clip.motionKeyframes, frame, {}, &ExportMotionKeyframe::frameOffset);
    if (next == clip.motionKeyframes.begin()) return *next;
    if (next == clip.motionKeyframes.end()) return clip.motionKeyframes.back();
    const auto previous = std::prev(next);
    if (previous->interpolation == videx::media::ExportInterpolation::Hold) return *previous;
    double ratio = static_cast<double>(frame - previous->frameOffset) /
                   static_cast<double>(next->frameOffset - previous->frameOffset);
    ratio = videx::media::exportInterpolationProgress(previous->interpolation, ratio);
    const auto mix = [ratio](const double left, const double right) {
        return left + (right - left) * ratio;
    };
    return {.frameOffset = frame,
            .opacity = mix(previous->opacity, next->opacity),
            .positionX = mix(previous->positionX, next->positionX),
            .positionY = mix(previous->positionY, next->positionY),
            .scaleX = mix(previous->scaleX, next->scaleX),
            .scaleY = mix(previous->scaleY, next->scaleY),
            .rotationDegrees = mix(previous->rotationDegrees, next->rotationDegrees),
            .anchorX = mix(previous->anchorX, next->anchorX),
            .anchorY = mix(previous->anchorY, next->anchorY),
            .interpolation = previous->interpolation};
}

double gainAt(const videx::media::ExportClip& clip, const double frame) {
    if (clip.gainKeyframes.empty()) return clip.audioGainDb;
    const auto next = std::ranges::lower_bound(
        clip.gainKeyframes, frame, {}, &videx::media::ExportGainKeyframe::frameOffset);
    if (next == clip.gainKeyframes.begin()) return next->gainDb;
    if (next == clip.gainKeyframes.end()) return clip.gainKeyframes.back().gainDb;
    const auto previous = std::prev(next);
    if (previous->interpolation == videx::media::ExportInterpolation::Hold)
        return previous->gainDb;
    double ratio = (frame - static_cast<double>(previous->frameOffset)) /
                   static_cast<double>(next->frameOffset - previous->frameOffset);
    ratio = videx::media::exportInterpolationProgress(previous->interpolation, ratio);
    return previous->gainDb + (next->gainDb - previous->gainDb) * ratio;
}

bool validGain(const videx::media::ExportGainKeyframe& keyframe) {
    return keyframe.frameOffset >= 0 && std::isfinite(keyframe.gainDb) &&
           keyframe.gainDb >= -60.0 && keyframe.gainDb <= 24.0 &&
           static_cast<int>(keyframe.interpolation) >=
               static_cast<int>(videx::media::ExportInterpolation::Linear) &&
           static_cast<int>(keyframe.interpolation) <=
               static_cast<int>(videx::media::ExportInterpolation::EaseInOut);
}

bool validMotion(const videx::media::ExportMotionKeyframe& keyframe) {
    return keyframe.frameOffset >= 0 && std::isfinite(keyframe.opacity) &&
           keyframe.opacity >= 0.0 && keyframe.opacity <= 1.0 &&
           std::isfinite(keyframe.positionX) && std::isfinite(keyframe.positionY) &&
           std::abs(keyframe.positionX) <= 100'000.0 &&
           std::abs(keyframe.positionY) <= 100'000.0 &&
           std::isfinite(keyframe.scaleX) && std::isfinite(keyframe.scaleY) &&
           keyframe.scaleX >= 0.01 && keyframe.scaleX <= 100.0 &&
           keyframe.scaleY >= 0.01 && keyframe.scaleY <= 100.0 &&
           std::isfinite(keyframe.rotationDegrees) &&
           keyframe.rotationDegrees >= -36'000.0 && keyframe.rotationDegrees <= 36'000.0 &&
           std::isfinite(keyframe.anchorX) && std::isfinite(keyframe.anchorY) &&
           keyframe.anchorX >= 0.0 && keyframe.anchorX <= 1.0 &&
           keyframe.anchorY >= 0.0 && keyframe.anchorY <= 1.0 &&
           static_cast<int>(keyframe.interpolation) >=
               static_cast<int>(videx::media::ExportInterpolation::Linear) &&
           static_cast<int>(keyframe.interpolation) <=
               static_cast<int>(videx::media::ExportInterpolation::EaseInOut);
}

void boxBlur(std::vector<std::uint8_t>& rgba, const int width, const int height,
             const int radius) {
    // Sliding-window implementation: O(w*h) regardless of radius. Preserves
    // the exact output of the naive version (true window at the edges, no
    // pixel replication, truncating division by the actual window size).
    if (radius <= 0 || width <= 0 || height <= 0) return;
    std::vector<std::uint8_t> horizontal(rgba.size());
    for (int y = 0; y < height; ++y) {
        const std::size_t rowBase = static_cast<std::size_t>(y) * width * 4U;
        std::array<int, 4> sums{};
        const int primedRight = std::min(width - 1, radius);
        for (int sample = 0; sample <= primedRight; ++sample) {
            for (int channel = 0; channel < 4; ++channel) {
                sums[static_cast<std::size_t>(channel)] +=
                    rgba[rowBase + static_cast<std::size_t>(sample) * 4U + channel];
            }
        }
        for (int x = 0; x < width; ++x) {
            const int left = std::max(0, x - radius);
            const int right = std::min(width - 1, x + radius);
            const int count = right - left + 1;
            for (int channel = 0; channel < 4; ++channel) {
                horizontal[rowBase + static_cast<std::size_t>(x) * 4U + channel] =
                    static_cast<std::uint8_t>(
                        sums[static_cast<std::size_t>(channel)] / count);
            }
            const int entering = x + 1 + radius;
            if (entering < width) {
                for (int channel = 0; channel < 4; ++channel) {
                    sums[static_cast<std::size_t>(channel)] +=
                        rgba[rowBase + static_cast<std::size_t>(entering) * 4U + channel];
                }
            }
            const int leaving = x - radius;
            if (leaving >= 0) {
                for (int channel = 0; channel < 4; ++channel) {
                    sums[static_cast<std::size_t>(channel)] -=
                        rgba[rowBase + static_cast<std::size_t>(leaving) * 4U + channel];
                }
            }
        }
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
    std::vector<int> columnSums(rowBytes, 0);
    const int primedBottom = std::min(height - 1, radius);
    for (int row = 0; row <= primedBottom; ++row) {
        const std::size_t base = static_cast<std::size_t>(row) * rowBytes;
        for (std::size_t index = 0; index < rowBytes; ++index) {
            columnSums[index] += horizontal[base + index];
        }
    }
    for (int y = 0; y < height; ++y) {
        const int top = std::max(0, y - radius);
        const int bottom = std::min(height - 1, y + radius);
        const int count = bottom - top + 1;
        const std::size_t base = static_cast<std::size_t>(y) * rowBytes;
        for (std::size_t index = 0; index < rowBytes; ++index) {
            rgba[base + index] = static_cast<std::uint8_t>(columnSums[index] / count);
        }
        const int entering = y + 1 + radius;
        if (entering < height) {
            const std::size_t enteringBase =
                static_cast<std::size_t>(entering) * rowBytes;
            for (std::size_t index = 0; index < rowBytes; ++index) {
                columnSums[index] += horizontal[enteringBase + index];
            }
        }
        const int leaving = y - radius;
        if (leaving >= 0) {
            const std::size_t leavingBase =
                static_cast<std::size_t>(leaving) * rowBytes;
            for (std::size_t index = 0; index < rowBytes; ++index) {
                columnSums[index] -= horizontal[leavingBase + index];
            }
        }
    }
}

void applyEffects(std::vector<std::uint8_t>& rgba, const int width, const int height,
                  const std::vector<videx::media::ExportEffect>& effects,
                  const std::int64_t localFrame) {
    using videx::media::ExportEffectType;
    for (const auto& effect : effects) {
        if (!effect.enabled) continue;
        const double amount = effectValueAt(effect, localFrame);
        if (effect.type == ExportEffectType::Blur) {
            boxBlur(rgba, width, height,
                    std::clamp(static_cast<int>(std::lround(amount)), 0, 20));
            continue;
        }
        // Brightness and contrast are pure per-channel maps: evaluate the
        // (identical) per-pixel formula once per input value instead of per
        // pixel. Bit-identical to the general loop below.
        if (effect.type == ExportEffectType::Brightness ||
            effect.type == ExportEffectType::Contrast) {
            std::array<std::uint8_t, 256> table{};
            for (int value = 0; value < 256; ++value) {
                double channel = value / 255.0;
                if (effect.type == ExportEffectType::Brightness) {
                    channel += amount;
                } else {
                    channel = (channel - 0.5) * (1.0 + amount) + 0.5;
                }
                table[static_cast<std::size_t>(value)] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(channel, 0.0, 1.0) * 255.0));
            }
            for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4U) {
                rgba[pixel] = table[rgba[pixel]];
                rgba[pixel + 1U] = table[rgba[pixel + 1U]];
                rgba[pixel + 2U] = table[rgba[pixel + 2U]];
            }
            continue;
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t pixel = (static_cast<std::size_t>(y) * width + x) * 4U;
                double red = rgba[pixel] / 255.0;
                double green = rgba[pixel + 1U] / 255.0;
                double blue = rgba[pixel + 2U] / 255.0;
                if (effect.type == ExportEffectType::Brightness) {
                    red += amount; green += amount; blue += amount;
                } else if (effect.type == ExportEffectType::Contrast) {
                    red = (red - 0.5) * (1.0 + amount) + 0.5;
                    green = (green - 0.5) * (1.0 + amount) + 0.5;
                    blue = (blue - 0.5) * (1.0 + amount) + 0.5;
                } else if (effect.type == ExportEffectType::Saturation) {
                    const double luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722;
                    red = luminance + (red - luminance) * (1.0 + amount);
                    green = luminance + (green - luminance) * (1.0 + amount);
                    blue = luminance + (blue - luminance) * (1.0 + amount);
                } else if (effect.type == ExportEffectType::Vignette) {
                    const double nx = (x - width * 0.5) / std::max(1.0, width * 0.5);
                    const double ny = (y - height * 0.5) / std::max(1.0, height * 0.5);
                    const double shade = std::clamp(1.0 - amount * (nx * nx + ny * ny),
                                                    0.0, 1.0);
                    red *= shade; green *= shade; blue *= shade;
                }
                rgba[pixel] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(red, 0.0, 1.0) * 255.0));
                rgba[pixel + 1U] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(green, 0.0, 1.0) * 255.0));
                rgba[pixel + 2U] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(blue, 0.0, 1.0) * 255.0));
            }
        }
    }
}

double sourceOffsetAt(const videx::media::ExportClip& clip, const double localFrame) {
    if (localFrame <= 0.0) return 0.0;
    if (clip.speedKeyframes.empty()) return localFrame * clip.playbackRate;
    double offset = std::min(localFrame,
        static_cast<double>(clip.speedKeyframes.front().frameOffset)) *
        clip.speedKeyframes.front().rate;
    for (std::size_t index = 0; index + 1U < clip.speedKeyframes.size(); ++index) {
        const auto& previous = clip.speedKeyframes[index];
        const auto& next = clip.speedKeyframes[index + 1U];
        if (localFrame <= previous.frameOffset) break;
        const double segment = static_cast<double>(next.frameOffset - previous.frameOffset);
        const double length = std::clamp(localFrame - previous.frameOffset, 0.0, segment);
        const double ratio = length / segment;
        const double curveIntegral =
            videx::media::exportInterpolationIntegral(previous.interpolation, ratio);
        offset += segment * (previous.rate * ratio +
            (next.rate - previous.rate) * curveIntegral);
    }
    const double lastFrame = static_cast<double>(clip.speedKeyframes.back().frameOffset);
    if (localFrame > lastFrame)
        offset += (localFrame - lastFrame) * clip.speedKeyframes.back().rate;
    return offset;
}

void blitCentered(const videx::media::VideoFrame& source, std::vector<std::uint8_t>& destination,
                  const int destinationWidth, const int destinationHeight) {
    std::fill(destination.begin(), destination.end(), 0U);
    if (source.width <= 0 || source.height <= 0) {
        return;
    }
    const int offsetX = std::max(0, (destinationWidth - source.width) / 2);
    const int offsetY = std::max(0, (destinationHeight - source.height) / 2);
    const int copyWidth = std::min(source.width, destinationWidth);
    const int copyHeight = std::min(source.height, destinationHeight);
    for (int row = 0; row < copyHeight; ++row) {
        const std::size_t from =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(source.width) * 4U;
        const std::size_t to = (static_cast<std::size_t>(row + offsetY) *
                                    static_cast<std::size_t>(destinationWidth) +
                                static_cast<std::size_t>(offsetX)) *
                               4U;
        std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(from),
                    static_cast<std::size_t>(copyWidth) * 4U,
                    destination.begin() + static_cast<std::ptrdiff_t>(to));
    }
}

bool validEffectAmount(const videx::media::ExportEffectType type, const double value) {
    if (!std::isfinite(value)) {
        return false;
    }
    using videx::media::ExportEffectType;
    switch (type) {
    case ExportEffectType::Brightness: return value >= -1.0 && value <= 1.0;
    case ExportEffectType::Contrast:
    case ExportEffectType::Saturation: return value >= -1.0 && value <= 3.0;
    case ExportEffectType::Blur: return value >= 0.0 && value <= 50.0;
    case ExportEffectType::Vignette: return value >= 0.0 && value <= 1.0;
    }
    return false;
}

std::string validateClipDynamics(const videx::media::ExportClip& clip) {
    if (!std::isfinite(clip.playbackRate) || clip.playbackRate < 0.25 ||
        clip.playbackRate > 4.0) {
        return "clip playback rate is invalid";
    }
    if (!std::isfinite(clip.audioGainDb) || clip.audioGainDb < -60.0 ||
        clip.audioGainDb > 24.0) {
        return "clip gain is invalid";
    }
    std::int64_t previousSpeed = -1;
    for (const auto& keyframe : clip.speedKeyframes) {
        if (keyframe.frameOffset < 0 || keyframe.frameOffset >= clip.duration ||
            keyframe.frameOffset <= previousSpeed || !std::isfinite(keyframe.rate) ||
            keyframe.rate < 0.25 || keyframe.rate > 4.0) {
            return "clip speed keyframe is invalid";
        }
        previousSpeed = keyframe.frameOffset;
    }
    std::int64_t previousGain = -1;
    for (const auto& keyframe : clip.gainKeyframes) {
        if (keyframe.frameOffset < 0 || keyframe.frameOffset >= clip.duration ||
            keyframe.frameOffset <= previousGain || !std::isfinite(keyframe.gainDb) ||
            keyframe.gainDb < -60.0 || keyframe.gainDb > 24.0) {
            return "clip gain keyframe is invalid";
        }
        previousGain = keyframe.frameOffset;
    }
    for (const auto& effect : clip.effects) {
        if (!validEffectAmount(effect.type, effect.amount)) {
            return "clip effect amount is invalid";
        }
        std::int64_t previous = -1;
        for (const auto& keyframe : effect.keyframes) {
            if (keyframe.frameOffset < 0 || keyframe.frameOffset >= clip.duration ||
                keyframe.frameOffset <= previous ||
                !validEffectAmount(effect.type, keyframe.value)) {
                return "clip effect keyframe is invalid";
            }
            previous = keyframe.frameOffset;
        }
    }
    return {};
}

} // namespace

namespace videx::media {

VideoFrameResult renderTimelineFrame(const TimelineExportRequest& request,
                                     const std::int64_t timelineFrame) {
    if (request.frameRateNumerator <= 0 || request.frameRateDenominator <= 0 ||
        request.width <= 0 || request.height <= 0 || timelineFrame < 0 ||
        timelineFrame >= request.durationFrames) {
        return {.error = "timeline frame request is invalid"};
    }
    for (const ExportClip& clip : request.clips) {
        if (clip.path.empty() || clip.duration <= 0 ||
            clip.sourceStart < 0 || !std::isfinite(clip.opacity) || clip.opacity < 0.0 ||
            clip.opacity > 1.0 || !std::isfinite(clip.scaleX) || !std::isfinite(clip.scaleY) ||
            clip.scaleX < 0.01 || clip.scaleY < 0.01 || !std::isfinite(clip.cropLeft) ||
            !std::isfinite(clip.cropRight) || !std::isfinite(clip.cropTop) ||
            !std::isfinite(clip.cropBottom) || clip.cropLeft < 0.0 || clip.cropRight < 0.0 ||
            clip.cropTop < 0.0 || clip.cropBottom < 0.0 ||
            clip.cropLeft + clip.cropRight >= 1.0 ||
            clip.cropTop + clip.cropBottom >= 1.0 || clip.maskWidth <= 0.0 ||
            clip.maskHeight <= 0.0 || clip.maskFeather < 0.0 || clip.maskFeather > 0.5) {
            return {.error = "timeline frame clip is invalid"};
        }
        std::int64_t previousMotion = -1;
        for (const ExportMotionKeyframe& keyframe : clip.motionKeyframes) {
            if (!validMotion(keyframe) || keyframe.frameOffset >= clip.duration ||
                keyframe.frameOffset <= previousMotion)
                return {.error = "timeline motion keyframe is invalid"};
            previousMotion = keyframe.frameOffset;
        }
        if (const std::string dynamicsError = validateClipDynamics(clip);
            !dynamicsError.empty()) {
            return {.error = "timeline frame " + dynamicsError};
        }
    }
    // The layer scratch buffer persists across calls (the preview server
    // renders thousands of frames per session); the output buffer cannot,
    // because it is moved into the returned frame.
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(request.width) * request.height * 4U, 0U);
    thread_local std::vector<std::uint8_t> layer;
    layer.resize(rgba.size());
    // Integer alpha blend (weight and alpha in 0..256 fixed point). RGB stays
    // within +-2/255 of the previous double-precision blend, and — like it —
    // any pixel touched by a visible layer becomes fully opaque.
    auto compositeLayer = [&rgba](const std::vector<std::uint8_t>& source,
                                  const double weight) {
        if (weight <= 0.0) {
            return;
        }
        const int weightFixed = std::max(1,
            static_cast<int>(std::lround(std::clamp(weight, 0.0, 1.0) * 256.0)));
        for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4U) {
            if (source[pixel + 3U] == 0U) continue;
            const int alpha = (source[pixel + 3U] * weightFixed) >> 8;
            rgba[pixel + 3U] = 255U;
            if (alpha <= 0) continue;
            if (alpha >= 255) {
                rgba[pixel] = source[pixel];
                rgba[pixel + 1U] = source[pixel + 1U];
                rgba[pixel + 2U] = source[pixel + 2U];
                continue;
            }
            const int inverse = 256 - alpha;
            rgba[pixel] = static_cast<std::uint8_t>(
                (source[pixel] * alpha + rgba[pixel] * inverse) >> 8);
            rgba[pixel + 1U] = static_cast<std::uint8_t>(
                (source[pixel + 1U] * alpha + rgba[pixel + 1U] * inverse) >> 8);
            rgba[pixel + 2U] = static_cast<std::uint8_t>(
                (source[pixel + 2U] * alpha + rgba[pixel + 2U] * inverse) >> 8);
        }
    };
    auto renderLayer = [&](const ExportClip& clip, const std::int64_t sourceLocal,
                           const std::int64_t effectLocal) -> std::string {
        const AVRational videoTimeBase{request.frameRateDenominator,
                                       request.frameRateNumerator};
        const std::int64_t sourceFrame = clip.sourceStart +
            static_cast<std::int64_t>(std::llround(
                sourceOffsetAt(clip, static_cast<double>(sourceLocal))));
        auto decoded = decodeVideoFrame(
            clip.path, av_rescale_q(sourceFrame, videoTimeBase, AV_TIME_BASE_Q),
            request.width, request.height);
        if (!decoded.succeeded() && sourceLocal >= clip.duration) {
            const std::int64_t fallback = clip.sourceStart +
                static_cast<std::int64_t>(std::llround(
                    sourceOffsetAt(clip, static_cast<double>(clip.duration - 1))));
            decoded = decodeVideoFrame(
                clip.path, av_rescale_q(fallback, videoTimeBase, AV_TIME_BASE_Q),
                request.width, request.height);
        }
        if (!decoded.succeeded()) return decoded.error;
        std::fill(layer.begin(), layer.end(), 0U);
        const ExportMotionKeyframe motion = motionAt(
            clip, std::clamp<std::int64_t>(effectLocal, 0, clip.duration - 1));
        constexpr double pi = 3.14159265358979323846;
        const double radians = motion.rotationDegrees * pi / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        const double centerX = request.width * 0.5 + motion.positionX;
        const double centerY = request.height * 0.5 + motion.positionY;
        const double sourceAnchorX = motion.anchorX * decoded.frame.width;
        const double sourceAnchorY = motion.anchorY * decoded.frame.height;
        const int cropLeft = static_cast<int>(std::floor(clip.cropLeft * decoded.frame.width));
        const int cropRight = static_cast<int>(std::ceil(
            (1.0 - clip.cropRight) * decoded.frame.width));
        const int cropTop = static_cast<int>(std::floor(clip.cropTop * decoded.frame.height));
        const int cropBottom = static_cast<int>(std::ceil(
            (1.0 - clip.cropBottom) * decoded.frame.height));
        const auto maskFactorAt = [&clip, &decoded](const int sourceX, const int sourceY) {
            const double normalizedX = (sourceX + 0.5) / decoded.frame.width;
            const double normalizedY = (sourceY + 0.5) / decoded.frame.height;
            const double dx = std::abs(normalizedX - clip.maskCenterX) /
                              (clip.maskWidth * 0.5);
            const double dy = std::abs(normalizedY - clip.maskCenterY) /
                              (clip.maskHeight * 0.5);
            const double distance = clip.maskShape == ExportMaskShape::Rectangle
                ? std::max(dx, dy) : std::sqrt(dx * dx + dy * dy);
            double maskFactor = distance <= 1.0 ? 1.0 : 0.0;
            if (clip.maskFeather > 0.0) {
                const double edge = clip.maskFeather * 2.0;
                maskFactor = std::clamp((1.0 + edge - distance) / (2.0 * edge), 0.0, 1.0);
            }
            if (clip.maskInverted) maskFactor = 1.0 - maskFactor;
            return maskFactor;
        };
        if (motion.rotationDegrees == 0.0) {
            // Separable fast path: with no rotation the source coordinate of a
            // column does not depend on the row (and vice versa), so the
            // per-pixel transform collapses to two lookup tables. The math is
            // bit-identical to the general loop with sine == 0.
            thread_local std::vector<int> columnLookup;
            thread_local std::vector<int> rowLookup;
            columnLookup.resize(static_cast<std::size_t>(request.width));
            rowLookup.resize(static_cast<std::size_t>(request.height));
            for (int column = 0; column < request.width; ++column) {
                const int sourceX = static_cast<int>(std::floor(
                    (column + 0.5 - centerX) / motion.scaleX + sourceAnchorX));
                columnLookup[static_cast<std::size_t>(column)] =
                    sourceX < cropLeft || sourceX >= cropRight || sourceX < 0 ||
                    sourceX >= decoded.frame.width ? -1 : sourceX;
            }
            for (int row = 0; row < request.height; ++row) {
                const int sourceY = static_cast<int>(std::floor(
                    (row + 0.5 - centerY) / motion.scaleY + sourceAnchorY));
                rowLookup[static_cast<std::size_t>(row)] =
                    sourceY < cropTop || sourceY >= cropBottom || sourceY < 0 ||
                    sourceY >= decoded.frame.height ? -1 : sourceY;
            }
            // Visible column span; with scaleX == 1 the run is contiguous in
            // the source so each row is a single memcpy.
            int firstColumn = 0;
            while (firstColumn < request.width &&
                   columnLookup[static_cast<std::size_t>(firstColumn)] < 0) ++firstColumn;
            int lastColumn = request.width - 1;
            while (lastColumn >= firstColumn &&
                   columnLookup[static_cast<std::size_t>(lastColumn)] < 0) --lastColumn;
            const bool contiguousRun =
                motion.scaleX == 1.0 && clip.maskShape == ExportMaskShape::None &&
                firstColumn <= lastColumn;
            for (int row = 0; row < request.height; ++row) {
                const int sourceY = rowLookup[static_cast<std::size_t>(row)];
                if (sourceY < 0) continue;
                const std::size_t destinationRow =
                    static_cast<std::size_t>(row) * request.width * 4U;
                const std::size_t sourceRow =
                    static_cast<std::size_t>(sourceY) * decoded.frame.width * 4U;
                if (contiguousRun) {
                    const int sourceStartX =
                        columnLookup[static_cast<std::size_t>(firstColumn)];
                    std::copy_n(decoded.frame.rgba.begin() +
                                    static_cast<std::ptrdiff_t>(
                                        sourceRow + static_cast<std::size_t>(sourceStartX) * 4U),
                                static_cast<std::size_t>(lastColumn - firstColumn + 1) * 4U,
                                layer.begin() + static_cast<std::ptrdiff_t>(
                                    destinationRow + static_cast<std::size_t>(firstColumn) * 4U));
                    continue;
                }
                for (int column = firstColumn; column <= lastColumn; ++column) {
                    const int sourceX = columnLookup[static_cast<std::size_t>(column)];
                    if (sourceX < 0) continue;
                    double maskFactor = 1.0;
                    if (clip.maskShape != ExportMaskShape::None) {
                        maskFactor = maskFactorAt(sourceX, sourceY);
                        if (maskFactor <= 0.0) continue;
                    }
                    const std::size_t source =
                        sourceRow + static_cast<std::size_t>(sourceX) * 4U;
                    const std::size_t destination =
                        destinationRow + static_cast<std::size_t>(column) * 4U;
                    std::copy_n(decoded.frame.rgba.begin() +
                                    static_cast<std::ptrdiff_t>(source),
                                4U, layer.begin() + static_cast<std::ptrdiff_t>(destination));
                    if (clip.maskShape != ExportMaskShape::None) {
                        layer[destination + 3U] = static_cast<std::uint8_t>(
                            layer[destination + 3U] * maskFactor);
                    }
                }
            }
        } else {
        for (int row = 0; row < request.height; ++row) {
            for (int column = 0; column < request.width; ++column) {
                const double destinationX = column + 0.5 - centerX;
                const double destinationY = row + 0.5 - centerY;
                const int sourceX = static_cast<int>(std::floor(
                    (cosine * destinationX + sine * destinationY) / motion.scaleX +
                    sourceAnchorX));
                const int sourceY = static_cast<int>(std::floor(
                    (-sine * destinationX + cosine * destinationY) / motion.scaleY +
                    sourceAnchorY));
                if (sourceX < cropLeft || sourceX >= cropRight || sourceY < cropTop ||
                    sourceY >= cropBottom || sourceX < 0 || sourceY < 0 ||
                    sourceX >= decoded.frame.width || sourceY >= decoded.frame.height) continue;
                double maskFactor = 1.0;
                if (clip.maskShape != ExportMaskShape::None) {
                    maskFactor = maskFactorAt(sourceX, sourceY);
                    if (maskFactor <= 0.0) continue;
                }
                const std::size_t source =
                    (static_cast<std::size_t>(sourceY) * decoded.frame.width + sourceX) * 4U;
                const std::size_t destination =
                    (static_cast<std::size_t>(row) * request.width + column) * 4U;
                std::copy_n(decoded.frame.rgba.begin() + static_cast<std::ptrdiff_t>(source),
                            4U, layer.begin() + static_cast<std::ptrdiff_t>(destination));
                layer[destination + 3U] = static_cast<std::uint8_t>(
                    layer[destination + 3U] * maskFactor);
            }
        }
        }
        applyEffects(layer, request.width, request.height, clip.effects,
                     std::clamp<std::int64_t>(effectLocal, 0, clip.duration - 1));
        return {};
    };

    for (std::size_t clipIndex = 0; clipIndex < request.clips.size(); ++clipIndex) {
        const ExportClip& clip = request.clips[clipIndex];
        if (clip.kind != ExportClipKind::Video || timelineFrame < clip.timelineStart ||
            timelineFrame >= clip.timelineStart + clip.duration) continue;
        const std::int64_t localFrame = timelineFrame - clip.timelineStart;
        const double motionOpacity = motionAt(clip, localFrame).opacity;
        double envelope = motionOpacity;
        if (clip.fadeInFrames > 0)
            envelope = std::min(envelope, motionOpacity *
                static_cast<double>(localFrame + 1) / clip.fadeInFrames);
        if (clip.fadeOutFrames > 0)
            envelope = std::min(envelope, motionOpacity *
                static_cast<double>(clip.duration - localFrame) / clip.fadeOutFrames);
        double incomingMix = 1.0;
        if (clip.videoTransitionInFrames > 0 && localFrame < clip.videoTransitionInFrames &&
            clipIndex > 0) {
            const ExportClip& outgoing = request.clips[clipIndex - 1U];
            if (outgoing.kind == ExportClipKind::Video &&
                outgoing.trackOrder == clip.trackOrder &&
                outgoing.timelineStart + outgoing.duration == clip.timelineStart) {
                incomingMix = std::clamp(static_cast<double>(localFrame + 1) /
                    clip.videoTransitionInFrames, 0.0, 1.0);
                if (const std::string error = renderLayer(
                        outgoing, outgoing.duration + localFrame, outgoing.duration - 1);
                    !error.empty()) return {.error = error};
                compositeLayer(layer, motionAt(outgoing, outgoing.duration - 1).opacity);
            }
        }
        if (const std::string error = renderLayer(clip, localFrame, localFrame); !error.empty())
            return {.error = error};
        compositeLayer(layer, std::clamp(envelope * incomingMix, 0.0, 1.0));
    }
    for (const ExportOverlay& overlay : request.overlays) {
        if (timelineFrame < overlay.timelineStart ||
            timelineFrame >= overlay.timelineStart + overlay.duration) continue;
        auto decoded = decodeVideoFrame(overlay.path, 0, request.width, request.height);
        if (!decoded.succeeded()) return {.error = decoded.error};
        blitCentered(decoded.frame, layer, request.width, request.height);
        compositeLayer(layer, 1.0);
    }
    return {.frame = {.width = request.width,
                      .height = request.height,
                      .timestampMicroseconds = av_rescale_q(
                          timelineFrame,
                          AVRational{request.frameRateDenominator,
                                     request.frameRateNumerator}, AV_TIME_BASE_Q),
                      .rgba = std::move(rgba)}};
}

AudioBufferResult renderTimelineAudio(const TimelineExportRequest& request,
                                      const std::int64_t timelineStartFrame,
                                      const std::int64_t durationFrames) {
    constexpr std::int32_t sampleRate = 48'000;
    constexpr std::int32_t channels = 2;
    if (request.frameRateNumerator <= 0 || request.frameRateDenominator <= 0 ||
        timelineStartFrame < 0 || durationFrames <= 0 ||
        timelineStartFrame > request.durationFrames ||
        durationFrames > request.durationFrames - timelineStartFrame) {
        return {.error = "timeline audio request is invalid"};
    }
    for (const ExportClip& clip : request.clips) {
        if (clip.kind != ExportClipKind::Audio || clip.path.empty() || clip.duration <= 0) {
            continue;
        }
        if (clip.fadeInFrames < 0 || clip.fadeOutFrames < 0 ||
            clip.audioTransitionInFrames < 0) {
            return {.error = "timeline audio clip is invalid"};
        }
        if (const std::string dynamicsError = validateClipDynamics(clip);
            !dynamicsError.empty()) {
            return {.error = "timeline audio " + dynamicsError};
        }
    }
    const double framesPerSecond = static_cast<double>(request.frameRateNumerator) /
                                   request.frameRateDenominator;
    const std::size_t outputSamples = static_cast<std::size_t>(std::ceil(
        static_cast<double>(durationFrames) * sampleRate / framesPerSecond));
    std::vector<float> mixed(outputSamples * channels, 0.0F);
    const double requestEnd = static_cast<double>(timelineStartFrame + durationFrames);
    for (std::size_t clipIndex = 0; clipIndex < request.clips.size(); ++clipIndex) {
        const ExportClip& clip = request.clips[clipIndex];
        if (clip.kind != ExportClipKind::Audio || clip.path.empty() || clip.duration <= 0)
            continue;
        const double nominalEnd = static_cast<double>(clip.timelineStart + clip.duration);
        std::int64_t outgoingTransition = 0;
        if (clipIndex + 1U < request.clips.size()) {
            const ExportClip& next = request.clips[clipIndex + 1U];
            if (next.kind == ExportClipKind::Audio &&
                next.trackOrder == clip.trackOrder && next.timelineStart == nominalEnd)
                outgoingTransition = next.audioTransitionInFrames;
        }
        const double clipEnd = nominalEnd + static_cast<double>(outgoingTransition);
        const double overlapStart = std::max(static_cast<double>(timelineStartFrame),
                                             static_cast<double>(clip.timelineStart));
        const double overlapEnd = std::min(requestEnd, clipEnd);
        if (overlapStart >= overlapEnd) continue;
        const double localStart = overlapStart - static_cast<double>(clip.timelineStart);
        const double localEnd = overlapEnd - static_cast<double>(clip.timelineStart);
        const double samplesPerFrame = sampleRate / framesPerSecond;
        const std::int64_t sourceSampleStart = static_cast<std::int64_t>(std::floor(
            (clip.sourceStart + sourceOffsetAt(clip, localStart)) * samplesPerFrame));
        const std::int64_t sourceSampleEnd = static_cast<std::int64_t>(std::ceil(
            (clip.sourceStart + sourceOffsetAt(clip, localEnd)) * samplesPerFrame));
        const auto decoded = decodeAudio(
            clip.path, av_rescale_q(sourceSampleStart, AVRational{1, sampleRate}, AV_TIME_BASE_Q),
            av_rescale_q(std::max<std::int64_t>(2, sourceSampleEnd - sourceSampleStart + 2),
                         AVRational{1, sampleRate}, AV_TIME_BASE_Q));
        if (!decoded.succeeded()) return decoded;
        const std::int64_t decodedSamples = static_cast<std::int64_t>(
            decoded.buffer.interleavedSamples.size() / channels);
        const std::size_t firstOutput = static_cast<std::size_t>(std::max<double>(0.0,
            std::floor((overlapStart - timelineStartFrame) * samplesPerFrame)));
        const std::size_t lastOutput = std::min(outputSamples,
            static_cast<std::size_t>(std::ceil(
                (overlapEnd - timelineStartFrame) * samplesPerFrame)));
        for (std::size_t outputSample = firstOutput; outputSample < lastOutput; ++outputSample) {
            const double timelineFrame = timelineStartFrame +
                outputSample * framesPerSecond / sampleRate;
            const double localFrame = timelineFrame - clip.timelineStart;
            const double sourcePosition =
                (clip.sourceStart + sourceOffsetAt(clip, localFrame)) * samplesPerFrame -
                sourceSampleStart;
            const auto left = static_cast<std::int64_t>(std::floor(sourcePosition));
            if (left < 0 || left + 1 >= decodedSamples) continue;
            const double fraction = sourcePosition - left;
            double envelope = 1.0;
            if (clip.fadeInFrames > 0)
                envelope = std::min(envelope, (localFrame + 1.0 / samplesPerFrame) /
                                              clip.fadeInFrames);
            if (clip.fadeOutFrames > 0 && timelineFrame < nominalEnd)
                envelope = std::min(envelope, (nominalEnd - timelineFrame) /
                                              clip.fadeOutFrames);
            if (clip.audioTransitionInFrames > 0 &&
                localFrame < clip.audioTransitionInFrames)
                envelope = std::min(envelope, (localFrame + 1.0 / samplesPerFrame) /
                                              clip.audioTransitionInFrames);
            if (outgoingTransition > 0 && timelineFrame >= nominalEnd)
                envelope = std::min(envelope, (clipEnd - timelineFrame) /
                                              outgoingTransition);
            const double gain = std::pow(10.0, gainAt(clip, localFrame) / 20.0);
            envelope = std::clamp(envelope * gain, 0.0, 16.0);
            for (std::int32_t channel = 0; channel < channels; ++channel) {
                const std::size_t source = static_cast<std::size_t>(left * channels + channel);
                const float value = static_cast<float>(
                    decoded.buffer.interleavedSamples[source] * (1.0 - fraction) +
                    decoded.buffer.interleavedSamples[source + channels] * fraction);
                mixed[outputSample * channels + channel] +=
                    value * static_cast<float>(envelope);
            }
        }
    }
    for (float& sample : mixed) sample = std::clamp(sample, -1.0F, 1.0F);
    return {.buffer = {
        .sampleRate = sampleRate,
        .channelCount = channels,
        .timestampMicroseconds = static_cast<std::int64_t>(std::llround(
            timelineStartFrame * 1'000'000.0 / framesPerSecond)),
        .interleavedSamples = std::move(mixed)}};
}

TimelineExportResult exportTimeline(const TimelineExportRequest& request,
                                    const ExportProgress& progress) {
    if (request.frameRateNumerator <= 0 || request.frameRateDenominator <= 0 ||
        request.width <= 0 || request.height <= 0 || request.durationFrames <= 0 ||
        request.outputPath.empty()) {
        return {.error = "export request is invalid"};
    }
    for (const ExportClip& clip : request.clips) {
        if (clip.duration <= 0 || clip.sourceStart < 0 ||
            !std::isfinite(clip.opacity) || clip.opacity < 0.0 || clip.opacity > 1.0 ||
            !std::isfinite(clip.audioGainDb) || clip.audioGainDb < -60.0 ||
            clip.audioGainDb > 24.0 || !std::isfinite(clip.playbackRate) ||
            clip.playbackRate < 0.25 || clip.playbackRate > 4.0 ||
            clip.fadeInFrames < 0 || clip.fadeOutFrames < 0 ||
            clip.fadeInFrames > clip.duration || clip.fadeOutFrames > clip.duration ||
            clip.videoTransitionInFrames < 0 || clip.audioTransitionInFrames < 0 ||
            clip.videoTransitionInFrames > clip.duration ||
            clip.audioTransitionInFrames > clip.duration ||
            (clip.kind == ExportClipKind::Video && clip.audioTransitionInFrames != 0) ||
            (clip.kind == ExportClipKind::Audio && clip.videoTransitionInFrames != 0)) {
            return {.error = "export clip properties are invalid"};
        }
        if (!std::isfinite(clip.positionX) || !std::isfinite(clip.positionY) ||
            !std::isfinite(clip.scaleX) || !std::isfinite(clip.scaleY) ||
            !std::isfinite(clip.rotationDegrees) || !std::isfinite(clip.anchorX) ||
            !std::isfinite(clip.anchorY) || std::abs(clip.positionX) > 100'000.0 ||
            std::abs(clip.positionY) > 100'000.0 || clip.scaleX < 0.01 ||
            clip.scaleX > 100.0 || clip.scaleY < 0.01 || clip.scaleY > 100.0 ||
            clip.anchorX < 0.0 || clip.anchorX > 1.0 || clip.anchorY < 0.0 ||
            clip.anchorY > 1.0) {
            return {.error = "export clip transform is invalid"};
        }
        if (!std::isfinite(clip.cropLeft) || !std::isfinite(clip.cropRight) ||
            !std::isfinite(clip.cropTop) || !std::isfinite(clip.cropBottom) ||
            clip.cropLeft < 0.0 || clip.cropRight < 0.0 || clip.cropTop < 0.0 ||
            clip.cropBottom < 0.0 || clip.cropLeft + clip.cropRight >= 1.0 ||
            clip.cropTop + clip.cropBottom >= 1.0) {
            return {.error = "export clip crop is invalid"};
        }
        if (static_cast<int>(clip.maskShape) < static_cast<int>(ExportMaskShape::None) ||
            static_cast<int>(clip.maskShape) > static_cast<int>(ExportMaskShape::Ellipse) ||
            !std::isfinite(clip.maskCenterX) || !std::isfinite(clip.maskCenterY) ||
            !std::isfinite(clip.maskWidth) || !std::isfinite(clip.maskHeight) ||
            !std::isfinite(clip.maskFeather) || clip.maskCenterX < 0.0 ||
            clip.maskCenterX > 1.0 || clip.maskCenterY < 0.0 || clip.maskCenterY > 1.0 ||
            clip.maskWidth <= 0.0 || clip.maskHeight <= 0.0 || clip.maskFeather < 0.0 ||
            clip.maskFeather > 0.5) {
            return {.error = "export clip mask is invalid"};
        }
        std::int64_t previousSpeedFrame = -1;
        for (const ExportSpeedKeyframe& keyframe : clip.speedKeyframes) {
            if (keyframe.frameOffset < 0 || keyframe.frameOffset >= clip.duration ||
                keyframe.frameOffset <= previousSpeedFrame || !std::isfinite(keyframe.rate) ||
                keyframe.rate < 0.25 || keyframe.rate > 4.0) {
                return {.error = "export speed keyframe is invalid"};
            }
            previousSpeedFrame = keyframe.frameOffset;
        }
        std::int64_t previousMotionFrame = -1;
        for (const ExportMotionKeyframe& keyframe : clip.motionKeyframes) {
            if (!validMotion(keyframe) || keyframe.frameOffset >= clip.duration ||
                keyframe.frameOffset <= previousMotionFrame) {
                return {.error = "export motion keyframe is invalid"};
            }
            previousMotionFrame = keyframe.frameOffset;
        }
        std::int64_t previousGainFrame = -1;
        for (const ExportGainKeyframe& keyframe : clip.gainKeyframes) {
            if (!validGain(keyframe) || keyframe.frameOffset >= clip.duration ||
                keyframe.frameOffset <= previousGainFrame) {
                return {.error = "export gain keyframe is invalid"};
            }
            previousGainFrame = keyframe.frameOffset;
        }
        for (const ExportEffect& effect : clip.effects) {
            auto validAmount = [type = effect.type](const double value) {
                if (!std::isfinite(value)) return false;
                switch (type) {
                case ExportEffectType::Brightness: return value >= -1.0 && value <= 1.0;
                case ExportEffectType::Contrast:
                case ExportEffectType::Saturation: return value >= -1.0 && value <= 3.0;
                case ExportEffectType::Blur: return value >= 0.0 && value <= 50.0;
                case ExportEffectType::Vignette: return value >= 0.0 && value <= 1.0;
                }
                return false;
            };
            if (!validAmount(effect.amount)) {
                return {.error = "export effect amount is invalid"};
            }
            std::int64_t previous = -1;
            for (const ExportEffectKeyframe& keyframe : effect.keyframes) {
                if (keyframe.frameOffset < 0 || keyframe.frameOffset >= clip.duration ||
                    keyframe.frameOffset <= previous || !validAmount(keyframe.value)) {
                    return {.error = "export effect keyframe is invalid"};
                }
                previous = keyframe.frameOffset;
            }
        }
    }
    std::vector<VideoFrame> overlayFrames;
    overlayFrames.reserve(request.overlays.size());
    for (const ExportOverlay& overlay : request.overlays) {
        if (overlay.path.empty() || overlay.duration <= 0 ||
            overlay.timelineStart >= request.durationFrames || overlay.timelineStart >
                std::numeric_limits<std::int64_t>::max() - overlay.duration ||
            overlay.timelineStart + overlay.duration <= 0) {
            return {.error = "export overlay is invalid"};
        }
        auto decoded = decodeVideoFrame(overlay.path, 0, request.width, request.height);
        if (!decoded.succeeded()) return {.error = decoded.error};
        overlayFrames.push_back(std::move(decoded.frame));
    }

    const AVCodec* videoCodec = findVideoEncoder();
    const AVCodec* audioCodec = avcodec_find_encoder_by_name("aac");
    if (videoCodec == nullptr || audioCodec == nullptr) {
        return {.error = "H.264 or AAC encoder is unavailable"};
    }

    const std::filesystem::path temporaryPath = request.outputPath.string() + ".part.mp4";
    std::error_code fileError;
    std::filesystem::remove(temporaryPath, fileError);

    AVFormatContext* output = nullptr;
    const std::string temporaryPathText = utf8Path(temporaryPath);
    int result = avformat_alloc_output_context2(&output, nullptr, "mp4", temporaryPathText.c_str());
    if (result < 0 || output == nullptr) {
        return {.error = "cannot create MP4 container: " + ffmpegError(result)};
    }
    auto releaseOutput = [&] {
        if (output->pb != nullptr) {
            avio_closep(&output->pb);
        }
        avformat_free_context(output);
    };

    CodecContext videoEncoder(avcodec_alloc_context3(videoCodec));
    CodecContext audioEncoder(avcodec_alloc_context3(audioCodec));
    if (!videoEncoder || !audioEncoder) {
        releaseOutput();
        return {.error = "cannot allocate export encoders"};
    }

    videoEncoder->codec_id = videoCodec->id;
    videoEncoder->codec_type = AVMEDIA_TYPE_VIDEO;
    videoEncoder->width = request.width - (request.width % 2);
    videoEncoder->height = request.height - (request.height % 2);
    videoEncoder->pix_fmt = selectPixelFormat(videoCodec);
    videoEncoder->time_base = {request.frameRateDenominator, request.frameRateNumerator};
    videoEncoder->framerate = {request.frameRateNumerator, request.frameRateDenominator};
    videoEncoder->bit_rate =
        request.videoBitrate > 0 ? request.videoBitrate : 5'000'000;
    videoEncoder->gop_size = std::max(1, request.frameRateNumerator / request.frameRateDenominator * 2);
    videoEncoder->max_b_frames = 0;

    audioEncoder->codec_id = audioCodec->id;
    audioEncoder->codec_type = AVMEDIA_TYPE_AUDIO;
    audioEncoder->sample_fmt = selectSampleFormat(audioCodec);
    audioEncoder->sample_rate = audioSampleRate;
    audioEncoder->time_base = {1, audioSampleRate};
    audioEncoder->bit_rate = 192'000;
    AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
    if (av_channel_layout_copy(&audioEncoder->ch_layout, &stereoLayout) < 0) {
        releaseOutput();
        return {.error = "cannot configure stereo channel layout"};
    }
    if ((output->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        videoEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        audioEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    result = avcodec_open2(videoEncoder.get(), videoCodec, nullptr);
    if (result < 0) {
        const std::string error = "cannot open H.264 encoder " + std::string(videoCodec->name) +
                                  ": " + ffmpegError(result);
        releaseOutput();
        return {.error = error};
    }
    result = avcodec_open2(audioEncoder.get(), audioCodec, nullptr);
    if (result < 0) {
        const std::string error = "cannot open AAC encoder: " + ffmpegError(result);
        releaseOutput();
        return {.error = error};
    }

    AVStream* videoStream = avformat_new_stream(output, nullptr);
    AVStream* audioStream = avformat_new_stream(output, nullptr);
    if (videoStream == nullptr || audioStream == nullptr ||
        avcodec_parameters_from_context(videoStream->codecpar, videoEncoder.get()) < 0 ||
        avcodec_parameters_from_context(audioStream->codecpar, audioEncoder.get()) < 0) {
        releaseOutput();
        return {.error = "cannot create MP4 streams"};
    }
    videoStream->time_base = videoEncoder->time_base;
    audioStream->time_base = audioEncoder->time_base;

    result = avio_open(&output->pb, temporaryPathText.c_str(), AVIO_FLAG_WRITE);
    if (result < 0 || (result = avformat_write_header(output, nullptr)) < 0) {
        const std::string error = "cannot open MP4 output: " + ffmpegError(result);
        releaseOutput();
        return {.error = error};
    }

    Frame videoFrame(av_frame_alloc());
    Frame audioFrame(av_frame_alloc());
    Packet packet(av_packet_alloc());
    if (!videoFrame || !audioFrame || !packet) {
        releaseOutput();
        return {.error = "cannot allocate export buffers"};
    }
    videoFrame->format = videoEncoder->pix_fmt;
    videoFrame->width = videoEncoder->width;
    videoFrame->height = videoEncoder->height;
    if (av_frame_get_buffer(videoFrame.get(), 32) < 0) {
        releaseOutput();
        return {.error = "cannot allocate video frame"};
    }

    const int audioFrameSize = audioEncoder->frame_size > 0 ? audioEncoder->frame_size : 1024;
    audioFrame->format = audioEncoder->sample_fmt;
    audioFrame->sample_rate = audioEncoder->sample_rate;
    audioFrame->nb_samples = audioFrameSize;
    if (av_channel_layout_copy(&audioFrame->ch_layout, &audioEncoder->ch_layout) < 0 ||
        av_frame_get_buffer(audioFrame.get(), 0) < 0) {
        releaseOutput();
        return {.error = "cannot allocate audio frame"};
    }
    SwrContext* rawResampler = nullptr;
    AVChannelLayout inputLayout = AV_CHANNEL_LAYOUT_STEREO;
    result = swr_alloc_set_opts2(&rawResampler, &audioEncoder->ch_layout,
                                 audioEncoder->sample_fmt, audioSampleRate, &inputLayout,
                                 AV_SAMPLE_FMT_FLT, audioSampleRate, 0, nullptr);
    ResampleContext audioResampler(rawResampler);
    if (result < 0 || !audioResampler || swr_init(audioResampler.get()) < 0) {
        releaseOutput();
        return {.error = "cannot initialize export audio converter"};
    }

    const AVRational videoTimeBase{request.frameRateDenominator, request.frameRateNumerator};
    const AVRational audioTimeBase{1, audioSampleRate};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(videoEncoder->width) *
                                   static_cast<std::size_t>(videoEncoder->height) * 4U);
    ScaleContext scaler(sws_getContext(videoEncoder->width, videoEncoder->height, AV_PIX_FMT_RGBA,
                                       videoEncoder->width, videoEncoder->height,
                                       videoEncoder->pix_fmt, SWS_BILINEAR, nullptr, nullptr,
                                       nullptr));
    if (!scaler) {
        releaseOutput();
        return {.error = "cannot initialize export color conversion"};
    }

    std::vector<AudioCache> audioCaches;
    std::int64_t nextAudioSample = 0;
    const std::int64_t totalAudioSamples =
        av_rescale_q(request.durationFrames, videoTimeBase, audioTimeBase);

    auto encodeAudioUntil = [&](const std::int64_t targetSample) -> std::string {
        while (nextAudioSample < std::min(targetSample, totalAudioSamples)) {
            std::vector<float> mixed(static_cast<std::size_t>(audioFrameSize) * audioChannels);
            const std::int64_t samplesToMix =
                std::min<std::int64_t>(audioFrameSize, totalAudioSamples - nextAudioSample);
            for (std::size_t clipIndex = 0; clipIndex < request.clips.size(); ++clipIndex) {
                const ExportClip& clip = request.clips[clipIndex];
                if (clip.kind != ExportClipKind::Audio) {
                    continue;
                }
                const std::int64_t clipStart = av_rescale_q(clip.timelineStart, videoTimeBase,
                                                            audioTimeBase);
                const std::int64_t nominalClipEnd = av_rescale_q(
                    clip.timelineStart + clip.duration, videoTimeBase, audioTimeBase);
                std::int64_t clipEnd = nominalClipEnd;
                std::int64_t outgoingTransitionSamples = 0;
                if (clipIndex + 1U < request.clips.size()) {
                    const ExportClip& next = request.clips[clipIndex + 1U];
                    if (next.kind == ExportClipKind::Audio &&
                        next.trackOrder == clip.trackOrder &&
                        next.timelineStart == clip.timelineStart + clip.duration &&
                        next.audioTransitionInFrames > 0) {
                        outgoingTransitionSamples = av_rescale_q(
                            next.audioTransitionInFrames, videoTimeBase, audioTimeBase);
                        clipEnd += outgoingTransitionSamples;
                    }
                }
                const std::int64_t overlapStart = std::max(nextAudioSample, clipStart);
                const std::int64_t overlapEnd =
                    std::min(nextAudioSample + samplesToMix, clipEnd);
                if (overlapStart >= overlapEnd) {
                    continue;
                }

                auto cache = std::ranges::find_if(audioCaches, [clipIndex](const AudioCache& item) {
                    return item.clipIndex == clipIndex;
                });
                if (cache == audioCaches.end() || overlapStart < cache->timelineSampleStart ||
                    overlapEnd > cache->timelineSampleEnd) {
                    const std::int64_t decodeStart = overlapStart;
                    const std::int64_t remaining = clipEnd - decodeStart;
                    const std::int64_t timelineSamples = std::min(audioCacheSamples, remaining);
                    const double framesPerSample =
                        static_cast<double>(request.frameRateNumerator) /
                        (static_cast<double>(request.frameRateDenominator) * audioSampleRate);
                    const double samplesPerFrame = 1.0 / framesPerSample;
                    const double localStart = (decodeStart - clipStart) * framesPerSample;
                    const double localEnd = (decodeStart + timelineSamples - clipStart) *
                                            framesPerSample;
                    const auto sourceSample = static_cast<std::int64_t>(std::floor(
                        (clip.sourceStart + sourceOffsetAt(clip, localStart)) *
                        samplesPerFrame));
                    const auto sourceEndSample = static_cast<std::int64_t>(std::ceil(
                        (clip.sourceStart + sourceOffsetAt(clip, localEnd)) *
                        samplesPerFrame));
                    const std::int64_t decodeSamples =
                        std::max<std::int64_t>(2, sourceEndSample - sourceSample + 2);
                    const auto decoded = decodeAudio(
                        clip.path, av_rescale_q(sourceSample, audioTimeBase, AV_TIME_BASE_Q),
                        av_rescale_q(decodeSamples, audioTimeBase, AV_TIME_BASE_Q));
                    if (!decoded.succeeded()) {
                        return decoded.error;
                    }
                    if (cache == audioCaches.end()) {
                        audioCaches.push_back({.clipIndex = clipIndex});
                        cache = std::prev(audioCaches.end());
                    }
                    cache->timelineSampleStart = decodeStart;
                    cache->timelineSampleEnd = decodeStart + timelineSamples;
                    cache->sourceSampleStart = sourceSample;
                    cache->samples = decoded.buffer.interleavedSamples;
                }

                const std::int64_t destinationOffset = overlapStart - nextAudioSample;
                const std::int64_t copyFrames = overlapEnd - overlapStart;
                const std::int64_t fadeInSamples =
                    av_rescale_q(clip.fadeInFrames, videoTimeBase, audioTimeBase);
                const std::int64_t fadeOutSamples =
                    av_rescale_q(clip.fadeOutFrames, videoTimeBase, audioTimeBase);
                const std::int64_t transitionInSamples =
                    av_rescale_q(clip.audioTransitionInFrames, videoTimeBase, audioTimeBase);
                for (std::int64_t sample = 0; sample < copyFrames; ++sample) {
                    const std::int64_t timelineSample = overlapStart + sample;
                    double envelope = 1.0;
                    if (fadeInSamples > 0) {
                        envelope = std::min(envelope,
                            static_cast<double>(timelineSample - clipStart + 1) /
                            static_cast<double>(fadeInSamples));
                    }
                    if (fadeOutSamples > 0 && timelineSample < nominalClipEnd) {
                        envelope = std::min(envelope,
                            static_cast<double>(nominalClipEnd - timelineSample) /
                            static_cast<double>(fadeOutSamples));
                    }
                    if (transitionInSamples > 0 && timelineSample < clipStart + transitionInSamples) {
                        envelope = std::min(envelope,
                            static_cast<double>(timelineSample - clipStart + 1) /
                            static_cast<double>(transitionInSamples));
                    }
                    if (outgoingTransitionSamples > 0 && timelineSample >= nominalClipEnd) {
                        envelope = std::min(envelope,
                            static_cast<double>(clipEnd - timelineSample) /
                            static_cast<double>(outgoingTransitionSamples));
                    }
                    const double framesPerSample =
                        static_cast<double>(request.frameRateNumerator) /
                        (static_cast<double>(request.frameRateDenominator) * audioSampleRate);
                    const double samplesPerFrame = 1.0 / framesPerSample;
                    const double localFrame = (timelineSample - clipStart) * framesPerSample;
                    const float gain = static_cast<float>(
                        std::pow(10.0, gainAt(clip, localFrame) / 20.0));
                    const double sourcePosition =
                        (clip.sourceStart + sourceOffsetAt(clip, localFrame)) *
                            samplesPerFrame - cache->sourceSampleStart;
                    const auto sourceOffset = static_cast<std::int64_t>(
                        std::floor(sourcePosition));
                    const double fraction = sourcePosition - sourceOffset;
                    const auto cachedFrames = static_cast<std::int64_t>(
                        cache->samples.size() / audioChannels);
                    if (sourceOffset < 0 || sourceOffset + 1 >= cachedFrames) {
                        break;
                    }
                    for (int channel = 0; channel < audioChannels; ++channel) {
                        const std::size_t destination = static_cast<std::size_t>(
                            (destinationOffset + sample) * audioChannels + channel);
                        const std::size_t source = static_cast<std::size_t>(
                            sourceOffset * audioChannels + channel);
                        const float interpolated = static_cast<float>(
                            cache->samples[source] * (1.0 - fraction) +
                            cache->samples[source + audioChannels] * fraction);
                        mixed[destination] += interpolated * gain *
                                              static_cast<float>(std::clamp(envelope, 0.0, 1.0));
                    }
                }
            }
            for (float& sample : mixed) {
                sample = std::clamp(sample, -1.0F, 1.0F);
            }

            if (av_frame_make_writable(audioFrame.get()) < 0) {
                return "cannot make audio frame writable";
            }
            const std::uint8_t* input = reinterpret_cast<const std::uint8_t*>(mixed.data());
            const int converted = swr_convert(audioResampler.get(), audioFrame->data,
                                              audioFrameSize, &input,
                                              static_cast<int>(samplesToMix));
            if (converted < 0) {
                return "cannot convert export audio: " + ffmpegError(converted);
            }
            audioFrame->nb_samples = converted;
            audioFrame->pts = nextAudioSample;
            result = avcodec_send_frame(audioEncoder.get(), audioFrame.get());
            if (result < 0 ||
                (result = writeAvailablePackets(audioEncoder.get(), output, audioStream,
                                                packet.get())) < 0) {
                return "cannot encode AAC audio: " + ffmpegError(result);
            }
            nextAudioSample += samplesToMix;
        }
        return {};
    };

    for (std::int64_t timelineFrame = 0; timelineFrame < request.durationFrames;
         ++timelineFrame) {
        if (progress && !progress(timelineFrame, request.durationFrames)) {
            releaseOutput();
            std::filesystem::remove(temporaryPath, fileError);
            return {.error = "export cancelled"};
        }

        std::fill(rgba.begin(), rgba.end(), 0U);
        auto renderLayer = [&](const ExportClip& clip, const std::int64_t sourceLocal,
                               const std::int64_t effectLocal,
                               std::vector<std::uint8_t>& layer) -> std::string {
            const std::int64_t sourceFrame = clip.sourceStart +
                static_cast<std::int64_t>(std::llround(
                    sourceOffsetAt(clip, static_cast<double>(sourceLocal))));
            auto decoded = decodeVideoFrame(
                clip.path, av_rescale_q(sourceFrame, videoTimeBase, AV_TIME_BASE_Q),
                videoEncoder->width, videoEncoder->height);
            if (!decoded.succeeded() && sourceLocal >= clip.duration) {
                const std::int64_t fallback = clip.sourceStart +
                    static_cast<std::int64_t>(std::llround(
                        sourceOffsetAt(clip, static_cast<double>(clip.duration - 1))));
                decoded = decodeVideoFrame(
                    clip.path, av_rescale_q(fallback, videoTimeBase, AV_TIME_BASE_Q),
                    videoEncoder->width, videoEncoder->height);
            }
            if (!decoded.succeeded()) return decoded.error;
            std::fill(layer.begin(), layer.end(), 0U);
            const ExportMotionKeyframe motion = motionAt(
                clip, std::clamp<std::int64_t>(effectLocal, 0, clip.duration - 1));
            constexpr double pi = 3.14159265358979323846;
            const double radians = motion.rotationDegrees * pi / 180.0;
            const double cosine = std::cos(radians);
            const double sine = std::sin(radians);
            const double centerX = videoEncoder->width * 0.5 + motion.positionX;
            const double centerY = videoEncoder->height * 0.5 + motion.positionY;
            const double sourceAnchorX = motion.anchorX * decoded.frame.width;
            const double sourceAnchorY = motion.anchorY * decoded.frame.height;
            const int cropLeft = static_cast<int>(std::floor(
                clip.cropLeft * decoded.frame.width));
            const int cropRight = static_cast<int>(std::ceil(
                (1.0 - clip.cropRight) * decoded.frame.width));
            const int cropTop = static_cast<int>(std::floor(
                clip.cropTop * decoded.frame.height));
            const int cropBottom = static_cast<int>(std::ceil(
                (1.0 - clip.cropBottom) * decoded.frame.height));
            for (int row = 0; row < videoEncoder->height; ++row) {
                for (int column = 0; column < videoEncoder->width; ++column) {
                    const double destinationX = column + 0.5 - centerX;
                    const double destinationY = row + 0.5 - centerY;
                    const int sourceX = static_cast<int>(std::floor(
                        (cosine * destinationX + sine * destinationY) / motion.scaleX +
                        sourceAnchorX));
                    const int sourceY = static_cast<int>(std::floor(
                        (-sine * destinationX + cosine * destinationY) / motion.scaleY +
                        sourceAnchorY));
                    if (sourceX < 0 || sourceY < 0 || sourceX >= decoded.frame.width ||
                        sourceY >= decoded.frame.height) continue;
                    if (sourceX < cropLeft || sourceX >= cropRight || sourceY < cropTop ||
                        sourceY >= cropBottom) continue;
                    double maskFactor = 1.0;
                    if (clip.maskShape != ExportMaskShape::None) {
                        const double normalizedX = (sourceX + 0.5) / decoded.frame.width;
                        const double normalizedY = (sourceY + 0.5) / decoded.frame.height;
                        const double dx = std::abs(normalizedX - clip.maskCenterX) /
                                          (clip.maskWidth * 0.5);
                        const double dy = std::abs(normalizedY - clip.maskCenterY) /
                                          (clip.maskHeight * 0.5);
                        const double distance = clip.maskShape == ExportMaskShape::Rectangle
                            ? std::max(dx, dy) : std::sqrt(dx * dx + dy * dy);
                        maskFactor = distance <= 1.0 ? 1.0 : 0.0;
                        if (clip.maskFeather > 0.0) {
                            const double edge = clip.maskFeather * 2.0;
                            maskFactor = std::clamp((1.0 + edge - distance) / (2.0 * edge),
                                                    0.0, 1.0);
                        }
                        if (clip.maskInverted) maskFactor = 1.0 - maskFactor;
                        if (maskFactor <= 0.0) continue;
                    }
                    const std::size_t source =
                        (static_cast<std::size_t>(sourceY) * decoded.frame.width + sourceX) * 4U;
                    const std::size_t destination =
                        (static_cast<std::size_t>(row) * videoEncoder->width + column) * 4U;
                    std::copy_n(decoded.frame.rgba.begin() +
                                    static_cast<std::ptrdiff_t>(source), 4U,
                                layer.begin() + static_cast<std::ptrdiff_t>(destination));
                    layer[destination + 3U] = static_cast<std::uint8_t>(
                        layer[destination + 3U] * maskFactor);
                }
            }
            applyEffects(layer, videoEncoder->width, videoEncoder->height, clip.effects,
                         std::clamp<std::int64_t>(effectLocal, 0, clip.duration - 1));
            return {};
        };
        auto compositeLayer = [&](const std::vector<std::uint8_t>& layer,
                                  const double weight) {
            for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4U) {
                const double alpha = std::clamp(
                    static_cast<double>(layer[pixel + 3U]) / 255.0 * weight, 0.0, 1.0);
                if (alpha <= 0.0) continue;
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    rgba[pixel + channel] = static_cast<std::uint8_t>(std::clamp(
                        layer[pixel + channel] * alpha +
                            rgba[pixel + channel] * (1.0 - alpha), 0.0, 255.0));
                }
                rgba[pixel + 3U] = 255U;
            }
        };

        std::vector<std::uint8_t> layer(rgba.size());
        for (std::size_t clipIndex = 0; clipIndex < request.clips.size(); ++clipIndex) {
            const ExportClip& clip = request.clips[clipIndex];
            if (clip.kind != ExportClipKind::Video || timelineFrame < clip.timelineStart ||
                timelineFrame >= clip.timelineStart + clip.duration) continue;
            const std::int64_t localFrame = timelineFrame - clip.timelineStart;
            const double motionOpacity = motionAt(clip, localFrame).opacity;
            double envelope = motionOpacity;
            if (clip.fadeInFrames > 0) {
                envelope = std::min(envelope, motionOpacity *
                    static_cast<double>(localFrame + 1) / clip.fadeInFrames);
            }
            if (clip.fadeOutFrames > 0) {
                envelope = std::min(envelope, motionOpacity *
                    static_cast<double>(clip.duration - localFrame) / clip.fadeOutFrames);
            }
            double incomingMix = 1.0;
            if (clip.videoTransitionInFrames > 0 && localFrame < clip.videoTransitionInFrames &&
                clipIndex > 0) {
                const ExportClip& outgoing = request.clips[clipIndex - 1U];
                if (outgoing.kind == ExportClipKind::Video &&
                    outgoing.trackOrder == clip.trackOrder &&
                    outgoing.timelineStart + outgoing.duration == clip.timelineStart) {
                    incomingMix = std::clamp(static_cast<double>(localFrame + 1) /
                        clip.videoTransitionInFrames, 0.0, 1.0);
                    if (const std::string error = renderLayer(
                            outgoing, outgoing.duration + localFrame,
                            outgoing.duration - 1, layer); !error.empty()) {
                        releaseOutput();
                        std::filesystem::remove(temporaryPath, fileError);
                        return {.error = error};
                    }
                    compositeLayer(layer, motionAt(outgoing, outgoing.duration - 1).opacity);
                }
            }
            if (const std::string error = renderLayer(clip, localFrame, localFrame, layer);
                !error.empty()) {
                releaseOutput();
                std::filesystem::remove(temporaryPath, fileError);
                return {.error = error};
            }
            compositeLayer(layer, std::clamp(envelope * incomingMix, 0.0, 1.0));
        }
        for (std::size_t overlayIndex = 0; overlayIndex < request.overlays.size();
             ++overlayIndex) {
            const ExportOverlay& overlay = request.overlays[overlayIndex];
            if (timelineFrame < overlay.timelineStart ||
                timelineFrame >= overlay.timelineStart + overlay.duration) continue;
            blitCentered(overlayFrames[overlayIndex], layer, videoEncoder->width,
                         videoEncoder->height);
            compositeLayer(layer, 1.0);
        }

        if (av_frame_make_writable(videoFrame.get()) < 0) {
            releaseOutput();
            std::filesystem::remove(temporaryPath, fileError);
            return {.error = "cannot make video frame writable"};
        }
        const std::uint8_t* sourceData[4]{rgba.data(), nullptr, nullptr, nullptr};
        const int sourceLines[4]{videoEncoder->width * 4, 0, 0, 0};
        sws_scale(scaler.get(), sourceData, sourceLines, 0, videoEncoder->height,
                  videoFrame->data, videoFrame->linesize);
        videoFrame->pts = timelineFrame;
        result = avcodec_send_frame(videoEncoder.get(), videoFrame.get());
        if (result < 0 ||
            (result = writeAvailablePackets(videoEncoder.get(), output, videoStream,
                                            packet.get())) < 0) {
            releaseOutput();
            std::filesystem::remove(temporaryPath, fileError);
            return {.error = "cannot encode H.264 video: " + ffmpegError(result)};
        }

        const std::int64_t audioTarget =
            av_rescale_q(timelineFrame + 1, videoTimeBase, audioTimeBase);
        if (const std::string audioError = encodeAudioUntil(audioTarget); !audioError.empty()) {
            releaseOutput();
            std::filesystem::remove(temporaryPath, fileError);
            return {.error = audioError};
        }
    }

    if (const std::string audioError = encodeAudioUntil(totalAudioSamples); !audioError.empty()) {
        releaseOutput();
        std::filesystem::remove(temporaryPath, fileError);
        return {.error = audioError};
    }
    result = avcodec_send_frame(videoEncoder.get(), nullptr);
    if (result >= 0) {
        result = writeAvailablePackets(videoEncoder.get(), output, videoStream, packet.get());
    }
    if (result >= 0) {
        result = avcodec_send_frame(audioEncoder.get(), nullptr);
    }
    if (result >= 0) {
        result = writeAvailablePackets(audioEncoder.get(), output, audioStream, packet.get());
    }
    if (result >= 0) {
        result = av_write_trailer(output);
    }
    releaseOutput();
    if (result < 0) {
        std::filesystem::remove(temporaryPath, fileError);
        return {.error = "cannot finalize MP4: " + ffmpegError(result)};
    }

    std::filesystem::rename(temporaryPath, request.outputPath, fileError);
    if (fileError) {
        return {.error = "cannot publish completed MP4: " + fileError.message()};
    }
    if (progress) {
        static_cast<void>(progress(request.durationFrames, request.durationFrames));
    }
    return {.videoEncoder = videoCodec->name, .audioEncoder = audioCodec->name};
}

} // namespace videx::media
