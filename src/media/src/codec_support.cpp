#include <videx/media/codec_support.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace videx::media {

std::vector<std::string> availableExportEncoders() {
    std::vector<std::string> encoders;
    void* iterator = nullptr;
    while (const AVCodec* codec = av_codec_iterate(&iterator)) {
        if (av_codec_is_encoder(codec) == 0) {
            continue;
        }
        if (codec->id == AV_CODEC_ID_H264 || codec->id == AV_CODEC_ID_AAC) {
            encoders.emplace_back(codec->name);
        }
    }
    return encoders;
}

} // namespace videx::media
