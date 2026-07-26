#pragma once

#include <videx/core/timeline.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace videx::core {

struct AddTrackCommand final {
    TrackKind kind = TrackKind::Video;
};
struct RemoveTrackCommand final { TrackId trackId; };
struct MoveTrackCommand final { TrackId trackId; std::int32_t delta = 0; };

struct SetTrackLockedCommand final {
    TrackId trackId;
    bool locked = false;
};

struct SetTrackSyncLockedCommand final {
    TrackId trackId;
    bool syncLocked = true;
};

struct SetTrackMutedCommand final {
    TrackId trackId;
    bool muted = false;
};

struct SetTrackEnabledCommand final {
    TrackId trackId;
    bool enabled = true;
};

struct SetTrackSoloCommand final {
    TrackId trackId;
    bool solo = false;
};

struct SetTrackHeightModeCommand final {
    TrackId trackId;
    std::int32_t heightMode = 1;
};

struct InsertClipCommand final {
    TrackId targetTrack;
    AssetId assetId;
    Frame timelineStart = 0;
    Frame sourceStart = 0;
    Frame duration = 0;
    LinkId linkId;
};

struct OverwriteClipCommand final {
    TrackId targetTrack;
    AssetId assetId;
    Frame timelineStart = 0;
    Frame sourceStart = 0;
    Frame duration = 0;
    LinkId linkId;
};

struct MoveClipCommand final {
    ClipId clipId;
    Frame newTimelineStart = 0;
};

struct MoveClipToTrackCommand final {
    ClipId clipId;
    TrackId targetTrack;
    Frame newTimelineStart = 0;
};

struct PasteClipsCommand final {
    std::vector<ClipPlacement> placements;
};

struct TrimClipCommand final {
    ClipId clipId;
    Frame newTimelineStart = 0;
    Frame newTimelineEnd = 0;
};

struct SlipClipCommand final {
    ClipId clipId;
    Frame sourceDelta = 0;
};

struct RollEditCommand final {
    ClipId leftClipId;
    ClipId rightClipId;
    Frame newCut = 0;
};

struct RippleTrimEndCommand final {
    ClipId clipId;
    Frame newTimelineEnd = 0;
};

struct SlideClipCommand final {
    ClipId clipId;
    Frame timelineDelta = 0;
};

struct SplitClipCommand final {
    ClipId clipId;
    Frame splitPosition = 0;
    LinkId rightLinkId;
};

struct LiftClipCommand final {
    ClipId clipId;
};

struct SetClipPropertiesCommand final {
    ClipId clipId;
    double opacity = 1.0;
    double audioGainDb = 0.0;
    double playbackRate = 1.0;
};
struct SetClipLinkCommand final { ClipId clipId; LinkId linkId; };

struct SetClipFadesCommand final {
    ClipId clipId;
    Frame fadeInFrames = 0;
    Frame fadeOutFrames = 0;
};
struct SetClipTransitionsCommand final {
    ClipId clipId;
    Frame videoFrames = 0;
    Frame audioFrames = 0;
};

struct SetClipTransformCommand final {
    ClipId clipId;
    double positionX = 0.0;
    double positionY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDegrees = 0.0;
    double anchorX = 0.5;
    double anchorY = 0.5;
};
struct SetClipCropCommand final {
    ClipId clipId;
    double left = 0.0;
    double right = 0.0;
    double top = 0.0;
    double bottom = 0.0;
};
struct SetMotionKeyframeCommand final {
    ClipId clipId;
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
};
struct RemoveMotionKeyframeCommand final { ClipId clipId; Frame frameOffset = 0; };
struct SetGainKeyframeCommand final {
    ClipId clipId;
    Frame frameOffset = 0;
    double gainDb = 0.0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
};
struct RemoveGainKeyframeCommand final { ClipId clipId; Frame frameOffset = 0; };
struct SetSpeedKeyframeCommand final {
    ClipId clipId;
    Frame frameOffset = 0;
    double rate = 1.0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
};
struct RemoveSpeedKeyframeCommand final { ClipId clipId; Frame frameOffset = 0; };
struct SetClipMaskCommand final {
    ClipId clipId;
    MaskShape shape = MaskShape::None;
    double centerX = 0.5;
    double centerY = 0.5;
    double width = 1.0;
    double height = 1.0;
    double feather = 0.0;
    bool inverted = false;
};

