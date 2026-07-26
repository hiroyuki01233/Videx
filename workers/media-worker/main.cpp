#include <videx/core/application_info.hpp>
#include <videx/media/audio_buffer.hpp>
#include <videx/media/codec_support.hpp>
#include <videx/media/media_probe.hpp>
#include <videx/media/timeline_export.hpp>
#include <videx/media/waveform.hpp>
#include <videx/media/video_frame.hpp>

#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

std::string escapeJson(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

void printMediaInfo(const videx::media::MediaInfo& media) {
    std::cout << "{\"format\":\"" << escapeJson(media.formatName) << "\","
              << "\"duration_us\":" << media.durationMicroseconds << ",\"streams\":[";

    bool first = true;
    for (const auto& stream : media.streams) {
        if (!first) {
            std::cout << ',';
        }
        first = false;

        std::cout << "{\"index\":" << stream.index << ",\"kind\":\""
                  << videx::media::streamKindName(stream.kind) << "\",\"codec\":\""
                  << escapeJson(stream.codecName) << "\",\"time_base\":["
                  << stream.timeBase.numerator << ',' << stream.timeBase.denominator << ']'
                  << ",\"duration_ticks\":" << stream.durationTicks << ",\"width\":" << stream.width
                  << ",\"height\":" << stream.height << ",\"sample_rate\":" << stream.sampleRate
                  << ",\"channels\":" << stream.channelCount << '}';
    }
    std::cout << "]}\n";
}

template <typename Integer> void writeLittleEndian(const Integer value) {
    std::array<char, sizeof(Integer)> bytes{};
    using Unsigned = std::make_unsigned_t<Integer>;
    const Unsigned unsignedValue = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<char>((unsignedValue >> (index * 8U)) & 0xFFU);
    }
    std::cout.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool printVideoFrame(const std::filesystem::path& path, const std::string& timestampText) {
    std::int64_t timestamp = 0;
    const auto parseResult = std::from_chars(timestampText.data(),
                                             timestampText.data() + timestampText.size(), timestamp);
    if (parseResult.ec != std::errc{} ||
        parseResult.ptr != timestampText.data() + timestampText.size()) {
        std::cerr << "frame timestamp is invalid\n";
        return false;
    }

    const auto result = videx::media::decodeVideoFrame(path, timestamp);
    if (!result.succeeded()) {
        std::cerr << result.error << '\n';
        return false;
    }

#if defined(_WIN32)
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    std::cout.write("VXF1", 4);
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(result.frame.width));
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(result.frame.height));
    writeLittleEndian<std::int64_t>(result.frame.timestampMicroseconds);
    writeLittleEndian<std::uint64_t>(static_cast<std::uint64_t>(result.frame.rgba.size()));
    std::cout.write(reinterpret_cast<const char*>(result.frame.rgba.data()),
                    static_cast<std::streamsize>(result.frame.rgba.size()));
    return std::cout.good();
}

bool printBlendedVideoFrame(const std::filesystem::path& outgoingPath,
                            const std::string& outgoingTimestampText,
                            const std::filesystem::path& incomingPath,
                            const std::string& incomingTimestampText,
                            const std::string& blendText) {
    std::int64_t outgoingTimestamp = 0;
    std::int64_t incomingTimestamp = 0;
    double blend = 0.0;
    const auto parse = []<typename Value>(const std::string& text, Value& value) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    if (!parse(outgoingTimestampText, outgoingTimestamp) ||
        !parse(incomingTimestampText, incomingTimestamp) || !parse(blendText, blend) ||
        blend < 0.0 || blend > 1.0) {
        std::cerr << "blend-frame arguments are invalid\n";
        return false;
    }
    const auto outgoing = videx::media::decodeVideoFrame(outgoingPath, outgoingTimestamp);
    if (!outgoing.succeeded()) {
        std::cerr << outgoing.error << '\n';
        return false;
    }
    const auto incoming = videx::media::decodeVideoFrame(
        incomingPath, incomingTimestamp, outgoing.frame.width, outgoing.frame.height);
    if (!incoming.succeeded()) {
        std::cerr << incoming.error << '\n';
        return false;
    }
    std::vector<std::uint8_t> incomingCanvas(outgoing.frame.rgba.size(), 0U);
    if (incoming.frame.rgba.size() == outgoing.frame.rgba.size() &&
        incoming.frame.width == outgoing.frame.width) {
        incomingCanvas = incoming.frame.rgba;
    } else {
        const int offsetX = std::max(0, (outgoing.frame.width - incoming.frame.width) / 2);
        const int offsetY = std::max(0, (outgoing.frame.height - incoming.frame.height) / 2);
        const int copyWidth = std::min(incoming.frame.width, outgoing.frame.width);
        const int copyHeight = std::min(incoming.frame.height, outgoing.frame.height);
        for (int row = 0; row < copyHeight; ++row) {
            const std::size_t from = static_cast<std::size_t>(row) *
                                     static_cast<std::size_t>(incoming.frame.width) * 4U;
            const std::size_t to = (static_cast<std::size_t>(row + offsetY) *
                                        static_cast<std::size_t>(outgoing.frame.width) +
                                    static_cast<std::size_t>(offsetX)) *
                                   4U;
            std::copy_n(incoming.frame.rgba.begin() + static_cast<std::ptrdiff_t>(from),
                        static_cast<std::size_t>(copyWidth) * 4U,
                        incomingCanvas.begin() + static_cast<std::ptrdiff_t>(to));
        }
    }
    std::vector<std::uint8_t> rgba(outgoing.frame.rgba.size());
    for (std::size_t index = 0; index < rgba.size(); ++index) {
        rgba[index] = static_cast<std::uint8_t>(std::lround(
            outgoing.frame.rgba[index] * (1.0 - blend) + incomingCanvas[index] * blend));
    }
#if defined(_WIN32)
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    std::cout.write("VXF1", 4);
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(outgoing.frame.width));
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(outgoing.frame.height));
    writeLittleEndian<std::int64_t>(incoming.frame.timestampMicroseconds);
    writeLittleEndian<std::uint64_t>(static_cast<std::uint64_t>(rgba.size()));
    std::cout.write(reinterpret_cast<const char*>(rgba.data()),
                    static_cast<std::streamsize>(rgba.size()));
    return std::cout.good();
}

