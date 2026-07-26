#include "project_file.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <set>
#include <utility>

namespace {

QString unsignedText(const std::uint64_t value) {
    return QString::number(value);
}

QString signedText(const std::int64_t value) {
    return QString::number(value);
}

bool parseUnsigned(const QJsonValue& value, std::uint64_t& result) {
    bool ok = false;
    result = value.toString().toULongLong(&ok);
    return ok;
}

bool parseSigned(const QJsonValue& value, std::int64_t& result) {
    bool ok = false;
    result = value.toString().toLongLong(&ok);
    return ok;
}

bool parseArray(const QJsonValue& value, QJsonArray& result) {
    if (value.isUndefined() || value.isNull()) {
        result = QJsonArray{};
        return true;
    }
    if (!value.isArray()) {
        return false;
    }
    result = value.toArray();
    return true;
}

QJsonObject serializeSequence(const videx::core::Sequence& sequence) {
    QJsonArray tracks;
    for (const auto& track : sequence.tracks()) {
        QJsonArray clips;
        for (const auto& clip : track.clips) {
            QJsonArray speedKeyframes;
            for (const auto& keyframe : clip.speedKeyframes) {
                speedKeyframes.append(QJsonObject{
                    {QStringLiteral("frame"), signedText(keyframe.frameOffset)},
                    {QStringLiteral("rate"), keyframe.rate},
                    {QStringLiteral("interpolation"), static_cast<int>(keyframe.interpolation)},
                });
            }
            QJsonArray motionKeyframes;
            for (const auto& keyframe : clip.motionKeyframes) {
                motionKeyframes.append(QJsonObject{
                    {QStringLiteral("frame"), signedText(keyframe.frameOffset)},
                    {QStringLiteral("opacity"), keyframe.opacity},
                    {QStringLiteral("position_x"), keyframe.positionX},
                    {QStringLiteral("position_y"), keyframe.positionY},
                    {QStringLiteral("scale_x"), keyframe.scaleX},
                    {QStringLiteral("scale_y"), keyframe.scaleY},
                    {QStringLiteral("rotation_degrees"), keyframe.rotationDegrees},
                    {QStringLiteral("anchor_x"), keyframe.anchorX},
                    {QStringLiteral("anchor_y"), keyframe.anchorY},
                    {QStringLiteral("interpolation"), static_cast<int>(keyframe.interpolation)},
                });
            }
            QJsonArray gainKeyframes;
            for (const auto& keyframe : clip.gainKeyframes) {
                gainKeyframes.append(QJsonObject{
                    {QStringLiteral("frame"), signedText(keyframe.frameOffset)},
                    {QStringLiteral("gain_db"), keyframe.gainDb},
                    {QStringLiteral("interpolation"), static_cast<int>(keyframe.interpolation)},
                });
            }
            QJsonArray effects;
            for (const auto& effect : clip.effects) {
                QJsonArray keyframes;
                for (const auto& keyframe : effect.keyframes) {
                    keyframes.append(QJsonObject{
                        {QStringLiteral("frame"), signedText(keyframe.frameOffset)},
                        {QStringLiteral("value"), keyframe.value},
                        {QStringLiteral("interpolation"),
                         static_cast<int>(keyframe.interpolation)},
                    });
                }
                effects.append(QJsonObject{
                    {QStringLiteral("id"), unsignedText(effect.id.value)},
                    {QStringLiteral("type"), static_cast<int>(effect.type)},
                    {QStringLiteral("enabled"), effect.enabled},
                    {QStringLiteral("amount"), effect.amount},
                    {QStringLiteral("keyframes"), keyframes},
                });
            }
            clips.append(QJsonObject{
                {QStringLiteral("id"), unsignedText(clip.id.value)},
                {QStringLiteral("asset_id"), unsignedText(clip.assetId.value)},
                {QStringLiteral("start"), signedText(clip.timeline.start)},
                {QStringLiteral("duration"), signedText(clip.timeline.duration)},
                {QStringLiteral("source_start"), signedText(clip.sourceStart)},
                {QStringLiteral("link_id"), unsignedText(clip.linkId.value)},
                {QStringLiteral("opacity"), clip.opacity},
                {QStringLiteral("audio_gain_db"), clip.audioGainDb},
                {QStringLiteral("playback_rate"), clip.playbackRate},
                {QStringLiteral("fade_in_frames"), signedText(clip.fadeInFrames)},
                {QStringLiteral("fade_out_frames"), signedText(clip.fadeOutFrames)},
                {QStringLiteral("video_transition_in_frames"),
                 signedText(clip.videoTransitionInFrames)},
                {QStringLiteral("audio_transition_in_frames"),
                 signedText(clip.audioTransitionInFrames)},
                {QStringLiteral("position_x"), clip.positionX},
                {QStringLiteral("position_y"), clip.positionY},
                {QStringLiteral("scale_x"), clip.scaleX},
                {QStringLiteral("scale_y"), clip.scaleY},
                {QStringLiteral("rotation_degrees"), clip.rotationDegrees},
                {QStringLiteral("anchor_x"), clip.anchorX},
                {QStringLiteral("anchor_y"), clip.anchorY},
                {QStringLiteral("crop_left"), clip.cropLeft},
                {QStringLiteral("crop_right"), clip.cropRight},
                {QStringLiteral("crop_top"), clip.cropTop},
                {QStringLiteral("crop_bottom"), clip.cropBottom},
                {QStringLiteral("speed_keyframes"), speedKeyframes},
                {QStringLiteral("motion_keyframes"), motionKeyframes},
                {QStringLiteral("gain_keyframes"), gainKeyframes},
                {QStringLiteral("mask_shape"), static_cast<int>(clip.maskShape)},
                {QStringLiteral("mask_center_x"), clip.maskCenterX},
                {QStringLiteral("mask_center_y"), clip.maskCenterY},
                {QStringLiteral("mask_width"), clip.maskWidth},
                {QStringLiteral("mask_height"), clip.maskHeight},
                {QStringLiteral("mask_feather"), clip.maskFeather},
                {QStringLiteral("mask_inverted"), clip.maskInverted},
                {QStringLiteral("effects"), effects},
            });
        }

        tracks.append(QJsonObject{
            {QStringLiteral("id"), unsignedText(track.id.value)},
            {QStringLiteral("kind"),
             track.kind == videx::core::TrackKind::Video ? QStringLiteral("video")
                                                         : QStringLiteral("audio")},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("sync_locked"), track.syncLocked},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("enabled"), track.enabled},
            {QStringLiteral("solo"), track.solo},
            {QStringLiteral("name"), QString::fromStdString(track.name)},
            {QStringLiteral("color"), unsignedText(track.color)},
            {QStringLiteral("height_mode"), track.heightMode},
            {QStringLiteral("clips"), clips},
        });
    }
    QJsonArray markers;
    for (const auto& marker : sequence.markers()) {
        markers.append(QJsonObject{{QStringLiteral("id"), unsignedText(marker.id.value)},
                                   {QStringLiteral("comment"),
                                    QString::fromStdString(marker.comment)},
                                   {QStringLiteral("color"), unsignedText(marker.color)},
                                   {QStringLiteral("position"), signedText(marker.position)},
                                   {QStringLiteral("name"),
                                    QString::fromStdString(marker.name)}});
    }
    QJsonArray captions;
    for (const auto& caption : sequence.captions()) {
        captions.append(QJsonObject{{QStringLiteral("id"), unsignedText(caption.id.value)},
                                    {QStringLiteral("start"),
                                     signedText(caption.timeline.start)},
                                    {QStringLiteral("duration"),
                                     signedText(caption.timeline.duration)},
                                    {QStringLiteral("text"),
                                     QString::fromStdString(caption.text)},
                                    {QStringLiteral("position_x"), caption.positionX},
                                    {QStringLiteral("position_y"), caption.positionY},
                                    {QStringLiteral("font_size"), caption.fontSize},
                                    {QStringLiteral("text_color"), unsignedText(caption.textColor)},
                                    {QStringLiteral("background_color"),
                                     unsignedText(caption.backgroundColor)},
                                    {QStringLiteral("bold"), caption.bold},
                                    {QStringLiteral("italic"), caption.italic}});
    }

    return {
        {QStringLiteral("frame_rate"),
         QJsonObject{
             {QStringLiteral("numerator"), sequence.frameRate().numerator},
             {QStringLiteral("denominator"), sequence.frameRate().denominator},
         }},
        {QStringLiteral("tracks"), tracks},
        {QStringLiteral("markers"), markers},
        {QStringLiteral("captions"), captions},
    };
}

