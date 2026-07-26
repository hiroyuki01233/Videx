#include <videx/media/audio_buffer.hpp>
#include <videx/media/media_probe.hpp>
#include <videx/media/video_frame.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void writeLittleEndian16(std::ofstream& stream, const std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
    };
    stream.write(bytes, sizeof(bytes));
}

void writeLittleEndian32(std::ofstream& stream, const std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    stream.write(bytes, sizeof(bytes));
}

bool writeTestWave(const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }

    stream.write("RIFF", 4);
    writeLittleEndian32(stream, 38U);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    writeLittleEndian32(stream, 16U);
    writeLittleEndian16(stream, 1U);
    writeLittleEndian16(stream, 1U);
    writeLittleEndian32(stream, 48'000U);
    writeLittleEndian32(stream, 96'000U);
    writeLittleEndian16(stream, 2U);
    writeLittleEndian16(stream, 16U);
    stream.write("data", 4);
    writeLittleEndian32(stream, 2U);
    writeLittleEndian16(stream, 0U);
    return stream.good();
}

bool writeTestImage(const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream << "P6\n2 2\n255\n";
    const char pixels[] = {
        static_cast<char>(255), 0, 0, 0, static_cast<char>(255), 0,
        0, 0, static_cast<char>(255), static_cast<char>(255), static_cast<char>(255),
        static_cast<char>(255),
    };
    stream.write(pixels, sizeof(pixels));
    return stream.good();
}

} // namespace

int main() {
    const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("videx-media-probe-" + std::to_string(uniqueValue) + ".wav");
    const std::filesystem::path imagePath =
        std::filesystem::temp_directory_path() /
        ("videx-video-frame-" + std::to_string(uniqueValue) + ".ppm");

    if (!writeTestWave(path)) {
        std::cerr << "could not create the test wave file\n";
        return 1;
    }
    if (!writeTestImage(imagePath)) {
        std::cerr << "could not create the test image file\n";
        return 1;
    }

    const auto result = videx::media::probeMedia(path);
    const auto audioResult = videx::media::decodeAudio(path, 0, 100'000);
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    if (!result.succeeded()) {
        std::cerr << result.error << '\n';
        return 1;
    }
    if (result.media.formatName.find("wav") == std::string_view::npos) {
        std::cerr << "unexpected container: " << result.media.formatName << '\n';
        return 1;
    }
    if (result.media.streams.size() != 1U) {
        std::cerr << "expected one audio stream\n";
        return 1;
    }

    const auto& stream = result.media.streams.front();
    if (stream.kind != videx::media::StreamKind::Audio || stream.sampleRate != 48'000 ||
        stream.channelCount != 1) {
        std::cerr << "unexpected audio stream metadata\n";
        return 1;
    }
    if (!audioResult.succeeded() || audioResult.buffer.sampleRate != 48'000 ||
        audioResult.buffer.channelCount != 2 || audioResult.buffer.interleavedSamples.empty()) {
        std::cerr << "could not decode normalized audio: " << audioResult.error << '\n';
        return 1;
    }

    const auto frameResult = videx::media::decodeVideoFrame(imagePath, 0, 2, 2);
    std::filesystem::remove(imagePath, removeError);
    if (!frameResult.succeeded()) {
        std::cerr << frameResult.error << '\n';
        return 1;
    }
    if (frameResult.frame.width != 2 || frameResult.frame.height != 2 ||
        frameResult.frame.rgba.size() != 16U) {
        std::cerr << "decoded frame has unexpected dimensions\n";
        return 1;
    }
    if (frameResult.frame.rgba[0] < 240U || frameResult.frame.rgba[1] > 20U ||
        frameResult.frame.rgba[2] > 20U || frameResult.frame.rgba[3] != 255U) {
        std::cerr << "decoded frame has unexpected pixel values\n";
        return 1;
    }

    return 0;
}