bool printAudio(const std::filesystem::path& path, const std::string& timestampText,
                const std::string& durationText) {
    std::int64_t timestamp = 0;
    std::int64_t duration = 0;
    const auto timestampResult = std::from_chars(
        timestampText.data(), timestampText.data() + timestampText.size(), timestamp);
    const auto durationResult =
        std::from_chars(durationText.data(), durationText.data() + durationText.size(), duration);
    if (timestampResult.ec != std::errc{} || durationResult.ec != std::errc{} ||
        timestampResult.ptr != timestampText.data() + timestampText.size() ||
        durationResult.ptr != durationText.data() + durationText.size()) {
        std::cerr << "audio range is invalid\n";
        return false;
    }

    const auto result = videx::media::decodeAudio(path, timestamp, duration);
    if (!result.succeeded()) {
        std::cerr << result.error << '\n';
        return false;
    }

#if defined(_WIN32)
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    const std::uint64_t payloadSize =
        static_cast<std::uint64_t>(result.buffer.interleavedSamples.size() * sizeof(float));
    std::cout.write("VXA1", 4);
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(result.buffer.sampleRate));
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(result.buffer.channelCount));
    writeLittleEndian<std::int64_t>(result.buffer.timestampMicroseconds);
    writeLittleEndian<std::uint64_t>(payloadSize);
    std::cout.write(reinterpret_cast<const char*>(result.buffer.interleavedSamples.data()),
                    static_cast<std::streamsize>(payloadSize));
    return std::cout.good();
}

