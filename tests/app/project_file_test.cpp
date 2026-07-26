#include "project_file.hpp"

#include <videx/core/timeline.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() try {
    using namespace videx;

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return fail("could not create temporary directory");
    }

    const core::AssetId assetId{9'007'199'254'740'993ULL};
    core::Track videoTrack{
        .id = core::TrackId{11},
        .kind = core::TrackKind::Video,
        .clips = {{
            .id = core::ClipId{12},
            .assetId = assetId,
            .timeline = {.start = 48, .duration = 120},
            .sourceStart = 24,
            .linkId = core::LinkId{77},
            .opacity = 0.75,
            .audioGainDb = -3.0,
            .playbackRate = 1.25,
            .fadeInFrames = 12,
            .fadeOutFrames = 18,
            .videoTransitionInFrames = 10,
            .positionX = 120.0,
            .positionY = -45.0,
            .scaleX = 1.2,
            .scaleY = 0.8,
            .rotationDegrees = 15.0,
            .anchorX = 0.4,
            .anchorY = 0.6,
            .cropLeft = 0.1,
            .cropRight = 0.2,
            .cropTop = 0.05,
            .cropBottom = 0.15,
            .speedKeyframes = {
                {.frameOffset = 0, .rate = 0.75,
                 .interpolation = core::KeyframeInterpolation::Hold},
                {.frameOffset = 60, .rate = 1.5,
                 .interpolation = core::KeyframeInterpolation::EaseInOut},
            },
            .motionKeyframes = {
                {.frameOffset = 0, .opacity = 1.0, .positionX = 0.0, .positionY = 0.0,
                 .scaleX = 1.0, .scaleY = 1.0, .rotationDegrees = 0.0,
                 .anchorX = 0.5, .anchorY = 0.5,
                 .interpolation = core::KeyframeInterpolation::Linear},
                {.frameOffset = 60, .opacity = 0.5, .positionX = 300.0, .positionY = -100.0,
                 .scaleX = 1.5, .scaleY = 0.75, .rotationDegrees = 45.0,
                 .anchorX = 0.4, .anchorY = 0.6,
                 .interpolation = core::KeyframeInterpolation::Ease},
            },
            .gainKeyframes = {
                {.frameOffset = 0, .gainDb = -60.0,
                 .interpolation = core::KeyframeInterpolation::Linear},
                {.frameOffset = 60, .gainDb = -3.0,
                 .interpolation = core::KeyframeInterpolation::Ease},
            },
            .maskShape = core::MaskShape::Ellipse,
            .maskCenterX = 0.4,
            .maskCenterY = 0.6,
            .maskWidth = 0.7,
            .maskHeight = 0.5,
            .maskFeather = 0.08,
            .maskInverted = true,
            .effects = {{
                .id = core::EffectId{16},
                .type = core::EffectType::Brightness,
                .enabled = true,
                .amount = 0.2,
                .keyframes = {
                    {.frameOffset = 0,
                     .value = 0.0,
                     .interpolation = core::KeyframeInterpolation::Linear},
                    {.frameOffset = 60,
                     .value = 0.5,
                     .interpolation = core::KeyframeInterpolation::Ease},
                },
            }},
        }},
    };
    core::Track audioTrack{
        .id = core::TrackId{13},
        .kind = core::TrackKind::Audio,
        .locked = true,
        .syncLocked = false,
        .muted = true,
        .enabled = false,
        .solo = true,
        .name = "Voice Over",
        .color = 0x40C4FF88U,
        .heightMode = 2,
    };

    ui::ProjectData original{
        .sequence = core::Sequence::fromTracks({24'000, 1'001},
                                               {std::move(videoTrack), std::move(audioTrack)},
                                               {{.id = core::MarkerId{14},
                                                 .position = 72,
                                                 .name = "Beat",
                                                 .comment = "Drop lands here",
                                                 .color = 0xFFD34C4CU}},
                                               {{.id = core::CaptionId{15},
                                                 .timeline = {.start = 48, .duration = 48},
                                                 .text = "Hello, Videx",
                                                 .positionX = 0.3,
                                                 .positionY = 0.7,
                                                 .fontSize = 64.0,
                                                 .textColor = 0xFFCC00FFU,
                                                 .backgroundColor = 0x11223388U,
                                                 .bold = true,
                                                 .italic = true},
                                                // UTF-8 bytes for Japanese text with a
                                                // newline, to prove multibyte captions
                                                // survive the round trip regardless of
                                                // the compiler's source charset.
                                                {.id = core::CaptionId{17},
                                                 .timeline = {.start = 120, .duration = 24},
                                                 .text = "\xE3\x81\x93\xE3\x82\x93\xE3\x81"
                                                         "\xAB\xE3\x81\xA1\xE3\x81\xAF"
                                                         "\n\xE6\x97\xA5\xE6\x9C\xAC\xE8"
                                                         "\xAA\x9E Videx"}}),
        .assets = {{
            .id = assetId,
            .path = QStringLiteral("C:/media/example.mov"),
            .metadata = QJsonObject{
                {QStringLiteral("duration_us"), 5'000'000},
                {QStringLiteral("streams"),
                 QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("video")}}}},
                // Title style lives in asset metadata; it must survive the
                // round trip byte-for-byte (Japanese text included).
                {QStringLiteral("kind"), QStringLiteral("title")},
                {QStringLiteral("title_text"),
                 QString::fromUtf8("\xE8\xA6\x8B\xE5\x87\xBA\xE3\x81\x97 Videx")},
                {QStringLiteral("title_color"), QStringLiteral("4278255615")},
                {QStringLiteral("title_size"), 48.5},
                {QStringLiteral("title_bold"), true},
            },
        }},
    };

    const QString projectPath = temporaryDirectory.filePath(QStringLiteral("round-trip.videx"));
    QString error;
    if (!ui::saveProjectFile(projectPath, original, error)) {
        std::cerr << error.toStdString() << '\n';
        return fail("could not save project");
    }

    auto loaded = ui::loadProjectFile(projectPath, error);
    if (!loaded.has_value()) {
        std::cerr << error.toStdString() << '\n';
        return fail("could not load project");
    }
    if (loaded->sequence.frameRate() != core::FrameRate{24'000, 1'001} ||
        loaded->sequence.tracks().size() != 2 || loaded->sequence.markers().size() != 1 ||
        loaded->sequence.captions().size() != 2 || loaded->assets.size() != 1) {
        return fail("project structure changed during round trip");
    }
    if (loaded->sequence.markers().front().position != 72 ||
        loaded->sequence.markers().front().name != "Beat" ||
        loaded->sequence.markers().front().comment != "Drop lands here" ||
        loaded->sequence.markers().front().color != 0xFFD34C4CU ||
        loaded->sequence.captions().front().timeline != core::FrameRange{48, 48} ||
        loaded->sequence.captions().front().text != "Hello, Videx" ||
        loaded->sequence.captions().front().positionX != 0.3 ||
        loaded->sequence.captions().front().positionY != 0.7 ||
        loaded->sequence.captions().front().fontSize != 64.0 ||
        loaded->sequence.captions().front().textColor != 0xFFCC00FFU ||
        loaded->sequence.captions().front().backgroundColor != 0x11223388U ||
        !loaded->sequence.captions().front().bold ||
        !loaded->sequence.captions().front().italic) {
        return fail("marker or caption changed during round trip");
    }
    if (loaded->sequence.captions()[1].timeline != core::FrameRange{120, 24} ||
        loaded->sequence.captions()[1].text !=
            "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF"
            "\n\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E Videx") {
        return fail("Japanese caption changed during round trip");
    }
    if (!loaded->sequence.tracks()[1].locked || loaded->sequence.tracks()[1].syncLocked ||
        !loaded->sequence.tracks()[1].muted || loaded->sequence.tracks()[1].enabled) {
        return fail("track controls changed during round trip");
    }
    if (!loaded->sequence.tracks()[1].solo ||
        loaded->sequence.tracks()[1].name != "Voice Over" ||
        loaded->sequence.tracks()[1].color != 0x40C4FF88U ||
        loaded->sequence.tracks()[1].heightMode != 2) {
        return fail("track metadata changed during round trip");
    }
    const core::Clip* clip = loaded->sequence.findClip(core::ClipId{12});
    if (clip == nullptr || clip->assetId != assetId || clip->timeline.start != 48 ||
        clip->timeline.duration != 120 || clip->sourceStart != 24 ||
        clip->linkId != core::LinkId{77} || clip->opacity != 0.75 ||
        clip->audioGainDb != -3.0 || clip->playbackRate != 1.25 ||
        clip->fadeInFrames != 12 || clip->fadeOutFrames != 18) {
        return fail("clip data changed during round trip");
    }
    if (clip->videoTransitionInFrames != 10 || clip->audioTransitionInFrames != 0) {
        return fail("clip transition changed during round trip");
    }
    if (clip->cropLeft != 0.1 || clip->cropRight != 0.2 || clip->cropTop != 0.05 ||
        clip->cropBottom != 0.15) {
        return fail("clip crop changed during round trip");
    }
    if (clip->speedKeyframes.size() != 2 || clip->speedKeyframes.front().rate != 0.75 ||
        clip->speedKeyframes.back().frameOffset != 60 ||
        clip->speedKeyframes.back().interpolation !=
            core::KeyframeInterpolation::EaseInOut) {
        return fail("speed keyframes changed during round trip");
    }
    if (clip->motionKeyframes.size() != 2 ||
        clip->motionKeyframes.back().frameOffset != 60 ||
        clip->motionKeyframes.back().positionX != 300.0 ||
        clip->motionKeyframes.back().opacity != 0.5 ||
        clip->motionKeyframes.back().interpolation != core::KeyframeInterpolation::Ease) {
        return fail("motion keyframes changed during round trip");
    }
    if (clip->gainKeyframes.size() != 2 ||
        clip->gainKeyframes.front().gainDb != -60.0 ||
        clip->gainKeyframes.back().frameOffset != 60 ||
        clip->gainKeyframes.back().interpolation != core::KeyframeInterpolation::Ease) {
        return fail("gain keyframes changed during round trip");
    }
    if (clip->maskShape != core::MaskShape::Ellipse || clip->maskCenterX != 0.4 ||
        clip->maskCenterY != 0.6 || clip->maskWidth != 0.7 || clip->maskHeight != 0.5 ||
        clip->maskFeather != 0.08 || !clip->maskInverted) {
        return fail("clip mask changed during round trip");
    }
    if (clip->positionX != 120.0 || clip->positionY != -45.0 || clip->scaleX != 1.2 ||
        clip->scaleY != 0.8 || clip->rotationDegrees != 15.0 || clip->anchorX != 0.4 ||
        clip->anchorY != 0.6) {
        return fail("clip transform changed during round trip");
    }
    if (clip->effects.size() != 1 || clip->effects.front().id != core::EffectId{16} ||
        clip->effects.front().type != core::EffectType::Brightness ||
        clip->effects.front().amount != 0.2 || clip->effects.front().keyframes.size() != 2 ||
        clip->effects.front().keyframes.back().frameOffset != 60 ||
        clip->effects.front().keyframes.back().interpolation !=
            core::KeyframeInterpolation::Ease) {
        return fail("clip effects changed during round trip");
    }
    if (loaded->assets.front().id != assetId ||
        loaded->assets.front().path != QStringLiteral("C:/media/example.mov")) {
        return fail("asset data changed during round trip");
    }
    {
        const QJsonObject& loadedMetadata = loaded->assets.front().metadata;
        if (loadedMetadata.value(QStringLiteral("kind")).toString() !=
                QStringLiteral("title") ||
            loadedMetadata.value(QStringLiteral("title_text")).toString() !=
                QString::fromUtf8("\xE8\xA6\x8B\xE5\x87\xBA\xE3\x81\x97 Videx") ||
            loadedMetadata.value(QStringLiteral("title_color")).toString() !=
                QStringLiteral("4278255615") ||
            loadedMetadata.value(QStringLiteral("title_size")).toDouble() != 48.5 ||
            !loadedMetadata.value(QStringLiteral("title_bold")).toBool(false)) {
            return fail("title asset metadata changed during round trip");
        }
    }

    const QString invalidPath = temporaryDirectory.filePath(QStringLiteral("invalid.videx"));
    QFile invalidFile(invalidPath);
    if (!invalidFile.open(QIODevice::WriteOnly)) {
        return fail("could not create invalid project fixture");
    }
    const QJsonObject invalidProject{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("sequence"),
         QJsonObject{
             {QStringLiteral("frame_rate"),
              QJsonObject{{QStringLiteral("numerator"), 24},
                          {QStringLiteral("denominator"), 1}}},
             {QStringLiteral("tracks"), QJsonArray{}},
         }},
        {QStringLiteral("assets"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"),
              QString::number(std::numeric_limits<std::uint64_t>::max())},
             {QStringLiteral("path"), QStringLiteral("bad.mov")},
             {QStringLiteral("metadata"), QJsonObject{}},
         }}},
    };
    invalidFile.write(QJsonDocument(invalidProject).toJson());
    invalidFile.close();
    if (ui::loadProjectFile(invalidPath, error).has_value()) {
        return fail("invalid project was accepted");
    }

    return 0;
} catch (const std::exception& exception) {
    std::cerr << "unhandled exception: " << exception.what() << '\n';
    return 1;
}
