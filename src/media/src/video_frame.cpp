#include <videx/media/video_frame.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

struct FormatContextDeleter final {
    void operator()(AVFormatContext* context) const noexcept {
        if (context != nullptr) {
            avformat_close_input(&context);
        }
    }
};

struct CodecContextDeleter final {
    void operator()(AVCodecContext* context) const noexcept {
        avcodec_free_context(&context);
    }
};

struct PacketDeleter final {
    void operator()(AVPacket* packet) const noexcept {
        av_packet_free(&packet);
    }
};

struct FrameDeleter final {
    void operator()(AVFrame* frame) const noexcept {
        av_frame_free(&frame);
    }
};

struct ScaleContextDeleter final {
    void operator()(SwsContext* context) const noexcept {
        sws_freeContext(context);
    }
};

using FormatContext = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContext = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using ScaleContext = std::unique_ptr<SwsContext, ScaleContextDeleter>;

std::string ffmpegError(const int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0) {
        return "unknown FFmpeg error";
    }
    return buffer;
}

std::string utf8Path(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

// Titles are app-rendered stills stored as "VXTI" + width + height (little
// endian u32) + raw RGBA rows. The FFmpeg build ships without image decoders,
// so this dedicated reader keeps preview and export parity for text layers.
// Title stills are re-requested for every preview frame; without a cache each
// request re-reads the full RGBA raster (8+ MB at 1080p) and re-runs the
// bilinear fit. Keyed by path + mtime + output size, so edits invalidate.
struct TitleCacheEntry final {
    std::string key;
    std::uint64_t lastUse = 0;
    videx::media::VideoFrame frame;
};

std::mutex titleCacheMutex;
std::vector<TitleCacheEntry> titleCache;
std::uint64_t titleCacheTick = 0;
constexpr std::size_t titleCacheCapacity = 6;

std::string titleCacheKey(const std::filesystem::path& path,
                          const std::int32_t maximumWidth,
                          const std::int32_t maximumHeight) {
    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    return path.string() + '|' +
           std::to_string(writeTime.time_since_epoch().count()) + '|' +
           std::to_string(maximumWidth) + 'x' + std::to_string(maximumHeight);
}

videx::media::VideoFrameResult decodeTitleImageUncached(
    const std::filesystem::path& path, const std::int32_t maximumWidth,
    const std::int32_t maximumHeight) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {.error = "could not open the title image"};
    }
    std::array<char, 4> magic{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    file.read(magic.data(), 4);
    file.read(reinterpret_cast<char*>(&width), 4);
    file.read(reinterpret_cast<char*>(&height), 4);
    if (!file || std::string_view(magic.data(), magic.size()) != "VXTI" || width == 0 ||
        height == 0 || width > 8192U || height > 8192U) {
        return {.error = "title image header is invalid"};
    }
    const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4U;
    std::vector<std::uint8_t> rgba(byteCount);
    file.read(reinterpret_cast<char*>(rgba.data()),
              static_cast<std::streamsize>(byteCount));
    if (!file) {
        return {.error = "title image data is truncated"};
    }
    // A title raster represents the full frame canvas, so it always scales to
    // fit the requested output size (up or down) — bilinear keeps text edges
    // usable when exporting above the raster resolution.
    const double scale =
        std::min(static_cast<double>(maximumWidth) / width,
                 static_cast<double>(maximumHeight) / height);
    const auto scaledWidth = std::max<std::int32_t>(
        1, static_cast<std::int32_t>(std::lround(width * scale)));
    const auto scaledHeight = std::max<std::int32_t>(
        1, static_cast<std::int32_t>(std::lround(height * scale)));
    if (scaledWidth == static_cast<std::int32_t>(width) &&
        scaledHeight == static_cast<std::int32_t>(height)) {
        return {.frame = {.width = static_cast<std::int32_t>(width),
                          .height = static_cast<std::int32_t>(height),
                          .timestampMicroseconds = 0,
                          .rgba = std::move(rgba)}};
    }
    std::vector<std::uint8_t> scaled(
        static_cast<std::size_t>(scaledWidth) * scaledHeight * 4U);
    for (std::int32_t y = 0; y < scaledHeight; ++y) {
        const double sourceY = std::clamp(
            (y + 0.5) * height / scaledHeight - 0.5, 0.0,
            static_cast<double>(height - 1U));
        const auto y0 = static_cast<std::size_t>(sourceY);
        const std::size_t y1 = std::min<std::size_t>(y0 + 1U, height - 1U);
        const double fractionY = sourceY - y0;
        for (std::int32_t x = 0; x < scaledWidth; ++x) {
            const double sourceX = std::clamp(
                (x + 0.5) * width / scaledWidth - 0.5, 0.0,
                static_cast<double>(width - 1U));
            const auto x0 = static_cast<std::size_t>(sourceX);
            const std::size_t x1 = std::min<std::size_t>(x0 + 1U, width - 1U);
            const double fractionX = sourceX - x0;
            const std::size_t destination =
                (static_cast<std::size_t>(y) * scaledWidth + x) * 4U;
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                const double top =
                    rgba[(y0 * width + x0) * 4U + channel] * (1.0 - fractionX) +
                    rgba[(y0 * width + x1) * 4U + channel] * fractionX;
                const double bottom =
                    rgba[(y1 * width + x0) * 4U + channel] * (1.0 - fractionX) +
                    rgba[(y1 * width + x1) * 4U + channel] * fractionX;
                scaled[destination + channel] = static_cast<std::uint8_t>(
                    std::clamp(top * (1.0 - fractionY) + bottom * fractionY, 0.0,
                               255.0));
            }
        }
    }
    return {.frame = {.width = scaledWidth,
                      .height = scaledHeight,
                      .timestampMicroseconds = 0,
                      .rgba = std::move(scaled)}};
}

