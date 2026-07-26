#include <videx/core/timeline.hpp>
#include <videx/media/media_probe.hpp>
#include <videx/media/timeline_export.hpp>
#include <videx/media/video_frame.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    using namespace videx::media;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() /
        ("videx-export-test-" + std::to_string(unique) + ".mp4");
    const std::filesystem::path effectedOutput =
        std::filesystem::temp_directory_path() /
        ("videx-export-effects-test-" + std::to_string(unique) + ".mp4");
    const std::filesystem::path markedRangeOutput =
        std::filesystem::temp_directory_path() /
        ("videx-export-marked-range-test-" + std::to_string(unique) + ".mp4");

    const TimelineExportRequest request{
        .outputPath = output,
        .frameRateNumerator = 24,
        .frameRateDenominator = 1,
        .width = 320,
        .height = 180,
        .durationFrames = 12,
    };
    const TimelineExportResult exported = exportTimeline(request);
    if (!exported.succeeded()) {
        std::cerr << exported.error << '\n';
        return 1;
    }
    const ProbeResult probe = probeMedia(output);
    std::error_code removeError;
    if (!probe.succeeded()) {
        std::cerr << probe.error << '\n';
        return 1;
    }

    bool foundH264 = false;
    bool foundAac = false;
    for (const StreamInfo& stream : probe.media.streams) {
        foundH264 = foundH264 ||
                    (stream.kind == StreamKind::Video && stream.codecName == "h264" &&
                     stream.width == 320 && stream.height == 180);
        foundAac = foundAac ||
                   (stream.kind == StreamKind::Audio && stream.codecName == "aac" &&
                    stream.sampleRate == 48'000 && stream.channelCount == 2);
    }
    if (!foundH264 || !foundAac) {
        std::cerr << "exported MP4 does not contain the expected H.264/AAC streams\n";
        return 1;
    }

    // The persistent decoder session must not change pixels: an ascending
    // (sequential, seek-free) pass and a descending (seek-forced) pass over
    // the same timestamps must produce byte-identical frames.
    {
        std::vector<std::vector<std::uint8_t>> ascending;
        for (int frame = 0; frame < 10; ++frame) {
            const VideoFrameResult sequentialFrame =
                decodeVideoFrame(output, frame * 1'000'000LL / 24, 320, 180);
            if (!sequentialFrame.succeeded()) {
                std::cerr << "sequential decode failed at frame " << frame << ": "
                          << sequentialFrame.error << '\n';
                return 1;
            }
            ascending.push_back(sequentialFrame.frame.rgba);
        }
        for (int frame = 9; frame >= 0; --frame) {
            const VideoFrameResult seeked =
                decodeVideoFrame(output, frame * 1'000'000LL / 24, 320, 180);
            if (!seeked.succeeded() ||
                seeked.frame.rgba != ascending[static_cast<std::size_t>(frame)]) {
                std::cerr << "sequential and seeked decodes disagree at frame "
                          << frame << '\n';
                return 1;
            }
        }
    }

    TimelineExportRequest effectedRequest{
        .outputPath = effectedOutput,
        .frameRateNumerator = 24,
        .frameRateDenominator = 1,
        .width = 320,
        .height = 180,
        .durationFrames = 12,
        .clips = {{
            .kind = ExportClipKind::Video,
            .path = output,
            .timelineStart = 0,
            .duration = 12,
            .sourceStart = 0,
            .effects = {{
                .type = ExportEffectType::Brightness,
                .enabled = true,
                .amount = 1.0,
            }},
        }, {
            .kind = ExportClipKind::Video,
            .path = output,
            .timelineStart = 0,
            .duration = 6,
            .sourceStart = 0,
            .fadeInFrames = 2,
            .fadeOutFrames = 2,
            .positionX = 8.0,
            .positionY = -4.0,
            .scaleX = 0.9,
            .scaleY = 0.9,
            .rotationDegrees = 5.0,
            .cropLeft = 0.5,
            .speedKeyframes = {{0, 0.5, ExportInterpolation::Linear},
                               {4, 1.5, ExportInterpolation::Ease}},
            .maskShape = ExportMaskShape::Ellipse,
            .maskCenterX = 0.7,
            .maskCenterY = 0.5,
            .maskWidth = 0.55,
            .maskHeight = 0.8,
            .maskFeather = 0.02,
            .effects = {{
                .type = ExportEffectType::Brightness,
                .enabled = true,
                .amount = 0.1,
                .keyframes = {{0, 0.0, ExportInterpolation::Linear},
                              {4, 0.4, ExportInterpolation::Ease}},
            }, {
                .type = ExportEffectType::Vignette,
                .enabled = true,
                .amount = 0.25,
            }},
        }, {
            .kind = ExportClipKind::Video,
            .path = output,
            .timelineStart = 6,
            .duration = 6,
            .sourceStart = 6,
            .videoTransitionInFrames = 3,
            .motionKeyframes = {{.frameOffset = 0, .opacity = 1.0,
                                 .positionX = 0.0, .positionY = 0.0,
                                 .scaleX = 1.0, .scaleY = 1.0,
                                 .rotationDegrees = 0.0, .anchorX = 0.5, .anchorY = 0.5,
                                 .interpolation = ExportInterpolation::Linear},
                                {.frameOffset = 4, .opacity = 0.6,
                                 .positionX = 60.0, .positionY = -20.0,
                                 .scaleX = 1.2, .scaleY = 0.8,
                                 .rotationDegrees = 20.0, .anchorX = 0.5, .anchorY = 0.5,
                                 .interpolation = ExportInterpolation::Ease}},
        }, {
            .kind = ExportClipKind::Audio,
            .path = output,
            .timelineStart = 0,
            .duration = 6,
            .sourceStart = 0,
        }, {
            .kind = ExportClipKind::Audio,
            .path = output,
            .timelineStart = 6,
            .duration = 6,
            .sourceStart = 6,
            .audioTransitionInFrames = 3,
            .speedKeyframes = {{0, 0.75, ExportInterpolation::Linear},
                               {4, 1.25, ExportInterpolation::Ease}},
        }},
        .overlays = {{.path = output, .timelineStart = 0, .duration = 2}},
    };
    const TimelineExportResult effected = exportTimeline(effectedRequest);
    const VideoFrameResult renderedPreview = renderTimelineFrame(effectedRequest, 3);
    const ProbeResult effectedProbe = effected.succeeded() ? probeMedia(effectedOutput)
                                                            : ProbeResult{};
    const VideoFrameResult overlayFrame = effected.succeeded()
        ? decodeVideoFrame(effectedOutput, 0, 320, 180) : VideoFrameResult{};
    const VideoFrameResult layeredFrame = effected.succeeded()
        ? decodeVideoFrame(effectedOutput, 125'000, 320, 180) : VideoFrameResult{};
    const TimelineExportResult markedRange = exportTimeline({
        .outputPath = markedRangeOutput,
        .frameRateNumerator = 24,
        .frameRateDenominator = 1,
        .width = 320,
        .height = 180,
        .durationFrames = 4,
        .clips = {{.kind = ExportClipKind::Video,
                   .path = output,
                   .timelineStart = -4,
                   .duration = 12,
                   .sourceStart = 0}},
        .overlays = {{.path = output, .timelineStart = -2, .duration = 4}},
    });
    const ProbeResult markedRangeProbe = markedRange.succeeded()
        ? probeMedia(markedRangeOutput) : ProbeResult{};
    const TimelineExportRequest motionRequest{
        .frameRateNumerator = 24,
        .frameRateDenominator = 1,
        .width = 320,
        .height = 180,
        .durationFrames = 5,
        .clips = {{.kind = ExportClipKind::Video,
                   .path = output,
                   .timelineStart = 0,
                   .duration = 5,
                   .sourceStart = 0,
                   .motionKeyframes = {
                       {.frameOffset = 0, .opacity = 1.0, .positionX = 0.0,
                        .positionY = 0.0, .scaleX = 1.0, .scaleY = 1.0,
                        .rotationDegrees = 0.0, .anchorX = 0.5, .anchorY = 0.5,
                        .interpolation = ExportInterpolation::Linear},
                       {.frameOffset = 4, .opacity = 0.5, .positionX = 100.0,
                        .positionY = 0.0, .scaleX = 0.5, .scaleY = 0.5,
                        .rotationDegrees = 0.0, .anchorX = 0.5, .anchorY = 0.5,
                        .interpolation = ExportInterpolation::Ease}},
                   .effects = {{.type = ExportEffectType::Brightness,
                                .enabled = true, .amount = 1.0}}}},
    };
    const VideoFrameResult motionStart = renderTimelineFrame(motionRequest, 0);
    const VideoFrameResult motionEnd = renderTimelineFrame(motionRequest, 4);

    // A transition may only pair clips on the same track order: moving the
    // incoming clip to another track must render exactly as if it carried no
    // transition, instead of dissolving from an unrelated layer.
    TimelineExportRequest crossTrackRequest{
        .outputPath = {},
        .frameRateNumerator = 24,
        .frameRateDenominator = 1,
        .width = 320,
        .height = 180,
        .durationFrames = 12,
        .clips = {{.kind = ExportClipKind::Video,
                   .path = output,
                   .timelineStart = 0,
                   .duration = 6,
                   .sourceStart = 0},
                  {.kind = ExportClipKind::Video,
                   .path = output,
                   .trackOrder = 1,
                   .timelineStart = 6,
                   .duration = 6,
                   .sourceStart = 6,
                   .videoTransitionInFrames = 3}},
    };
    TimelineExportRequest sameLookRequest = crossTrackRequest;
    sameLookRequest.clips[1].videoTransitionInFrames = 0;
    const VideoFrameResult crossTrackFrame = renderTimelineFrame(crossTrackRequest, 7);
    const VideoFrameResult noTransitionFrame = renderTimelineFrame(sameLookRequest, 7);
    const std::filesystem::path titleOutput =
        std::filesystem::temp_directory_path() /
        ("videx-title-export-" + std::to_string(unique) + ".mp4");

    // Title clips are raw RGBA stills (.vxtitle); a red 16x16 title on a
    // front track must composite over the base video at the canvas centre.
    const std::filesystem::path titlePath =
        std::filesystem::temp_directory_path() / "videx-title-test.vxtitle";
    {
        std::ofstream titleFile(titlePath, std::ios::binary);
        const std::uint32_t titleWidth = 16;
        const std::uint32_t titleHeight = 16;
        titleFile.write("VXTI", 4);
        titleFile.write(reinterpret_cast<const char*>(&titleWidth), 4);
        titleFile.write(reinterpret_cast<const char*>(&titleHeight), 4);
        for (std::uint32_t pixel = 0; pixel < titleWidth * titleHeight; ++pixel) {
            const std::array<std::uint8_t, 4> red{255U, 0U, 0U, 255U};
            titleFile.write(reinterpret_cast<const char*>(red.data()), 4);
        }
    }
    TimelineExportRequest titleRequest{
        .outputPath = {},
        .frameRateNumerator = 24,
        .frameRateDenominator = 1,
        .width = 320,
        .height = 180,
        .durationFrames = 6,
        .clips = {{.kind = ExportClipKind::Video,
                   .path = output,
                   .timelineStart = 0,
                   .duration = 6,
                   .sourceStart = 0},
                  {.kind = ExportClipKind::Video,
                   .path = titlePath,
                   .trackOrder = 1,
                   .timelineStart = 0,
                   .duration = 6,
                   .sourceStart = 0}},
    };
    const VideoFrameResult titleFrame = renderTimelineFrame(titleRequest, 2);

    // Export parity: the same title request written through exportTimeline
    // must show the red still in the decoded MP4 as well.
    TimelineExportRequest titleExportRequest = titleRequest;
    titleExportRequest.outputPath = titleOutput;
    const TimelineExportResult titleExported = exportTimeline(titleExportRequest);
    const VideoFrameResult titleExportFrame =
        titleExported.succeeded() ? decodeVideoFrame(titleOutput, 83'000, 320, 180)
                                  : VideoFrameResult{};

    std::filesystem::remove(effectedOutput, removeError);
    std::filesystem::remove(markedRangeOutput, removeError);
    std::filesystem::remove(output, removeError);
    if (!effected.succeeded() || !effectedProbe.succeeded() || !layeredFrame.succeeded() ||
        !overlayFrame.succeeded() || !renderedPreview.succeeded() ||
        !markedRange.succeeded() || !markedRangeProbe.succeeded() ||
        !motionStart.succeeded() || !motionEnd.succeeded()) {
        if (!markedRange.succeeded()) std::cerr << markedRange.error << '\n';
        else if (!markedRangeProbe.succeeded()) std::cerr << markedRangeProbe.error << '\n';
        else std::cerr << (effected.succeeded() ? effectedProbe.error : effected.error) << '\n';
        return 1;
    }
    if (motionStart.frame.rgba[0] < 220U || motionEnd.frame.rgba[0] > 20U) {
        std::cerr << "motion keyframe interpolation did not move the rendered layer\n";
        return 1;
    }
    if (!crossTrackFrame.succeeded() || !noTransitionFrame.succeeded() ||
        crossTrackFrame.frame.rgba != noTransitionFrame.frame.rgba) {
        std::cerr << "a transition must ignore clips on other tracks\n";
        return 1;
    }
    std::filesystem::remove(titlePath, removeError);
    std::filesystem::remove(titleOutput, removeError);
    if (!titleFrame.succeeded()) {
        std::cerr << "title still failed to render: " << titleFrame.error << '\n';
        return 1;
    }
    if (!titleExported.succeeded() || !titleExportFrame.succeeded()) {
        std::cerr << "title export failed: "
                  << (titleExported.succeeded() ? titleExportFrame.error
                                                : titleExported.error)
                  << '\n';
        return 1;
    }
    {
        // Lossy H.264 thresholds: the red still must dominate both the canvas
        // centre and the mid-band that only exists after upscaling.
        const auto exportedPixel = [&titleExportFrame](const int x, const int y) {
            return (static_cast<std::size_t>(y) * titleExportFrame.frame.width + x) * 4U;
        };
        const std::size_t centre = exportedPixel(160, 90);
        const std::size_t offCentre = exportedPixel(100, 90);
        if (titleExportFrame.frame.rgba[centre] < 170U ||
            titleExportFrame.frame.rgba[centre + 1U] > 95U ||
            titleExportFrame.frame.rgba[offCentre] < 170U) {
            std::cerr << "exported title frame did not match the preview composite\n";
            return 1;
        }
    }
    {
        const std::size_t centre =
            (static_cast<std::size_t>(90) * titleFrame.frame.width + 160) * 4U;
        if (titleFrame.frame.rgba[centre] < 200U ||
            titleFrame.frame.rgba[centre + 1U] > 80U) {
            std::cerr << "title still did not composite over the base video\n";
            return 1;
        }
        // Title stills scale to the output canvas: the 1:1 16x16 red still
        // must fill a 180x180 band on the 320x180 canvas, so a pixel well
        // outside the native 16x16 centre must also be red (upscaling proof).
        const std::size_t offCentre =
            (static_cast<std::size_t>(90) * titleFrame.frame.width + 100) * 4U;
        if (titleFrame.frame.rgba[offCentre] < 200U ||
            titleFrame.frame.rgba[offCentre + 1U] > 80U) {
            std::cerr << "title still was not scaled up to the output canvas\n";
            return 1;
        }
    }
    const auto pixelValue = [&layeredFrame](const int x, const int y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * layeredFrame.frame.width + x) * 4U;
        return layeredFrame.frame.rgba[offset];
    };
    const auto corner = pixelValue(2, 2);
    const auto croppedSide = pixelValue(100, 90);
    const auto visibleSide = pixelValue(210, 90);
    const auto maskOutside = pixelValue(315, 2);
    const auto overlayPixel = overlayFrame.frame.rgba[0];
    const auto previewPixel = [&renderedPreview](const int x, const int y) {
        return renderedPreview.frame.rgba[
            (static_cast<std::size_t>(y) * renderedPreview.frame.width + x) * 4U];
    };
    const int previewDifference = std::abs(static_cast<int>(previewPixel(210, 90)) -
                                           static_cast<int>(visibleSide));
    if (corner < 180 || croppedSide < 180 || visibleSide > 160 || maskOutside < 180 ||
        overlayPixel > 80 || previewDifference > 35) {
        std::cerr << "multilayer crop/mask composition did not preserve the lower layer: "
                  << static_cast<int>(corner) << ", " << static_cast<int>(croppedSide)
                  << ", " << static_cast<int>(visibleSide) << ", "
                  << static_cast<int>(maskOutside) << ", overlay "
                  << static_cast<int>(overlayPixel) << ", preview delta "
                  << previewDifference << '\n';
        return 1;
    }

    static_assert(exportInterpolationProgress(ExportInterpolation::EaseIn, 0.5) == 0.125);
    static_assert(exportInterpolationProgress(ExportInterpolation::EaseOut, 0.5) == 0.875);
    static_assert(
        exportInterpolationProgress(ExportInterpolation::EaseInOut, 0.25) == 0.0625);
    static_assert(exportInterpolationProgress(ExportInterpolation::EaseInOut, 0.5) == 0.5);
    static_assert(exportInterpolationIntegral(ExportInterpolation::EaseIn, 1.0) == 0.25);
    static_assert(exportInterpolationIntegral(ExportInterpolation::EaseOut, 1.0) == 0.75);
    static_assert(exportInterpolationIntegral(ExportInterpolation::EaseInOut, 1.0) == 0.5);
    static_assert(
        exportInterpolationIntegral(ExportInterpolation::EaseInOut, 0.5) == 0.0625);

    // Preview (core) and export (media) easing math must agree bit-for-bit so a
    // curve chosen in the inspector renders identically in the exported file.
    for (int mode = 0; mode <= 5; ++mode) {
        for (int step = 0; step <= 64; ++step) {
            const double ratio = static_cast<double>(step) / 64.0;
            const double coreProgress = videx::core::interpolationProgress(
                static_cast<videx::core::KeyframeInterpolation>(mode), ratio);
            const double mediaProgress = exportInterpolationProgress(
                static_cast<ExportInterpolation>(mode), ratio);
            const double coreIntegral = videx::core::interpolationIntegral(
                static_cast<videx::core::KeyframeInterpolation>(mode), ratio);
            const double mediaIntegral = exportInterpolationIntegral(
                static_cast<ExportInterpolation>(mode), ratio);
            if (coreProgress != mediaProgress || coreIntegral != mediaIntegral) {
                std::cerr << "core/media easing parity broke at mode " << mode
                          << " ratio " << ratio << '\n';
                return 1;
            }
        }
    }

    // Easing presets must reshape an opacity ramp: over a white still faded
    // 0->1, the quarter point renders darkest for EaseIn, then EaseInOut,
    // then Linear, then EaseOut; EaseInOut is symmetric around the midpoint.
    const std::filesystem::path whitePath =
        std::filesystem::temp_directory_path() /
        ("videx-easing-test-" + std::to_string(unique) + ".vxtitle");
    {
        std::ofstream whiteFile(whitePath, std::ios::binary);
        const std::uint32_t whiteSize = 16;
        whiteFile.write("VXTI", 4);
        whiteFile.write(reinterpret_cast<const char*>(&whiteSize), 4);
        whiteFile.write(reinterpret_cast<const char*>(&whiteSize), 4);
        for (std::uint32_t pixel = 0; pixel < whiteSize * whiteSize; ++pixel) {
            const std::array<std::uint8_t, 4> white{255U, 255U, 255U, 255U};
            whiteFile.write(reinterpret_cast<const char*>(white.data()), 4);
        }
    }
    const auto easedCentre = [&whitePath](const ExportInterpolation interpolation,
                                          const std::int64_t frame) {
        const TimelineExportRequest ramp{
            .frameRateNumerator = 24,
            .frameRateDenominator = 1,
            .width = 320,
            .height = 180,
            .durationFrames = 9,
            .clips = {{.kind = ExportClipKind::Video,
                       .path = whitePath,
                       .timelineStart = 0,
                       .duration = 9,
                       .sourceStart = 0,
                       .motionKeyframes = {
                           {.frameOffset = 0, .opacity = 0.0, .positionX = 0.0,
                            .positionY = 0.0, .scaleX = 1.0, .scaleY = 1.0,
                            .rotationDegrees = 0.0, .anchorX = 0.5, .anchorY = 0.5,
                            .interpolation = interpolation},
                           {.frameOffset = 8, .opacity = 1.0, .positionX = 0.0,
                            .positionY = 0.0, .scaleX = 1.0, .scaleY = 1.0,
                            .rotationDegrees = 0.0, .anchorX = 0.5, .anchorY = 0.5,
                            .interpolation = ExportInterpolation::Linear}}}},
        };
        const VideoFrameResult rendered = renderTimelineFrame(ramp, frame);
        if (!rendered.succeeded()) return -1;
        const std::size_t centre =
            (static_cast<std::size_t>(90) * rendered.frame.width + 160) * 4U;
        return static_cast<int>(rendered.frame.rgba[centre]);
    };
    const int quarterEaseIn = easedCentre(ExportInterpolation::EaseIn, 2);
    const int quarterEaseInOut = easedCentre(ExportInterpolation::EaseInOut, 2);
    const int quarterLinear = easedCentre(ExportInterpolation::Linear, 2);
    const int quarterEaseOut = easedCentre(ExportInterpolation::EaseOut, 2);
    const int threeQuarterEaseInOut = easedCentre(ExportInterpolation::EaseInOut, 6);
    std::filesystem::remove(whitePath, removeError);
    if (quarterEaseIn < 0 || quarterEaseInOut < 0 || quarterLinear < 0 ||
        quarterEaseOut < 0 || threeQuarterEaseInOut < 0) {
        std::cerr << "easing ramp frames failed to render\n";
        return 1;
    }
    if (!(quarterEaseIn + 8 < quarterEaseInOut && quarterEaseInOut + 20 < quarterLinear &&
          quarterLinear + 40 < quarterEaseOut)) {
        std::cerr << "easing presets did not order the quarter-point brightness: "
                  << quarterEaseIn << ", " << quarterEaseInOut << ", " << quarterLinear
                  << ", " << quarterEaseOut << '\n';
        return 1;
    }
    if (std::abs(quarterEaseInOut + threeQuarterEaseInOut - 255) > 6) {
        std::cerr << "EaseInOut is not symmetric around the midpoint: "
                  << quarterEaseInOut << " + " << threeQuarterEaseInOut << '\n';
        return 1;
    }

    // The sliding-window box blur must match the naive reference bit-for-bit
    // (true edge windows, truncating division). A full-canvas patterned title
    // composites 1:1, so the rendered frame is exactly blur(pattern).
    {
        const int canvasWidth = 320;
        const int canvasHeight = 180;
        std::vector<std::uint8_t> pattern(
            static_cast<std::size_t>(canvasWidth) * canvasHeight * 4U);
        for (int y = 0; y < canvasHeight; ++y) {
            for (int x = 0; x < canvasWidth; ++x) {
                const std::size_t pixel =
                    (static_cast<std::size_t>(y) * canvasWidth + x) * 4U;
                pattern[pixel] = static_cast<std::uint8_t>((x * 7 + y * 13) % 256);
                pattern[pixel + 1U] = static_cast<std::uint8_t>((x * 3 + y * 5) % 256);
                pattern[pixel + 2U] = static_cast<std::uint8_t>((x + y * 2) % 256);
                pattern[pixel + 3U] = 255U;
            }
        }
        const std::filesystem::path patternPath =
            std::filesystem::temp_directory_path() /
            ("videx-blur-test-" + std::to_string(unique) + ".vxtitle");
        {
            std::ofstream patternFile(patternPath, std::ios::binary);
            const std::uint32_t patternWidth = canvasWidth;
            const std::uint32_t patternHeight = canvasHeight;
            patternFile.write("VXTI", 4);
            patternFile.write(reinterpret_cast<const char*>(&patternWidth), 4);
            patternFile.write(reinterpret_cast<const char*>(&patternHeight), 4);
            patternFile.write(reinterpret_cast<const char*>(pattern.data()),
                              static_cast<std::streamsize>(pattern.size()));
        }
        const int radius = 6;
        const TimelineExportRequest blurRequest{
            .frameRateNumerator = 24,
            .frameRateDenominator = 1,
            .width = canvasWidth,
            .height = canvasHeight,
            .durationFrames = 2,
            .clips = {{.kind = ExportClipKind::Video,
                       .path = patternPath,
                       .timelineStart = 0,
                       .duration = 2,
                       .sourceStart = 0,
                       .effects = {{.type = ExportEffectType::Blur,
                                    .enabled = true,
                                    .amount = static_cast<double>(radius)}}}},
        };
        const VideoFrameResult blurred = renderTimelineFrame(blurRequest, 0);
        std::filesystem::remove(patternPath, removeError);
        if (!blurred.succeeded() || blurred.frame.width != canvasWidth ||
            blurred.frame.height != canvasHeight) {
            std::cerr << "blur pattern frame failed to render\n";
            return 1;
        }
        // Naive reference blur (the pre-optimization algorithm).
        std::vector<std::uint8_t> horizontal(pattern.size());
        for (int y = 0; y < canvasHeight; ++y) {
            for (int x = 0; x < canvasWidth; ++x) {
                const int left = std::max(0, x - radius);
                const int right = std::min(canvasWidth - 1, x + radius);
                for (int channel = 0; channel < 4; ++channel) {
                    int sum = 0;
                    for (int sample = left; sample <= right; ++sample) {
                        sum += pattern[(static_cast<std::size_t>(y) * canvasWidth +
                                        sample) * 4U + channel];
                    }
                    horizontal[(static_cast<std::size_t>(y) * canvasWidth + x) * 4U +
                               channel] =
                        static_cast<std::uint8_t>(sum / (right - left + 1));
                }
            }
        }
        std::vector<std::uint8_t> reference(pattern.size());
        for (int y = 0; y < canvasHeight; ++y) {
            const int top = std::max(0, y - radius);
            const int bottom = std::min(canvasHeight - 1, y + radius);
            for (int x = 0; x < canvasWidth; ++x) {
                for (int channel = 0; channel < 4; ++channel) {
                    int sum = 0;
                    for (int sample = top; sample <= bottom; ++sample) {
                        sum += horizontal[(static_cast<std::size_t>(sample) * canvasWidth +
                                           x) * 4U + channel];
                    }
                    reference[(static_cast<std::size_t>(y) * canvasWidth + x) * 4U +
                              channel] =
                        static_cast<std::uint8_t>(sum / (bottom - top + 1));
                }
            }
        }
        int worstDelta = 0;
        for (std::size_t index = 0; index < reference.size(); index += 4U) {
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                worstDelta = std::max(worstDelta,
                    std::abs(static_cast<int>(blurred.frame.rgba[index + channel]) -
                             static_cast<int>(reference[index + channel])));
            }
        }
        if (worstDelta > 2) {
            std::cerr << "optimized box blur diverged from the naive reference by "
                      << worstDelta << '\n';
            return 1;
        }
    }
    return 0;
}