template <typename Integer> bool parseInteger(const std::string_view text, Integer& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parseEffects(const std::string_view text, std::vector<videx::media::ExportEffect>& effects) {
    if (text == "-" || text.empty()) {
        return true;
    }
    std::size_t effectStart = 0;
    while (effectStart <= text.size()) {
        const std::size_t effectEnd = text.find('|', effectStart);
        const std::string_view entry = text.substr(
            effectStart, effectEnd == std::string_view::npos ? text.size() - effectStart
                                                              : effectEnd - effectStart);
        std::array<std::size_t, 3> commas{};
        std::size_t search = 0;
        for (std::size_t index = 0; index < commas.size(); ++index) {
            commas[index] = entry.find(',', search);
            if (commas[index] == std::string_view::npos) {
                return false;
            }
            search = commas[index] + 1U;
        }
        int type = -1;
        int enabled = 0;
        videx::media::ExportEffect effect;
        if (!parseInteger(entry.substr(0, commas[0]), type) || type < 0 || type > 4 ||
            !parseInteger(entry.substr(commas[0] + 1U, commas[1] - commas[0] - 1U), enabled) ||
            (enabled != 0 && enabled != 1) ||
            !parseInteger(entry.substr(commas[1] + 1U, commas[2] - commas[1] - 1U),
                          effect.amount)) {
            return false;
        }
        effect.type = static_cast<videx::media::ExportEffectType>(type);
        effect.enabled = enabled != 0;
        const std::string_view keyframes = entry.substr(commas[2] + 1U);
        if (keyframes != "-") {
            std::size_t keyStart = 0;
            while (keyStart <= keyframes.size()) {
                const std::size_t keyEnd = keyframes.find(';', keyStart);
                const std::string_view key = keyframes.substr(
                    keyStart, keyEnd == std::string_view::npos ? keyframes.size() - keyStart
                                                                : keyEnd - keyStart);
                const std::size_t firstColon = key.find(':');
                const std::size_t secondColon =
                    firstColon == std::string_view::npos ? std::string_view::npos
                                                         : key.find(':', firstColon + 1U);
                int interpolation = 0;
                videx::media::ExportEffectKeyframe keyframe;
                if (firstColon == std::string_view::npos ||
                    secondColon == std::string_view::npos ||
                    !parseInteger(key.substr(0, firstColon), keyframe.frameOffset) ||
                    !parseInteger(key.substr(firstColon + 1U,
                                             secondColon - firstColon - 1U), keyframe.value) ||
                    !parseInteger(key.substr(secondColon + 1U), interpolation) ||
                    interpolation < 0 ||
                    interpolation >
                        static_cast<int>(videx::media::ExportInterpolation::EaseInOut)) {
                    return false;
                }
                keyframe.interpolation =
                    static_cast<videx::media::ExportInterpolation>(interpolation);
                effect.keyframes.push_back(keyframe);
                if (keyEnd == std::string_view::npos) {
                    break;
                }
                keyStart = keyEnd + 1U;
            }
        }
        effects.push_back(std::move(effect));
        if (effectEnd == std::string_view::npos) {
            break;
        }
        effectStart = effectEnd + 1U;
    }
    return true;
}

bool parseSpeedKeyframes(
    const std::string_view text,
    std::vector<videx::media::ExportSpeedKeyframe>& keyframes) {
    if (text == "-") return true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string_view entry = text.substr(
            start, end == std::string_view::npos ? text.size() - start : end - start);
        const std::size_t first = entry.find(':');
        const std::size_t second = first == std::string_view::npos
                                       ? std::string_view::npos : entry.find(':', first + 1U);
        int interpolation = 0;
        videx::media::ExportSpeedKeyframe keyframe;
        if (first == std::string_view::npos || second == std::string_view::npos ||
            !parseInteger(entry.substr(0, first), keyframe.frameOffset) ||
            !parseInteger(entry.substr(first + 1U, second - first - 1U), keyframe.rate) ||
            !parseInteger(entry.substr(second + 1U), interpolation) || interpolation < 0 ||
            interpolation > static_cast<int>(videx::media::ExportInterpolation::EaseInOut)) {
            return false;
        }
        keyframe.interpolation =
            static_cast<videx::media::ExportInterpolation>(interpolation);
        keyframes.push_back(keyframe);
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return true;
}

bool parseMotionKeyframes(
    const std::string_view text,
    std::vector<videx::media::ExportMotionKeyframe>& keyframes) {
    if (text == "-" || text.empty()) return true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string_view entry = text.substr(
            start, end == std::string_view::npos ? text.size() - start : end - start);
        std::array<std::size_t, 9> commas{};
        std::size_t search = 0;
        for (std::size_t index = 0; index < commas.size(); ++index) {
            commas[index] = entry.find(',', search);
            if (commas[index] == std::string_view::npos) return false;
            search = commas[index] + 1U;
        }
        const auto part = [entry, &commas](const std::size_t index) {
            const std::size_t first = index == 0 ? 0 : commas[index - 1U] + 1U;
            const std::size_t last = index < commas.size() ? commas[index] : entry.size();
            return entry.substr(first, last - first);
        };
        int interpolation = 0;
        videx::media::ExportMotionKeyframe keyframe;
        if (!parseInteger(part(0), keyframe.frameOffset) ||
            !parseInteger(part(1), keyframe.opacity) ||
            !parseInteger(part(2), keyframe.positionX) ||
            !parseInteger(part(3), keyframe.positionY) ||
            !parseInteger(part(4), keyframe.scaleX) ||
            !parseInteger(part(5), keyframe.scaleY) ||
            !parseInteger(part(6), keyframe.rotationDegrees) ||
            !parseInteger(part(7), keyframe.anchorX) ||
            !parseInteger(part(8), keyframe.anchorY) ||
            !parseInteger(part(9), interpolation) || interpolation < 0 ||
            interpolation >
                static_cast<int>(videx::media::ExportInterpolation::EaseInOut)) {
            return false;
        }
        keyframe.interpolation =
            static_cast<videx::media::ExportInterpolation>(interpolation);
        keyframes.push_back(keyframe);
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return true;
}

bool parseGainKeyframes(
    const std::string_view text,
    std::vector<videx::media::ExportGainKeyframe>& keyframes) {
    if (text == "-" || text.empty()) return true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string_view entry = text.substr(
            start, end == std::string_view::npos ? text.size() - start : end - start);
        const std::size_t first = entry.find(',');
        const std::size_t second = first == std::string_view::npos
                                       ? std::string_view::npos : entry.find(',', first + 1U);
        int interpolation = 0;
        videx::media::ExportGainKeyframe keyframe;
        if (first == std::string_view::npos || second == std::string_view::npos ||
            !parseInteger(entry.substr(0, first), keyframe.frameOffset) ||
            !parseInteger(entry.substr(first + 1U, second - first - 1U), keyframe.gainDb) ||
            !parseInteger(entry.substr(second + 1U), interpolation) ||
            interpolation < 0 ||
            interpolation >
                static_cast<int>(videx::media::ExportInterpolation::EaseInOut)) return false;
        keyframe.interpolation =
            static_cast<videx::media::ExportInterpolation>(interpolation);
        keyframes.push_back(keyframe);
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return true;
}

double rampSourceOffset(const std::vector<videx::media::ExportSpeedKeyframe>& keys,
                        const double baseRate, const double frame) {
    if (frame <= 0.0) return 0.0;
    if (keys.empty()) return frame * baseRate;
    double result = std::min(frame, static_cast<double>(keys.front().frameOffset)) *
                    keys.front().rate;
    for (std::size_t index = 0; index + 1U < keys.size(); ++index) {
        const auto& previous = keys[index];
        const auto& next = keys[index + 1U];
        if (frame <= previous.frameOffset) break;
        const double segment = static_cast<double>(next.frameOffset - previous.frameOffset);
        const double ratio = std::clamp((frame - previous.frameOffset) / segment, 0.0, 1.0);
        const double curveIntegral =
            videx::media::exportInterpolationIntegral(previous.interpolation, ratio);
        result += segment * (previous.rate * ratio +
            (next.rate - previous.rate) * curveIntegral);
    }
    const double lastFrame = static_cast<double>(keys.back().frameOffset);
    if (frame > lastFrame) result += (frame - lastFrame) * keys.back().rate;
    return result;
}

bool printRampAudio(const std::filesystem::path& path, const std::filesystem::path* arguments) {
    std::int64_t sourceStart = 0;
    std::int64_t localStart = 0;
    std::int64_t outputFrames = 0;
    std::int32_t fpsNumerator = 0;
    std::int32_t fpsDenominator = 0;
    double baseRate = 1.0;
    std::vector<videx::media::ExportSpeedKeyframe> keys;
    if (!parseInteger(arguments[3].string(), sourceStart) ||
        !parseInteger(arguments[4].string(), localStart) ||
        !parseInteger(arguments[5].string(), outputFrames) ||
        !parseInteger(arguments[6].string(), fpsNumerator) ||
        !parseInteger(arguments[7].string(), fpsDenominator) ||
        !parseInteger(arguments[8].string(), baseRate) ||
        !parseSpeedKeyframes(arguments[9].string(), keys) || sourceStart < 0 || localStart < 0 ||
        outputFrames <= 0 || fpsNumerator <= 0 || fpsDenominator <= 0 || baseRate <= 0.0) {
        std::cerr << "audio ramp request is invalid\n";
        return false;
    }
    const double fps = static_cast<double>(fpsNumerator) / fpsDenominator;
    const double firstOffset = rampSourceOffset(keys, baseRate, static_cast<double>(localStart));
    const double lastOffset = rampSourceOffset(
        keys, baseRate, static_cast<double>(localStart + outputFrames));
    const std::int64_t timestamp = static_cast<std::int64_t>(
        std::llround((sourceStart + firstOffset) * 1'000'000.0 / fps));
    const std::int64_t duration = std::max<std::int64_t>(1,
        static_cast<std::int64_t>(std::ceil((lastOffset - firstOffset + 2.0) * 1'000'000.0 / fps)));
    const auto decoded = videx::media::decodeAudio(path, timestamp, duration);
    if (!decoded.succeeded()) { std::cerr << decoded.error << '\n'; return false; }
    const std::size_t outputSamples = static_cast<std::size_t>(
        std::ceil(outputFrames * decoded.buffer.sampleRate / fps));
    std::vector<float> samples(outputSamples * decoded.buffer.channelCount, 0.0F);
    const std::size_t inputSamples = decoded.buffer.interleavedSamples.size() /
                                     decoded.buffer.channelCount;
    for (std::size_t sample = 0; sample < outputSamples; ++sample) {
        const double frame = localStart + sample * fps / decoded.buffer.sampleRate;
        const double input = std::max(
            0.0, (rampSourceOffset(keys, baseRate, frame) - firstOffset) *
                     decoded.buffer.sampleRate / fps);
        const auto left = static_cast<std::size_t>(std::floor(input));
        if (left >= inputSamples) break;
        const std::size_t right = std::min(left + 1U, inputSamples - 1U);
        const float mix = static_cast<float>(input - std::floor(input));
        for (std::int32_t channel = 0; channel < decoded.buffer.channelCount; ++channel)
            samples[sample * decoded.buffer.channelCount + channel] =
                decoded.buffer.interleavedSamples[left * decoded.buffer.channelCount + channel] *
                    (1.0F - mix) +
                decoded.buffer.interleavedSamples[right * decoded.buffer.channelCount + channel] * mix;
    }
#if defined(_WIN32)
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    std::cout.write("VXA1", 4);
    writeLittleEndian<std::uint32_t>(decoded.buffer.sampleRate);
    writeLittleEndian<std::uint32_t>(decoded.buffer.channelCount);
    writeLittleEndian<std::int64_t>(timestamp);
    writeLittleEndian<std::uint64_t>(samples.size() * sizeof(float));
    std::cout.write(reinterpret_cast<const char*>(samples.data()),
                    static_cast<std::streamsize>(samples.size() * sizeof(float)));
    return std::cout.good();
}

bool parseManifestLine(const std::string& line,
                       videx::media::TimelineExportRequest& request);
bool finishExportRequest(const std::filesystem::path* arguments, bool frameOnly,
                         bool audioOnly, videx::media::TimelineExportRequest& request);

bool writeVideoFrameBlob(const videx::media::VideoFrame& frame) {
    std::cout.write("VXF1", 4);
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(frame.width));
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(frame.height));
    writeLittleEndian<std::int64_t>(frame.timestampMicroseconds);
    writeLittleEndian<std::uint64_t>(frame.rgba.size());
    std::cout.write(reinterpret_cast<const char*>(frame.rgba.data()),
                    static_cast<std::streamsize>(frame.rgba.size()));
    return std::cout.good();
}