videx::media::VideoFrameResult decodeTitleImage(const std::filesystem::path& path,
                                                const std::int32_t maximumWidth,
                                                const std::int32_t maximumHeight) {
    const std::string key = titleCacheKey(path, maximumWidth, maximumHeight);
    if (!key.empty()) {
        const std::scoped_lock lock(titleCacheMutex);
        for (TitleCacheEntry& entry : titleCache) {
            if (entry.key == key) {
                entry.lastUse = ++titleCacheTick;
                return {.frame = entry.frame};
            }
        }
    }
    videx::media::VideoFrameResult decoded =
        decodeTitleImageUncached(path, maximumWidth, maximumHeight);
    if (!key.empty() && decoded.succeeded()) {
        const std::scoped_lock lock(titleCacheMutex);
        if (titleCache.size() >= titleCacheCapacity) {
            const auto oldest = std::min_element(
                titleCache.begin(), titleCache.end(),
                [](const TitleCacheEntry& a, const TitleCacheEntry& b) {
                    return a.lastUse < b.lastUse;
                });
            titleCache.erase(oldest);
        }
        titleCache.push_back({.key = key,
                              .lastUse = ++titleCacheTick,
                              .frame = decoded.frame});
    }
    return decoded;
}

// Playback and export request frames in ascending order; reopening and
// probing the file for every frame dominates decode time. Sessions keep the
// demuxer and decoder alive per file so a sequential request only decodes
// forward from the previous frame. Requests that jump backwards (or repeat a
// timestamp) seek within the open file instead of reopening it.
struct DecoderSession final {
    std::string key;
    std::uint64_t lastUse = 0;
    FormatContext format{nullptr};
    CodecContext codec{nullptr};
    int streamIndex = -1;
    std::int64_t streamStart = 0;
    AVRational timeBase{1, AV_TIME_BASE};
    // Stream-time timestamp of the last frame handed out; AV_NOPTS_VALUE
    // until the session has delivered one.
    std::int64_t lastDelivered = AV_NOPTS_VALUE;
    bool inputExhausted = false;
    // Colour converter reused across frames (sws_getCachedContext replaces it
    // only when the geometry changes).
    SwsContext* scaler = nullptr;

    DecoderSession() = default;
    DecoderSession(const DecoderSession&) = delete;
    DecoderSession& operator=(const DecoderSession&) = delete;
    ~DecoderSession() {
        if (scaler != nullptr) {
            sws_freeContext(scaler);
        }
    }
};

std::mutex decoderCacheMutex;
std::vector<std::unique_ptr<DecoderSession>> decoderCache;
std::uint64_t decoderCacheTick = 0;
constexpr std::size_t decoderCacheCapacity = 4;

std::string decoderCacheKey(const std::filesystem::path& path) {
    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    return path.string() + '|' +
           std::to_string(writeTime.time_since_epoch().count());
}

// Takes a session out of the cache when it can continue decoding forward to
// the requested time without seeking. Alternating requests (two clips reading
// the same file at different offsets) each keep their own session this way.
std::unique_ptr<DecoderSession> acquireSequentialSession(
    const std::string& key, const std::int64_t timestampMicroseconds) {
    const std::scoped_lock lock(decoderCacheMutex);
    for (auto iterator = decoderCache.begin(); iterator != decoderCache.end();
         ++iterator) {
        DecoderSession& session = **iterator;
        if (session.key != key || session.inputExhausted ||
            session.lastDelivered == AV_NOPTS_VALUE) {
            continue;
        }
        const std::int64_t target =
            av_rescale_q(timestampMicroseconds, AV_TIME_BASE_Q, session.timeBase) +
            session.streamStart;
        const std::int64_t window =
            av_rescale_q(3'000'000, AV_TIME_BASE_Q, session.timeBase);
        if (target > session.lastDelivered &&
            target - session.lastDelivered <= window) {
            std::unique_ptr<DecoderSession> taken = std::move(*iterator);
            decoderCache.erase(iterator);
            return taken;
        }
    }
    return nullptr;
}

