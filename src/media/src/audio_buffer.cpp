#include <videx/media/audio_buffer.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

namespace {

constexpr int outputSampleRate = 48'000;
constexpr int outputChannels = 2;
constexpr std::int64_t maximumDecodeDuration = 30'000'000;

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
struct ResampleContextDeleter final {
    void operator()(SwrContext* context) const noexcept {
        swr_free(&context);
    }
};

using FormatContext = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContext = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
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

} // namespace

namespace videx::media {

AudioBufferResult decodeAudio(const std::filesystem::path& path,
                              const std::int64_t timestampMicroseconds,
                              const std::int64_t durationMicroseconds) {
    if (timestampMicroseconds < 0 || durationMicroseconds <= 0 ||
        durationMicroseconds > maximumDecodeDuration) {
        return {.error = "audio request is invalid"};
    }

    AVFormatContext* rawFormat = nullptr;
    const std::string pathText = utf8Path(path);
    int result = avformat_open_input(&rawFormat, pathText.c_str(), nullptr, nullptr);
    if (result < 0) {
        return {.error = "cannot open media: " + ffmpegError(result)};
    }
    FormatContext format(rawFormat);
    result = avformat_find_stream_info(format.get(), nullptr);
    if (result < 0) {
        return {.error = "cannot read stream information: " + ffmpegError(result)};
    }

    const int streamIndex =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        return {.error = "media has no decodable audio stream"};
    }
    AVStream* stream = format->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        return {.error = "audio decoder is unavailable"};
    }

    CodecContext codecContext(avcodec_alloc_context3(codec));
    if (!codecContext) {
        return {.error = "cannot allocate audio decoder"};
    }
    result = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
    if (result < 0 || (result = avcodec_open2(codecContext.get(), codec, nullptr)) < 0) {
        return {.error = "cannot open audio decoder: " + ffmpegError(result)};
    }

    AVChannelLayout outputLayout = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext* rawResampler = nullptr;
    result = swr_alloc_set_opts2(&rawResampler, &outputLayout, AV_SAMPLE_FMT_FLT,
                                 outputSampleRate, &codecContext->ch_layout,
                                 codecContext->sample_fmt, codecContext->sample_rate, 0, nullptr);
    ResampleContext resampler(rawResampler);
    if (result < 0 || !resampler || (result = swr_init(resampler.get())) < 0) {
        return {.error = "cannot initialize audio resampler: " + ffmpegError(result)};
    }

    const std::int64_t streamStart =
        stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const std::int64_t targetTimestamp =
        av_rescale_q(timestampMicroseconds, AV_TIME_BASE_Q, stream->time_base) + streamStart;
    if (timestampMicroseconds > 0) {
        result = av_seek_frame(format.get(), streamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            return {.error = "cannot seek audio: " + ffmpegError(result)};
        }
        avcodec_flush_buffers(codecContext.get());
    }

    const std::int64_t wantedFrames =
        av_rescale(durationMicroseconds, outputSampleRate, AV_TIME_BASE);
    AudioBuffer output{
        .sampleRate = outputSampleRate,
        .channelCount = outputChannels,
        .timestampMicroseconds = timestampMicroseconds,
    };
    output.interleavedSamples.reserve(
        static_cast<std::size_t>(wantedFrames) * outputChannels);

    Packet packet(av_packet_alloc());
    Frame decoded(av_frame_alloc());
    if (!packet || !decoded) {
        return {.error = "cannot allocate audio decode buffers"};
    }

    std::int64_t writtenFrames = 0;
    std::string decodeError;
    const auto receiveFrames = [&]() -> int {
        int receiveResult = 0;
        while (writtenFrames < wantedFrames &&
               (receiveResult = avcodec_receive_frame(codecContext.get(), decoded.get())) >= 0) {
            const int outputCapacity = static_cast<int>(av_rescale_rnd(
                swr_get_delay(resampler.get(), codecContext->sample_rate) + decoded->nb_samples,
                outputSampleRate, codecContext->sample_rate, AV_ROUND_UP));
            std::vector<float> converted(static_cast<std::size_t>(outputCapacity) * outputChannels);
            std::uint8_t* destination = reinterpret_cast<std::uint8_t*>(converted.data());
            const int convertedFrames =
                swr_convert(resampler.get(), &destination, outputCapacity,
                            const_cast<const std::uint8_t**>(decoded->extended_data),
                            decoded->nb_samples);
            if (convertedFrames < 0) {
                decodeError = "cannot resample audio: " + ffmpegError(convertedFrames);
                av_frame_unref(decoded.get());
                return convertedFrames;
            }

            std::int64_t skipFrames = 0;
            if (decoded->best_effort_timestamp != AV_NOPTS_VALUE) {
                const std::int64_t frameStart =
                    av_rescale_q(decoded->best_effort_timestamp - streamStart,
                                 stream->time_base, AV_TIME_BASE_Q);
                if (frameStart < timestampMicroseconds) {
                    skipFrames = std::min<std::int64_t>(
                        convertedFrames,
                        av_rescale(timestampMicroseconds - frameStart, outputSampleRate,
                                   AV_TIME_BASE));
                }
            }
            const std::int64_t availableFrames = convertedFrames - skipFrames;
            const std::int64_t takeFrames =
                std::min(availableFrames, wantedFrames - writtenFrames);
            const auto begin = converted.begin() + skipFrames * outputChannels;
            output.interleavedSamples.insert(output.interleavedSamples.end(), begin,
                                              begin + takeFrames * outputChannels);
            writtenFrames += takeFrames;
            av_frame_unref(decoded.get());
        }
        return receiveResult;
    };

    bool inputExhausted = false;
    bool decoderDrained = false;
    while (writtenFrames < wantedFrames && !decoderDrained) {
        if (!inputExhausted) {
            result = av_read_frame(format.get(), packet.get());
            if (result == AVERROR_EOF) {
                inputExhausted = true;
            } else if (result < 0) {
                return {.error = "cannot read audio: " + ffmpegError(result)};
            } else if (packet->stream_index != streamIndex) {
                av_packet_unref(packet.get());
                continue;
            }
        }

        if (inputExhausted) {
            result = avcodec_send_packet(codecContext.get(), nullptr);
        } else {
            result = avcodec_send_packet(codecContext.get(), packet.get());
            if (result == AVERROR(EAGAIN)) {
                const int drained = receiveFrames();
                if (drained < 0 && drained != AVERROR(EAGAIN) && drained != AVERROR_EOF) {
                    av_packet_unref(packet.get());
                    return {.error = decodeError.empty()
                                         ? "cannot decode audio: " + ffmpegError(drained)
                                         : decodeError};
                }
                result = avcodec_send_packet(codecContext.get(), packet.get());
            }
            av_packet_unref(packet.get());
        }
        if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            return {.error = "cannot submit audio packet: " + ffmpegError(result)};
        }

        result = receiveFrames();
        if (result == AVERROR_EOF) {
            decoderDrained = true;
        } else if (result < 0 && result != AVERROR(EAGAIN)) {
            return {.error = decodeError.empty()
                                 ? "cannot decode audio: " + ffmpegError(result)
                                 : decodeError};
        }
    }

    while (writtenFrames < wantedFrames) {
        const std::int64_t delay = swr_get_delay(resampler.get(), outputSampleRate);
        if (delay <= 0) {
            break;
        }
        const int outputCapacity =
            static_cast<int>(std::min<std::int64_t>(delay, wantedFrames - writtenFrames));
        std::vector<float> converted(static_cast<std::size_t>(outputCapacity) * outputChannels);
        std::uint8_t* destination = reinterpret_cast<std::uint8_t*>(converted.data());
        const int convertedFrames =
            swr_convert(resampler.get(), &destination, outputCapacity, nullptr, 0);
        if (convertedFrames <= 0) {
            break;
        }
        const std::int64_t takeFrames =
            std::min<std::int64_t>(convertedFrames, wantedFrames - writtenFrames);
        output.interleavedSamples.insert(output.interleavedSamples.end(), converted.begin(),
                                          converted.begin() + takeFrames * outputChannels);
        writtenFrames += takeFrames;
    }

    if (output.interleavedSamples.empty()) {
        return {.error = "no audio samples were decoded"};
    }
    return {.buffer = std::move(output)};
}

} // namespace videx::media
