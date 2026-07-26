#include <videx/core/timeline.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace {

using videx::core::Clip;
using videx::core::ClipId;
using videx::core::EditError;
using videx::core::EditResult;
using videx::core::Frame;
using videx::core::Track;
using videx::core::TrackId;

bool tryAdd(const Frame left, const Frame right, Frame& result) noexcept {
    if ((right > 0 && left > std::numeric_limits<Frame>::max() - right) ||
        (right < 0 && left < std::numeric_limits<Frame>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}

bool trySub(const Frame left, const Frame right, Frame& result) noexcept {
    if ((right < 0 && left > std::numeric_limits<Frame>::max() + right) ||
        (right > 0 && left < std::numeric_limits<Frame>::min() + right)) {
        return false;
    }
    result = left - right;
    return true;
}

bool validClipRange(const Frame timelineStart, const Frame sourceStart, const Frame duration) {
    Frame timelineEnd = 0;
    Frame sourceEnd = 0;
    return timelineStart >= 0 && sourceStart >= 0 && duration > 0 &&
           tryAdd(timelineStart, duration, timelineEnd) && tryAdd(sourceStart, duration, sourceEnd);
}

bool validTransform(const Clip& clip) {
    return std::isfinite(clip.positionX) && std::isfinite(clip.positionY) &&
           std::isfinite(clip.scaleX) && std::isfinite(clip.scaleY) &&
           std::isfinite(clip.rotationDegrees) && std::isfinite(clip.anchorX) &&
           std::isfinite(clip.anchorY) && std::abs(clip.positionX) <= 100'000.0 &&
           std::abs(clip.positionY) <= 100'000.0 && clip.scaleX >= 0.01 &&
           clip.scaleX <= 100.0 && clip.scaleY >= 0.01 && clip.scaleY <= 100.0 &&
           clip.rotationDegrees >= -36'000.0 && clip.rotationDegrees <= 36'000.0 &&
           clip.anchorX >= 0.0 && clip.anchorX <= 1.0 && clip.anchorY >= 0.0 &&
           clip.anchorY <= 1.0;
}

bool validMotionKeyframe(const videx::core::MotionKeyframe& keyframe) {
    Clip values{.opacity = keyframe.opacity,
                .positionX = keyframe.positionX,
                .positionY = keyframe.positionY,
                .scaleX = keyframe.scaleX,
                .scaleY = keyframe.scaleY,
                .rotationDegrees = keyframe.rotationDegrees,
                .anchorX = keyframe.anchorX,
                .anchorY = keyframe.anchorY};
    return keyframe.opacity >= 0.0 && keyframe.opacity <= 1.0 &&
           std::isfinite(keyframe.opacity) && validTransform(values) &&
           static_cast<int>(keyframe.interpolation) >=
               static_cast<int>(videx::core::KeyframeInterpolation::Linear) &&
           static_cast<int>(keyframe.interpolation) <=
               static_cast<int>(videx::core::KeyframeInterpolation::EaseInOut);
}

bool validCrop(const Clip& clip) {
    return std::isfinite(clip.cropLeft) && std::isfinite(clip.cropRight) &&
           std::isfinite(clip.cropTop) && std::isfinite(clip.cropBottom) &&
           clip.cropLeft >= 0.0 && clip.cropRight >= 0.0 && clip.cropTop >= 0.0 &&
           clip.cropBottom >= 0.0 && clip.cropLeft <= 1.0 && clip.cropRight <= 1.0 &&
           clip.cropTop <= 1.0 && clip.cropBottom <= 1.0 &&
           clip.cropLeft + clip.cropRight < 1.0 && clip.cropTop + clip.cropBottom < 1.0;
}

bool validMask(const Clip& clip) {
    return static_cast<int>(clip.maskShape) >= static_cast<int>(videx::core::MaskShape::None) &&
           static_cast<int>(clip.maskShape) <= static_cast<int>(videx::core::MaskShape::Ellipse) &&
           std::isfinite(clip.maskCenterX) && std::isfinite(clip.maskCenterY) &&
           std::isfinite(clip.maskWidth) && std::isfinite(clip.maskHeight) &&
           std::isfinite(clip.maskFeather) && clip.maskCenterX >= 0.0 &&
           clip.maskCenterX <= 1.0 && clip.maskCenterY >= 0.0 && clip.maskCenterY <= 1.0 &&
           clip.maskWidth > 0.0 && clip.maskWidth <= 2.0 && clip.maskHeight > 0.0 &&
           clip.maskHeight <= 2.0 && clip.maskFeather >= 0.0 && clip.maskFeather <= 0.5;
}

bool validEffectValue(const videx::core::EffectType type, const double value) {
    if (!std::isfinite(value)) {
        return false;
    }
    using videx::core::EffectType;
    switch (type) {
    case EffectType::Brightness: return value >= -1.0 && value <= 1.0;
    case EffectType::Contrast: return value >= -1.0 && value <= 3.0;
    case EffectType::Saturation: return value >= -1.0 && value <= 3.0;
    case EffectType::Blur: return value >= 0.0 && value <= 50.0;
    case EffectType::Vignette: return value >= 0.0 && value <= 1.0;
    }
    return false;
}

void retimeEffectKeyframes(Clip& clip, const Frame removedFromStart = 0) {
    for (auto& effect : clip.effects) {
        std::vector<videx::core::EffectKeyframe> retained;
        retained.reserve(effect.keyframes.size());
        for (auto keyframe : effect.keyframes) {
            Frame shifted = 0;
            if (tryAdd(keyframe.frameOffset, -removedFromStart, shifted) && shifted >= 0 &&
                shifted < clip.timeline.duration) {
                keyframe.frameOffset = shifted;
                retained.push_back(keyframe);
            }
        }
        effect.keyframes = std::move(retained);
    }
    std::vector<videx::core::SpeedKeyframe> retainedSpeed;
    retainedSpeed.reserve(clip.speedKeyframes.size());
    for (auto keyframe : clip.speedKeyframes) {
        Frame shifted = 0;
        if (tryAdd(keyframe.frameOffset, -removedFromStart, shifted) && shifted >= 0 &&
            shifted < clip.timeline.duration) {
            keyframe.frameOffset = shifted;
            retainedSpeed.push_back(keyframe);
        }
    }
    clip.speedKeyframes = std::move(retainedSpeed);
    std::vector<videx::core::MotionKeyframe> retainedMotion;
    retainedMotion.reserve(clip.motionKeyframes.size());
    for (auto keyframe : clip.motionKeyframes) {
        Frame shifted = 0;
        if (tryAdd(keyframe.frameOffset, -removedFromStart, shifted) && shifted >= 0 &&
            shifted < clip.timeline.duration) {
            keyframe.frameOffset = shifted;
            retainedMotion.push_back(keyframe);
        }
    }
    clip.motionKeyframes = std::move(retainedMotion);
    std::vector<videx::core::GainKeyframe> retainedGain;
    retainedGain.reserve(clip.gainKeyframes.size());
    for (auto keyframe : clip.gainKeyframes) {
        Frame shifted = 0;
        if (tryAdd(keyframe.frameOffset, -removedFromStart, shifted) && shifted >= 0 &&
            shifted < clip.timeline.duration) {
            keyframe.frameOffset = shifted;
            retainedGain.push_back(keyframe);
        }
    }
    clip.gainKeyframes = std::move(retainedGain);
}

void clampEdgeEnvelopes(Clip& clip) {
    clip.fadeInFrames = std::min(clip.fadeInFrames, clip.timeline.duration);
    clip.fadeOutFrames = std::min(clip.fadeOutFrames, clip.timeline.duration);
    clip.videoTransitionInFrames = std::min(clip.videoTransitionInFrames, clip.timeline.duration);
    clip.audioTransitionInFrames = std::min(clip.audioTransitionInFrames, clip.timeline.duration);
}

void sortClips(Track& track) {
    std::ranges::sort(track.clips, {}, [](const Clip& clip) { return clip.timeline.start; });
}

bool hasCollisions(const Track& track) {
    for (std::size_t index = 1; index < track.clips.size(); ++index) {
        if (track.clips[index - 1].timeline.end() > track.clips[index].timeline.start) {
            return true;
        }
    }
    return false;
}

Track* findTrack(std::vector<Track>& tracks, const TrackId trackId) {
    const auto iterator = std::ranges::find(tracks, trackId, &Track::id);
    return iterator == tracks.end() ? nullptr : &*iterator;
}

std::pair<Track*, Clip*> findClip(std::vector<Track>& tracks, const ClipId clipId) {
    for (Track& track : tracks) {
        const auto iterator = std::ranges::find(track.clips, clipId, &Clip::id);
        if (iterator != track.clips.end()) {
            return {&track, &*iterator};
        }
    }
    return {nullptr, nullptr};
}

EditResult failure(const EditError error, const std::uint64_t revision, std::string message) {
    return {
        .error = error,
        .revision = revision,
        .message = std::move(message),
    };
}

} // namespace

namespace videx::core {

bool FrameRate::isValid() const noexcept {
    return numerator > 0 && denominator > 0;
}

double FrameRate::framesPerSecond() const noexcept {
    if (!isValid()) {
        return 0.0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

bool FrameRange::isValid() const noexcept {
    Frame rangeEnd = 0;
    return start >= 0 && duration >= 0 && tryAdd(start, duration, rangeEnd);
}

Frame FrameRange::end() const {
    Frame rangeEnd = 0;
    if (!tryAdd(start, duration, rangeEnd)) {
        throw std::overflow_error("frame range end overflow");
    }
    return rangeEnd;
}

bool FrameRange::contains(const Frame position) const {
    return isValid() && position >= start && position < end();
}

bool FrameRange::overlaps(const FrameRange& other) const {
    return isValid() && other.isValid() && start < other.end() && other.start < end();
}

Sequence::Sequence(const FrameRate frameRate) : frameRate_(frameRate) {
    if (!frameRate_.isValid()) {
        throw std::invalid_argument("sequence frame rate must be positive");
    }
}

Sequence Sequence::fromTracks(const FrameRate frameRate, std::vector<Track> tracks,
                              std::vector<Marker> markers, std::vector<Caption> captions) {
    Sequence sequence(frameRate);
    std::unordered_set<std::uint64_t> entityIds;
    std::uint64_t maximumId = 0;

    for (Track& track : tracks) {
        if (!track.id || !entityIds.insert(track.id.value).second) {
            throw std::invalid_argument("track IDs must be non-zero and unique");
        }
        if (track.name.size() > 256U || track.heightMode < 0 || track.heightMode > 2) {
            throw std::invalid_argument("loaded track metadata is invalid");
        }
        maximumId = std::max(maximumId, track.id.value);

        sortClips(track);
        for (const Clip& clip : track.clips) {
            if (!clip.id || !clip.assetId || clip.sourceStart < 0 || clip.timeline.duration <= 0 ||
                !clip.timeline.isValid() || !entityIds.insert(clip.id.value).second ||
                !std::isfinite(clip.opacity) || clip.opacity < 0.0 || clip.opacity > 1.0 ||
                !std::isfinite(clip.audioGainDb) || clip.audioGainDb < -60.0 ||
                clip.audioGainDb > 24.0 || !std::isfinite(clip.playbackRate) ||
                clip.playbackRate < 0.25 || clip.playbackRate > 4.0) {
                throw std::invalid_argument("loaded clip is invalid");
            }
            if (clip.fadeInFrames < 0 || clip.fadeOutFrames < 0 ||
                clip.fadeInFrames > clip.timeline.duration ||
                clip.fadeOutFrames > clip.timeline.duration) {
                throw std::invalid_argument("loaded clip fades are invalid");
            }
            if (clip.videoTransitionInFrames < 0 || clip.audioTransitionInFrames < 0 ||
                clip.videoTransitionInFrames > clip.timeline.duration ||
                clip.audioTransitionInFrames > clip.timeline.duration ||
                (track.kind == TrackKind::Video && clip.audioTransitionInFrames != 0) ||
                (track.kind == TrackKind::Audio && clip.videoTransitionInFrames != 0)) {
                throw std::invalid_argument("loaded clip transitions are invalid");
            }
            if (!validTransform(clip)) {
                throw std::invalid_argument("loaded clip transform is invalid");
            }
            Frame previousMotionFrame = -1;
            for (const MotionKeyframe& keyframe : clip.motionKeyframes) {
                if (keyframe.frameOffset <= previousMotionFrame || keyframe.frameOffset < 0 ||
                    keyframe.frameOffset >= clip.timeline.duration ||
                    !validMotionKeyframe(keyframe)) {
                    throw std::invalid_argument("loaded motion keyframe is invalid");
                }
                previousMotionFrame = keyframe.frameOffset;
            }
            Frame previousGainFrame = -1;
            for (const GainKeyframe& keyframe : clip.gainKeyframes) {
                if (keyframe.frameOffset <= previousGainFrame || keyframe.frameOffset < 0 ||
                    keyframe.frameOffset >= clip.timeline.duration ||
                    !std::isfinite(keyframe.gainDb) || keyframe.gainDb < -60.0 ||
                    keyframe.gainDb > 24.0 ||
                    static_cast<int>(keyframe.interpolation) < 0 ||
                    static_cast<int>(keyframe.interpolation) >
                        static_cast<int>(KeyframeInterpolation::EaseInOut)) {
                    throw std::invalid_argument("loaded gain keyframe is invalid");
                }
                previousGainFrame = keyframe.frameOffset;
            }
            if (!validCrop(clip)) {
                throw std::invalid_argument("loaded clip crop is invalid");
            }
            if (!validMask(clip)) {
                throw std::invalid_argument("loaded clip mask is invalid");
            }
            for (const ClipEffect& effect : clip.effects) {
                if (!effect.id || !validEffectValue(effect.type, effect.amount) ||
                    !entityIds.insert(effect.id.value).second) {
                    throw std::invalid_argument("loaded clip effect is invalid");
                }
                maximumId = std::max(maximumId, effect.id.value);
                Frame previousFrame = -1;
                for (const EffectKeyframe& keyframe : effect.keyframes) {
                    if (keyframe.frameOffset < 0 || keyframe.frameOffset >= clip.timeline.duration ||
                        keyframe.frameOffset <= previousFrame ||
                        !validEffectValue(effect.type, keyframe.value)) {
                        throw std::invalid_argument("loaded effect keyframe is invalid");
                    }
                    previousFrame = keyframe.frameOffset;
                }
            }
            Frame previousSpeedFrame = -1;
            for (const SpeedKeyframe& keyframe : clip.speedKeyframes) {
                if (keyframe.frameOffset < 0 || keyframe.frameOffset >= clip.timeline.duration ||
                    keyframe.frameOffset <= previousSpeedFrame || !std::isfinite(keyframe.rate) ||
                    keyframe.rate < 0.25 || keyframe.rate > 4.0) {
                    throw std::invalid_argument("loaded speed keyframe is invalid");
                }
                previousSpeedFrame = keyframe.frameOffset;
            }
            Frame sourceEnd = 0;
            if (!tryAdd(clip.sourceStart, clip.timeline.duration, sourceEnd)) {
                throw std::invalid_argument("loaded clip source range overflows");
            }
            maximumId = std::max(maximumId, clip.id.value);
            maximumId = std::max(maximumId, clip.linkId.value);
        }
        if (hasCollisions(track)) {
            throw std::invalid_argument("loaded track contains overlapping clips");
        }
    }
    for (const Marker& marker : markers) {
        if (!marker.id || marker.position < 0 || marker.name.empty() || marker.name.size() > 512U ||
            !entityIds.insert(marker.id.value).second) {
            throw std::invalid_argument("loaded marker is invalid");
        }
        maximumId = std::max(maximumId, marker.id.value);
    }
    for (const Caption& caption : captions) {
        if (!caption.id || !caption.timeline.isValid() || caption.timeline.duration <= 0 ||
            caption.text.empty() || caption.text.size() > 10'000U ||
            !std::isfinite(caption.positionX) || !std::isfinite(caption.positionY) ||
            !std::isfinite(caption.fontSize) || caption.positionX < 0.0 ||
            caption.positionX > 1.0 || caption.positionY < 0.0 || caption.positionY > 1.0 ||
            caption.fontSize < 8.0 || caption.fontSize > 300.0 ||
            !entityIds.insert(caption.id.value).second) {
            throw std::invalid_argument("loaded caption is invalid");
        }
        maximumId = std::max(maximumId, caption.id.value);
    }

    if (maximumId == std::numeric_limits<std::uint64_t>::max()) {
        throw std::invalid_argument("loaded entity ID range is exhausted");
    }
    // Legacy files may interleave kinds; regroup video-before-audio while
    // preserving relative order so per-kind stacking and reordering work.
    std::stable_partition(tracks.begin(), tracks.end(), [](const Track& track) {
        return track.kind == TrackKind::Video;
    });
    sequence.tracks_ = std::move(tracks);
    sequence.markers_ = std::move(markers);
    sequence.captions_ = std::move(captions);
    sequence.nextEntityId_ = maximumId + 1U;
    return sequence;
}

std::uint64_t Sequence::nextId() noexcept {
    return nextEntityId_++;
}

void Sequence::restoreContent(const Sequence& snapshot, const std::uint64_t newRevision) {
    frameRate_ = snapshot.frameRate_;
    tracks_ = snapshot.tracks_;
    markers_ = snapshot.markers_;
    captions_ = snapshot.captions_;
    nextEntityId_ = std::max(nextEntityId_, snapshot.nextEntityId_);
    revision_ = newRevision;
}

TrackId Sequence::addTrack(const TrackKind kind) {
    const TrackId id{nextId()};
    // Keep tracks grouped: video tracks first (V1 = back layer), audio after.
    auto insertPosition = tracks_.end();
    if (kind == TrackKind::Video) {
        insertPosition = std::ranges::find(tracks_, TrackKind::Audio, &Track::kind);
    }
    tracks_.insert(insertPosition, {.id = id, .kind = kind});
    ++revision_;
    return id;
}

EditResult Sequence::removeTrack(const TrackId trackId) {
    const auto iterator = std::ranges::find(tracks_, trackId, &Track::id);
    if (iterator == tracks_.end()) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    tracks_.erase(iterator);
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::moveTrack(const TrackId trackId, const std::int32_t delta) {
    const auto iterator = std::ranges::find(tracks_, trackId, &Track::id);
    if (iterator == tracks_.end()) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    if (delta == 0) {
        return {.revision = revision_, .primaryTrack = trackId};
    }
    const auto index = static_cast<std::int64_t>(std::distance(tracks_.begin(), iterator));
    const auto destination = index + delta;
    if (destination < 0 || destination >= static_cast<std::int64_t>(tracks_.size())) {
        return failure(EditError::InvalidRange, revision_, "track move exceeds the track list");
    }
    // Stacking order is per kind; a track may only move within its own group.
    const auto step = delta > 0 ? 1 : -1;
    for (auto position = index + step;; position += step) {
        if (tracks_[static_cast<std::size_t>(position)].kind != iterator->kind) {
            return failure(EditError::InvalidRange, revision_,
                           "track can only move within its own kind");
        }
        if (position == destination) {
            break;
        }
    }
    Track moved = std::move(*iterator);
    tracks_.erase(iterator);
    tracks_.insert(tracks_.begin() + destination, std::move(moved));
    ++revision_;
    return {.revision = revision_, .primaryTrack = trackId};
}

EditResult Sequence::setTrackHeightMode(const TrackId trackId, const std::int32_t heightMode) {
    Track* track = ::findTrack(tracks_, trackId);
    if (track == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    if (heightMode < 0 || heightMode > 2) {
        return failure(EditError::InvalidRange, revision_, "track height mode is invalid");
    }
    if (track->heightMode != heightMode) {
        track->heightMode = heightMode;
        ++revision_;
    }
    return {.revision = revision_};
}

EditResult Sequence::setTrackSolo(const TrackId trackId, const bool solo) {
    Track* track = ::findTrack(tracks_, trackId);
    if (track == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    if (track->solo != solo) {
        track->solo = solo;
        ++revision_;
    }
    return {.revision = revision_};
}

EditResult Sequence::setTrackLocked(const TrackId trackId, const bool locked) {
    Track* track = ::findTrack(tracks_, trackId);
    if (track == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    if (track->locked != locked) {
        track->locked = locked;
        ++revision_;
    }
    return {.revision = revision_};
}

EditResult Sequence::setTrackSyncLocked(const TrackId trackId, const bool syncLocked) {
    Track* track = ::findTrack(tracks_, trackId);
    if (track == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    if (track->syncLocked != syncLocked) {
        track->syncLocked = syncLocked;
        ++revision_;
    }
    return {.revision = revision_};
}

EditResult Sequence::setTrackMuted(const TrackId trackId, const bool muted) {
    Track* track = ::findTrack(tracks_, trackId);
    if (track == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    if (track->muted != muted) {
        track->muted = muted;
        ++revision_;
    }
    return {.revision = revision_};
}

EditResult Sequence::setTrackEnabled(const TrackId trackId, const bool enabled) {
    Track* track = ::findTrack(tracks_, trackId);
    if (track == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "track not found");
    }
    if (track->enabled != enabled) {
        track->enabled = enabled;
        ++revision_;
    }
    return {.revision = revision_};
}

EditResult Sequence::setClipProperties(const ClipId clipId, const double opacity,
                                       const double audioGainDb, const double playbackRate) {
    if (!std::isfinite(opacity) || !std::isfinite(audioGainDb) ||
        !std::isfinite(playbackRate) || opacity < 0.0 || opacity > 1.0 ||
        audioGainDb < -60.0 || audioGainDb > 24.0 || playbackRate < 0.25 ||
        playbackRate > 4.0) {
        return failure(EditError::InvalidRange, revision_, "clip properties are invalid");
    }
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find_if(track.clips, [clipId](const Clip& candidate) {
            return candidate.id == clipId;
        });
        if (clip == track.clips.end()) {
            continue;
        }
        if (track.locked) {
            return failure(EditError::TrackLocked, revision_, "track is locked");
        }
        if (clip->opacity != opacity || clip->audioGainDb != audioGainDb ||
            clip->playbackRate != playbackRate) {
            clip->opacity = opacity;
            clip->audioGainDb = audioGainDb;
            clip->playbackRate = playbackRate;
            ++revision_;
        }
        return {.revision = revision_};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setClipLink(const ClipId clipId, const LinkId linkId) {
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find_if(track.clips, [clipId](const Clip& candidate) {
            return candidate.id == clipId;
        });
        if (clip == track.clips.end()) continue;
        if (track.locked) {
            return failure(EditError::TrackLocked, revision_, "track is locked");
        }
        if (clip->linkId != linkId) {
            clip->linkId = linkId;
            ++revision_;
        }
        return {.revision = revision_};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setClipFades(const ClipId clipId, const Frame fadeInFrames,
                                  const Frame fadeOutFrames) {
    for (Track& track : tracks_) {
        sortClips(track);
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) {
            continue;
        }
        if (track.locked) {
            return failure(EditError::TrackLocked, revision_, "track is locked");
        }
        if (fadeInFrames < 0 || fadeOutFrames < 0 ||
            fadeInFrames > clip->timeline.duration || fadeOutFrames > clip->timeline.duration) {
            return failure(EditError::InvalidRange, revision_, "clip fades are invalid");
        }
        if (clip->fadeInFrames != fadeInFrames || clip->fadeOutFrames != fadeOutFrames) {
            clip->fadeInFrames = fadeInFrames;
            clip->fadeOutFrames = fadeOutFrames;
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setClipTransitions(const ClipId clipId, const Frame videoFrames,
                                        const Frame audioFrames) {
    for (Track& track : tracks_) {
        sortClips(track);
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) {
            return failure(EditError::TrackLocked, revision_, "track is locked");
        }
        if (videoFrames < 0 || audioFrames < 0 || videoFrames > clip->timeline.duration ||
            audioFrames > clip->timeline.duration ||
            (track.kind == TrackKind::Video && audioFrames != 0) ||
            (track.kind == TrackKind::Audio && videoFrames != 0)) {
            return failure(EditError::InvalidRange, revision_, "clip transition is invalid");
        }
        if ((videoFrames > 0 || audioFrames > 0)) {
            if (clip == track.clips.begin() ||
                std::prev(clip)->timeline.end() != clip->timeline.start) {
                return failure(EditError::InvalidRange, revision_,
                               "transition requires an adjacent previous clip");
            }
        }
        if (clip->videoTransitionInFrames != videoFrames ||
            clip->audioTransitionInFrames != audioFrames) {
            clip->videoTransitionInFrames = videoFrames;
            clip->audioTransitionInFrames = audioFrames;
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setClipTransform(const ClipId clipId, const double positionX,
                                      const double positionY, const double scaleX,
                                      const double scaleY, const double rotationDegrees,
                                      const double anchorX, const double anchorY) {
    Clip values{.positionX = positionX,
                .positionY = positionY,
                .scaleX = scaleX,
                .scaleY = scaleY,
                .rotationDegrees = rotationDegrees,
                .anchorX = anchorX,
                .anchorY = anchorY};
    if (!validTransform(values)) {
        return failure(EditError::InvalidRange, revision_, "clip transform is invalid");
    }
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) {
            continue;
        }
        if (track.locked) {
            return failure(EditError::TrackLocked, revision_, "track is locked");
        }
        if (clip->positionX != positionX || clip->positionY != positionY ||
            clip->scaleX != scaleX || clip->scaleY != scaleY ||
            clip->rotationDegrees != rotationDegrees || clip->anchorX != anchorX ||
            clip->anchorY != anchorY) {
            clip->positionX = positionX;
            clip->positionY = positionY;
            clip->scaleX = scaleX;
            clip->scaleY = scaleY;
            clip->rotationDegrees = rotationDegrees;
            clip->anchorX = anchorX;
            clip->anchorY = anchorY;
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setClipCrop(const ClipId clipId, const double left,
                                 const double right, const double top,
                                 const double bottom) {
    Clip values{.cropLeft = left, .cropRight = right, .cropTop = top,
                .cropBottom = bottom};
    if (!validCrop(values)) {
        return failure(EditError::InvalidRange, revision_, "clip crop is invalid");
    }
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) {
            return failure(EditError::TrackLocked, revision_, "track is locked");
        }
        if (clip->cropLeft != left || clip->cropRight != right ||
            clip->cropTop != top || clip->cropBottom != bottom) {
            clip->cropLeft = left;
            clip->cropRight = right;
            clip->cropTop = top;
            clip->cropBottom = bottom;
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setMotionKeyframe(
    const ClipId clipId, const Frame frameOffset, const double opacity,
    const double positionX, const double positionY, const double scaleX,
    const double scaleY, const double rotationDegrees, const double anchorX,
    const double anchorY, const KeyframeInterpolation interpolation) {
    const MotionKeyframe values{frameOffset, opacity, positionX, positionY, scaleX,
                                scaleY, rotationDegrees, anchorX, anchorY, interpolation};
    if (!validMotionKeyframe(values)) {
        return failure(EditError::InvalidRange, revision_, "motion keyframe is invalid");
    }
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) return failure(EditError::TrackLocked, revision_, "track is locked");
        if (frameOffset < 0 || frameOffset >= clip->timeline.duration) {
            return failure(EditError::InvalidRange, revision_,
                           "motion keyframe is outside clip");
        }
        const auto existing = std::ranges::lower_bound(
            clip->motionKeyframes, frameOffset, {}, &MotionKeyframe::frameOffset);
        if (existing != clip->motionKeyframes.end() && existing->frameOffset == frameOffset) {
            if (*existing == values) return {.revision = revision_, .primaryClip = clipId};
            *existing = values;
        } else {
            clip->motionKeyframes.insert(existing, values);
        }
        ++revision_;
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::removeMotionKeyframe(const ClipId clipId, const Frame frameOffset) {
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) return failure(EditError::TrackLocked, revision_, "track is locked");
        const auto keyframe = std::ranges::find(clip->motionKeyframes, frameOffset,
                                                &MotionKeyframe::frameOffset);
        if (keyframe != clip->motionKeyframes.end()) {
            clip->motionKeyframes.erase(keyframe);
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setGainKeyframe(const ClipId clipId, const Frame frameOffset,
                                     const double gainDb,
                                     const KeyframeInterpolation interpolation) {
    if (!std::isfinite(gainDb) || gainDb < -60.0 || gainDb > 24.0 ||
        static_cast<int>(interpolation) < 0 ||
        static_cast<int>(interpolation) > static_cast<int>(KeyframeInterpolation::EaseInOut)) {
        return failure(EditError::InvalidRange, revision_, "gain keyframe is invalid");
    }
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) return failure(EditError::TrackLocked, revision_, "track is locked");
        if (frameOffset < 0 || frameOffset >= clip->timeline.duration) {
            return failure(EditError::InvalidRange, revision_, "gain keyframe is outside clip");
        }
        const GainKeyframe values{frameOffset, gainDb, interpolation};
        const auto existing = std::ranges::lower_bound(
            clip->gainKeyframes, frameOffset, {}, &GainKeyframe::frameOffset);
        if (existing != clip->gainKeyframes.end() && existing->frameOffset == frameOffset) {
            if (*existing == values) return {.revision = revision_, .primaryClip = clipId};
            *existing = values;
        } else {
            clip->gainKeyframes.insert(existing, values);
        }
        ++revision_;
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::removeGainKeyframe(const ClipId clipId, const Frame frameOffset) {
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) return failure(EditError::TrackLocked, revision_, "track is locked");
        const auto keyframe = std::ranges::find(clip->gainKeyframes, frameOffset,
                                                &GainKeyframe::frameOffset);
        if (keyframe != clip->gainKeyframes.end()) {
            clip->gainKeyframes.erase(keyframe);
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setSpeedKeyframe(const ClipId clipId, const Frame frameOffset,
                                      const double rate,
                                      const KeyframeInterpolation interpolation) {
    if (!std::isfinite(rate) || rate < 0.25 || rate > 4.0) {
        return failure(EditError::InvalidRange, revision_, "speed keyframe rate is invalid");
    }
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) return failure(EditError::TrackLocked, revision_, "track is locked");
        if (frameOffset < 0 || frameOffset >= clip->timeline.duration) {
            return failure(EditError::InvalidRange, revision_, "speed keyframe is outside clip");
        }
        const auto existing = std::ranges::lower_bound(
            clip->speedKeyframes, frameOffset, {}, &SpeedKeyframe::frameOffset);
        if (existing != clip->speedKeyframes.end() && existing->frameOffset == frameOffset) {
            if (existing->rate == rate && existing->interpolation == interpolation)
                return {.revision = revision_, .primaryClip = clipId};
            existing->rate = rate;
            existing->interpolation = interpolation;
        } else {
            clip->speedKeyframes.insert(existing, {frameOffset, rate, interpolation});
        }
        ++revision_;
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::removeSpeedKeyframe(const ClipId clipId, const Frame frameOffset) {
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) return failure(EditError::TrackLocked, revision_, "track is locked");
        const auto keyframe = std::ranges::find(clip->speedKeyframes, frameOffset,
                                                &SpeedKeyframe::frameOffset);
        if (keyframe != clip->speedKeyframes.end()) {
            clip->speedKeyframes.erase(keyframe);
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::setClipMask(const ClipId clipId, const MaskShape shape,
                                 const double centerX, const double centerY,
                                 const double width, const double height,
                                 const double feather, const bool inverted) {
    Clip values{.maskShape = shape, .maskCenterX = centerX, .maskCenterY = centerY,
                .maskWidth = width, .maskHeight = height, .maskFeather = feather,
                .maskInverted = inverted};
    if (!validMask(values)) {
        return failure(EditError::InvalidRange, revision_, "clip mask is invalid");
    }
    for (Track& track : tracks_) {
        const auto clip = std::ranges::find(track.clips, clipId, &Clip::id);
        if (clip == track.clips.end()) continue;
        if (track.locked) return failure(EditError::TrackLocked, revision_, "track is locked");
        if (clip->maskShape != shape || clip->maskCenterX != centerX ||
            clip->maskCenterY != centerY || clip->maskWidth != width ||
            clip->maskHeight != height || clip->maskFeather != feather ||
            clip->maskInverted != inverted) {
            clip->maskShape = shape;
            clip->maskCenterX = centerX;
            clip->maskCenterY = centerY;
            clip->maskWidth = width;
            clip->maskHeight = height;
            clip->maskFeather = feather;
            clip->maskInverted = inverted;
            ++revision_;
        }
        return {.revision = revision_, .primaryClip = clipId};
    }
    return failure(EditError::ClipNotFound, revision_, "clip not found");
}

EditResult Sequence::addEffect(const ClipId clipId, const EffectType type) {
    auto [track, clip] = ::findClip(tracks_, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "track is locked");
    }
    const EffectId effectId{nextId()};
    clip->effects.push_back({.id = effectId, .type = type});
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId, .primaryEffect = effectId};
}

EditResult Sequence::removeEffect(const ClipId clipId, const EffectId effectId) {
    auto [track, clip] = ::findClip(tracks_, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "track is locked");
    }
    const auto effect = std::ranges::find(clip->effects, effectId, &ClipEffect::id);
    if (effect == clip->effects.end()) {
        return failure(EditError::ClipNotFound, revision_, "effect not found");
    }
    clip->effects.erase(effect);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId};
}

EditResult Sequence::moveEffect(const ClipId clipId, const EffectId effectId,
                                const std::int32_t delta) {
    auto [track, clip] = ::findClip(tracks_, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "track is locked");
    }
    const auto effect = std::ranges::find(clip->effects, effectId, &ClipEffect::id);
    if (effect == clip->effects.end()) {
        return failure(EditError::ClipNotFound, revision_, "effect not found");
    }
    const auto index = static_cast<std::int64_t>(effect - clip->effects.begin());
    const std::int64_t target = std::clamp<std::int64_t>(
        index + delta, 0, static_cast<std::int64_t>(clip->effects.size()) - 1);
    if (target == index) {
        return {.revision = revision_, .primaryClip = clipId, .primaryEffect = effectId};
    }
    ClipEffect moved = std::move(*effect);
    clip->effects.erase(effect);
    clip->effects.insert(clip->effects.begin() + target, std::move(moved));
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId, .primaryEffect = effectId};
}

EditResult Sequence::setEffect(const ClipId clipId, const EffectId effectId, const bool enabled,
                               const double amount) {
    auto [track, clip] = ::findClip(tracks_, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "track is locked");
    }
    const auto effect = std::ranges::find(clip->effects, effectId, &ClipEffect::id);
    if (effect == clip->effects.end()) {
        return failure(EditError::ClipNotFound, revision_, "effect not found");
    }
    if (!validEffectValue(effect->type, amount)) {
        return failure(EditError::InvalidRange, revision_, "effect amount is invalid");
    }
    if (effect->enabled != enabled || effect->amount != amount) {
        effect->enabled = enabled;
        effect->amount = amount;
        ++revision_;
    }
    return {.revision = revision_, .primaryClip = clipId, .primaryEffect = effectId};
}

EditResult Sequence::setEffectKeyframe(const ClipId clipId, const EffectId effectId,
                                       const Frame frameOffset, const double value,
                                       const KeyframeInterpolation interpolation) {
    auto [track, clip] = ::findClip(tracks_, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "track is locked");
    }
    const auto effect = std::ranges::find(clip->effects, effectId, &ClipEffect::id);
    if (effect == clip->effects.end()) {
        return failure(EditError::ClipNotFound, revision_, "effect not found");
    }
    if (frameOffset < 0 || frameOffset >= clip->timeline.duration ||
        !validEffectValue(effect->type, value)) {
        return failure(EditError::InvalidRange, revision_, "effect keyframe is invalid");
    }
    auto keyframe = std::ranges::find(effect->keyframes, frameOffset,
                                      &EffectKeyframe::frameOffset);
    if (keyframe == effect->keyframes.end()) {
        effect->keyframes.push_back({frameOffset, value, interpolation});
        std::ranges::sort(effect->keyframes, {}, &EffectKeyframe::frameOffset);
    } else {
        keyframe->value = value;
        keyframe->interpolation = interpolation;
    }
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId, .primaryEffect = effectId};
}

EditResult Sequence::removeEffectKeyframe(const ClipId clipId, const EffectId effectId,
                                          const Frame frameOffset) {
    auto [track, clip] = ::findClip(tracks_, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "track is locked");
    }
    const auto effect = std::ranges::find(clip->effects, effectId, &ClipEffect::id);
    if (effect == clip->effects.end()) {
        return failure(EditError::ClipNotFound, revision_, "effect not found");
    }
    const auto keyframe = std::ranges::find(effect->keyframes, frameOffset,
                                            &EffectKeyframe::frameOffset);
    if (keyframe == effect->keyframes.end()) {
        return failure(EditError::ClipNotFound, revision_, "effect keyframe not found");
    }
    effect->keyframes.erase(keyframe);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId, .primaryEffect = effectId};
}

EditResult Sequence::addMarker(const Frame position, std::string name) {
    if (position < 0 || name.empty() || name.size() > 512U) {
        return failure(EditError::InvalidRange, revision_, "marker is invalid");
    }
    markers_.push_back({.id = MarkerId{nextId()}, .position = position, .name = std::move(name)});
    std::ranges::sort(markers_, {}, &Marker::position);
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::setMarker(const MarkerId markerId, const Frame position,
                               std::string name, std::string comment,
                               const std::uint32_t color) {
    if (position < 0 || name.empty() || name.size() > 512U || comment.size() > 10'000U) {
        return failure(EditError::InvalidRange, revision_, "marker is invalid");
    }
    const auto marker = std::ranges::find_if(markers_, [markerId](const Marker& candidate) {
        return candidate.id == markerId;
    });
    if (marker == markers_.end()) {
        return failure(EditError::ClipNotFound, revision_, "marker not found");
    }
    marker->position = position;
    marker->name = std::move(name);
    marker->comment = std::move(comment);
    marker->color = color;
    std::ranges::sort(markers_, {}, &Marker::position);
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::removeMarker(const MarkerId markerId) {
    const auto marker = std::ranges::find_if(markers_, [markerId](const Marker& candidate) {
        return candidate.id == markerId;
    });
    if (marker == markers_.end()) {
        return failure(EditError::ClipNotFound, revision_, "marker not found");
    }
    markers_.erase(marker);
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::addCaption(const FrameRange range, std::string text) {
    if (!range.isValid() || range.duration <= 0 || text.empty() || text.size() > 10'000U) {
        return failure(EditError::InvalidRange, revision_, "caption is invalid");
    }
    captions_.push_back({.id = CaptionId{nextId()}, .timeline = range, .text = std::move(text)});
    std::ranges::sort(captions_, {}, [](const Caption& caption) { return caption.timeline.start; });
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::setCaption(const CaptionId captionId, const FrameRange range,
                                std::string text, const double positionX,
                                const double positionY, const double fontSize,
                                const std::uint32_t textColor,
                                const std::uint32_t backgroundColor, const bool bold,
                                const bool italic) {
    if (!range.isValid() || range.duration <= 0 || text.empty() || text.size() > 10'000U ||
        !std::isfinite(positionX) || !std::isfinite(positionY) || !std::isfinite(fontSize) ||
        positionX < 0.0 || positionX > 1.0 || positionY < 0.0 || positionY > 1.0 ||
        fontSize < 8.0 || fontSize > 300.0) {
        return failure(EditError::InvalidRange, revision_, "caption style is invalid");
    }
    auto caption = std::ranges::find_if(captions_, [captionId](const Caption& candidate) {
        return candidate.id == captionId;
    });
    if (caption == captions_.end()) {
        return failure(EditError::ClipNotFound, revision_, "caption not found");
    }
    caption->timeline = range;
    caption->text = std::move(text);
    caption->positionX = positionX;
    caption->positionY = positionY;
    caption->fontSize = fontSize;
    caption->textColor = textColor;
    caption->backgroundColor = backgroundColor;
    caption->bold = bold;
    caption->italic = italic;
    std::ranges::sort(captions_, {}, [](const Caption& value) { return value.timeline.start; });
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::removeCaption(const CaptionId captionId) {
    const auto caption = std::ranges::find_if(captions_, [captionId](const Caption& candidate) {
        return candidate.id == captionId;
    });
    if (caption == captions_.end()) {
        return failure(EditError::ClipNotFound, revision_, "caption not found");
    }
    captions_.erase(caption);
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::insertClip(const TrackId targetTrack, const AssetId assetId,
                                const Frame timelineStart, const Frame sourceStart,
                                const Frame duration, const LinkId linkId) {
    if (!assetId) {
        return failure(EditError::InvalidAsset, revision_, "asset ID is invalid");
    }
    if (!validClipRange(timelineStart, sourceStart, duration)) {
        return failure(EditError::InvalidRange, revision_, "clip range is invalid");
    }

    std::vector<Track> editedTracks = tracks_;
    std::uint64_t editedNextId = nextEntityId_;
    Track* target = ::findTrack(editedTracks, targetTrack);
    if (target == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "target track not found");
    }
    if (target->locked) {
        return failure(EditError::TrackLocked, revision_, "target track is locked");
    }

    std::vector<ClipId> createdClips;
    for (Track& track : editedTracks) {
        if (track.locked || (&track != target && !track.syncLocked)) {
            continue;
        }

        std::vector<Clip> replacements;
        replacements.reserve(track.clips.size() + 1U);
        for (Clip clip : track.clips) {
            const Frame clipEnd = clip.timeline.end();
            if (clip.timeline.start < timelineStart && clipEnd > timelineStart) {
                const Frame leftDuration = timelineStart - clip.timeline.start;
                const Frame rightDuration = clipEnd - timelineStart;
                Clip right = clip;
                right.id = ClipId{editedNextId++};
                right.timeline = {.start = timelineStart + duration, .duration = rightDuration};
                right.sourceStart += leftDuration;
                clip.timeline.duration = leftDuration;
                retimeEffectKeyframes(clip);
                retimeEffectKeyframes(right, leftDuration);
                clip.fadeOutFrames = 0;
                right.fadeInFrames = 0;
                right.videoTransitionInFrames = 0;
                right.audioTransitionInFrames = 0;
                clampEdgeEnvelopes(clip);
                clampEdgeEnvelopes(right);
                replacements.push_back(clip);
                replacements.push_back(right);
                createdClips.push_back(replacements.back().id);
            } else {
                if (clip.timeline.start >= timelineStart) {
                    Frame shiftedStart = 0;
                    if (!tryAdd(clip.timeline.start, duration, shiftedStart)) {
                        return failure(EditError::Overflow, revision_, "insert shift overflow");
                    }
                    clip.timeline.start = shiftedStart;
                }
                replacements.push_back(clip);
            }
        }
        track.clips = std::move(replacements);
    }

    const ClipId insertedId{editedNextId++};
    target = ::findTrack(editedTracks, targetTrack);
    target->clips.push_back({
        .id = insertedId,
        .assetId = assetId,
        .timeline = {.start = timelineStart, .duration = duration},
        .sourceStart = sourceStart,
        .linkId = linkId,
    });

    for (Track& track : editedTracks) {
        sortClips(track);
        if (hasCollisions(track)) {
            return failure(EditError::Collision, revision_, "insert produced a collision");
        }
    }

    std::vector<Marker> editedMarkers = markers_;
    for (Marker& marker : editedMarkers) {
        if (marker.position >= timelineStart) {
            Frame shifted = 0;
            if (!tryAdd(marker.position, duration, shifted)) {
                return failure(EditError::Overflow, revision_, "insert marker shift overflow");
            }
            marker.position = shifted;
        }
    }
    std::vector<Caption> editedCaptions;
    editedCaptions.reserve(captions_.size() + 1U);
    for (const Caption& caption : captions_) {
        const Frame captionStart = caption.timeline.start;
        const Frame captionEnd = caption.timeline.end();
        if (captionEnd <= timelineStart) {
            editedCaptions.push_back(caption);
        } else if (captionStart >= timelineStart) {
            Frame shiftedStart = 0;
            if (!tryAdd(captionStart, duration, shiftedStart)) {
                return failure(EditError::Overflow, revision_, "insert caption shift overflow");
            }
            Caption shifted = caption;
            shifted.timeline.start = shiftedStart;
            editedCaptions.push_back(std::move(shifted));
        } else {
            if (editedNextId == std::numeric_limits<std::uint64_t>::max()) {
                return failure(EditError::Overflow, revision_,
                               "insert caption ID range is exhausted");
            }
            Caption left = caption;
            left.timeline.duration = timelineStart - captionStart;
            editedCaptions.push_back(std::move(left));
            Caption right = caption;
            right.id = CaptionId{editedNextId++};
            right.timeline = {.start = timelineStart + duration,
                              .duration = captionEnd - timelineStart};
            editedCaptions.push_back(std::move(right));
        }
    }

    createdClips.push_back(insertedId);
    tracks_ = std::move(editedTracks);
    markers_ = std::move(editedMarkers);
    captions_ = std::move(editedCaptions);
    nextEntityId_ = editedNextId;
    ++revision_;
    return {
        .revision = revision_,
        .primaryClip = insertedId,
        .createdClips = std::move(createdClips),
    };
}

EditResult Sequence::overwriteClip(const TrackId targetTrack, const AssetId assetId,
                                   const Frame timelineStart, const Frame sourceStart,
                                   const Frame duration, const LinkId linkId) {
    if (!assetId) {
        return failure(EditError::InvalidAsset, revision_, "asset ID is invalid");
    }
    if (!validClipRange(timelineStart, sourceStart, duration)) {
        return failure(EditError::InvalidRange, revision_, "clip range is invalid");
    }

    std::vector<Track> editedTracks = tracks_;
    std::uint64_t editedNextId = nextEntityId_;
    Track* target = ::findTrack(editedTracks, targetTrack);
    if (target == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "target track not found");
    }
    if (target->locked) {
        return failure(EditError::TrackLocked, revision_, "target track is locked");
    }

    const Frame overwriteEnd = timelineStart + duration;
    std::vector<Clip> replacements;
    std::vector<ClipId> createdClips;
    replacements.reserve(target->clips.size() + 1U);
    for (Clip clip : target->clips) {
        const Frame clipStart = clip.timeline.start;
        const Frame clipEnd = clip.timeline.end();
        if (clipEnd <= timelineStart || clipStart >= overwriteEnd) {
            replacements.push_back(clip);
            continue;
        }

        if (clipStart < timelineStart) {
            Clip left = clip;
            left.timeline.duration = timelineStart - clipStart;
            retimeEffectKeyframes(left);
            left.fadeOutFrames = 0;
            clampEdgeEnvelopes(left);
            replacements.push_back(left);
        }
        if (clipEnd > overwriteEnd) {
            Clip right = clip;
            right.id = ClipId{editedNextId++};
            right.timeline = {.start = overwriteEnd, .duration = clipEnd - overwriteEnd};
            right.sourceStart += overwriteEnd - clipStart;
            retimeEffectKeyframes(right, overwriteEnd - clipStart);
            right.fadeInFrames = 0;
            right.videoTransitionInFrames = 0;
            right.audioTransitionInFrames = 0;
            clampEdgeEnvelopes(right);
            replacements.push_back(right);
            createdClips.push_back(replacements.back().id);
        }
    }

    const ClipId insertedId{editedNextId++};
    replacements.push_back({
        .id = insertedId,
        .assetId = assetId,
        .timeline = {.start = timelineStart, .duration = duration},
        .sourceStart = sourceStart,
        .linkId = linkId,
    });
    target->clips = std::move(replacements);
    sortClips(*target);
    if (hasCollisions(*target)) {
        return failure(EditError::Collision, revision_, "overwrite produced a collision");
    }

    createdClips.push_back(insertedId);
    tracks_ = std::move(editedTracks);
    nextEntityId_ = editedNextId;
    ++revision_;
    return {
        .revision = revision_,
        .primaryClip = insertedId,
        .createdClips = std::move(createdClips),
    };
}

EditResult Sequence::moveClip(const ClipId clipId, const Frame newTimelineStart) {
    if (newTimelineStart < 0) {
        return failure(EditError::InvalidRange, revision_, "move position is invalid");
    }

    std::vector<Track> editedTracks = tracks_;
    auto [track, clip] = ::findClip(editedTracks, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }

    Frame newEnd = 0;
    if (!tryAdd(newTimelineStart, clip->timeline.duration, newEnd)) {
        return failure(EditError::Overflow, revision_, "move range overflow");
    }
    clip->timeline.start = newTimelineStart;
    sortClips(*track);
    if (hasCollisions(*track)) {
        return failure(EditError::Collision, revision_, "move would overlap another clip");
    }

    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId};
}

EditResult Sequence::moveClipToTrack(const ClipId clipId, const TrackId targetTrackId,
                                     const Frame newTimelineStart) {
    if (newTimelineStart < 0) {
        return failure(EditError::InvalidRange, revision_, "move position is invalid");
    }
    std::vector<Track> editedTracks = tracks_;
    auto [sourceTrack, sourceClip] = ::findClip(editedTracks, clipId);
    Track* targetTrack = ::findTrack(editedTracks, targetTrackId);
    if (sourceTrack == nullptr || sourceClip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (targetTrack == nullptr) {
        return failure(EditError::TrackNotFound, revision_, "target track not found");
    }
    if (sourceTrack->locked || targetTrack->locked) {
        return failure(EditError::TrackLocked, revision_, "source or target track is locked");
    }
    if (sourceTrack->kind != targetTrack->kind) {
        return failure(EditError::InvalidRange, revision_, "clip and target track kinds differ");
    }
    if (sourceTrack == targetTrack) {
        return moveClip(clipId, newTimelineStart);
    }
    Frame newEnd = 0;
    if (!tryAdd(newTimelineStart, sourceClip->timeline.duration, newEnd)) {
        return failure(EditError::Overflow, revision_, "move range overflow");
    }
    Clip moved = *sourceClip;
    moved.timeline.start = newTimelineStart;
    const auto sourceIterator = std::ranges::find(sourceTrack->clips, clipId, &Clip::id);
    sourceTrack->clips.erase(sourceIterator);
    targetTrack->clips.push_back(std::move(moved));
    sortClips(*targetTrack);
    if (hasCollisions(*targetTrack)) {
        return failure(EditError::Collision, revision_, "move would overlap another clip");
    }
    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId, .primaryTrack = targetTrackId};
}

EditResult Sequence::pasteClips(std::vector<ClipPlacement> placements) {
    if (placements.empty()) {
        return {.revision = revision_};
    }
    std::vector<Track> editedTracks = tracks_;
    std::uint64_t editedNextId = nextEntityId_;
    std::unordered_map<std::uint64_t, LinkId> pastedLinks;
    std::vector<ClipId> created;
    created.reserve(placements.size());
    for (ClipPlacement& placement : placements) {
        Track* target = ::findTrack(editedTracks, placement.targetTrack);
        if (target == nullptr) {
            return failure(EditError::TrackNotFound, revision_, "paste target track not found");
        }
        if (target->locked) {
            return failure(EditError::TrackLocked, revision_, "paste target track is locked");
        }
        if (target->kind != placement.kind) {
            return failure(EditError::InvalidRange, revision_,
                           "pasted clip and target track kinds differ");
        }
        Clip pasted = placement.clip;
        if (!pasted.assetId || !validClipRange(placement.timelineStart, pasted.sourceStart,
                                               pasted.timeline.duration) ||
            !std::isfinite(pasted.opacity) || pasted.opacity < 0.0 || pasted.opacity > 1.0 ||
            !std::isfinite(pasted.audioGainDb) || pasted.audioGainDb < -60.0 ||
            pasted.audioGainDb > 24.0 || !std::isfinite(pasted.playbackRate) ||
            pasted.playbackRate < 0.25 || pasted.playbackRate > 4.0 ||
            pasted.fadeInFrames < 0 || pasted.fadeOutFrames < 0 ||
            pasted.fadeInFrames > pasted.timeline.duration ||
            pasted.fadeOutFrames > pasted.timeline.duration) {
            return failure(EditError::InvalidRange, revision_, "pasted clip is invalid");
        }
        if (!validTransform(pasted)) {
            return failure(EditError::InvalidRange, revision_, "pasted transform is invalid");
        }
        if (!validCrop(pasted)) {
            return failure(EditError::InvalidRange, revision_, "pasted crop is invalid");
        }
        if (!validMask(pasted)) {
            return failure(EditError::InvalidRange, revision_, "pasted mask is invalid");
        }
        if (pasted.videoTransitionInFrames < 0 || pasted.audioTransitionInFrames < 0 ||
            pasted.videoTransitionInFrames > pasted.timeline.duration ||
            pasted.audioTransitionInFrames > pasted.timeline.duration ||
            (placement.kind == TrackKind::Video && pasted.audioTransitionInFrames != 0) ||
            (placement.kind == TrackKind::Audio && pasted.videoTransitionInFrames != 0)) {
            return failure(EditError::InvalidRange, revision_, "pasted transitions are invalid");
        }
        Frame previousMotionFrame = -1;
        for (const MotionKeyframe& keyframe : pasted.motionKeyframes) {
            if (keyframe.frameOffset <= previousMotionFrame || keyframe.frameOffset < 0 ||
                keyframe.frameOffset >= pasted.timeline.duration ||
                !validMotionKeyframe(keyframe)) {
                return failure(EditError::InvalidRange, revision_,
                               "pasted motion keyframe is invalid");
            }
            previousMotionFrame = keyframe.frameOffset;
        }
        Frame previousGainFrame = -1;
        for (const GainKeyframe& keyframe : pasted.gainKeyframes) {
            if (keyframe.frameOffset <= previousGainFrame || keyframe.frameOffset < 0 ||
                keyframe.frameOffset >= pasted.timeline.duration ||
                !std::isfinite(keyframe.gainDb) || keyframe.gainDb < -60.0 ||
                keyframe.gainDb > 24.0 ||
                static_cast<int>(keyframe.interpolation) < 0 ||
                static_cast<int>(keyframe.interpolation) >
                    static_cast<int>(KeyframeInterpolation::EaseInOut)) {
                return failure(EditError::InvalidRange, revision_,
                               "pasted gain keyframe is invalid");
            }
            previousGainFrame = keyframe.frameOffset;
        }
        Frame previousSpeedFrame = -1;
        for (const SpeedKeyframe& keyframe : pasted.speedKeyframes) {
            if (keyframe.frameOffset < 0 || keyframe.frameOffset >= pasted.timeline.duration ||
                keyframe.frameOffset <= previousSpeedFrame || !std::isfinite(keyframe.rate) ||
                keyframe.rate < 0.25 || keyframe.rate > 4.0) {
                return failure(EditError::InvalidRange, revision_,
                               "pasted speed keyframe is invalid");
            }
            previousSpeedFrame = keyframe.frameOffset;
        }
        if (editedNextId == std::numeric_limits<std::uint64_t>::max()) {
            return failure(EditError::Overflow, revision_, "paste entity ID range is exhausted");
        }
        pasted.id = ClipId{editedNextId++};
        pasted.timeline.start = placement.timelineStart;
        for (ClipEffect& effect : pasted.effects) {
            if (!validEffectValue(effect.type, effect.amount) ||
                editedNextId == std::numeric_limits<std::uint64_t>::max()) {
                return failure(EditError::InvalidRange, revision_, "pasted effect is invalid");
            }
            effect.id = EffectId{editedNextId++};
            Frame previous = -1;
            for (const EffectKeyframe& keyframe : effect.keyframes) {
                if (keyframe.frameOffset < 0 ||
                    keyframe.frameOffset >= pasted.timeline.duration ||
                    keyframe.frameOffset <= previous ||
                    !validEffectValue(effect.type, keyframe.value)) {
                    return failure(EditError::InvalidRange, revision_,
                                   "pasted effect keyframe is invalid");
                }
                previous = keyframe.frameOffset;
            }
        }
        if (pasted.linkId) {
            auto [link, inserted] = pastedLinks.try_emplace(pasted.linkId.value);
            if (inserted) {
                if (editedNextId == std::numeric_limits<std::uint64_t>::max()) {
                    return failure(EditError::Overflow, revision_,
                                   "paste link ID range is exhausted");
                }
                link->second = LinkId{editedNextId++};
            }
            pasted.linkId = link->second;
        }
        created.push_back(pasted.id);
        target->clips.push_back(std::move(pasted));
    }
    for (Track& track : editedTracks) {
        sortClips(track);
        if (hasCollisions(track)) {
            return failure(EditError::Collision, revision_, "paste would overlap another clip");
        }
    }
    tracks_ = std::move(editedTracks);
    nextEntityId_ = editedNextId;
    ++revision_;
    return {.revision = revision_,
            .primaryClip = created.front(),
            .createdClips = std::move(created)};
}

EditResult Sequence::trimClip(const ClipId clipId, const Frame newTimelineStart,
                              const Frame newTimelineEnd) {
    if (newTimelineStart < 0 || newTimelineEnd <= newTimelineStart) {
        return failure(EditError::InvalidRange, revision_, "trim range is invalid");
    }

    std::vector<Track> editedTracks = tracks_;
    auto [track, clip] = ::findClip(editedTracks, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }

    const Frame startDelta = newTimelineStart - clip->timeline.start;
    Frame newSourceStart = 0;
    if (!tryAdd(clip->sourceStart, startDelta, newSourceStart) || newSourceStart < 0) {
        return failure(EditError::InvalidRange, revision_, "trim exceeds the source start");
    }
    const Frame newDuration = newTimelineEnd - newTimelineStart;
    Frame sourceEnd = 0;
    if (!tryAdd(newSourceStart, newDuration, sourceEnd)) {
        return failure(EditError::Overflow, revision_, "trim source range overflow");
    }

    clip->timeline = {.start = newTimelineStart, .duration = newDuration};
    clip->sourceStart = newSourceStart;
    retimeEffectKeyframes(*clip, startDelta);
    clip->fadeInFrames = std::min(clip->fadeInFrames, newDuration);
    clip->fadeOutFrames = std::min(clip->fadeOutFrames, newDuration);
    clip->videoTransitionInFrames = std::min(clip->videoTransitionInFrames, newDuration);
    clip->audioTransitionInFrames = std::min(clip->audioTransitionInFrames, newDuration);
    sortClips(*track);
    if (hasCollisions(*track)) {
        return failure(EditError::Collision, revision_, "trim would overlap another clip");
    }

    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId};
}

EditResult Sequence::slipClip(const ClipId clipId, const Frame sourceDelta) {
    std::vector<Track> editedTracks = tracks_;
    auto [track, clip] = ::findClip(editedTracks, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }
    Frame newSourceStart = 0;
    Frame newSourceEnd = 0;
    if (!tryAdd(clip->sourceStart, sourceDelta, newSourceStart) || newSourceStart < 0) {
        return failure(EditError::InvalidRange, revision_, "slip exceeds the source start");
    }
    if (!tryAdd(newSourceStart, clip->timeline.duration, newSourceEnd)) {
        return failure(EditError::Overflow, revision_, "slip source range overflow");
    }
    if (sourceDelta == 0) {
        return {.revision = revision_, .primaryClip = clipId};
    }
    clip->sourceStart = newSourceStart;
    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId};
}

EditResult Sequence::rollEdit(const ClipId leftClipId, const ClipId rightClipId,
                              const Frame newCut) {
    if (leftClipId == rightClipId) {
        return failure(EditError::InvalidRange, revision_, "roll clips must be different");
    }
    std::vector<Track> editedTracks = tracks_;
    auto [leftTrack, left] = ::findClip(editedTracks, leftClipId);
    auto [rightTrack, right] = ::findClip(editedTracks, rightClipId);
    if (left == nullptr || right == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "roll clip not found");
    }
    if (leftTrack != rightTrack || left->timeline.end() != right->timeline.start) {
        return failure(EditError::InvalidRange, revision_, "roll clips must share an adjacent cut");
    }
    if (leftTrack->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }
    if (newCut <= left->timeline.start || newCut >= right->timeline.end()) {
        return failure(EditError::InvalidRange, revision_, "roll cut would create an empty clip");
    }
    const Frame oldCut = right->timeline.start;
    const Frame rightEnd = right->timeline.end();
    const Frame delta = newCut - oldCut;
    Frame newRightSourceStart = 0;
    if (!tryAdd(right->sourceStart, delta, newRightSourceStart) || newRightSourceStart < 0) {
        return failure(EditError::InvalidRange, revision_, "roll exceeds the right source start");
    }
    Frame leftSourceEnd = 0;
    if (!tryAdd(left->sourceStart, newCut - left->timeline.start, leftSourceEnd)) {
        return failure(EditError::Overflow, revision_, "roll source range overflow");
    }
    if (delta == 0) {
        return {.revision = revision_, .primaryClip = leftClipId};
    }
    left->timeline.duration = newCut - left->timeline.start;
    right->timeline = {.start = newCut, .duration = rightEnd - newCut};
    right->sourceStart = newRightSourceStart;
    retimeEffectKeyframes(*left);
    retimeEffectKeyframes(*right, delta);
    left->fadeInFrames = std::min(left->fadeInFrames, left->timeline.duration);
    left->fadeOutFrames = std::min(left->fadeOutFrames, left->timeline.duration);
    right->fadeInFrames = std::min(right->fadeInFrames, right->timeline.duration);
    right->fadeOutFrames = std::min(right->fadeOutFrames, right->timeline.duration);
    for (Clip* affected : {left, right}) {
        affected->videoTransitionInFrames =
            std::min(affected->videoTransitionInFrames, affected->timeline.duration);
        affected->audioTransitionInFrames =
            std::min(affected->audioTransitionInFrames, affected->timeline.duration);
    }
    sortClips(*leftTrack);
    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_, .primaryClip = leftClipId};
}

EditResult Sequence::rippleTrimEnd(const ClipId clipId, const Frame newTimelineEnd) {
    std::vector<Track> editedTracks = tracks_;
    auto [targetTrack, targetClip] = ::findClip(editedTracks, clipId);
    if (targetTrack == nullptr || targetClip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (targetTrack->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }
    const Frame oldEnd = targetClip->timeline.end();
    if (newTimelineEnd <= targetClip->timeline.start) {
        return failure(EditError::InvalidRange, revision_, "ripple trim would empty the clip");
    }
    const Frame delta = newTimelineEnd - oldEnd;
    if (delta == 0) {
        return {.revision = revision_, .primaryClip = clipId};
    }
    Frame sourceEnd = 0;
    if (!tryAdd(targetClip->sourceStart, newTimelineEnd - targetClip->timeline.start, sourceEnd)) {
        return failure(EditError::Overflow, revision_, "ripple trim source range overflow");
    }
    targetClip->timeline.duration = newTimelineEnd - targetClip->timeline.start;
    retimeEffectKeyframes(*targetClip);
    targetClip->fadeInFrames = std::min(targetClip->fadeInFrames, targetClip->timeline.duration);
    targetClip->fadeOutFrames = std::min(targetClip->fadeOutFrames, targetClip->timeline.duration);
    targetClip->videoTransitionInFrames =
        std::min(targetClip->videoTransitionInFrames, targetClip->timeline.duration);
    targetClip->audioTransitionInFrames =
        std::min(targetClip->audioTransitionInFrames, targetClip->timeline.duration);
    for (Track& track : editedTracks) {
        const bool isTargetTrack = &track == targetTrack;
        if (track.locked || (!isTargetTrack && !track.syncLocked)) {
            continue;
        }
        for (Clip& clip : track.clips) {
            if (clip.id == clipId || clip.timeline.start < oldEnd) {
                continue;
            }
            Frame shifted = 0;
            if (!tryAdd(clip.timeline.start, delta, shifted) || shifted < 0) {
                return failure(EditError::InvalidRange, revision_,
                               "ripple trim would move content before zero");
            }
            clip.timeline.start = shifted;
        }
        sortClips(track);
        if (hasCollisions(track)) {
            return failure(EditError::Collision, revision_, "ripple trim produced a collision");
        }
    }
    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId};
}

EditResult Sequence::slideClip(const ClipId clipId, const Frame timelineDelta) {
    if (timelineDelta == 0) {
        return {.revision = revision_, .primaryClip = clipId};
    }
    std::vector<Track> editedTracks = tracks_;
    auto [track, clip] = ::findClip(editedTracks, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }
    sortClips(*track);
    const auto iterator = std::ranges::find(track->clips, clipId, &Clip::id);
    if (iterator == track->clips.begin() || std::next(iterator) == track->clips.end()) {
        return failure(EditError::InvalidRange, revision_, "slide requires two neighbors");
    }
    Clip& selected = *iterator;
    Clip& left = *std::prev(iterator);
    Clip& right = *std::next(iterator);
    if (left.timeline.end() != selected.timeline.start ||
        selected.timeline.end() != right.timeline.start) {
        return failure(EditError::InvalidRange, revision_, "slide neighbors must be adjacent");
    }
    Frame newLeftDuration = 0;
    Frame newRightDuration = 0;
    Frame newSelectedStart = 0;
    Frame newRightStart = 0;
    Frame newRightSourceStart = 0;
    if (!tryAdd(left.timeline.duration, timelineDelta, newLeftDuration) ||
        !trySub(right.timeline.duration, timelineDelta, newRightDuration) ||
        !tryAdd(selected.timeline.start, timelineDelta, newSelectedStart) ||
        !tryAdd(right.timeline.start, timelineDelta, newRightStart)) {
        return failure(EditError::Overflow, revision_, "slide range overflow");
    }
    if (newLeftDuration <= 0 || newRightDuration <= 0 || newSelectedStart < 0 ||
        !tryAdd(right.sourceStart, timelineDelta, newRightSourceStart) ||
        newRightSourceStart < 0) {
        return failure(EditError::InvalidRange, revision_, "slide exceeds available handles");
    }
    Frame leftSourceEnd = 0;
    if (!tryAdd(left.sourceStart, newLeftDuration, leftSourceEnd)) {
        return failure(EditError::Overflow, revision_, "slide source range overflow");
    }
    left.timeline.duration = newLeftDuration;
    selected.timeline.start = newSelectedStart;
    right.timeline.start = newRightStart;
    right.timeline.duration = newRightDuration;
    right.sourceStart = newRightSourceStart;
    retimeEffectKeyframes(left);
    retimeEffectKeyframes(right, timelineDelta);
    for (Clip* affected : {&left, &right}) {
        affected->fadeInFrames = std::min(affected->fadeInFrames, affected->timeline.duration);
        affected->fadeOutFrames = std::min(affected->fadeOutFrames, affected->timeline.duration);
        affected->videoTransitionInFrames =
            std::min(affected->videoTransitionInFrames, affected->timeline.duration);
        affected->audioTransitionInFrames =
            std::min(affected->audioTransitionInFrames, affected->timeline.duration);
    }
    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_, .primaryClip = clipId};
}

EditResult Sequence::splitClip(const ClipId clipId, const Frame splitPosition,
                               const LinkId rightLinkId) {
    std::vector<Track> editedTracks = tracks_;
    auto [track, clip] = ::findClip(editedTracks, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }
    if (splitPosition <= clip->timeline.start || splitPosition >= clip->timeline.end()) {
        return failure(EditError::InvalidRange, revision_, "split must be inside the clip");
    }

    const Frame leftDuration = splitPosition - clip->timeline.start;
    const Frame rightDuration = clip->timeline.end() - splitPosition;
    const Clip original = *clip;
    const ClipId leftId{nextEntityId_};
    const ClipId rightId{nextEntityId_ + 1U};

    Clip left = original;
    left.id = leftId;
    left.timeline.duration = leftDuration;
    retimeEffectKeyframes(left);
    left.fadeOutFrames = 0;
    clampEdgeEnvelopes(left);

    Clip right = original;
    right.id = rightId;
    right.timeline = {.start = splitPosition, .duration = rightDuration};
    right.sourceStart += leftDuration;
    retimeEffectKeyframes(right, leftDuration);
    right.fadeInFrames = 0;
    right.videoTransitionInFrames = 0;
    right.audioTransitionInFrames = 0;
    clampEdgeEnvelopes(right);
    if (original.linkId && rightLinkId) {
        right.linkId = rightLinkId;
    }

    const auto iterator = std::ranges::find(track->clips, clipId, &Clip::id);
    *iterator = left;
    track->clips.insert(iterator + 1, right);

    nextEntityId_ += 2U;
    tracks_ = std::move(editedTracks);
    ++revision_;
    return {
        .revision = revision_,
        .primaryClip = leftId,
        .createdClips = {leftId, rightId},
    };
}

EditResult Sequence::liftClip(const ClipId clipId) {
    std::vector<Track> editedTracks = tracks_;
    auto [track, clip] = ::findClip(editedTracks, clipId);
    if (track == nullptr || clip == nullptr) {
        return failure(EditError::ClipNotFound, revision_, "clip not found");
    }
    if (track->locked) {
        return failure(EditError::TrackLocked, revision_, "clip track is locked");
    }

    const auto iterator = std::ranges::find(track->clips, clipId, &Clip::id);
    track->clips.erase(iterator);
    tracks_ = std::move(editedTracks);
    ++revision_;
    return {.revision = revision_};
}

EditResult Sequence::liftRange(const FrameRange range) {
    if (!range.isValid() || range.duration <= 0) {
        return failure(EditError::InvalidRange, revision_, "lift range is invalid");
    }
    const Frame rangeEnd = range.end();
    std::vector<Track> editedTracks = tracks_;
    std::uint64_t editedNextId = nextEntityId_;
    std::vector<ClipId> createdClips;
    bool changed = false;
    for (Track& track : editedTracks) {
        if (track.locked) continue;
        std::vector<Clip> replacements;
        replacements.reserve(track.clips.size() + 1U);
        for (Clip clip : track.clips) {
            const Frame clipStart = clip.timeline.start;
            const Frame clipEnd = clip.timeline.end();
            if (clipEnd <= range.start || clipStart >= rangeEnd) {
                replacements.push_back(clip);
                continue;
            }
            changed = true;
            const bool keepLeft = clipStart < range.start;
            const bool keepRight = clipEnd > rangeEnd;
            if (keepLeft) {
                Clip left = clip;
                left.timeline.duration = range.start - clipStart;
                retimeEffectKeyframes(left);
                left.fadeOutFrames = 0;
                clampEdgeEnvelopes(left);
                replacements.push_back(left);
            }
            if (keepRight) {
                Clip right = clip;
                if (keepLeft) {
                    if (editedNextId == std::numeric_limits<std::uint64_t>::max()) {
                        return failure(EditError::Overflow, revision_,
                                       "lift clip ID range is exhausted");
                    }
                    right.id = ClipId{editedNextId++};
                    createdClips.push_back(right.id);
                }
                right.timeline = {.start = rangeEnd, .duration = clipEnd - rangeEnd};
                right.sourceStart += rangeEnd - clipStart;
                retimeEffectKeyframes(right, rangeEnd - clipStart);
                right.fadeInFrames = 0;
                right.videoTransitionInFrames = 0;
                right.audioTransitionInFrames = 0;
                clampEdgeEnvelopes(right);
                replacements.push_back(right);
            }
        }
        track.clips = std::move(replacements);
        sortClips(track);
        if (hasCollisions(track)) {
            return failure(EditError::Collision, revision_, "lift produced a collision");
        }
    }
    std::vector<Caption> editedCaptions;
    editedCaptions.reserve(captions_.size() + 1U);
    for (Caption caption : captions_) {
        const Frame captionStart = caption.timeline.start;
        const Frame captionEnd = caption.timeline.end();
        if (captionEnd <= range.start || captionStart >= rangeEnd) {
            editedCaptions.push_back(caption);
            continue;
        }
        changed = true;
        const bool keepLeft = captionStart < range.start;
        const bool keepRight = captionEnd > rangeEnd;
        if (keepLeft) {
            Caption left = caption;
            left.timeline.duration = range.start - captionStart;
            editedCaptions.push_back(left);
        }
        if (keepRight) {
            Caption right = caption;
            if (keepLeft) {
                if (editedNextId == std::numeric_limits<std::uint64_t>::max()) {
                    return failure(EditError::Overflow, revision_,
                                   "lift caption ID range is exhausted");
                }
                right.id = CaptionId{editedNextId++};
            }
            right.timeline = {.start = rangeEnd, .duration = captionEnd - rangeEnd};
            editedCaptions.push_back(right);
        }
    }
    if (!changed) return {.revision = revision_};
    tracks_ = std::move(editedTracks);
    captions_ = std::move(editedCaptions);
    nextEntityId_ = editedNextId;
    ++revision_;
    return {.revision = revision_, .createdClips = std::move(createdClips)};
}

EditResult Sequence::extractRange(const FrameRange range) {
    if (!range.isValid() || range.duration <= 0) {
        return failure(EditError::InvalidRange, revision_, "extract range is invalid");
    }

    const Frame rangeEnd = range.end();
    std::vector<Track> editedTracks = tracks_;
    std::uint64_t editedNextId = nextEntityId_;
    std::vector<ClipId> createdClips;
    bool changed = false;

    for (Track& track : editedTracks) {
        if (track.locked || !track.syncLocked) {
            continue;
        }

        std::vector<Clip> replacements;
        replacements.reserve(track.clips.size() + 1U);
        for (Clip clip : track.clips) {
            const Frame clipStart = clip.timeline.start;
            const Frame clipEnd = clip.timeline.end();
            if (clipEnd <= range.start) {
                replacements.push_back(clip);
                continue;
            }
            if (clipStart >= rangeEnd) {
                clip.timeline.start -= range.duration;
                replacements.push_back(clip);
                changed = true;
                continue;
            }

            changed = true;
            const bool keepLeft = clipStart < range.start;
            const bool keepRight = clipEnd > rangeEnd;
            if (keepLeft) {
                Clip left = clip;
                left.timeline.duration = range.start - clipStart;
                retimeEffectKeyframes(left);
                left.fadeOutFrames = 0;
                clampEdgeEnvelopes(left);
                replacements.push_back(left);
            }
            if (keepRight) {
                Clip right = clip;
                if (keepLeft) {
                    if (editedNextId == std::numeric_limits<std::uint64_t>::max()) {
                        return failure(EditError::Overflow, revision_,
                                       "extract clip ID range is exhausted");
                    }
                    right.id = ClipId{editedNextId++};
                    createdClips.push_back(right.id);
                }
                right.timeline = {.start = range.start, .duration = clipEnd - rangeEnd};
                right.sourceStart += rangeEnd - clipStart;
                retimeEffectKeyframes(right, rangeEnd - clipStart);
                right.fadeInFrames = 0;
                right.videoTransitionInFrames = 0;
                right.audioTransitionInFrames = 0;
                clampEdgeEnvelopes(right);
                replacements.push_back(right);
            }
        }
        track.clips = std::move(replacements);
        sortClips(track);
        if (hasCollisions(track)) {
            return failure(EditError::Collision, revision_, "extract produced a collision");
        }
    }

    std::vector<Marker> editedMarkers;
    editedMarkers.reserve(markers_.size());
    for (Marker marker : markers_) {
        if (marker.position < range.start) {
            editedMarkers.push_back(std::move(marker));
        } else if (marker.position >= rangeEnd) {
            marker.position -= range.duration;
            editedMarkers.push_back(std::move(marker));
            changed = true;
        } else {
            changed = true;
        }
    }
    std::vector<Caption> editedCaptions;
    editedCaptions.reserve(captions_.size());
    const auto collapsedFrame = [range, rangeEnd](const Frame frame) {
        if (frame <= range.start) return frame;
        if (frame >= rangeEnd) return frame - range.duration;
        return range.start;
    };
    for (Caption caption : captions_) {
        const Frame newStart = collapsedFrame(caption.timeline.start);
        const Frame newEnd = collapsedFrame(caption.timeline.end());
        if (newEnd > newStart) {
            if (newStart != caption.timeline.start || newEnd != caption.timeline.end())
                changed = true;
            caption.timeline = {.start = newStart, .duration = newEnd - newStart};
            editedCaptions.push_back(std::move(caption));
        } else {
            changed = true;
        }
    }

    if (!changed) {
        return {.revision = revision_};
    }
    tracks_ = std::move(editedTracks);
    markers_ = std::move(editedMarkers);
    captions_ = std::move(editedCaptions);
    nextEntityId_ = editedNextId;
    ++revision_;
    return {.revision = revision_, .createdClips = std::move(createdClips)};
}

const Track* Sequence::findTrack(const TrackId trackId) const noexcept {
    const auto iterator = std::ranges::find(tracks_, trackId, &Track::id);
    return iterator == tracks_.end() ? nullptr : &*iterator;
}

const Clip* Sequence::findClip(const ClipId clipId) const noexcept {
    for (const Track& track : tracks_) {
        const auto iterator = std::ranges::find(track.clips, clipId, &Clip::id);
        if (iterator != track.clips.end()) {
            return &*iterator;
        }
    }
    return nullptr;
}

} // namespace videx::core