std::unique_ptr<DecoderSession> acquireAnySession(const std::string& key) {
    const std::scoped_lock lock(decoderCacheMutex);
    for (auto iterator = decoderCache.begin(); iterator != decoderCache.end();
         ++iterator) {
        if ((*iterator)->key == key) {
            std::unique_ptr<DecoderSession> taken = std::move(*iterator);
            decoderCache.erase(iterator);
            return taken;
        }
    }
    return nullptr;
}

void storeDecoderSession(std::unique_ptr<DecoderSession> session) {
    const std::scoped_lock lock(decoderCacheMutex);
    session->lastUse = ++decoderCacheTick;
    decoderCache.push_back(std::move(session));
    while (decoderCache.size() > decoderCacheCapacity) {
        const auto oldest = std::min_element(
            decoderCache.begin(), decoderCache.end(),
            [](const std::unique_ptr<DecoderSession>& a,
               const std::unique_ptr<DecoderSession>& b) {
                return a->lastUse < b->lastUse;
            });
        decoderCache.erase(oldest);
    }
}

std::unique_ptr<DecoderSession> openDecoderSession(const std::filesystem::path& path,
                                                   const std::string& key,
                                                   std::string& error) {
    AVFormatContext* rawFormat = nullptr;
    const std::string pathText = utf8Path(path);
    int result = avformat_open_input(&rawFormat, pathText.c_str(), nullptr, nullptr);
    if (result < 0) {
        error = "cannot open media: " + ffmpegError(result);
        return nullptr;
    }
    auto session = std::make_unique<DecoderSession>();
    session->key = key;
    session->format = FormatContext(rawFormat);
    result = avformat_find_stream_info(session->format.get(), nullptr);
    if (result < 0) {
        error = "cannot read stream information: " + ffmpegError(result);
        return nullptr;
    }
    session->streamIndex = av_find_best_stream(session->format.get(),
                                               AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (session->streamIndex < 0) {
        error = "media has no decodable video stream";
        return nullptr;
    }
    AVStream* stream = session->format->streams[session->streamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        error = "video decoder is unavailable";
        return nullptr;
    }
    session->codec = CodecContext(avcodec_alloc_context3(codec));
    if (!session->codec) {
        error = "cannot allocate video decoder";
        return nullptr;
    }
    result = avcodec_parameters_to_context(session->codec.get(), stream->codecpar);
    if (result < 0) {
        error = "cannot configure video decoder: " + ffmpegError(result);
        return nullptr;
    }
    result = avcodec_open2(session->codec.get(), codec, nullptr);
    if (result < 0) {
        error = "cannot open video decoder: " + ffmpegError(result);
        return nullptr;
    }
    session->streamStart =
        stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    session->timeBase = stream->time_base;
    return session;
}

} // namespace