// Long-lived preview server: the host writes one "REQ <frame> <fps-num>
// <fps-den> <width> <height> <duration-frames> <manifest-lines>" line plus
// that many manifest lines per request, and reads back a VXF1 frame blob (or
// VXE1 + u32 length + message on failure). Keeping one process alive lets the
// decoder sessions persist, which is what makes playback smooth.
int serveTimelineFrames() {
#if defined(_WIN32)
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    const auto writeErrorBlob = [](const std::string& message) {
        std::cout.write("VXE1", 4);
        writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(message.size()));
        std::cout.write(message.data(),
                        static_cast<std::streamsize>(message.size()));
        std::cout.flush();
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::int64_t frame = 0;
        std::int64_t lineCount = 0;
        videx::media::TimelineExportRequest request{};
        {
            std::istringstream header(line);
            std::string tag;
            std::int64_t fpsNumerator = 0;
            std::int64_t fpsDenominator = 0;
            std::int64_t width = 0;
            std::int64_t height = 0;
            std::int64_t durationFrames = 0;
            header >> tag >> frame >> fpsNumerator >> fpsDenominator >> width >>
                height >> durationFrames >> lineCount;
            if (!header || tag != "REQ" || fpsNumerator <= 0 || fpsDenominator <= 0 ||
                width <= 0 || height <= 0 || durationFrames <= 0 || lineCount < 0 ||
                lineCount > 100'000) {
                writeErrorBlob("frame request header is invalid");
                continue;
            }
            request.frameRateNumerator = static_cast<std::int32_t>(fpsNumerator);
            request.frameRateDenominator = static_cast<std::int32_t>(fpsDenominator);
            request.width = static_cast<std::int32_t>(width);
            request.height = static_cast<std::int32_t>(height);
            request.durationFrames = durationFrames;
        }
        bool manifestValid = true;
        for (std::int64_t index = 0; index < lineCount; ++index) {
            std::string manifestLine;
            if (!std::getline(std::cin, manifestLine)) {
                return 1;
            }
            if (!parseManifestLine(manifestLine, request)) {
                manifestValid = false;
            }
        }
        if (!manifestValid) {
            writeErrorBlob("frame manifest is invalid");
            continue;
        }
        const auto result = videx::media::renderTimelineFrame(request, frame);
        if (!result.succeeded()) {
            writeErrorBlob(result.error);
            continue;
        }
        if (!writeVideoFrameBlob(result.frame)) {
            return 1;
        }
        std::cout.flush();
    }
    return 0;
}

