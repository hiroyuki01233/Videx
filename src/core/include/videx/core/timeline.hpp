#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace videx::core {

using Frame = std::int64_t;

struct FrameRate final {
    std::int32_t numerator = 24;
    std::int32_t denominator = 1;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] double framesPerSecond() const noexcept;
    auto operator<=>(const FrameRate&) const = default;
};

struct FrameRange final {
    Frame start = 0;
    Frame duration = 0;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] Frame end() const;
    [[nodiscard]] bool contains(Frame position) const;
    [[nodiscard]] bool overlaps(const FrameRange& other) const;
    auto operator<=>(const FrameRange&) const = default;
};

template <typename Tag> struct EntityId final {
    std::uint64_t value = 0;

    [[nodiscard]] explicit operator bool() const noexcept {
        return value != 0;
    }
    auto operator<=>(const EntityId&) const = default;
};

struct AssetTag;
struct ClipTag;
struct TrackTag;
struct LinkTag;
struct MarkerTag;
struct CaptionTag;
struct EffectTag;

using AssetId = EntityId<AssetTag>;
using ClipId = EntityId<ClipTag>;
using TrackId = EntityId<TrackTag>;
using LinkId = EntityId<LinkTag>;
using MarkerId = EntityId<MarkerTag>;
using CaptionId = EntityId<CaptionTag>;
using EffectId = EntityId<EffectTag>;

enum class TrackKind {
    Video,
    Audio,
};

enum class EffectType { Brightness, Contrast, Saturation, Blur, Vignette };
enum class KeyframeInterpolation { Linear, Hold, Ease, EaseIn, EaseOut, EaseInOut };
enum class MaskShape { None, Rectangle, Ellipse };

// Shared easing curve: maps a linear 0..1 ratio to the interpolated progress.
// Every evaluator (app preview, worker render, export) must use this exact
// math so preview and export stay identical.
[[nodiscard]] constexpr double interpolationProgress(
    const KeyframeInterpolation interpolation, const double ratio) noexcept {
    switch (interpolation) {
    case KeyframeInterpolation::Hold:
        return 0.0;
    case KeyframeInterpolation::Ease:
        return ratio * ratio * (3.0 - 2.0 * ratio);
    case KeyframeInterpolation::EaseIn:
        return ratio * ratio * ratio;
    case KeyframeInterpolation::EaseOut: {
        const double inverse = 1.0 - ratio;
        return 1.0 - inverse * inverse * inverse;
    }
    case KeyframeInterpolation::EaseInOut: {
        if (ratio < 0.5) {
            return 4.0 * ratio * ratio * ratio;
        }
        const double inverse = 1.0 - ratio;
        return 1.0 - 4.0 * inverse * inverse * inverse;
    }
    case KeyframeInterpolation::Linear:
    default:
        return ratio;
    }
}

// Antiderivative of interpolationProgress over [0, ratio]; used to integrate
// speed-ramp segments analytically. Must stay byte-identical with the media
// worker's copy so preview and export read the same source frames.
[[nodiscard]] constexpr double interpolationIntegral(
    const KeyframeInterpolation interpolation, const double ratio) noexcept {
    switch (interpolation) {
    case KeyframeInterpolation::Hold:
        return 0.0;
    case KeyframeInterpolation::Ease:
        return ratio * ratio * ratio - 0.5 * ratio * ratio * ratio * ratio;
    case KeyframeInterpolation::EaseIn:
        return 0.25 * ratio * ratio * ratio * ratio;
    case KeyframeInterpolation::EaseOut: {
        const double inverse = 1.0 - ratio;
        return ratio + (inverse * inverse * inverse * inverse - 1.0) * 0.25;
    }
    case KeyframeInterpolation::EaseInOut: {
        if (ratio < 0.5) {
            return ratio * ratio * ratio * ratio;
        }
        const double inverse = 1.0 - ratio;
        return ratio - 0.5 + inverse * inverse * inverse * inverse;
    }
    case KeyframeInterpolation::Linear:
    default:
        return 0.5 * ratio * ratio;
    }
}

struct EffectKeyframe final {
    Frame frameOffset = 0;
    double value = 0.0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    auto operator<=>(const EffectKeyframe&) const = default;
};

struct ClipEffect final {
    EffectId id;
    EffectType type = EffectType::Brightness;
    bool enabled = true;
    double amount = 0.0;
    std::vector<EffectKeyframe> keyframes;
    auto operator<=>(const ClipEffect&) const = default;
};

struct SpeedKeyframe final {
    Frame frameOffset = 0;
    double rate = 1.0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    auto operator<=>(const SpeedKeyframe&) const = default;
};