namespace videx::media {

VideoFrameResult decodeVideoFrame(const std::filesystem::path& path,
                                  const std::int64_t timestampMicroseconds,
                                  const std::int32_t maximumWidth,
                                  const std::int32_t maximumHeight) {
    if (timestampMicroseconds < 0 || maximumWidth <= 0 || maximumHeight <= 0) {
        return {.error = "frame request is invalid"};
    }
    if (path.extension() == ".vxtitle") {
        return decodeTitleImage(path, maximumWidth, maximumHeight);
    }

    const std::string key = decoderCacheKey(path);
    std::unique_ptr<DecoderSession> session;
    bool sequential = false;
    if (!key.empty()) {
        session = acquireSequentialSession(key, timestampMicroseconds);
        sequential = session != nullptr;
        if (!session) {
            session = acquireAnySession(key);
        }
    }
    const bool freshSession = session == nullptr;
    if (!session) {
        std::string openError;
        session = openDecoderSession(path, key, openError);
        if (!session) {
            return {.error = openError};
        }
    }
    AVStream* stream = session->format->streams[session->streamIndex];
    const std::int64_t streamStart = session->streamStart;
    const std::int64_t targetTimestamp =
        av_rescale_q(timestampMicroseconds, AV_TIME_BASE_Q, stream->time_base) + streamStart;
    if (!sequential && (!freshSession || timestampMicroseconds > 0)) {
        const int result = av_seek_frame(session->format.get(), session->streamIndex,
                                         targetTimestamp, AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            return {.error = "cannot seek video: " + ffmpegError(result)};
        }
        avcodec_flush_buffers(session->codec.get());
        session->inputExhausted = false;
        session->lastDelivered = AV_NOPTS_VALUE;
    }
    AVCodecContext* const codecContextPointer = session->codec.get();
    AVFormatContext* const formatPointer = session->format.get();
    const int streamIndex = session->streamIndex;
    int result = 0;

    Packet packet(av_packet_alloc());
    Frame decoded(av_frame_alloc());
    Frame fallback(av_frame_alloc());
    if (!packet || !decoded || !fallback) {
        return {.error = "cannot allocate decode buffers"};
    }

    bool foundFrame = false;
    bool haveFallback = false;
    const auto receiveUntilTarget = [&]() -> int {
        int receiveResult = 0;
        while ((receiveResult = avcodec_receive_frame(codecContextPointer, decoded.get())) >= 0) {
            const std::int64_t frameTimestamp = decoded->best_effort_timestamp;
            if (frameTimestamp == AV_NOPTS_VALUE || frameTimestamp >= targetTimestamp) {
                foundFrame = true;
                return 0;
            }
            av_frame_unref(fallback.get());
            av_frame_move_ref(fallback.get(), decoded.get());
            haveFallback = true;
        }
        return receiveResult;
    };

    while (!foundFrame) {
        if (!session->inputExhausted) {
            result = av_read_frame(formatPointer, packet.get());
            if (result == AVERROR_EOF) {
                session->inputExhausted = true;
            } else if (result < 0) {
                return {.error = "cannot read video: " + ffmpegError(result)};
            } else if (packet->stream_index != streamIndex) {
                av_packet_unref(packet.get());
                continue;
            }
        }

        if (session->inputExhausted) {
            result = avcodec_send_packet(codecContextPointer, nullptr);
        } else {
            result = avcodec_send_packet(codecContextPointer, packet.get());
            if (result == AVERROR(EAGAIN)) {
                const int drained = receiveUntilTarget();
                if (!foundFrame && drained < 0 && drained != AVERROR(EAGAIN) &&
                    drained != AVERROR_EOF) {
                    av_packet_unref(packet.get());
                    return {.error = "cannot decode video frame: " + ffmpegError(drained)};
                }
                if (!foundFrame) {
                    result = avcodec_send_packet(codecContextPointer, packet.get());
                }
            }
            av_packet_unref(packet.get());
            if (foundFrame) {
                break;
            }
        }
        if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            return {.error = "cannot submit video packet: " + ffmpegError(result)};
        }

        result = receiveUntilTarget();
        if (foundFrame || result == AVERROR_EOF) {
            break;
        }
        if (result < 0 && result != AVERROR(EAGAIN)) {
            return {.error = "cannot decode video frame: " + ffmpegError(result)};
        }
    }

    if (!foundFrame && haveFallback) {
        av_frame_unref(decoded.get());
        av_frame_move_ref(decoded.get(), fallback.get());
        foundFrame = true;
    }
    if (!foundFrame) {
        return {.error = "no video frame was decoded"};
    }
    session->lastDelivered = decoded->best_effort_timestamp == AV_NOPTS_VALUE
                                 ? targetTimestamp
                                 : decoded->best_effort_timestamp;

    const double scale = std::min(
        {1.0, static_cast<double>(maximumWidth) / static_cast<double>(decoded->width),
         static_cast<double>(maximumHeight) / static_cast<double>(decoded->height)});
    const int outputWidth = std::max(1, static_cast<int>(decoded->width * scale));
    const int outputHeight = std::max(1, static_cast<int>(decoded->height * scale));

    session->scaler = sws_getCachedContext(
        session->scaler, decoded->width, decoded->height,
        static_cast<AVPixelFormat>(decoded->format), outputWidth, outputHeight,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (session->scaler == nullptr) {
        return {.error = "cannot create video color converter"};
    }

    VideoFrame output{
        .width = outputWidth,
        .height = outputHeight,
        .timestampMicroseconds =
            decoded->best_effort_timestamp == AV_NOPTS_VALUE
                ? timestampMicroseconds
                : av_rescale_q(decoded->best_effort_timestamp - streamStart, stream->time_base,
                               AV_TIME_BASE_Q),
    };
    output.rgba.resize(static_cast<std::size_t>(outputWidth) *
                       static_cast<std::size_t>(outputHeight) * 4U);
    std::uint8_t* destinationData[4]{output.rgba.data(), nullptr, nullptr, nullptr};
    int destinationLinesize[4]{outputWidth * 4, 0, 0, 0};
    const int scaledRows =
        sws_scale(session->scaler, decoded->data, decoded->linesize, 0, decoded->height,
                  destinationData, destinationLinesize);
    if (scaledRows != outputHeight) {
        return {.error = "video color conversion was incomplete"};
    }

    if (!key.empty()) {
        storeDecoderSession(std::move(session));
    }
    return {.frame = std::move(output)};
}

} // namespace videx::media