bool exportProject(const std::filesystem::path* arguments, const bool frameOnly = false,
                   const bool audioOnly = false, const std::filesystem::path* bitrateArg = nullptr) {
    videx::media::TimelineExportRequest request{.outputPath = arguments[2]};
    if (!parseInteger(arguments[3].string(), request.frameRateNumerator) ||
        !parseInteger(arguments[4].string(), request.frameRateDenominator) ||
        !parseInteger(arguments[5].string(), request.width) ||
        !parseInteger(arguments[6].string(), request.height) ||
        !parseInteger(arguments[7].string(), request.durationFrames)) {
        std::cerr << "export settings are invalid\n";
        return false;
    }
    if (bitrateArg != nullptr && !bitrateArg->empty()) {
        std::int64_t bitrate = 0;
        if (parseInteger(bitrateArg->string(), bitrate) && bitrate > 0) {
            request.videoBitrate = bitrate;
        }
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!parseManifestLine(line, request)) {
            return false;
        }
    }
    return finishExportRequest(arguments, frameOnly, audioOnly, request);
}

bool parseManifestLine(const std::string& line,
                       videx::media::TimelineExportRequest& request) {
        if (line.starts_with("C\t")) {
            const std::size_t first = line.find('\t');
            const std::size_t second = line.find('\t', first + 1U);
            const std::size_t third = line.find('\t', second + 1U);
            videx::media::ExportOverlay overlay;
            if (second == std::string::npos || third == std::string::npos ||
                !parseInteger(std::string_view(line).substr(first + 1U, second - first - 1U),
                              overlay.timelineStart) ||
                !parseInteger(std::string_view(line).substr(second + 1U, third - second - 1U),
                              overlay.duration)) {
                std::cerr << "export overlay manifest is invalid\n";
                return false;
            }
            const std::string pathText = line.substr(third + 1U);
            const std::u8string pathUtf8(reinterpret_cast<const char8_t*>(pathText.data()),
                                         pathText.size());
            overlay.path = std::filesystem::path(pathUtf8);
            request.overlays.push_back(std::move(overlay));
            return true;
        }
        std::array<std::size_t, 34> separators{};
        std::size_t searchFrom = 0;
        bool valid = true;
        for (std::size_t index = 0; index < separators.size(); ++index) {
            separators[index] = line.find('\t', searchFrom);
            if (separators[index] == std::string::npos) {
                valid = false;
                break;
            }
            searchFrom = separators[index] + 1U;
        }
        videx::media::ExportClip clip;
        if (!valid || (line[0] != 'V' && line[0] != 'A') || separators[0] != 1U ||
            !parseInteger(std::string_view(line).substr(separators[0] + 1U,
                                                        separators[1] - separators[0] - 1U),
                          clip.timelineStart) ||
            !parseInteger(std::string_view(line).substr(separators[1] + 1U,
                                                        separators[2] - separators[1] - 1U),
                          clip.duration) ||
            !parseInteger(std::string_view(line).substr(separators[2] + 1U,
                                                        separators[3] - separators[2] - 1U),
                          clip.sourceStart) ||
            !parseInteger(std::string_view(line).substr(separators[3] + 1U,
                                                        separators[4] - separators[3] - 1U),
                          clip.opacity) ||
            !parseInteger(std::string_view(line).substr(separators[4] + 1U,
                                                        separators[5] - separators[4] - 1U),
                          clip.audioGainDb) ||
            !parseInteger(std::string_view(line).substr(separators[5] + 1U,
                                                        separators[6] - separators[5] - 1U),
                          clip.playbackRate)) {
            std::cerr << "export clip manifest is invalid\n";
            return false;
        }
        if (!parseInteger(std::string_view(line).substr(separators[6] + 1U,
                                                         separators[7] - separators[6] - 1U),
                          clip.fadeInFrames) ||
            !parseInteger(std::string_view(line).substr(separators[7] + 1U,
                                                         separators[8] - separators[7] - 1U),
                          clip.fadeOutFrames)) {
            std::cerr << "export clip manifest is invalid\n";
            return false;
        }
        if (!parseInteger(std::string_view(line).substr(separators[8] + 1U,
                                                         separators[9] - separators[8] - 1U),
                          clip.videoTransitionInFrames) ||
            !parseInteger(std::string_view(line).substr(separators[9] + 1U,
                                                         separators[10] - separators[9] - 1U),
                          clip.audioTransitionInFrames)) {
            std::cerr << "export clip transitions are invalid\n";
            return false;
        }
        if (!parseInteger(std::string_view(line).substr(separators[10] + 1U,
                                                         separators[11] - separators[10] - 1U),
                          clip.positionX) ||
            !parseInteger(std::string_view(line).substr(separators[11] + 1U,
                                                         separators[12] - separators[11] - 1U),
                          clip.positionY) ||
            !parseInteger(std::string_view(line).substr(separators[12] + 1U,
                                                         separators[13] - separators[12] - 1U),
                          clip.scaleX) ||
            !parseInteger(std::string_view(line).substr(separators[13] + 1U,
                                                         separators[14] - separators[13] - 1U),
                          clip.scaleY) ||
            !parseInteger(std::string_view(line).substr(separators[14] + 1U,
                                                         separators[15] - separators[14] - 1U),
                          clip.rotationDegrees) ||
            !parseInteger(std::string_view(line).substr(separators[15] + 1U,
                                                         separators[16] - separators[15] - 1U),
                          clip.anchorX) ||
            !parseInteger(std::string_view(line).substr(separators[16] + 1U,
                                                         separators[17] - separators[16] - 1U),
                          clip.anchorY)) {
            std::cerr << "export clip transform is invalid\n";
            return false;
        }
        if (!parseInteger(std::string_view(line).substr(separators[17] + 1U,
                                                         separators[18] - separators[17] - 1U),
                          clip.cropLeft) ||
            !parseInteger(std::string_view(line).substr(separators[18] + 1U,
                                                         separators[19] - separators[18] - 1U),
                          clip.cropRight) ||
            !parseInteger(std::string_view(line).substr(separators[19] + 1U,
                                                         separators[20] - separators[19] - 1U),
                          clip.cropTop) ||
            !parseInteger(std::string_view(line).substr(separators[20] + 1U,
                                                         separators[21] - separators[20] - 1U),
                          clip.cropBottom)) {
            std::cerr << "export clip crop is invalid\n";
            return false;
        }
        if (!parseSpeedKeyframes(std::string_view(line).substr(
                                    separators[21] + 1U,
                                    separators[22] - separators[21] - 1U),
                                clip.speedKeyframes)) {
            std::cerr << "export clip speed keyframes are invalid\n";
            return false;
        }
        int maskShape = 0;
        int maskInverted = 0;
        if (!parseInteger(std::string_view(line).substr(
                              separators[22] + 1U, separators[23] - separators[22] - 1U),
                          maskShape) ||
            !parseInteger(std::string_view(line).substr(
                              separators[23] + 1U, separators[24] - separators[23] - 1U),
                          clip.maskCenterX) ||
            !parseInteger(std::string_view(line).substr(
                              separators[24] + 1U, separators[25] - separators[24] - 1U),
                          clip.maskCenterY) ||
            !parseInteger(std::string_view(line).substr(
                              separators[25] + 1U, separators[26] - separators[25] - 1U),
                          clip.maskWidth) ||
            !parseInteger(std::string_view(line).substr(
                              separators[26] + 1U, separators[27] - separators[26] - 1U),
                          clip.maskHeight) ||
            !parseInteger(std::string_view(line).substr(
                              separators[27] + 1U, separators[28] - separators[27] - 1U),
                          clip.maskFeather) ||
            !parseInteger(std::string_view(line).substr(
                              separators[28] + 1U, separators[29] - separators[28] - 1U),
                          maskInverted) || maskShape < 0 || maskShape > 2 ||
            (maskInverted != 0 && maskInverted != 1)) {
            std::cerr << "export clip mask is invalid\n";
            return false;
        }
        clip.maskShape = static_cast<videx::media::ExportMaskShape>(maskShape);
        clip.maskInverted = maskInverted != 0;
        if (!parseEffects(std::string_view(line).substr(
                              separators[29] + 1U,
                              separators[30] - separators[29] - 1U), clip.effects)) {
            std::cerr << "export clip effects are invalid\n";
            return false;
        }
        if (!parseMotionKeyframes(std::string_view(line).substr(
                                      separators[30] + 1U,
                                      separators[31] - separators[30] - 1U),
                                  clip.motionKeyframes)) {
            std::cerr << "export clip motion keyframes are invalid\n";
            return false;
        }
        if (!parseGainKeyframes(std::string_view(line).substr(
                                    separators[31] + 1U,
                                    separators[32] - separators[31] - 1U),
                                clip.gainKeyframes)) {
            std::cerr << "export clip gain keyframes are invalid\n";
            return false;
        }
        if (!parseInteger(std::string_view(line).substr(
                              separators[32] + 1U, separators[33] - separators[32] - 1U),
                          clip.trackOrder) ||
            clip.trackOrder < 0 || clip.trackOrder > 4096) {
            std::cerr << "export clip track order is invalid\n";
            return false;
        }
        clip.kind = line[0] == 'V' ? videx::media::ExportClipKind::Video
                                   : videx::media::ExportClipKind::Audio;
        const std::string pathText = line.substr(separators[33] + 1U);
        const std::u8string pathUtf8(reinterpret_cast<const char8_t*>(pathText.data()),
                                     pathText.size());
        clip.path = std::filesystem::path(pathUtf8);
        request.clips.push_back(std::move(clip));
        return true;
}

