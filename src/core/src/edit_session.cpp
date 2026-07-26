#include <videx/core/edit_session.hpp>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace {

using videx::core::AddTrackCommand;
using videx::core::RemoveTrackCommand;
using videx::core::MoveTrackCommand;
using videx::core::EditCommand;
using videx::core::EditResult;
using videx::core::ExtractRangeCommand;
using videx::core::LiftRangeCommand;
using videx::core::InsertClipCommand;
using videx::core::LiftClipCommand;
using videx::core::MoveClipCommand;
using videx::core::MoveClipToTrackCommand;
using videx::core::PasteClipsCommand;
using videx::core::OverwriteClipCommand;
using videx::core::Sequence;
using videx::core::SetTrackLockedCommand;
using videx::core::SetTrackMutedCommand;
using videx::core::SetTrackEnabledCommand;
using videx::core::SetTrackSoloCommand;
using videx::core::SetTrackHeightModeCommand;
using videx::core::SetTrackSyncLockedCommand;
using videx::core::SetClipPropertiesCommand;
using videx::core::SetClipLinkCommand;
using videx::core::SetClipFadesCommand;
using videx::core::SetClipTransitionsCommand;
using videx::core::SetClipTransformCommand;
using videx::core::SetClipCropCommand;
using videx::core::SetMotionKeyframeCommand;
using videx::core::RemoveMotionKeyframeCommand;
using videx::core::SetGainKeyframeCommand;
using videx::core::RemoveGainKeyframeCommand;
using videx::core::SetSpeedKeyframeCommand;
using videx::core::RemoveSpeedKeyframeCommand;
using videx::core::SetClipMaskCommand;
using videx::core::AddEffectCommand;
using videx::core::RemoveEffectCommand;
using videx::core::MoveEffectCommand;
using videx::core::SetEffectCommand;
using videx::core::SetEffectKeyframeCommand;
using videx::core::RemoveEffectKeyframeCommand;
using videx::core::AddMarkerCommand;
using videx::core::SetMarkerCommand;
using videx::core::RemoveMarkerCommand;
using videx::core::AddCaptionCommand;
using videx::core::SetCaptionCommand;
using videx::core::RemoveCaptionCommand;
using videx::core::SplitClipCommand;
using videx::core::TrimClipCommand;
using videx::core::SlipClipCommand;
using videx::core::RollEditCommand;
using videx::core::RippleTrimEndCommand;
using videx::core::SlideClipCommand;