struct MotionKeyframe final {
    Frame frameOffset = 0;
    double opacity = 1.0;
    double positionX = 0.0;
    double positionY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDegrees = 0.0;
    double anchorX = 0.5;
    double anchorY = 0.5;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    auto operator<=>(const MotionKeyframe&) const = default;
};

struct GainKeyframe final {
    Frame frameOffset = 0;
    double gainDb = 0.0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    auto operator<=>(const GainKeyframe&) const = default;
};

struct Clip final {
    ClipId id;
    AssetId assetId;
    FrameRange timeline;
    Frame sourceStart = 0;
    LinkId linkId;
    double opacity = 1.0;
    double audioGainDb = 0.0;
    double playbackRate = 1.0;
    Frame fadeInFrames = 0;
    Frame fadeOutFrames = 0;
    Frame videoTransitionInFrames = 0;
    Frame audioTransitionInFrames = 0;
    double positionX = 0.0;
    double positionY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDegrees = 0.0;
    double anchorX = 0.5;
    double anchorY = 0.5;
    double cropLeft = 0.0;
    double cropRight = 0.0;
    double cropTop = 0.0;
    double cropBottom = 0.0;
    std::vector<SpeedKeyframe> speedKeyframes;
    std::vector<MotionKeyframe> motionKeyframes;
    std::vector<GainKeyframe> gainKeyframes;
    MaskShape maskShape = MaskShape::None;
    double maskCenterX = 0.5;
    double maskCenterY = 0.5;
    double maskWidth = 1.0;
    double maskHeight = 1.0;
    double maskFeather = 0.0;
    bool maskInverted = false;
    std::vector<ClipEffect> effects;

    auto operator<=>(const Clip&) const = default;
};

struct Track final {
    TrackId id;
    TrackKind kind = TrackKind::Video;
    bool locked = false;
    bool syncLocked = true;
    bool muted = false;
    bool enabled = true;
    // Audio-only: when any track is soloed, only soloed tracks are mixed.
    bool solo = false;
    // Optional user-facing name; empty means the kind-relative default (V1...).
    std::string name;
    // 0 keeps the theme color; otherwise an RGBA label color.
    std::uint32_t color = 0;
    // Display height preset: 0 = minimal, 1 = standard, 2 = expanded.
    std::int32_t heightMode = 1;
    std::vector<Clip> clips;
};

struct Marker final {
    MarkerId id;
    Frame position = 0;
    std::string name;
    std::string comment;
    // ARGB flag colour shown on the timeline ruler; defaults to Premiere-style
    // green so legacy projects load with the familiar look.
    std::uint32_t color = 0xFF2E9E4FU;
    auto operator<=>(const Marker&) const = default;
};

struct Caption final {
    CaptionId id;
    FrameRange timeline;
    std::string text;
    double positionX = 0.5;
    double positionY = 0.85;
    double fontSize = 42.0;
    std::uint32_t textColor = 0xFFFFFFFFU;
    std::uint32_t backgroundColor = 0x99000000U;
    bool bold = false;
    bool italic = false;
    auto operator<=>(const Caption&) const = default;
};

struct ClipPlacement final {
    TrackId targetTrack;
    TrackKind kind = TrackKind::Video;
    Clip clip;
    Frame timelineStart = 0;
};

enum class EditError {
    None,
    InvalidRange,
    InvalidAsset,
    TrackNotFound,
    ClipNotFound,
    TrackLocked,
    Collision,
    Overflow,
    StaleRevision,
    NothingToUndo,
    NothingToRedo,
};

struct EditResult final {
    EditError error = EditError::None;
    std::uint64_t revision = 0;
    ClipId primaryClip;
    TrackId primaryTrack;
    EffectId primaryEffect;
    std::vector<ClipId> createdClips;
    std::string message;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == EditError::None;
    }
};

class Sequence final {
public:
    explicit Sequence(FrameRate frameRate = {});
    [[nodiscard]] static Sequence fromTracks(FrameRate frameRate, std::vector<Track> tracks,
                                             std::vector<Marker> markers = {},
                                             std::vector<Caption> captions = {});

    [[nodiscard]] FrameRate frameRate() const noexcept {
        return frameRate_;
    }
    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept {
        return tracks_;
    }
    [[nodiscard]] const std::vector<Marker>& markers() const noexcept { return markers_; }
    [[nodiscard]] const std::vector<Caption>& captions() const noexcept { return captions_; }