bool finishExportRequest(const std::filesystem::path* arguments, const bool frameOnly,
                         const bool audioOnly,
                         videx::media::TimelineExportRequest& request) {
    if (frameOnly) {
        std::int64_t timelineFrame = 0;
        if (!parseInteger(arguments[2].string(), timelineFrame)) {
            std::cerr << "timeline frame is invalid\n";
            return false;
        }
        const auto result = videx::media::renderTimelineFrame(request, timelineFrame);
        if (!result.succeeded()) {
            std::cerr << result.error << '\n';
            return false;
        }
#if defined(_WIN32)
        static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
        return writeVideoFrameBlob(result.frame);
    }
    if (audioOnly) {
        std::int64_t timelineStart = 0;
        std::int64_t duration = 0;
        if (!parseInteger(arguments[2].string(), timelineStart) ||
            !parseInteger(arguments[8].string(), duration)) {
            std::cerr << "timeline audio range is invalid\n";
            return false;
        }
        const auto result = videx::media::renderTimelineAudio(request, timelineStart, duration);
        if (!result.succeeded()) {
            std::cerr << result.error << '\n';
            return false;
        }
#if defined(_WIN32)
        static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
        const std::uint64_t payloadSize = result.buffer.interleavedSamples.size() * sizeof(float);
        std::cout.write("VXA1", 4);
        writeLittleEndian<std::uint32_t>(result.buffer.sampleRate);
        writeLittleEndian<std::uint32_t>(result.buffer.channelCount);
        writeLittleEndian<std::int64_t>(result.buffer.timestampMicroseconds);
        writeLittleEndian<std::uint64_t>(payloadSize);
        std::cout.write(reinterpret_cast<const char*>(result.buffer.interleavedSamples.data()),
                        static_cast<std::streamsize>(payloadSize));
        return std::cout.good();
    }

    std::int64_t lastReported = -1;
    const auto result = videx::media::exportTimeline(
        request, [&lastReported](const std::int64_t completed, const std::int64_t total) {
            if (completed != lastReported) {
                std::cerr << "PROGRESS " << completed << ' ' << total << '\n';
                lastReported = completed;
            }
            return true;
        });
    if (!result.succeeded()) {
        std::cerr << result.error << '\n';
        return false;
    }
    std::cout << "{\"video_encoder\":\"" << escapeJson(result.videoEncoder)
              << "\",\"audio_encoder\":\"" << escapeJson(result.audioEncoder) << "\"}\n";
    return true;
}