EditResult execute(Sequence& sequence, const EditCommand& command) {
    return std::visit(
        [&sequence](const auto& typedCommand) -> EditResult {
            using Command = std::decay_t<decltype(typedCommand)>;
            if constexpr (std::is_same_v<Command, AddTrackCommand>) {
                const auto trackId = sequence.addTrack(typedCommand.kind);
                return {.revision = sequence.revision(), .primaryTrack = trackId};
            } else if constexpr (std::is_same_v<Command, RemoveTrackCommand>) {
                return sequence.removeTrack(typedCommand.trackId);
            } else if constexpr (std::is_same_v<Command, MoveTrackCommand>) {
                return sequence.moveTrack(typedCommand.trackId, typedCommand.delta);
            } else if constexpr (std::is_same_v<Command, SetTrackLockedCommand>) {
                return sequence.setTrackLocked(typedCommand.trackId, typedCommand.locked);
            } else if constexpr (std::is_same_v<Command, SetTrackSyncLockedCommand>) {
                return sequence.setTrackSyncLocked(typedCommand.trackId, typedCommand.syncLocked);
            } else if constexpr (std::is_same_v<Command, SetTrackMutedCommand>) {
                return sequence.setTrackMuted(typedCommand.trackId, typedCommand.muted);
            } else if constexpr (std::is_same_v<Command, SetTrackEnabledCommand>) {
                return sequence.setTrackEnabled(typedCommand.trackId, typedCommand.enabled);
            } else if constexpr (std::is_same_v<Command, SetTrackSoloCommand>) {
                return sequence.setTrackSolo(typedCommand.trackId, typedCommand.solo);
            } else if constexpr (std::is_same_v<Command, SetTrackHeightModeCommand>) {
                return sequence.setTrackHeightMode(typedCommand.trackId,
                                                   typedCommand.heightMode);
            } else if constexpr (std::is_same_v<Command, InsertClipCommand>) {
                return sequence.insertClip(typedCommand.targetTrack, typedCommand.assetId,
                                           typedCommand.timelineStart, typedCommand.sourceStart,
                                           typedCommand.duration, typedCommand.linkId);
            } else if constexpr (std::is_same_v<Command, OverwriteClipCommand>) {
                return sequence.overwriteClip(typedCommand.targetTrack, typedCommand.assetId,
                                              typedCommand.timelineStart, typedCommand.sourceStart,
                                              typedCommand.duration, typedCommand.linkId);
            } else if constexpr (std::is_same_v<Command, MoveClipCommand>) {
                return sequence.moveClip(typedCommand.clipId, typedCommand.newTimelineStart);
            } else if constexpr (std::is_same_v<Command, MoveClipToTrackCommand>) {
                return sequence.moveClipToTrack(typedCommand.clipId, typedCommand.targetTrack,
                                                typedCommand.newTimelineStart);
            } else if constexpr (std::is_same_v<Command, PasteClipsCommand>) {
                return sequence.pasteClips(typedCommand.placements);
            } else if constexpr (std::is_same_v<Command, TrimClipCommand>) {
                return sequence.trimClip(typedCommand.clipId, typedCommand.newTimelineStart,
                                         typedCommand.newTimelineEnd);
            } else if constexpr (std::is_same_v<Command, SlipClipCommand>) {
                return sequence.slipClip(typedCommand.clipId, typedCommand.sourceDelta);
            } else if constexpr (std::is_same_v<Command, RollEditCommand>) {
                return sequence.rollEdit(typedCommand.leftClipId, typedCommand.rightClipId,
                                         typedCommand.newCut);
            } else if constexpr (std::is_same_v<Command, RippleTrimEndCommand>) {
                return sequence.rippleTrimEnd(typedCommand.clipId,
                                              typedCommand.newTimelineEnd);
            } else if constexpr (std::is_same_v<Command, SlideClipCommand>) {
                return sequence.slideClip(typedCommand.clipId, typedCommand.timelineDelta);
            } else if constexpr (std::is_same_v<Command, SplitClipCommand>) {
                return sequence.splitClip(typedCommand.clipId, typedCommand.splitPosition,
                                          typedCommand.rightLinkId);
            } else if constexpr (std::is_same_v<Command, LiftClipCommand>) {
                return sequence.liftClip(typedCommand.clipId);
            } else if constexpr (std::is_same_v<Command, SetClipPropertiesCommand>) {
                return sequence.setClipProperties(typedCommand.clipId, typedCommand.opacity,
                                                  typedCommand.audioGainDb,
                                                  typedCommand.playbackRate);
            } else if constexpr (std::is_same_v<Command, SetClipLinkCommand>) {
                return sequence.setClipLink(typedCommand.clipId, typedCommand.linkId);
            } else if constexpr (std::is_same_v<Command, SetClipFadesCommand>) {
                return sequence.setClipFades(typedCommand.clipId, typedCommand.fadeInFrames,
                                             typedCommand.fadeOutFrames);
            } else if constexpr (std::is_same_v<Command, SetClipTransitionsCommand>) {
                return sequence.setClipTransitions(typedCommand.clipId,
                                                   typedCommand.videoFrames,
                                                   typedCommand.audioFrames);
            } else if constexpr (std::is_same_v<Command, SetClipTransformCommand>) {
                return sequence.setClipTransform(
                    typedCommand.clipId, typedCommand.positionX, typedCommand.positionY,
                    typedCommand.scaleX, typedCommand.scaleY, typedCommand.rotationDegrees,
                    typedCommand.anchorX, typedCommand.anchorY);
            } else if constexpr (std::is_same_v<Command, SetClipCropCommand>) {
                return sequence.setClipCrop(typedCommand.clipId, typedCommand.left,
                                            typedCommand.right, typedCommand.top,
                                            typedCommand.bottom);
            } else if constexpr (std::is_same_v<Command, SetMotionKeyframeCommand>) {
                return sequence.setMotionKeyframe(
                    typedCommand.clipId, typedCommand.frameOffset, typedCommand.opacity,
                    typedCommand.positionX, typedCommand.positionY, typedCommand.scaleX,
                    typedCommand.scaleY, typedCommand.rotationDegrees,
                    typedCommand.anchorX, typedCommand.anchorY,
                    typedCommand.interpolation);
            } else if constexpr (std::is_same_v<Command, RemoveMotionKeyframeCommand>) {
                return sequence.removeMotionKeyframe(typedCommand.clipId,
                                                     typedCommand.frameOffset);
            } else if constexpr (std::is_same_v<Command, SetGainKeyframeCommand>) {
                return sequence.setGainKeyframe(typedCommand.clipId,
                                                typedCommand.frameOffset,
                                                typedCommand.gainDb,
                                                typedCommand.interpolation);
            } else if constexpr (std::is_same_v<Command, RemoveGainKeyframeCommand>) {
                return sequence.removeGainKeyframe(typedCommand.clipId,
                                                   typedCommand.frameOffset);
            } else if constexpr (std::is_same_v<Command, SetSpeedKeyframeCommand>) {
                return sequence.setSpeedKeyframe(typedCommand.clipId,
                                                 typedCommand.frameOffset,
                                                 typedCommand.rate,
                                                 typedCommand.interpolation);
            } else if constexpr (std::is_same_v<Command, RemoveSpeedKeyframeCommand>) {
                return sequence.removeSpeedKeyframe(typedCommand.clipId,
                                                    typedCommand.frameOffset);
            } else if constexpr (std::is_same_v<Command, SetClipMaskCommand>) {
                return sequence.setClipMask(
                    typedCommand.clipId, typedCommand.shape, typedCommand.centerX,
                    typedCommand.centerY, typedCommand.width, typedCommand.height,
                    typedCommand.feather, typedCommand.inverted);
            } else if constexpr (std::is_same_v<Command, AddEffectCommand>) {
                return sequence.addEffect(typedCommand.clipId, typedCommand.type);
            } else if constexpr (std::is_same_v<Command, RemoveEffectCommand>) {
                return sequence.removeEffect(typedCommand.clipId, typedCommand.effectId);
            } else if constexpr (std::is_same_v<Command, MoveEffectCommand>) {
                return sequence.moveEffect(typedCommand.clipId, typedCommand.effectId,
                                           typedCommand.delta);
            } else if constexpr (std::is_same_v<Command, SetEffectCommand>) {
                return sequence.setEffect(typedCommand.clipId, typedCommand.effectId,
                                          typedCommand.enabled, typedCommand.amount);
            } else if constexpr (std::is_same_v<Command, SetEffectKeyframeCommand>) {
                return sequence.setEffectKeyframe(
                    typedCommand.clipId, typedCommand.effectId, typedCommand.frameOffset,
                    typedCommand.value, typedCommand.interpolation);
            } else if constexpr (std::is_same_v<Command, RemoveEffectKeyframeCommand>) {
                return sequence.removeEffectKeyframe(
                    typedCommand.clipId, typedCommand.effectId, typedCommand.frameOffset);
            } else if constexpr (std::is_same_v<Command, AddMarkerCommand>) {
                return sequence.addMarker(typedCommand.position, typedCommand.name);
            } else if constexpr (std::is_same_v<Command, SetMarkerCommand>) {
                return sequence.setMarker(typedCommand.markerId, typedCommand.position,
                                          typedCommand.name, typedCommand.comment,
                                          typedCommand.color);
            } else if constexpr (std::is_same_v<Command, RemoveMarkerCommand>) {
                return sequence.removeMarker(typedCommand.markerId);
            } else if constexpr (std::is_same_v<Command, AddCaptionCommand>) {
                return sequence.addCaption(typedCommand.range, typedCommand.text);
            } else if constexpr (std::is_same_v<Command, SetCaptionCommand>) {
                return sequence.setCaption(
                    typedCommand.captionId, typedCommand.range, typedCommand.text,
                    typedCommand.positionX, typedCommand.positionY, typedCommand.fontSize,
                    typedCommand.textColor, typedCommand.backgroundColor,
                    typedCommand.bold, typedCommand.italic);
            } else if constexpr (std::is_same_v<Command, RemoveCaptionCommand>) {
                return sequence.removeCaption(typedCommand.captionId);
            } else if constexpr (std::is_same_v<Command, LiftRangeCommand>) {
                return sequence.liftRange(typedCommand.range);
            } else if constexpr (std::is_same_v<Command, ExtractRangeCommand>) {
                return sequence.extractRange(typedCommand.range);
            }
        },
        command);
}

} // namespace