std::optional<videx::core::Sequence> parseSequence(const QJsonObject& object, QString& error) {
    const QJsonObject rateObject = object.value(QStringLiteral("frame_rate")).toObject();
    const videx::core::FrameRate frameRate{
        .numerator = rateObject.value(QStringLiteral("numerator")).toInt(),
        .denominator = rateObject.value(QStringLiteral("denominator")).toInt(),
    };
    if (!frameRate.isValid()) {
        error = QStringLiteral("Project frame rate is invalid");
        return std::nullopt;
    }

    std::vector<videx::core::Track> tracks;
    QJsonArray trackArray;
    if (!parseArray(object.value(QStringLiteral("tracks")), trackArray)) {
        error = QStringLiteral("Project track list is invalid");
        return std::nullopt;
    }
    tracks.reserve(static_cast<std::size_t>(trackArray.size()));
    for (const QJsonValue& trackValue : trackArray) {
        if (!trackValue.isObject()) {
            error = QStringLiteral("Project track entry is invalid");
            return std::nullopt;
        }

        const QJsonObject trackObject = trackValue.toObject();
        std::uint64_t trackId = 0;
        if (!parseUnsigned(trackObject.value(QStringLiteral("id")), trackId)) {
            error = QStringLiteral("Project track ID is invalid");
            return std::nullopt;
        }

        const QString kindText = trackObject.value(QStringLiteral("kind")).toString();
        if (kindText != QStringLiteral("video") && kindText != QStringLiteral("audio")) {
            error = QStringLiteral("Project track kind is invalid");
            return std::nullopt;
        }
        const auto kind = kindText == QStringLiteral("audio") ? videx::core::TrackKind::Audio
                                                               : videx::core::TrackKind::Video;
        videx::core::Track track{
            .id = videx::core::TrackId{trackId},
            .kind = kind,
            .locked = trackObject.value(QStringLiteral("locked")).toBool(false),
            .syncLocked = trackObject.value(QStringLiteral("sync_locked")).toBool(true),
            .muted = trackObject.value(QStringLiteral("muted")).toBool(false),
            .enabled = trackObject.value(QStringLiteral("enabled")).toBool(true),
            .solo = trackObject.value(QStringLiteral("solo")).toBool(false),
            .name = trackObject.value(QStringLiteral("name")).toString().toStdString(),
        };
        if (trackObject.contains(QStringLiteral("color"))) {
            std::uint64_t trackColor = 0;
            if (!parseUnsigned(trackObject.value(QStringLiteral("color")), trackColor) ||
                trackColor > std::numeric_limits<std::uint32_t>::max()) {
                error = QStringLiteral("Project track color is invalid");
                return std::nullopt;
            }
            track.color = static_cast<std::uint32_t>(trackColor);
        }
        track.heightMode = std::clamp(
            trackObject.value(QStringLiteral("height_mode")).toInt(1), 0, 2);

        QJsonArray clipArray;
        if (!parseArray(trackObject.value(QStringLiteral("clips")), clipArray)) {
            error = QStringLiteral("Project clip list is invalid");
            return std::nullopt;
        }
        track.clips.reserve(static_cast<std::size_t>(clipArray.size()));
        for (const QJsonValue& clipValue : clipArray) {
            const QJsonObject clipObject = clipValue.toObject();
            std::uint64_t clipId = 0;
            std::uint64_t assetId = 0;
            std::uint64_t linkId = 0;
            std::int64_t start = 0;
            std::int64_t duration = 0;
            std::int64_t sourceStart = 0;
            std::int64_t fadeInFrames = 0;
            std::int64_t fadeOutFrames = 0;
            std::int64_t videoTransitionInFrames = 0;
            std::int64_t audioTransitionInFrames = 0;
            if (!clipValue.isObject() ||
                !parseUnsigned(clipObject.value(QStringLiteral("id")), clipId) ||
                !parseUnsigned(clipObject.value(QStringLiteral("asset_id")), assetId) ||
                (!clipObject.value(QStringLiteral("link_id")).isUndefined() &&
                 !parseUnsigned(clipObject.value(QStringLiteral("link_id")), linkId)) ||
                !parseSigned(clipObject.value(QStringLiteral("start")), start) ||
                !parseSigned(clipObject.value(QStringLiteral("duration")), duration) ||
                !parseSigned(clipObject.value(QStringLiteral("source_start")), sourceStart) ||
                (!clipObject.value(QStringLiteral("fade_in_frames")).isUndefined() &&
                 !parseSigned(clipObject.value(QStringLiteral("fade_in_frames")), fadeInFrames)) ||
                (!clipObject.value(QStringLiteral("fade_out_frames")).isUndefined() &&
                 !parseSigned(clipObject.value(QStringLiteral("fade_out_frames")), fadeOutFrames)) ||
                (!clipObject.value(QStringLiteral("video_transition_in_frames")).isUndefined() &&
                 !parseSigned(clipObject.value(QStringLiteral("video_transition_in_frames")),
                              videoTransitionInFrames)) ||
                (!clipObject.value(QStringLiteral("audio_transition_in_frames")).isUndefined() &&
                 !parseSigned(clipObject.value(QStringLiteral("audio_transition_in_frames")),
                              audioTransitionInFrames))) {
                error = QStringLiteral("Project clip entry is invalid");
                return std::nullopt;
            }
            std::vector<videx::core::ClipEffect> effects;
            std::vector<videx::core::SpeedKeyframe> speedKeyframes;
            std::vector<videx::core::MotionKeyframe> motionKeyframes;
            std::vector<videx::core::GainKeyframe> gainKeyframes;
            const int maskShape = clipObject.value(QStringLiteral("mask_shape")).toInt(0);
            if (maskShape < static_cast<int>(videx::core::MaskShape::None) ||
                maskShape > static_cast<int>(videx::core::MaskShape::Ellipse)) {
                error = QStringLiteral("Project clip mask is invalid");
                return std::nullopt;
            }
            QJsonArray speedKeyframeArray;
            QJsonArray motionKeyframeArray;
            QJsonArray gainKeyframeArray;
            QJsonArray effectArray;
            if (!parseArray(clipObject.value(QStringLiteral("speed_keyframes")),
                            speedKeyframeArray) ||
                !parseArray(clipObject.value(QStringLiteral("motion_keyframes")),
                            motionKeyframeArray) ||
                !parseArray(clipObject.value(QStringLiteral("gain_keyframes")),
                            gainKeyframeArray) ||
                !parseArray(clipObject.value(QStringLiteral("effects")), effectArray)) {
                error = QStringLiteral("Project clip keyframe/effect list is invalid");
                return std::nullopt;
            }
            for (const QJsonValue& keyframeValue : speedKeyframeArray) {
                const QJsonObject keyframeObject = keyframeValue.toObject();
                std::int64_t frameOffset = 0;
                const int interpolation =
                    keyframeObject.value(QStringLiteral("interpolation")).toInt(0);
                if (!keyframeValue.isObject() ||
                    !parseSigned(keyframeObject.value(QStringLiteral("frame")), frameOffset) ||
                    interpolation < 0 || interpolation >
                        static_cast<int>(videx::core::KeyframeInterpolation::EaseInOut)) {
                    error = QStringLiteral("Project speed keyframe is invalid");
                    return std::nullopt;
                }
                speedKeyframes.push_back({
                    .frameOffset = frameOffset,
                    .rate = keyframeObject.value(QStringLiteral("rate")).toDouble(1.0),
                    .interpolation =
                        static_cast<videx::core::KeyframeInterpolation>(interpolation),
                });
            }
            for (const QJsonValue& keyframeValue : motionKeyframeArray) {
                const QJsonObject keyframeObject = keyframeValue.toObject();
                std::int64_t frameOffset = 0;
                const int interpolation =
                    keyframeObject.value(QStringLiteral("interpolation")).toInt(0);
                if (!keyframeValue.isObject() ||
                    !parseSigned(keyframeObject.value(QStringLiteral("frame")), frameOffset) ||
                    interpolation < 0 || interpolation >
                        static_cast<int>(videx::core::KeyframeInterpolation::EaseInOut)) {
                    error = QStringLiteral("Project motion keyframe is invalid");
                    return std::nullopt;
                }
                motionKeyframes.push_back({
                    .frameOffset = frameOffset,
                    .opacity = keyframeObject.value(QStringLiteral("opacity")).toDouble(1.0),
                    .positionX = keyframeObject.value(QStringLiteral("position_x")).toDouble(),
                    .positionY = keyframeObject.value(QStringLiteral("position_y")).toDouble(),
                    .scaleX = keyframeObject.value(QStringLiteral("scale_x")).toDouble(1.0),
                    .scaleY = keyframeObject.value(QStringLiteral("scale_y")).toDouble(1.0),
                    .rotationDegrees = keyframeObject.value(
                        QStringLiteral("rotation_degrees")).toDouble(),
                    .anchorX = keyframeObject.value(QStringLiteral("anchor_x")).toDouble(0.5),
                    .anchorY = keyframeObject.value(QStringLiteral("anchor_y")).toDouble(0.5),
                    .interpolation =
                        static_cast<videx::core::KeyframeInterpolation>(interpolation),
                });
            }
            for (const QJsonValue& keyframeValue : gainKeyframeArray) {
                const QJsonObject keyframeObject = keyframeValue.toObject();
                std::int64_t frameOffset = 0;
                const int interpolation =
                    keyframeObject.value(QStringLiteral("interpolation")).toInt(0);
                if (!keyframeValue.isObject() ||
                    !parseSigned(keyframeObject.value(QStringLiteral("frame")), frameOffset) ||
                    interpolation < 0 || interpolation >
                        static_cast<int>(videx::core::KeyframeInterpolation::EaseInOut)) {
                    error = QStringLiteral("Project gain keyframe is invalid");
                    return std::nullopt;
                }
                gainKeyframes.push_back({
                    .frameOffset = frameOffset,
                    .gainDb = keyframeObject.value(QStringLiteral("gain_db")).toDouble(),
                    .interpolation =
                        static_cast<videx::core::KeyframeInterpolation>(interpolation),
                });
            }
            for (const QJsonValue& effectValue : effectArray) {
                const QJsonObject effectObject = effectValue.toObject();
                std::uint64_t effectId = 0;
                const int type = effectObject.value(QStringLiteral("type")).toInt(-1);
                if (!effectValue.isObject() ||
                    !parseUnsigned(effectObject.value(QStringLiteral("id")), effectId) ||
                    type < static_cast<int>(videx::core::EffectType::Brightness) ||
                    type > static_cast<int>(videx::core::EffectType::Vignette)) {
                    error = QStringLiteral("Project effect entry is invalid");
                    return std::nullopt;
                }
                videx::core::ClipEffect effect{
                    .id = videx::core::EffectId{effectId},
                    .type = static_cast<videx::core::EffectType>(type),
                    .enabled = effectObject.value(QStringLiteral("enabled")).toBool(true),
                    .amount = effectObject.value(QStringLiteral("amount")).toDouble(0.0),
                };
                QJsonArray effectKeyframeArray;
                if (!parseArray(effectObject.value(QStringLiteral("keyframes")),
                                effectKeyframeArray)) {
                    error = QStringLiteral("Project effect keyframe list is invalid");
                    return std::nullopt;
                }
                for (const QJsonValue& keyframeValue : effectKeyframeArray) {
                    const QJsonObject keyframeObject = keyframeValue.toObject();
                    std::int64_t frameOffset = 0;
                    const int interpolation =
                        keyframeObject.value(QStringLiteral("interpolation")).toInt(0);
                    if (!keyframeValue.isObject() ||
                        !parseSigned(keyframeObject.value(QStringLiteral("frame")), frameOffset) ||
                        interpolation < 0 ||
                        interpolation >
                            static_cast<int>(videx::core::KeyframeInterpolation::EaseInOut)) {
                        error = QStringLiteral("Project effect keyframe is invalid");
                        return std::nullopt;
                    }
                    effect.keyframes.push_back({
                        .frameOffset = frameOffset,
                        .value = keyframeObject.value(QStringLiteral("value")).toDouble(),
                        .interpolation =
                            static_cast<videx::core::KeyframeInterpolation>(interpolation),
                    });
                }
                effects.push_back(std::move(effect));
            }
            track.clips.push_back({
                .id = videx::core::ClipId{clipId},
                .assetId = videx::core::AssetId{assetId},
                .timeline = {.start = start, .duration = duration},
                .sourceStart = sourceStart,
                .linkId = videx::core::LinkId{linkId},
                .opacity = clipObject.value(QStringLiteral("opacity")).toDouble(1.0),
                .audioGainDb = clipObject.value(QStringLiteral("audio_gain_db")).toDouble(0.0),
                .playbackRate = clipObject.value(QStringLiteral("playback_rate")).toDouble(1.0),
                .fadeInFrames = fadeInFrames,
                .fadeOutFrames = fadeOutFrames,
                .videoTransitionInFrames = videoTransitionInFrames,
                .audioTransitionInFrames = audioTransitionInFrames,
                .positionX = clipObject.value(QStringLiteral("position_x")).toDouble(0.0),
                .positionY = clipObject.value(QStringLiteral("position_y")).toDouble(0.0),
                .scaleX = clipObject.value(QStringLiteral("scale_x")).toDouble(1.0),
                .scaleY = clipObject.value(QStringLiteral("scale_y")).toDouble(1.0),
                .rotationDegrees =
                    clipObject.value(QStringLiteral("rotation_degrees")).toDouble(0.0),
                .anchorX = clipObject.value(QStringLiteral("anchor_x")).toDouble(0.5),
                .anchorY = clipObject.value(QStringLiteral("anchor_y")).toDouble(0.5),
                .cropLeft = clipObject.value(QStringLiteral("crop_left")).toDouble(0.0),
                .cropRight = clipObject.value(QStringLiteral("crop_right")).toDouble(0.0),
                .cropTop = clipObject.value(QStringLiteral("crop_top")).toDouble(0.0),
                .cropBottom = clipObject.value(QStringLiteral("crop_bottom")).toDouble(0.0),
                .speedKeyframes = std::move(speedKeyframes),
                .motionKeyframes = std::move(motionKeyframes),
                .gainKeyframes = std::move(gainKeyframes),
                .maskShape = static_cast<videx::core::MaskShape>(maskShape),
                .maskCenterX = clipObject.value(QStringLiteral("mask_center_x")).toDouble(0.5),
                .maskCenterY = clipObject.value(QStringLiteral("mask_center_y")).toDouble(0.5),
                .maskWidth = clipObject.value(QStringLiteral("mask_width")).toDouble(1.0),
                .maskHeight = clipObject.value(QStringLiteral("mask_height")).toDouble(1.0),
                .maskFeather = clipObject.value(QStringLiteral("mask_feather")).toDouble(0.0),
                .maskInverted = clipObject.value(QStringLiteral("mask_inverted")).toBool(false),
                .effects = std::move(effects),
            });
        }
        tracks.push_back(std::move(track));
    }

    std::vector<videx::core::Marker> markers;
    QJsonArray markerArray;
    if (!parseArray(object.value(QStringLiteral("markers")), markerArray)) {
        error = QStringLiteral("Project marker list is invalid");
        return std::nullopt;
    }
    for (const QJsonValue& value : markerArray) {
        const QJsonObject markerObject = value.toObject();
        std::uint64_t id = 0;
        std::int64_t position = 0;
        std::uint64_t color = 0xFF2E9E4FULL;
        if (!value.isObject() || !parseUnsigned(markerObject.value(QStringLiteral("id")), id) ||
            !parseSigned(markerObject.value(QStringLiteral("position")), position)) {
            error = QStringLiteral("Project marker entry is invalid");
            return std::nullopt;
        }
        if (markerObject.contains(QStringLiteral("color")) &&
            (!parseUnsigned(markerObject.value(QStringLiteral("color")), color) ||
             color > 0xFFFFFFFFULL)) {
            error = QStringLiteral("Project marker color is invalid");
            return std::nullopt;
        }
        markers.push_back({.id = videx::core::MarkerId{id},
                           .position = position,
                           .name = markerObject.value(QStringLiteral("name")).toString().toStdString(),
                           .comment = markerObject.value(QStringLiteral("comment"))
                                          .toString().toStdString(),
                           .color = static_cast<std::uint32_t>(color)});
    }
    std::vector<videx::core::Caption> captions;
    QJsonArray captionArray;
    if (!parseArray(object.value(QStringLiteral("captions")), captionArray)) {
        error = QStringLiteral("Project caption list is invalid");
        return std::nullopt;
    }
    for (const QJsonValue& value : captionArray) {
        const QJsonObject captionObject = value.toObject();
        std::uint64_t id = 0;
        std::int64_t start = 0;
        std::int64_t duration = 0;
        std::uint64_t textColor = 0xFFFFFFFFU;
        std::uint64_t backgroundColor = 0x99000000U;
        if (!value.isObject() || !parseUnsigned(captionObject.value(QStringLiteral("id")), id) ||
            !parseSigned(captionObject.value(QStringLiteral("start")), start) ||
            !parseSigned(captionObject.value(QStringLiteral("duration")), duration)) {
            error = QStringLiteral("Project caption entry is invalid");
            return std::nullopt;
        }
        if (captionObject.contains(QStringLiteral("text_color")) &&
            (!parseUnsigned(captionObject.value(QStringLiteral("text_color")), textColor) ||
             textColor > 0xFFFFFFFFULL)) {
            error = QStringLiteral("Project caption text color is invalid");
            return std::nullopt;
        }
        if (captionObject.contains(QStringLiteral("background_color")) &&
            (!parseUnsigned(captionObject.value(QStringLiteral("background_color")),
                            backgroundColor) ||
             backgroundColor > 0xFFFFFFFFULL)) {
            error = QStringLiteral("Project caption background color is invalid");
            return std::nullopt;
        }
        captions.push_back({.id = videx::core::CaptionId{id},
                            .timeline = {.start = start, .duration = duration},
                            .text = captionObject.value(QStringLiteral("text")).toString().toStdString(),
                            .positionX = captionObject.value(QStringLiteral("position_x")).toDouble(0.5),
                            .positionY = captionObject.value(QStringLiteral("position_y")).toDouble(0.85),
                            .fontSize = captionObject.value(QStringLiteral("font_size")).toDouble(42.0),
                            .textColor = static_cast<std::uint32_t>(textColor),
                            .backgroundColor = static_cast<std::uint32_t>(backgroundColor),
                            .bold = captionObject.value(QStringLiteral("bold")).toBool(false),
                            .italic = captionObject.value(QStringLiteral("italic")).toBool(false)});
    }

    try {
        return videx::core::Sequence::fromTracks(frameRate, std::move(tracks),
                                                 std::move(markers), std::move(captions));
    } catch (const std::exception& exception) {
        error = QStringLiteral("Project timeline is invalid: %1")
                    .arg(QString::fromUtf8(exception.what()));
        return std::nullopt;
    }
}

} // namespace