bool printWaveform(const std::filesystem::path& path, const std::string& durationText,
                   const std::string& bucketText) {
    std::int64_t duration = 0;
    std::int32_t bucketCount = 0;
    if (!parseInteger(durationText, duration) || !parseInteger(bucketText, bucketCount)) {
        std::cerr << "waveform settings are invalid\n";
        return false;
    }
    const auto result = videx::media::generateWaveform(path, duration, bucketCount);
    if (!result.succeeded()) {
        std::cerr << result.error << '\n';
        return false;
    }
#if defined(_WIN32)
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    std::cout.write("VXW1", 4);
    writeLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(result.peaks.size()));
    std::cout.write(reinterpret_cast<const char*>(result.peaks.data()),
                    static_cast<std::streamsize>(result.peaks.size() * sizeof(float)));
    return std::cout.good();
}

} // namespace

int runWorker(const int argc, const std::filesystem::path* arguments) {
    if (argc == 2 && arguments[1] == std::filesystem::path{"--version"}) {
        std::cout << videx::core::ApplicationInfo::version() << '\n';
        return 0;
    }

    if (argc == 2 && arguments[1] == std::filesystem::path{"--encoders"}) {
        for (const std::string& encoder : videx::media::availableExportEncoders()) {
            std::cout << encoder << '\n';
        }
        return 0;
    }

    if (argc == 3 && arguments[1] == std::filesystem::path{"probe"}) {
        const auto result = videx::media::probeMedia(arguments[2]);
        if (!result.succeeded()) {
            std::cerr << result.error << '\n';
            return 1;
        }
        printMediaInfo(result.media);
        return 0;
    }

    if (argc == 4 && arguments[1] == std::filesystem::path{"frame"}) {
        return printVideoFrame(arguments[2], arguments[3].string()) ? 0 : 1;
    }

    if (argc == 7 && arguments[1] == std::filesystem::path{"blend-frame"}) {
        return printBlendedVideoFrame(arguments[2], arguments[3].string(), arguments[4],
                                      arguments[5].string(), arguments[6].string()) ? 0 : 1;
    }

    if (argc == 5 && arguments[1] == std::filesystem::path{"audio"}) {
        return printAudio(arguments[2], arguments[3].string(), arguments[4].string()) ? 0 : 1;
    }

    if (argc == 10 && arguments[1] == std::filesystem::path{"audio-ramp"}) {
        return printRampAudio(arguments[2], arguments) ? 0 : 1;
    }

    if ((argc == 8 || argc == 9) && arguments[1] == std::filesystem::path{"export"}) {
        return exportProject(arguments, false, false,
                             argc == 9 ? &arguments[8] : nullptr) ? 0 : 1;
    }

    if (argc == 8 && arguments[1] == std::filesystem::path{"timeline-frame"}) {
        return exportProject(arguments, true) ? 0 : 1;
    }

    if (argc == 2 && arguments[1] == std::filesystem::path{"timeline-frame-serve"}) {
        return serveTimelineFrames();
    }

    if (argc == 9 && arguments[1] == std::filesystem::path{"timeline-audio"}) {
        return exportProject(arguments, false, true) ? 0 : 1;
    }

    if (argc == 5 && arguments[1] == std::filesystem::path{"waveform"}) {
        return printWaveform(arguments[2], arguments[3].string(), arguments[4].string()) ? 0 : 1;
    }

    std::cerr << "usage:\n"
              << "  videx-media-worker probe <media-path>\n"
              << "  videx-media-worker frame <media-path> <timestamp-us>\n"
              << "  videx-media-worker blend-frame <outgoing-path> <outgoing-us> "
                 "<incoming-path> <incoming-us> <blend>\n"
              << "  videx-media-worker audio <media-path> <timestamp-us> <duration-us>\n"
              << "  videx-media-worker audio-ramp <media-path> <source-start-frame> "
                 "<local-start-frame> <output-frames> <fps-num> <fps-den> <base-rate> "
                 "<speed-keyframes>\n"
              << "  videx-media-worker waveform <media-path> <duration-us> <buckets>\n"
              << "  videx-media-worker export <output> <fps-num> <fps-den> <width> <height> "
                 "<duration-frames>\n"
              << "  videx-media-worker timeline-frame <frame> <fps-num> <fps-den> <width> "
                 "<height> <duration-frames>\n"
              << "  videx-media-worker timeline-audio <start-frame> <fps-num> <fps-den> "
                 "<width> <height> <sequence-duration> <duration-frames>\n";
    return 2;
}

#if defined(_WIN32)
int wmain(const int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runWorker(argc, arguments.data());
}
#else
int main(const int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runWorker(argc, arguments.data());
}
#endif