namespace videx::core {

EditSession::EditSession(const FrameRate frameRate) : sequence_(frameRate) {}

EditSession::EditSession(Sequence sequence) : sequence_(std::move(sequence)) {}

const std::string& EditSession::undoLabel() const noexcept {
    static const std::string empty;
    return undoStack_.empty() ? empty : undoStack_.back().label;
}

const std::string& EditSession::redoLabel() const noexcept {
    static const std::string empty;
    return redoStack_.empty() ? empty : redoStack_.back().label;
}

std::vector<std::string> EditSession::undoLabels() const {
    std::vector<std::string> labels;
    labels.reserve(undoStack_.size());
    for (const HistoryEntry& entry : undoStack_) {
        labels.push_back(entry.label);
    }
    return labels;
}

std::vector<std::string> EditSession::redoLabels() const {
    std::vector<std::string> labels;
    labels.reserve(redoStack_.size());
    for (auto iterator = redoStack_.rbegin(); iterator != redoStack_.rend(); ++iterator) {
        labels.push_back(iterator->label);
    }
    return labels;
}

EditResult EditSession::apply(const CommandEnvelope& envelope) {
    if (envelope.baseRevision != sequence_.revision()) {
        return {
            .error = EditError::StaleRevision,
            .revision = sequence_.revision(),
            .message = "command base revision is stale",
        };
    }

    Sequence candidate = sequence_;
    EditResult result = execute(candidate, envelope.command);
    if (!result.succeeded()) {
        result.revision = sequence_.revision();
        return result;
    }

    if (candidate.revision() == sequence_.revision()) {
        return result;
    }

    undoStack_.push_back({.sequence = sequence_, .label = envelope.label});
    redoStack_.clear();
    sequence_ = std::move(candidate);
    result.revision = sequence_.revision();
    return result;
}

EditResult EditSession::apply(const TransactionEnvelope& envelope) {
    if (envelope.baseRevision != sequence_.revision()) {
        return {
            .error = EditError::StaleRevision,
            .revision = sequence_.revision(),
            .message = "transaction base revision is stale",
        };
    }

    Sequence candidate = sequence_;
    EditResult transactionResult{.revision = sequence_.revision()};
    // A placement command with a null target track binds to the track created
    // by the most recent AddTrackCommand in the same transaction. This lets
    // "add a track and put a clip on it" commit atomically as one undo step.
    TrackId transactionCreatedTrack{};
    for (const EditCommand& command : envelope.commands) {
        EditCommand bound = command;
        if (transactionCreatedTrack) {
            if (auto* insert = std::get_if<InsertClipCommand>(&bound);
                insert != nullptr && !insert->targetTrack) {
                insert->targetTrack = transactionCreatedTrack;
            } else if (auto* overwrite = std::get_if<OverwriteClipCommand>(&bound);
                       overwrite != nullptr && !overwrite->targetTrack) {
                overwrite->targetTrack = transactionCreatedTrack;
            } else if (auto* moveToTrack = std::get_if<MoveClipToTrackCommand>(&bound);
                       moveToTrack != nullptr && !moveToTrack->targetTrack) {
                moveToTrack->targetTrack = transactionCreatedTrack;
            }
        }
        EditResult result = execute(candidate, bound);
        if (!result.succeeded()) {
            result.revision = sequence_.revision();
            return result;
        }
        if (std::holds_alternative<AddTrackCommand>(bound) && result.primaryTrack) {
            transactionCreatedTrack = result.primaryTrack;
        }
        if (result.primaryClip) {
            transactionResult.primaryClip = result.primaryClip;
        }
        if (result.primaryTrack) {
            transactionResult.primaryTrack = result.primaryTrack;
        }
        if (result.primaryEffect) {
            transactionResult.primaryEffect = result.primaryEffect;
        }
        transactionResult.createdClips.insert(transactionResult.createdClips.end(),
                                              result.createdClips.begin(),
                                              result.createdClips.end());
    }

    if (candidate.revision() == sequence_.revision()) {
        return transactionResult;
    }

    undoStack_.push_back({.sequence = sequence_, .label = envelope.label});
    redoStack_.clear();
    sequence_ = std::move(candidate);
    transactionResult.revision = sequence_.revision();
    return transactionResult;
}

EditResult EditSession::undo() {
    if (undoStack_.empty()) {
        return {
            .error = EditError::NothingToUndo,
            .revision = sequence_.revision(),
            .message = "nothing to undo",
        };
    }

    HistoryEntry previous = std::move(undoStack_.back());
    undoStack_.pop_back();
    redoStack_.push_back({.sequence = sequence_, .label = previous.label});

    const std::uint64_t newRevision = sequence_.revision() + 1U;
    sequence_.restoreContent(previous.sequence, newRevision);
    return {.revision = sequence_.revision()};
}

EditResult EditSession::redo() {
    if (redoStack_.empty()) {
        return {
            .error = EditError::NothingToRedo,
            .revision = sequence_.revision(),
            .message = "nothing to redo",
        };
    }

    HistoryEntry next = std::move(redoStack_.back());
    redoStack_.pop_back();
    undoStack_.push_back({.sequence = sequence_, .label = next.label});

    const std::uint64_t newRevision = sequence_.revision() + 1U;
    sequence_.restoreContent(next.sequence, newRevision);
    return {.revision = sequence_.revision()};
}

void EditSession::clearHistory() noexcept {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace videx::core