namespace videx::ui {

bool saveProjectFile(const QString& path, const ProjectData& project, QString& error) {
    QJsonArray assets;
    for (const ProjectAsset& asset : project.assets) {
        assets.append(QJsonObject{
            {QStringLiteral("id"), unsignedText(asset.id.value)},
            {QStringLiteral("path"), asset.path},
            {QStringLiteral("metadata"), asset.metadata},
        });
    }

    const QJsonObject root{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("sequence"), serializeSequence(project.sequence)},
        {QStringLiteral("assets"), assets},
    };
    const QByteArray serialized = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    if (file.write(serialized) != serialized.size()) {
        error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

std::optional<ProjectData> loadProjectFile(const QString& path, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Project JSON is invalid: %1").arg(parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_version")).toInt() != 1) {
        error = QStringLiteral("Project schema version is not supported");
        return std::nullopt;
    }

    auto sequence = parseSequence(root.value(QStringLiteral("sequence")).toObject(), error);
    if (!sequence.has_value()) {
        return std::nullopt;
    }

    std::vector<ProjectAsset> assets;
    std::set<std::uint64_t> assetIds;
    QJsonArray assetArray;
    if (!parseArray(root.value(QStringLiteral("assets")), assetArray)) {
        error = QStringLiteral("Project asset list is invalid");
        return std::nullopt;
    }
    assets.reserve(static_cast<std::size_t>(assetArray.size()));
    for (const QJsonValue& assetValue : assetArray) {
        const QJsonObject assetObject = assetValue.toObject();
        std::uint64_t assetId = 0;
        if (!assetValue.isObject() ||
            !parseUnsigned(assetObject.value(QStringLiteral("id")), assetId) || assetId == 0 ||
            assetId == std::numeric_limits<std::uint64_t>::max() ||
            !assetIds.insert(assetId).second ||
            !assetObject.value(QStringLiteral("path")).isString() ||
            !assetObject.value(QStringLiteral("metadata")).isObject()) {
            error = QStringLiteral("Project asset entry is invalid");
            return std::nullopt;
        }
        assets.push_back({
            .id = core::AssetId{assetId},
            .path = assetObject.value(QStringLiteral("path")).toString(),
            .metadata = assetObject.value(QStringLiteral("metadata")).toObject(),
        });
    }

    for (const core::Track& track : sequence->tracks()) {
        for (const core::Clip& clip : track.clips) {
            if (!assetIds.contains(clip.assetId.value)) {
                error = QStringLiteral("Project clip references an unknown asset");
                return std::nullopt;
            }
        }
    }

    return ProjectData{.sequence = std::move(*sequence), .assets = std::move(assets)};
}

} // namespace videx::ui