    TrackId addTrack(TrackKind kind);
    EditResult removeTrack(TrackId trackId);
    EditResult moveTrack(TrackId trackId, std::int32_t delta);
    EditResult setTrackLocked(TrackId trackId, bool locked);
    EditResult setTrackSyncLocked(TrackId trackId, bool syncLocked);
    EditResult setTrackMuted(TrackId trackId, bool muted);
    EditResult setTrackEnabled(TrackId trackId, bool enabled);
    EditResult setTrackSolo(TrackId trackId, bool solo);
    EditResult setTrackHeightMode(TrackId trackId, std::int32_t heightMode);
    EditResult setClipProperties(ClipId clipId, double opacity, double audioGainDb,
                                 double playbackRate);
    EditResult setClipLink(ClipId clipId, LinkId linkId);
    EditResult setClipFades(ClipId clipId, Frame fadeInFrames, Frame fadeOutFrames);
    EditResult setClipTransitions(ClipId clipId, Frame videoFrames, Frame audioFrames);
    EditResult setClipTransform(ClipId clipId, double positionX, double positionY,
                                double scaleX, double scaleY, double rotationDegrees,
                                double anchorX, double anchorY);
    EditResult setClipCrop(ClipId clipId, double left, double right, double top,
                           double bottom);
    EditResult setMotionKeyframe(ClipId clipId, Frame frameOffset, double opacity,
                                 double positionX, double positionY, double scaleX,
                                 double scaleY, double rotationDegrees, double anchorX,
                                 double anchorY, KeyframeInterpolation interpolation);
    EditResult removeMotionKeyframe(ClipId clipId, Frame frameOffset);
    EditResult setGainKeyframe(ClipId clipId, Frame frameOffset, double gainDb,
                               KeyframeInterpolation interpolation);
    EditResult removeGainKeyframe(ClipId clipId, Frame frameOffset);
    EditResult setSpeedKeyframe(ClipId clipId, Frame frameOffset, double rate,
                                KeyframeInterpolation interpolation);
    EditResult removeSpeedKeyframe(ClipId clipId, Frame frameOffset);
    EditResult setClipMask(ClipId clipId, MaskShape shape, double centerX, double centerY,
                           double width, double height, double feather, bool inverted);
    EditResult addEffect(ClipId clipId, EffectType type);
    EditResult removeEffect(ClipId clipId, EffectId effectId);
    EditResult moveEffect(ClipId clipId, EffectId effectId, std::int32_t delta);
    EditResult setEffect(ClipId clipId, EffectId effectId, bool enabled, double amount);
    EditResult setEffectKeyframe(ClipId clipId, EffectId effectId, Frame frameOffset,
                                 double value, KeyframeInterpolation interpolation);
    EditResult removeEffectKeyframe(ClipId clipId, EffectId effectId, Frame frameOffset);
    EditResult addMarker(Frame position, std::string name);
    EditResult setMarker(MarkerId markerId, Frame position, std::string name,
                         std::string comment, std::uint32_t color);
    EditResult removeMarker(MarkerId markerId);
    EditResult addCaption(FrameRange range, std::string text);
    EditResult setCaption(CaptionId captionId, FrameRange range, std::string text,
                          double positionX, double positionY, double fontSize,
                          std::uint32_t textColor, std::uint32_t backgroundColor,
                          bool bold, bool italic);
    EditResult removeCaption(CaptionId captionId);

    EditResult insertClip(TrackId targetTrack, AssetId assetId, Frame timelineStart,
                          Frame sourceStart, Frame duration, LinkId linkId = {});
    EditResult overwriteClip(TrackId targetTrack, AssetId assetId, Frame timelineStart,
                             Frame sourceStart, Frame duration, LinkId linkId = {});
    EditResult moveClip(ClipId clipId, Frame newTimelineStart);
    EditResult moveClipToTrack(ClipId clipId, TrackId targetTrack, Frame newTimelineStart);
    EditResult pasteClips(std::vector<ClipPlacement> placements);
    EditResult trimClip(ClipId clipId, Frame newTimelineStart, Frame newTimelineEnd);
    EditResult slipClip(ClipId clipId, Frame sourceDelta);
    EditResult rollEdit(ClipId leftClipId, ClipId rightClipId, Frame newCut);
    EditResult rippleTrimEnd(ClipId clipId, Frame newTimelineEnd);
    EditResult slideClip(ClipId clipId, Frame timelineDelta);
    EditResult splitClip(ClipId clipId, Frame splitPosition, LinkId rightLinkId = {});
    EditResult liftClip(ClipId clipId);
    EditResult liftRange(FrameRange range);
    EditResult extractRange(FrameRange range);

    [[nodiscard]] const Track* findTrack(TrackId trackId) const noexcept;
    [[nodiscard]] const Clip* findClip(ClipId clipId) const noexcept;

  private:
    friend class EditSession;

    void restoreContent(const Sequence& snapshot, std::uint64_t newRevision);
    [[nodiscard]] std::uint64_t nextId() noexcept;

    FrameRate frameRate_;
    std::uint64_t revision_ = 0;
    std::uint64_t nextEntityId_ = 1;
    std::vector<Track> tracks_;
    std::vector<Marker> markers_;
    std::vector<Caption> captions_;
};

} // namespace videx::core