struct AddEffectCommand final { ClipId clipId; EffectType type = EffectType::Brightness; };
struct RemoveEffectCommand final { ClipId clipId; EffectId effectId; };
struct MoveEffectCommand final {
    ClipId clipId;
    EffectId effectId;
    std::int32_t delta = 0;
};
struct SetEffectCommand final {
    ClipId clipId;
    EffectId effectId;
    bool enabled = true;
    double amount = 0.0;
};
struct SetEffectKeyframeCommand final {
    ClipId clipId;
    EffectId effectId;
    Frame frameOffset = 0;
    double value = 0.0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
};
struct RemoveEffectKeyframeCommand final {
    ClipId clipId;
    EffectId effectId;
    Frame frameOffset = 0;
};

struct AddMarkerCommand final { Frame position = 0; std::string name; };
struct SetMarkerCommand final {
    MarkerId markerId;
    Frame position = 0;
    std::string name;
    std::string comment;
    std::uint32_t color = 0xFF2E9E4FU;
};
struct RemoveMarkerCommand final { MarkerId markerId; };
struct AddCaptionCommand final { FrameRange range; std::string text; };
struct SetCaptionCommand final {
    CaptionId captionId;
    FrameRange range;
    std::string text;
    double positionX = 0.5;
    double positionY = 0.85;
    double fontSize = 42.0;
    std::uint32_t textColor = 0xFFFFFFFFU;
    std::uint32_t backgroundColor = 0x99000000U;
    bool bold = false;
    bool italic = false;
};
struct RemoveCaptionCommand final { CaptionId captionId; };

struct ExtractRangeCommand final {
    FrameRange range;
};
struct LiftRangeCommand final { FrameRange range; };

using EditCommand =
    std::variant<AddTrackCommand, RemoveTrackCommand, MoveTrackCommand,
                 SetTrackLockedCommand, SetTrackSyncLockedCommand,
                 SetTrackMutedCommand, SetTrackEnabledCommand, SetTrackSoloCommand,
                 SetTrackHeightModeCommand, InsertClipCommand,
                 OverwriteClipCommand, MoveClipCommand, MoveClipToTrackCommand, PasteClipsCommand,
                 TrimClipCommand, SlipClipCommand,
                 RollEditCommand, RippleTrimEndCommand, SlideClipCommand, SplitClipCommand,
                 LiftClipCommand, SetClipPropertiesCommand, SetClipLinkCommand,
                 SetClipFadesCommand,
                 SetClipTransitionsCommand, SetClipTransformCommand, SetClipCropCommand,
                 SetMotionKeyframeCommand, RemoveMotionKeyframeCommand,
                 SetGainKeyframeCommand, RemoveGainKeyframeCommand,
                 SetSpeedKeyframeCommand, RemoveSpeedKeyframeCommand,
                 SetClipMaskCommand,
                 AddEffectCommand, RemoveEffectCommand, MoveEffectCommand,
                 SetEffectCommand, SetEffectKeyframeCommand, RemoveEffectKeyframeCommand,
                 AddMarkerCommand, SetMarkerCommand,
                 RemoveMarkerCommand, AddCaptionCommand, SetCaptionCommand, RemoveCaptionCommand,
                 LiftRangeCommand, ExtractRangeCommand>;

struct CommandEnvelope final {
    std::uint64_t baseRevision = 0;
    std::string label;
    EditCommand command;
};

struct TransactionEnvelope final {
    std::uint64_t baseRevision = 0;
    std::string label;
    std::vector<EditCommand> commands;
};

class EditSession final {
public:
    explicit EditSession(FrameRate frameRate = {});
    explicit EditSession(Sequence sequence);

    [[nodiscard]] const Sequence& sequence() const noexcept {
        return sequence_;
    }
    [[nodiscard]] bool canUndo() const noexcept {
        return !undoStack_.empty();
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return !redoStack_.empty();
    }
    [[nodiscard]] const std::string& undoLabel() const noexcept;
    [[nodiscard]] const std::string& redoLabel() const noexcept;
    [[nodiscard]] std::vector<std::string> undoLabels() const;
    [[nodiscard]] std::vector<std::string> redoLabels() const;

    EditResult apply(const CommandEnvelope& envelope);
    EditResult apply(const TransactionEnvelope& envelope);
    EditResult undo();
    EditResult redo();
    void clearHistory() noexcept;

  private:
    struct HistoryEntry final {
        Sequence sequence;
        std::string label;
    };

    Sequence sequence_;
    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
};

} // namespace videx::core
