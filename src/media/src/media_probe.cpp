#include <videx/media/media_probe.hpp>

#include <memory>
#include <string>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace {

struct FormatContextDeleter final {
    void operator()(AVFormatContext* context) const noexcept {
        if (context == nullptr) {
            return;
        }
        avformat_close_input(&context);
    }
};

using FormatContext = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

std::string ffmpegError(const int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0) {
        return "unknown FFmpeg error";
    }
    return buffer;
}

videx::media::StreamKind toStreamKind(const AVMediaType type) noexcept {
    using videx::media::StreamKind;
    switch (type) {
    case AVMEDIA_TYPE_VIDEO:
        return StreamKind::Video;
    case AVMEDIA_TYPE_AUDIO:
        return StreamKind::Audio;
    case AVMEDIA_TYPE_SUBTITLE:
        return StreamKind::Subtitle;
    case AVMEDIA_TYPE_DATA:
        return StreamKind::Data;
    case AVMEDIA_TYPE_ATTACHMENT:
        return StreamKind::Attachment;
    default:
        return StreamKind::Unknown;
    }
}

} // namespace

namespace videx::media {

ProbeResult probeMedia(const std::filesystem::path& path) {
    AVFormatContext* rawContext = nullptr;
    const std::u8string encodedPath = path.u8string();
    const std::string utf8Path(reinterpret_cast<const char*>(encodedPath.data()),
                               encodedPath.size());
    const int openResult = avformat_open_input(&rawContext, utf8Path.c_str(), nullptr, nullptr);
    if (openResult < 0) {
        return {.error = "cannot open media: " + ffmpegError(openResult)};
    }

    FormatContext context(rawContext);
    const int streamResult = avformat_find_stream_info(context.get(), nullptr);
    if (streamResult < 0) {
        return {.error = "cannot read stream information: " + ffmpegError(streamResult)};
    }

    MediaInfo info;
    if (context->iformat != nullptr && context->iformat->name != nullptr) {
        info.formatName = context->iformat->name;
    }
    if (context->duration != AV_NOPTS_VALUE) {
        info.durationMicroseconds = context->duration;
    }

    info.streams.reserve(context->nb_streams);
    for (unsigned int streamIndex = 0; streamIndex < context->nb_streams; ++streamIndex) {
        const AVStream* stream = context->streams[streamIndex];
        const AVCodecParameters* parameters = stream->codecpar;

        StreamInfo streamInfo;
        streamInfo.index = static_cast<std::int32_t>(streamIndex);
        streamInfo.kind = toStreamKind(parameters->codec_type);
        streamInfo.codecName = avcodec_get_name(parameters->codec_id);
        streamInfo.timeBase = {
            .numerator = stream->time_base.num,
            .denominator = stream->time_base.den,
        };
        if (stream->duration != AV_NOPTS_VALUE) {
            streamInfo.durationTicks = stream->duration;
        }
        streamInfo.width = parameters->width;
        streamInfo.height = parameters->height;
        streamInfo.sampleRate = parameters->sample_rate;
        streamInfo.channelCount = parameters->ch_layout.nb_channels;

        info.streams.push_back(std::move(streamInfo));
    }

    return {.media = std::move(info)};
}

const char* streamKindName(const StreamKind kind) noexcept {
    switch (kind) {
    case StreamKind::Video:
        return "video";
    case StreamKind::Audio:
        return "audio";
    case StreamKind::Subtitle:
        return "subtitle";
    case StreamKind::Data:
        return "data";
    case StreamKind::Attachment:
        return "attachment";
    case StreamKind::Unknown:
    default:
        return "unknown";
    }
}

} // namespace videx::media
