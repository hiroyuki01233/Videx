#include <videx/core/edit_session.hpp>
#include <videx/core/timeline.hpp>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "timeline test failed: " << message << '\n';
        ++failures;
    }
}

void testInsertSplitsAndShiftsSyncLockedTracks() {
    using namespace videx::core;

    Sequence sequence({24, 1});
    const TrackId videoTrack = sequence.addTrack(TrackKind::Video);
    const TrackId audioTrack = sequence.addTrack(TrackKind::Audio);

    const auto video = sequence.overwriteClip(videoTrack, AssetId{10}, 0, 100, 20);
    const auto audio = sequence.overwriteClip(audioTrack, AssetId{10}, 0, 200, 20);
    expect(video.succeeded() && audio.succeeded(), "fixture clips should be created");

    const auto insertion = sequence.insertClip(videoTrack, AssetId{20}, 8, 0, 4);
    expect(insertion.succeeded(), "insert should succeed");
    expect(sequence.findTrack(videoTrack)->clips.size() == 3U,
           "video clip should split around inserted media");
    expect(sequence.findTrack(audioTrack)->clips.size() == 2U,
           "sync-locked audio clip should split");

    const auto& videoClips = sequence.findTrack(videoTrack)->clips;
    expect(videoClips[0].timeline == FrameRange{0, 8}, "left video range should be preserved");
    expect(videoClips[1].timeline == FrameRange{8, 4}, "inserted video should occupy the gap");
    expect(videoClips[2].timeline == FrameRange{12, 12}, "right video should shift right");
    expect(videoClips[2].sourceStart == 108, "right video source should advance after split");

    const auto& audioClips = sequence.findTrack(audioTrack)->clips;
    expect(audioClips[0].timeline == FrameRange{0, 8}, "left audio range should be preserved");
    expect(audioClips[1].timeline == FrameRange{12, 12}, "right audio should shift right");
    expect(audioClips[1].sourceStart == 208, "right audio source should advance after split");
}

void testOverwritePreservesOuterHandles() {
    using namespace videx::core;

    Sequence sequence;
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const auto original = sequence.overwriteClip(track, AssetId{1}, 0, 50, 20);
    expect(original.succeeded(), "original overwrite should succeed");

    const auto replacement = sequence.overwriteClip(track, AssetId{2}, 5, 0, 10);
    expect(replacement.succeeded(), "middle overwrite should succeed");

    const auto& clips = sequence.findTrack(track)->clips;
    expect(clips.size() == 3U, "overwrite should preserve left and right parts");
    expect(clips[0].timeline == FrameRange{0, 5} && clips[0].sourceStart == 50,
           "left handle should be unchanged");
    expect(clips[1].assetId == AssetId{2} && clips[1].timeline == FrameRange{5, 10},
           "replacement should occupy the overwrite range");
    expect(clips[2].timeline == FrameRange{15, 5} && clips[2].sourceStart == 65,
           "right handle should preserve source timing");
}

void testFailedMoveIsAtomic() {
    using namespace videx::core;

    Sequence sequence;
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const auto first = sequence.overwriteClip(track, AssetId{1}, 0, 0, 10);
    const auto second = sequence.overwriteClip(track, AssetId{2}, 20, 0, 10);
    const std::uint64_t revisionBeforeMove = sequence.revision();

    const auto move = sequence.moveClip(second.primaryClip, 5);
    expect(move.error == EditError::Collision, "overlapping move should be rejected");
    expect(sequence.revision() == revisionBeforeMove, "failed move must not change revision");
    expect(sequence.findClip(first.primaryClip)->timeline == FrameRange{0, 10},
           "first clip must remain unchanged");
    expect(sequence.findClip(second.primaryClip)->timeline == FrameRange{20, 10},
           "moved clip must remain unchanged");
}

void testSplitReplacesIdentityAndPreservesSourceTime() {
    using namespace videx::core;

    Sequence sequence;
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const auto original = sequence.overwriteClip(track, AssetId{1}, 10, 100, 10);

    const auto split = sequence.splitClip(original.primaryClip, 14);
    expect(split.succeeded(), "split should succeed");
    expect(!sequence.findClip(original.primaryClip), "old clip identity should be removed");
    expect(split.createdClips.size() == 2U, "split should create two identities");

    const Clip* left = sequence.findClip(split.createdClips[0]);
    const Clip* right = sequence.findClip(split.createdClips[1]);
    expect(left != nullptr && left->timeline == FrameRange{10, 4} && left->sourceStart == 100,
           "left split should preserve its source start");
    expect(right != nullptr && right->timeline == FrameRange{14, 6} && right->sourceStart == 104,
           "right split should advance source time");
}

void testRegularTrimPreservesTheOppositeEdge() {
    using namespace videx::core;

    Sequence sequence;
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const auto original = sequence.overwriteClip(track, AssetId{1}, 10, 100, 20);

    const auto trimStart = sequence.trimClip(original.primaryClip, 14, 30);
    expect(trimStart.succeeded(), "start trim should succeed");
    const Clip* clip = sequence.findClip(original.primaryClip);
    expect(clip != nullptr && clip->timeline == FrameRange{14, 16} && clip->sourceStart == 104,
           "start trim should preserve the timeline end and advance source time");

    const auto trimEnd = sequence.trimClip(original.primaryClip, 14, 24);
    expect(trimEnd.succeeded(), "end trim should succeed");
    clip = sequence.findClip(original.primaryClip);
    expect(clip != nullptr && clip->timeline == FrameRange{14, 10} && clip->sourceStart == 104,
           "end trim should preserve timeline and source starts");
}

void testTrimRejectsCollisionAtomically() {
    using namespace videx::core;

    Sequence sequence;
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const auto first = sequence.overwriteClip(track, AssetId{1}, 0, 0, 10);
    sequence.overwriteClip(track, AssetId{2}, 20, 0, 10);
    const std::uint64_t revision = sequence.revision();

    const auto trim = sequence.trimClip(first.primaryClip, 0, 25);
    expect(trim.error == EditError::Collision, "overlapping trim should be rejected");
    expect(sequence.revision() == revision, "failed trim should not change revision");
    expect(sequence.findClip(first.primaryClip)->timeline == FrameRange{0, 10},
           "failed trim should preserve the original clip");
}

void testExtractClosesSyncLockedTracks() {
    using namespace videx::core;

    Sequence sequence;
    const TrackId videoTrack = sequence.addTrack(TrackKind::Video);
    const TrackId audioTrack = sequence.addTrack(TrackKind::Audio);
    const TrackId independentTrack = sequence.addTrack(TrackKind::Audio);
    sequence.setTrackSyncLocked(independentTrack, false);
    sequence.overwriteClip(videoTrack, AssetId{1}, 0, 100, 30);
    sequence.overwriteClip(audioTrack, AssetId{1}, 0, 200, 30);
    sequence.overwriteClip(independentTrack, AssetId{2}, 0, 0, 30);
    sequence.addMarker(5, "before");
    sequence.addMarker(12, "inside");
    sequence.addMarker(20, "after");
    sequence.addCaption({8, 10}, "spanning caption");

    const auto extract = sequence.extractRange({.start = 10, .duration = 5});
    expect(extract.succeeded(), "extract should succeed");
    expect(sequence.findTrack(videoTrack)->clips.size() == 2U,
           "extract should split a spanning video clip");
    expect(sequence.findTrack(audioTrack)->clips.size() == 2U,
           "extract should split a spanning audio clip");
    const auto& videoClips = sequence.findTrack(videoTrack)->clips;
    expect(videoClips[0].timeline == FrameRange{0, 10},
           "extract should preserve the left range");
    expect(videoClips[1].timeline == FrameRange{10, 15} &&
               videoClips[1].sourceStart == 115,
           "extract should close the right range and preserve source time");
    expect(sequence.findTrack(independentTrack)->clips.front().timeline == FrameRange{0, 30},
           "extract should leave tracks without sync lock unchanged");
    expect(sequence.markers().size() == 2U && sequence.markers()[0].position == 5 &&
               sequence.markers()[1].position == 15,
           "extract should remove enclosed markers and shift later markers");
    expect(sequence.captions().size() == 1U &&
               sequence.captions().front().timeline == FrameRange{8, 5},
           "extract should collapse the removed portion of spanning captions");
}

void testLockedTrackRejectsEdits() {
    using namespace videx::core;

    Sequence sequence;
    const TrackId track = sequence.addTrack(TrackKind::Video);
    expect(sequence.setTrackLocked(track, true).succeeded(), "track should lock");

    const auto edit = sequence.overwriteClip(track, AssetId{1}, 0, 0, 10);
    expect(edit.error == EditError::TrackLocked, "locked track should reject overwrite");
    expect(sequence.findTrack(track)->clips.empty(), "locked track should remain unchanged");
}

void testTypedCommandsUndoAndRedo() {
    using namespace videx::core;

    EditSession session;
    const auto addTrack = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add video track",
        .command = AddTrackCommand{TrackKind::Video},
    });
    expect(addTrack.succeeded() && addTrack.primaryTrack, "typed add-track command should succeed");

    const auto addClip = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add clip",
        .command =
            OverwriteClipCommand{
                .targetTrack = addTrack.primaryTrack,
                .assetId = AssetId{42},
                .timelineStart = 0,
                .sourceStart = 100,
                .duration = 12,
            },
    });
    expect(addClip.succeeded(), "typed overwrite command should succeed");
    const ClipId originalClip = addClip.primaryClip;
    const std::uint64_t revisionAfterClip = session.sequence().revision();

    const auto undo = session.undo();
    expect(undo.succeeded(), "undo should succeed");
    expect(session.sequence().revision() == revisionAfterClip + 1U,
           "undo should create a new monotonic revision");
    expect(session.sequence().findClip(originalClip) == nullptr, "undo should remove the clip");
    expect(session.canRedo() && session.redoLabel() == "Add clip", "redo should retain its label");

    const auto redo = session.redo();
    expect(redo.succeeded(), "redo should succeed");
    expect(session.sequence().findClip(originalClip) != nullptr,
           "redo should restore the same stable clip identity");
    expect(session.sequence().revision() == revisionAfterClip + 2U,
           "redo should create another monotonic revision");
}

void testStaleCommandDoesNotMutateHistory() {
    using namespace videx::core;

    EditSession session;
    const auto addTrack = session.apply({
        .baseRevision = 0,
        .label = "Add track",
        .command = AddTrackCommand{},
    });
    const std::uint64_t currentRevision = session.sequence().revision();

    const auto stale = session.apply({
        .baseRevision = 0,
        .label = "Stale clip",
        .command =
            OverwriteClipCommand{
                .targetTrack = addTrack.primaryTrack,
                .assetId = AssetId{1},
                .timelineStart = 0,
                .sourceStart = 0,
                .duration = 10,
            },
    });
    expect(stale.error == EditError::StaleRevision, "stale command should be rejected");
    expect(session.sequence().revision() == currentRevision,
           "stale command should not change revision");
    expect(session.sequence().findTrack(addTrack.primaryTrack)->clips.empty(),
           "stale command should not change content");
}

void testTransactionIsAtomicAndCreatesOneUndoStep() {
    using namespace videx::core;

    EditSession session;
    const auto tracks = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add tracks",
        .commands = {AddTrackCommand{TrackKind::Video}, AddTrackCommand{TrackKind::Audio}},
    });
    expect(tracks.succeeded(), "track transaction should succeed");
    const TrackId videoTrack = session.sequence().tracks()[0].id;
    const TrackId audioTrack = session.sequence().tracks()[1].id;
    session.clearHistory();

    const auto import = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Import linked media",
        .commands = {
            OverwriteClipCommand{videoTrack, AssetId{4}, 0, 0, 20},
            OverwriteClipCommand{audioTrack, AssetId{4}, 0, 0, 20},
        },
    });
    expect(import.succeeded(), "multi-track import transaction should succeed");
    expect(session.sequence().findTrack(videoTrack)->clips.size() == 1U &&
               session.sequence().findTrack(audioTrack)->clips.size() == 1U,
           "transaction should edit both tracks");
    expect(session.undo().succeeded(), "transaction should be undoable");
    expect(session.sequence().findTrack(videoTrack)->clips.empty() &&
               session.sequence().findTrack(audioTrack)->clips.empty(),
           "one undo should revert the whole transaction");

    session.clearHistory();
    const std::uint64_t revision = session.sequence().revision();
    const auto failed = session.apply({
        .baseRevision = revision,
        .label = "Invalid transaction",
        .commands = {
            OverwriteClipCommand{videoTrack, AssetId{5}, 0, 0, 10},
            OverwriteClipCommand{TrackId{999}, AssetId{5}, 0, 0, 10},
        },
    });
    expect(failed.error == EditError::TrackNotFound, "failed transaction should report its error");
    expect(session.sequence().revision() == revision &&
               session.sequence().findTrack(videoTrack)->clips.empty(),
           "failed transaction should not commit an earlier command");
}

void testSequenceLoadsStableIdsAndRejectsOverlap() {
    using namespace videx::core;

    std::vector<Track> tracks{
        Track{
            .id = TrackId{100},
            .kind = TrackKind::Video,
            .clips = {
                Clip{.id = ClipId{500},
                     .assetId = AssetId{9},
                     .timeline = FrameRange{10, 5},
                     .sourceStart = 20},
            },
        },
    };
    Sequence loaded = Sequence::fromTracks({30, 1}, tracks);
    expect(loaded.findClip(ClipId{500}) != nullptr, "loaded clip ID should be preserved");
    const TrackId newTrack = loaded.addTrack(TrackKind::Audio);
    expect(newTrack.value > 500, "new IDs must not reuse loaded entity IDs");

    tracks[0].clips.push_back({
        .id = ClipId{501},
        .assetId = AssetId{10},
        .timeline = FrameRange{12, 5},
        .sourceStart = 0,
    });
    bool rejectedOverlap = false;
    try {
        static_cast<void>(Sequence::fromTracks({30, 1}, tracks));
    } catch (const std::invalid_argument&) {
        rejectedOverlap = true;
    }
    expect(rejectedOverlap, "loaded overlapping clips should be rejected");
}

void testLinkIdentitySurvivesSplit() {
    using namespace videx::core;
    Sequence sequence({24, 1});
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const LinkId link{44};
    const auto inserted = sequence.overwriteClip(track, AssetId{8}, 0, 10, 20, link);
    expect(inserted.succeeded(), "linked clip should be inserted");
    const auto split = sequence.splitClip(inserted.primaryClip, 8);
    expect(split.succeeded(), "linked clip should split");
    const auto& clips = sequence.findTrack(track)->clips;
    expect(clips.size() == 2U && clips[0].linkId == link && clips[1].linkId == link,
           "split children should preserve link identity");
}

void testLiftRangeLeavesGapAndIsUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add track",
        .command = AddTrackCommand{TrackKind::Video},
    }).primaryTrack;
    session.apply({.baseRevision = session.sequence().revision(),
                   .label = "Add clip",
                   .command = OverwriteClipCommand{track, AssetId{4}, 0, 100, 20}});
    session.apply({.baseRevision = session.sequence().revision(),
                   .label = "Add caption",
                   .command = AddCaptionCommand{{2, 13}, "spanning caption"}});
    const auto lifted = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Lift range",
        .command = LiftRangeCommand{{5, 6}},
    });
    const auto& clips = session.sequence().findTrack(track)->clips;
    expect(lifted.succeeded() && clips.size() == 2U &&
               clips[0].timeline == FrameRange{0, 5} &&
               clips[1].timeline == FrameRange{11, 9} && clips[1].sourceStart == 111,
           "range lift should preserve both handles and leave empty sequence time");
    expect(session.sequence().captions().size() == 2U &&
               session.sequence().captions()[0].timeline == FrameRange{2, 3} &&
               session.sequence().captions()[1].timeline == FrameRange{11, 4},
           "range lift should split captions around the lifted gap");
    expect(session.undo().succeeded() &&
               session.sequence().findTrack(track)->clips.size() == 1U &&
               session.sequence().findTrack(track)->clips.front().timeline == FrameRange{0, 20} &&
               session.sequence().captions().size() == 1U,
           "range lift should undo as one edit");
}

void testClipLinksAreTransactionalAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId videoTrack = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add video track",
        .command = AddTrackCommand{TrackKind::Video},
    }).primaryTrack;
    const TrackId audioTrack = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add audio track",
        .command = AddTrackCommand{TrackKind::Audio},
    }).primaryTrack;
    const ClipId video = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add video",
        .command = OverwriteClipCommand{videoTrack, AssetId{10}, 0, 0, 20},
    }).primaryClip;
    const ClipId audio = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add audio",
        .command = OverwriteClipCommand{audioTrack, AssetId{10}, 0, 0, 20},
    }).primaryClip;
    const LinkId link{73};
    const auto linked = session.apply(TransactionEnvelope{
        .baseRevision = session.sequence().revision(),
        .label = "Link clips",
        .commands = {SetClipLinkCommand{video, link}, SetClipLinkCommand{audio, link}},
    });
    expect(linked.succeeded() && session.sequence().findClip(video)->linkId == link &&
               session.sequence().findClip(audio)->linkId == link,
           "link transaction should assign one identity to all selected clips");
    expect(session.undo().succeeded() && !session.sequence().findClip(video)->linkId &&
               !session.sequence().findClip(audio)->linkId,
           "link transaction should undo as one edit");
    expect(session.redo().succeeded() && session.sequence().findClip(video)->linkId == link &&
               session.sequence().findClip(audio)->linkId == link,
           "link transaction should redo as one edit");
}

void testClipPropertiesAreValidatedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add track",
        .command = AddTrackCommand{TrackKind::Video},
    }).primaryTrack;
    const ClipId clip = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add clip",
        .command = OverwriteClipCommand{track, AssetId{3}, 0, 0, 20},
    }).primaryClip;
    const auto changed = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Properties",
        .command = SetClipPropertiesCommand{clip, 0.5, -6.0, 1.5},
    });
    expect(changed.succeeded(), "valid clip properties should apply");
    expect(session.sequence().findClip(clip)->opacity == 0.5 &&
               session.sequence().findClip(clip)->audioGainDb == -6.0 &&
               session.sequence().findClip(clip)->playbackRate == 1.5,
           "clip properties should be stored");
    expect(session.undo().succeeded() && session.sequence().findClip(clip)->opacity == 1.0,
           "clip property change should undo as one edit");
    const auto invalid = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Invalid properties",
        .command = SetClipPropertiesCommand{clip, 2.0, 0.0, 1.0},
    });
    expect(invalid.error == EditError::InvalidRange,
           "invalid clip properties should be rejected");
}

void testSlipAndRollEditsAreAtomicAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Add track",
        .command = AddTrackCommand{TrackKind::Video},
    }).primaryTrack;
    const ClipId left = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Left clip",
        .command = OverwriteClipCommand{track, AssetId{1}, 0, 20, 10},
    }).primaryClip;
    const ClipId right = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Right clip",
        .command = OverwriteClipCommand{track, AssetId{2}, 10, 40, 10},
    }).primaryClip;

    const auto slipped = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Slip",
        .command = SlipClipCommand{left, 3},
    });
    expect(slipped.succeeded() && session.sequence().findClip(left)->sourceStart == 23 &&
               session.sequence().findClip(left)->timeline == FrameRange{0, 10},
           "slip should change source time without moving the clip");

    const auto rolled = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Roll",
        .command = RollEditCommand{left, right, 12},
    });
    expect(rolled.succeeded() &&
               session.sequence().findClip(left)->timeline == FrameRange{0, 12} &&
               session.sequence().findClip(right)->timeline == FrameRange{12, 8} &&
               session.sequence().findClip(right)->sourceStart == 42,
           "roll should move a shared cut while preserving the outer edges");
    expect(session.undo().succeeded() &&
               session.sequence().findClip(left)->timeline == FrameRange{0, 10} &&
               session.sequence().findClip(right)->sourceStart == 40,
           "roll should undo atomically");

    const auto invalid = session.apply({
        .baseRevision = session.sequence().revision(),
        .label = "Invalid slip",
        .command = SlipClipCommand{left, -100},
    });
    expect(invalid.error == EditError::InvalidRange &&
               session.sequence().findClip(left)->sourceStart == 23,
           "invalid slip should not mutate the sequence");
}

void testClipFadesAreValidatedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
                                         AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 20}}).primaryClip;
    const auto applied = session.apply({session.sequence().revision(), "Fades",
                                        SetClipFadesCommand{clip, 5, 7}});
    expect(applied.succeeded() && session.sequence().findClip(clip)->fadeInFrames == 5 &&
               session.sequence().findClip(clip)->fadeOutFrames == 7,
           "valid fades should be stored");
    expect(session.undo().succeeded() && session.sequence().findClip(clip)->fadeInFrames == 0,
           "fade edit should undo atomically");
    const auto invalid = session.apply({session.sequence().revision(), "Bad fades",
                                        SetClipFadesCommand{clip, 21, 0}});
    expect(invalid.error == EditError::InvalidRange,
           "fade longer than the clip should be rejected");
}

void testMoveBetweenCompatibleTracksIsAtomic() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId first = session.apply({session.sequence().revision(), "V1",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const TrackId second = session.apply({session.sequence().revision(), "V2",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const TrackId audio = session.apply({session.sequence().revision(), "A1",
        AddTrackCommand{TrackKind::Audio}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{first, AssetId{1}, 0, 10, 12}}).primaryClip;
    const auto moved = session.apply({session.sequence().revision(), "Move track",
        MoveClipToTrackCommand{clip, second, 20}});
    expect(moved.succeeded() && session.sequence().findTrack(first)->clips.empty() &&
               session.sequence().findClip(clip)->timeline.start == 20 &&
               session.sequence().findTrack(second)->clips.size() == 1,
           "clip should move between tracks of the same kind");
    const auto rejected = session.apply({session.sequence().revision(), "Wrong kind",
        MoveClipToTrackCommand{clip, audio, 0}});
    expect(rejected.error == EditError::InvalidRange &&
               session.sequence().findClip(clip)->timeline.start == 20,
           "cross-kind move should fail atomically");
    expect(session.undo().succeeded() && session.sequence().findTrack(first)->clips.size() == 1,
           "cross-track move should undo");
}

void testPastePreservesPropertiesAndRemapsLinks() {
    using namespace videx::core;
    Sequence sequence({24, 1});
    const TrackId video = sequence.addTrack(TrackKind::Video);
    const TrackId audio = sequence.addTrack(TrackKind::Audio);
    Clip videoTemplate{.assetId = AssetId{8},
                       .timeline = {0, 12},
                       .sourceStart = 30,
                       .linkId = LinkId{77},
                       .opacity = 0.6,
                       .playbackRate = 1.25,
                       .fadeInFrames = 3};
    Clip audioTemplate = videoTemplate;
    audioTemplate.audioGainDb = -4.0;
    const auto pasted = sequence.pasteClips({
        {.targetTrack = video, .kind = TrackKind::Video, .clip = videoTemplate,
         .timelineStart = 20},
        {.targetTrack = audio, .kind = TrackKind::Audio, .clip = audioTemplate,
         .timelineStart = 20},
    });
    expect(pasted.succeeded() && pasted.createdClips.size() == 2,
           "linked A/V paste should create both clips");
    const Clip* pastedVideo = sequence.findClip(pasted.createdClips[0]);
    const Clip* pastedAudio = sequence.findClip(pasted.createdClips[1]);
    expect(pastedVideo != nullptr && pastedAudio != nullptr && pastedVideo->linkId &&
               pastedVideo->linkId == pastedAudio->linkId && pastedVideo->linkId != LinkId{77},
           "paste should assign a fresh shared link ID");
    expect(pastedVideo->opacity == 0.6 && pastedVideo->playbackRate == 1.25 &&
               pastedVideo->fadeInFrames == 3 && pastedAudio->audioGainDb == -4.0,
           "paste should preserve render properties");
}

void testTransformIsValidatedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 12}}).primaryClip;
    const auto changed = session.apply({session.sequence().revision(), "Transform",
        SetClipTransformCommand{clip, 100.0, -20.0, 1.5, 0.75, 30.0, 0.4, 0.6}});
    const Clip* transformed = session.sequence().findClip(clip);
    expect(changed.succeeded() && transformed->positionX == 100.0 &&
               transformed->scaleX == 1.5 && transformed->rotationDegrees == 30.0 &&
               transformed->anchorY == 0.6,
           "valid transform should apply");
    expect(session.undo().succeeded() && session.sequence().findClip(clip)->scaleX == 1.0,
           "transform should undo atomically");
    const auto invalid = session.apply({session.sequence().revision(), "Bad transform",
        SetClipTransformCommand{clip, 0.0, 0.0, 0.0, 1.0, 0.0, 0.5, 0.5}});
    expect(invalid.error == EditError::InvalidRange,
           "zero scale should be rejected");
}

void testMotionKeyframesAreValidatedRetimedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 20}}).primaryClip;
    const auto first = session.apply({session.sequence().revision(), "Motion 1",
        SetMotionKeyframeCommand{clip, 2, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.5, 0.5,
                                 KeyframeInterpolation::Ease}});
    const auto second = session.apply({session.sequence().revision(), "Motion 2",
        SetMotionKeyframeCommand{clip, 12, 0.4, 240.0, -80.0, 1.5, 0.75, 45.0, 0.4, 0.6,
                                 KeyframeInterpolation::Hold}});
    expect(first.succeeded() && second.succeeded() &&
               session.sequence().findClip(clip)->motionKeyframes.size() == 2U,
           "motion keyframes should be sorted and stored");
    expect(session.apply({session.sequence().revision(), "Invalid motion",
        SetMotionKeyframeCommand{clip, 19, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.5, 0.5,
                                 KeyframeInterpolation::Linear}}).error == EditError::InvalidRange,
           "invalid motion scale should be rejected");
    expect(session.apply({session.sequence().revision(), "Trim",
        TrimClipCommand{clip, 5, 20}}).succeeded(), "motion fixture trim should succeed");
    const Clip* trimmed = session.sequence().findClip(clip);
    expect(trimmed->motionKeyframes.size() == 1U &&
               trimmed->motionKeyframes.front().frameOffset == 7,
           "trim should discard and retime motion keyframes");
    expect(session.undo().succeeded() &&
               session.sequence().findClip(clip)->motionKeyframes.size() == 2U,
           "motion retiming should undo atomically");
}

void testGainKeyframesAreValidatedRetimedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Audio}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 20}}).primaryClip;
    expect(session.apply({session.sequence().revision(), "Gain 1",
        SetGainKeyframeCommand{clip, 2, -60.0, KeyframeInterpolation::Ease}}).succeeded() &&
           session.apply({session.sequence().revision(), "Gain 2",
        SetGainKeyframeCommand{clip, 12, -3.0, KeyframeInterpolation::Hold}}).succeeded() &&
           session.sequence().findClip(clip)->gainKeyframes.size() == 2U,
           "gain keyframes should be sorted and stored");
    expect(session.apply({session.sequence().revision(), "Invalid gain",
        SetGainKeyframeCommand{clip, 3, 25.0, KeyframeInterpolation::Linear}}).error ==
           EditError::InvalidRange, "invalid gain should be rejected");
    expect(session.apply({session.sequence().revision(), "Trim",
        TrimClipCommand{clip, 5, 20}}).succeeded(), "gain fixture trim should succeed");
    const Clip* trimmed = session.sequence().findClip(clip);
    expect(trimmed->gainKeyframes.size() == 1U &&
           trimmed->gainKeyframes.front().frameOffset == 7,
           "trim should discard and retime gain keyframes");
    expect(session.undo().succeeded() &&
           session.sequence().findClip(clip)->gainKeyframes.size() == 2U,
           "gain retiming should undo atomically");
    expect(session.apply({session.sequence().revision(), "Remove gain",
        RemoveGainKeyframeCommand{clip, 2}}).succeeded() &&
           session.sequence().findClip(clip)->gainKeyframes.size() == 1U,
           "gain keyframes should be removable");
    expect(session.undo().succeeded() &&
           session.sequence().findClip(clip)->gainKeyframes.size() == 2U,
           "gain keyframe removal should undo");
    const auto split = session.apply({session.sequence().revision(), "Split",
        SplitClipCommand{clip, 10}});
    expect(split.succeeded() && split.createdClips.size() == 2U,
           "gain fixture split should succeed");
    const Clip* splitLeft = nullptr;
    const Clip* splitRight = nullptr;
    for (const ClipId id : split.createdClips) {
        const Clip* part = session.sequence().findClip(id);
        if (part != nullptr && part->timeline.start == 0) splitLeft = part;
        if (part != nullptr && part->timeline.start == 10) splitRight = part;
    }
    expect(splitLeft != nullptr && splitRight != nullptr &&
           splitLeft->gainKeyframes.size() == 1U &&
           splitLeft->gainKeyframes.front().frameOffset == 2 &&
           splitRight->gainKeyframes.size() == 1U &&
           splitRight->gainKeyframes.front().frameOffset == 2,
           "split should preserve and retime gain automation on both halves");
}

void testRippleTrimAndSlidePreserveCoverage() {
    using namespace videx::core;
    Sequence sequence({24, 1});
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const ClipId left = sequence.overwriteClip(track, AssetId{1}, 0, 10, 10).primaryClip;
    const ClipId middle = sequence.overwriteClip(track, AssetId{2}, 10, 20, 10).primaryClip;
    const ClipId right = sequence.overwriteClip(track, AssetId{3}, 20, 30, 10).primaryClip;
    const auto slide = sequence.slideClip(middle, 2);
    expect(slide.succeeded() && sequence.findClip(left)->timeline == FrameRange{0, 12} &&
               sequence.findClip(middle)->timeline == FrameRange{12, 10} &&
               sequence.findClip(right)->timeline == FrameRange{22, 8} &&
               sequence.findClip(right)->sourceStart == 32,
           "slide should preserve selected duration and outer boundaries");

    Sequence ripple({24, 1});
    const TrackId video = ripple.addTrack(TrackKind::Video);
    const TrackId audio = ripple.addTrack(TrackKind::Audio);
    const ClipId first = ripple.overwriteClip(video, AssetId{1}, 0, 0, 10).primaryClip;
    const ClipId second = ripple.overwriteClip(video, AssetId{2}, 10, 0, 10).primaryClip;
    const ClipId audioAfter = ripple.overwriteClip(audio, AssetId{3}, 10, 0, 10).primaryClip;
    const auto trimmed = ripple.rippleTrimEnd(first, 7);
    expect(trimmed.succeeded() && ripple.findClip(first)->timeline == FrameRange{0, 7} &&
               ripple.findClip(second)->timeline.start == 7 &&
               ripple.findClip(audioAfter)->timeline.start == 7,
           "ripple trim should shift downstream sync-locked tracks");
}

void testEffectsAreValidatedKeyframedRetimedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 20}}).primaryClip;
    const auto added = session.apply({session.sequence().revision(), "Effect",
        AddEffectCommand{clip, EffectType::Brightness}});
    const EffectId effect = added.primaryEffect;
    expect(added.succeeded() && effect, "effect should be added with a stable ID");
    expect(session.apply({session.sequence().revision(), "Amount",
               SetEffectCommand{clip, effect, true, 0.25}}).succeeded(),
           "valid effect amount should apply");
    expect(session.apply({session.sequence().revision(), "Keyframe",
               SetEffectKeyframeCommand{clip, effect, 4, 0.1,
                                        KeyframeInterpolation::Linear}}).succeeded() &&
               session.apply({session.sequence().revision(), "Keyframe",
                   SetEffectKeyframeCommand{clip, effect, 12, 0.6,
                                            KeyframeInterpolation::Ease}}).succeeded(),
           "effect keyframes should be addable");
    const auto invalid = session.apply({session.sequence().revision(), "Invalid amount",
        SetEffectCommand{clip, effect, true, 2.0}});
    expect(invalid.error == EditError::InvalidRange,
           "out-of-range effect amount should be rejected");

    const auto trimmed = session.apply({session.sequence().revision(), "Trim",
        TrimClipCommand{clip, 5, 18}});
    const Clip* edited = session.sequence().findClip(clip);
    expect(trimmed.succeeded() && edited != nullptr && edited->effects.size() == 1 &&
               edited->effects.front().keyframes.size() == 1 &&
               edited->effects.front().keyframes.front().frameOffset == 7,
           "trim should discard and retime effect keyframes");
    expect(session.undo().succeeded() &&
               session.sequence().findClip(clip)->effects.front().keyframes.size() == 2,
           "keyframe retiming should undo atomically");
}

void testTracksCanBeReorderedDeletedAndUndone() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId first = session.apply({session.sequence().revision(), "V1",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const TrackId second = session.apply({session.sequence().revision(), "V2",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    expect(session.apply({session.sequence().revision(), "Move",
               MoveTrackCommand{second, -1}}).succeeded() &&
               session.sequence().tracks().front().id == second,
           "track reordering should preserve identity");
    expect(session.undoLabels().back() == "Move",
           "history should expose the latest command label");
    expect(session.apply({session.sequence().revision(), "Delete",
               RemoveTrackCommand{first}}).succeeded() &&
               session.sequence().findTrack(first) == nullptr,
           "track deletion should remove its contents");
    expect(session.undo().succeeded() && session.sequence().findTrack(first) != nullptr,
           "track deletion should be undoable");
}

void testFromTracksNormalizesKindGrouping() {
    using namespace videx::core;
    // Legacy projects could interleave kinds; loading must regroup video
    // before audio while preserving relative order, so track reordering works.
    Sequence sequence = Sequence::fromTracks(
        {24, 1},
        {Track{.id = TrackId{1}, .kind = TrackKind::Video},
         Track{.id = TrackId{2}, .kind = TrackKind::Audio},
         Track{.id = TrackId{3}, .kind = TrackKind::Video}},
        {}, {});
    expect(sequence.tracks().size() == 3U &&
               sequence.tracks()[0].id == TrackId{1} &&
               sequence.tracks()[1].id == TrackId{3} &&
               sequence.tracks()[2].id == TrackId{2},
           "loading must group video tracks first and keep relative order");
    expect(sequence.moveTrack(TrackId{3}, -1).succeeded() &&
               sequence.tracks()[0].id == TrackId{3},
           "video tracks loaded from a legacy file must be reorderable");
}

void testTransactionBindsPlacementToNewTrack() {
    using namespace videx::core;
    EditSession session({24, 1});
    const auto combined = session.apply(TransactionEnvelope{
        session.sequence().revision(),
        "Drop on new track",
        {AddTrackCommand{TrackKind::Video},
         OverwriteClipCommand{TrackId{}, AssetId{7}, 0, 0, 12}}});
    expect(combined.succeeded(),
           "a null target track must bind to the track added in the transaction");
    expect(session.sequence().tracks().size() == 1U &&
               session.sequence().tracks().front().clips.size() == 1U,
           "the new track must hold the placed clip");
    expect(session.undo().succeeded() && session.sequence().tracks().empty(),
           "one undo must remove both the track and the clip");
    expect(session.redo().succeeded() && session.sequence().tracks().size() == 1U &&
               session.sequence().tracks().front().clips.size() == 1U,
           "redo must restore the track together with its clip");
    const auto failing = session.apply(TransactionEnvelope{
        session.sequence().revision(),
        "Invalid drop",
        {AddTrackCommand{TrackKind::Video},
         OverwriteClipCommand{TrackId{}, AssetId{8}, 0, 0, 0}}});
    expect(!failing.succeeded(), "an invalid placement must fail the whole transaction");
    expect(session.sequence().tracks().size() == 1U,
           "a failed transaction must not leave an empty track behind");
}

void testTracksGroupByKindAndSoloToggles() {
    using namespace videx::core;
    Sequence sequence({24, 1});
    const TrackId v1 = sequence.addTrack(TrackKind::Video);
    const TrackId a1 = sequence.addTrack(TrackKind::Audio);
    const TrackId v2 = sequence.addTrack(TrackKind::Video);
    expect(sequence.tracks().size() == 3U && sequence.tracks()[0].id == v1 &&
               sequence.tracks()[1].id == v2 && sequence.tracks()[2].id == a1,
           "new video tracks must insert before the audio group");
    expect(!sequence.moveTrack(v2, 1).succeeded(),
           "a video track must not move into the audio group");
    expect(sequence.moveTrack(v2, -1).succeeded() && sequence.tracks()[0].id == v2,
           "video tracks may reorder within their own group");
    expect(sequence.setTrackSolo(a1, true).succeeded() && sequence.findTrack(a1)->solo,
           "audio solo should toggle");
    expect(!sequence.setTrackSolo(TrackId{999}, true).succeeded(),
           "solo on a missing track must fail");
}

void testTransitionsRequireAdjacentCutsAndAreUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId video = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    session.apply({session.sequence().revision(), "First",
        OverwriteClipCommand{video, AssetId{1}, 0, 0, 12}});
    const ClipId second = session.apply({session.sequence().revision(), "Second",
        OverwriteClipCommand{video, AssetId{2}, 12, 0, 12}}).primaryClip;
    const auto set = session.apply({session.sequence().revision(), "Dissolve",
        SetClipTransitionsCommand{second, 6, 0}});
    expect(set.succeeded() &&
               session.sequence().findClip(second)->videoTransitionInFrames == 6,
           "adjacent clips should accept a video dissolve");
    expect(session.undo().succeeded() &&
               session.sequence().findClip(second)->videoTransitionInFrames == 0,
           "transition changes should undo");
    const ClipId isolated = session.apply({session.sequence().revision(), "Isolated",
        OverwriteClipCommand{video, AssetId{3}, 40, 0, 10}}).primaryClip;
    expect(session.apply({session.sequence().revision(), "Invalid",
               SetClipTransitionsCommand{isolated, 4, 0}}).error == EditError::InvalidRange,
           "a transition without a previous adjacent clip should be rejected");
}

void testCropIsValidatedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 20}}).primaryClip;
    expect(session.apply({session.sequence().revision(), "Crop",
               SetClipCropCommand{clip, 0.1, 0.2, 0.15, 0.05}}).succeeded() &&
               session.sequence().findClip(clip)->cropRight == 0.2,
           "valid crop should apply");
    expect(session.undo().succeeded() && session.sequence().findClip(clip)->cropLeft == 0.0,
           "crop should undo atomically");
    expect(session.apply({session.sequence().revision(), "Invalid crop",
               SetClipCropCommand{clip, 0.6, 0.5, 0.0, 0.0}}).error ==
               EditError::InvalidRange,
           "crop removing the whole image should be rejected");
}

void testSpeedKeyframesAreValidatedRetimedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 24}}).primaryClip;
    expect(session.apply({session.sequence().revision(), "Speed 1",
               SetSpeedKeyframeCommand{clip, 4, 0.5,
                                       KeyframeInterpolation::Hold}}).succeeded() &&
               session.apply({session.sequence().revision(), "Speed 2",
                   SetSpeedKeyframeCommand{clip, 16, 2.0,
                                           KeyframeInterpolation::Ease}}).succeeded(),
           "speed keyframes should be addable");
    expect(session.apply({session.sequence().revision(), "Trim",
               TrimClipCommand{clip, 8, 22}}).succeeded() &&
               session.sequence().findClip(clip)->speedKeyframes.size() == 1 &&
               session.sequence().findClip(clip)->speedKeyframes.front().frameOffset == 8,
           "trim should discard and retime speed keyframes");
    expect(session.undo().succeeded() &&
               session.sequence().findClip(clip)->speedKeyframes.size() == 2,
           "speed-keyframe retiming should undo");
    expect(session.apply({session.sequence().revision(), "Invalid speed",
               SetSpeedKeyframeCommand{clip, 3, 8.0,
                                       KeyframeInterpolation::Linear}}).error ==
               EditError::InvalidRange,
           "invalid speed keyframes should be rejected");
}

void testMasksAreValidatedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 20}}).primaryClip;
    expect(session.apply({session.sequence().revision(), "Mask",
               SetClipMaskCommand{clip, MaskShape::Ellipse, 0.4, 0.6, 0.8, 0.5,
                                  0.1, true}}).succeeded() &&
               session.sequence().findClip(clip)->maskInverted,
           "valid masks should apply");
    expect(session.undo().succeeded() &&
               session.sequence().findClip(clip)->maskShape == MaskShape::None,
           "mask changes should undo");
    expect(session.apply({session.sequence().revision(), "Invalid mask",
               SetClipMaskCommand{clip, MaskShape::Rectangle, 0.5, 0.5, 0.0, 1.0,
                                  0.0, false}}).error == EditError::InvalidRange,
           "empty masks should be rejected");
}

void testMarkersCarryColorCommentAndAreEditable() {
    using namespace videx::core;
    EditSession session({24, 1});
    expect(session.apply({session.sequence().revision(), "Marker A",
                          AddMarkerCommand{10, "First"}}).succeeded() &&
               session.apply({session.sequence().revision(), "Marker B",
                              AddMarkerCommand{50, "Second"}}).succeeded(),
           "markers should be addable");
    const Marker& first = session.sequence().markers().front();
    expect(first.comment.empty() && first.color == 0xFF2E9E4FU,
           "new markers should default to an empty comment and the green flag");
    const MarkerId firstId = first.id;
    expect(session.apply({session.sequence().revision(), "Edit marker",
               SetMarkerCommand{firstId, 80, "Renamed", "Check this cut",
                                0xFFD34C4CU}}).succeeded(),
           "marker edits should apply");
    expect(session.sequence().markers().front().name == "Second" &&
               session.sequence().markers().back().name == "Renamed" &&
               session.sequence().markers().back().comment == "Check this cut" &&
               session.sequence().markers().back().color == 0xFFD34C4CU,
           "moving a marker past its neighbour should keep markers sorted");
    expect(session.undo().succeeded() &&
               session.sequence().markers().front().name == "First" &&
               session.sequence().markers().front().position == 10 &&
               session.sequence().markers().front().comment.empty(),
           "marker edits should undo atomically");
    expect(session.apply({session.sequence().revision(), "Bad marker",
               SetMarkerCommand{firstId, -1, "Bad", "", 0U}}).error ==
               EditError::InvalidRange,
           "negative marker positions should be rejected");
    expect(session.apply({session.sequence().revision(), "Bad marker",
               SetMarkerCommand{firstId, 5, "", "", 0U}}).error ==
               EditError::InvalidRange,
           "empty marker names should be rejected");
    expect(session.apply({session.sequence().revision(), "Missing marker",
               SetMarkerCommand{MarkerId{9999}, 5, "Ghost", "", 0U}}).error ==
               EditError::ClipNotFound,
           "editing an unknown marker should fail");
}

void testEasingPresetsAreAcceptedAndOutOfRangeRejected() {
    using namespace videx::core;

    static_assert(interpolationProgress(KeyframeInterpolation::EaseIn, 0.5) == 0.125);
    static_assert(interpolationProgress(KeyframeInterpolation::EaseOut, 0.5) == 0.875);
    static_assert(interpolationProgress(KeyframeInterpolation::EaseInOut, 0.25) == 0.0625);
    static_assert(interpolationProgress(KeyframeInterpolation::EaseInOut, 0.5) == 0.5);
    static_assert(interpolationProgress(KeyframeInterpolation::EaseInOut, 0.75) == 0.9375);
    static_assert(interpolationProgress(KeyframeInterpolation::EaseIn, 1.0) == 1.0);
    static_assert(interpolationProgress(KeyframeInterpolation::EaseOut, 1.0) == 1.0);
    static_assert(interpolationProgress(KeyframeInterpolation::EaseInOut, 1.0) == 1.0);
    static_assert(interpolationIntegral(KeyframeInterpolation::EaseIn, 1.0) == 0.25);
    static_assert(interpolationIntegral(KeyframeInterpolation::EaseOut, 1.0) == 0.75);
    static_assert(interpolationIntegral(KeyframeInterpolation::EaseInOut, 1.0) == 0.5);
    static_assert(interpolationIntegral(KeyframeInterpolation::EaseInOut, 0.5) == 0.0625);

    EditSession session({24, 1});
    const TrackId track = session.apply({session.sequence().revision(), "Track",
        AddTrackCommand{TrackKind::Video}}).primaryTrack;
    const ClipId clip = session.apply({session.sequence().revision(), "Clip",
        OverwriteClipCommand{track, AssetId{1}, 0, 0, 20}}).primaryClip;
    expect(session.apply({session.sequence().revision(), "EaseIn motion",
        SetMotionKeyframeCommand{clip, 2, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.5, 0.5,
                                 KeyframeInterpolation::EaseIn}}).succeeded(),
           "EaseIn motion keyframes should be accepted");
    expect(session.apply({session.sequence().revision(), "EaseInOut motion",
        SetMotionKeyframeCommand{clip, 12, 0.5, 0.0, 0.0, 1.0, 1.0, 0.0, 0.5, 0.5,
                                 KeyframeInterpolation::EaseInOut}}).succeeded(),
           "EaseInOut motion keyframes should be accepted");
    expect(session.apply({session.sequence().revision(), "EaseOut gain",
        SetGainKeyframeCommand{clip, 4, -6.0,
                               KeyframeInterpolation::EaseOut}}).succeeded(),
           "EaseOut gain keyframes should be accepted");
    expect(session.apply({session.sequence().revision(), "EaseInOut speed",
        SetSpeedKeyframeCommand{clip, 6, 1.5,
                                KeyframeInterpolation::EaseInOut}}).succeeded(),
           "EaseInOut speed keyframes should be accepted");
    expect(session.apply({session.sequence().revision(), "Out-of-range motion",
        SetMotionKeyframeCommand{clip, 15, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.5, 0.5,
                                 static_cast<KeyframeInterpolation>(6)}}).error ==
               EditError::InvalidRange,
           "interpolation values past EaseInOut should be rejected");
    expect(session.apply({session.sequence().revision(), "Out-of-range gain",
        SetGainKeyframeCommand{clip, 16, 0.0,
                               static_cast<KeyframeInterpolation>(6)}}).error ==
               EditError::InvalidRange,
           "gain interpolation values past EaseInOut should be rejected");
}

void testCaptionStylesAreValidatedAndUndoable() {
    using namespace videx::core;
    EditSession session({24, 1});
    expect(session.apply({session.sequence().revision(), "Caption",
                          AddCaptionCommand{{0, 48}, "Title"}}).succeeded(),
           "captions should be addable");
    const CaptionId id = session.sequence().captions().front().id;
    expect(session.apply({session.sequence().revision(), "Style caption",
               SetCaptionCommand{id, {12, 60}, "Styled", 0.25, 0.75, 64.0,
                                 0xFFCC00FFU, 0x10101080U, true, true}}).succeeded() &&
               session.sequence().captions().front().bold,
           "caption styles should apply");
    expect(session.undo().succeeded() && session.sequence().captions().front().text == "Title",
           "caption style changes should undo");
    expect(session.apply({session.sequence().revision(), "Invalid caption",
               SetCaptionCommand{id, {0, 10}, "Bad", 2.0, 0.5, 42.0,
                                 0xFFFFFFFFU, 0U, false, false}}).error ==
               EditError::InvalidRange,
           "invalid caption positions should be rejected");
}

} // namespace

void testRippleTrimShiftsUnsyncedTargetTrack() {
    using namespace videx::core;

    Sequence sequence({24, 1});
    const TrackId track = sequence.addTrack(TrackKind::Video);
    expect(sequence.setTrackSyncLocked(track, false).succeeded(),
           "sync lock should be clearable");
    const auto first = sequence.overwriteClip(track, AssetId{1}, 0, 0, 100);
    const auto second = sequence.overwriteClip(track, AssetId{1}, 100, 100, 100);
    expect(first.succeeded() && second.succeeded(), "fixture clips should be created");

    const auto ripple = sequence.rippleTrimEnd(first.primaryClip, 150);
    expect(ripple.succeeded(), "ripple trim on an unsynced track should succeed");
    const auto& clips = sequence.findTrack(track)->clips;
    expect(clips.size() == 2U, "both clips should remain after ripple trim");
    expect(clips[0].timeline == FrameRange{0, 150}, "trimmed clip should extend");
    expect(clips[1].timeline == FrameRange{150, 100},
           "downstream clip on the edited track should shift even without sync lock");
}

void testSplitClampsFadesToPieceDurations() {
    using namespace videx::core;

    Sequence sequence({24, 1});
    const TrackId track = sequence.addTrack(TrackKind::Video);
    const auto created = sequence.overwriteClip(track, AssetId{1}, 0, 0, 100);
    expect(sequence.setClipFades(created.primaryClip, 80, 80).succeeded(),
           "long fades should be accepted before the split");

    const auto split = sequence.splitClip(created.primaryClip, 30);
    expect(split.succeeded(), "split should succeed");
    const auto& clips = sequence.findTrack(track)->clips;
    expect(clips[0].fadeInFrames <= clips[0].timeline.duration &&
               clips[0].fadeOutFrames <= clips[0].timeline.duration,
           "left piece fades must fit its duration");
    expect(clips[1].fadeInFrames <= clips[1].timeline.duration &&
               clips[1].fadeOutFrames <= clips[1].timeline.duration,
           "right piece fades must fit its duration");

    bool reloads = true;
    try {
        static_cast<void>(Sequence::fromTracks(sequence.frameRate(), sequence.tracks(),
                                               sequence.markers(), sequence.captions()));
    } catch (const std::exception&) {
        reloads = false;
    }
    expect(reloads, "split result should round-trip through fromTracks");
}

void testInsertShiftsMarkersAndCaptions() {
    using namespace videx::core;

    Sequence sequence({24, 1});
    const TrackId track = sequence.addTrack(TrackKind::Video);
    expect(sequence.overwriteClip(track, AssetId{1}, 0, 0, 100).succeeded(),
           "fixture clip should be created");
    expect(sequence.addMarker(50, "beat").succeeded(), "marker should be added");
    expect(sequence.addCaption({40, 20}, "hello").succeeded(), "caption should be added");

    const auto insertion = sequence.insertClip(track, AssetId{2}, 10, 0, 25);
    expect(insertion.succeeded(), "insert should succeed");
    expect(sequence.markers().front().position == 75,
           "marker after the insert point should shift right");
    expect(sequence.captions().front().timeline == FrameRange{65, 20},
           "caption after the insert point should shift right");

    const auto extract = sequence.extractRange({10, 25});
    expect(extract.succeeded(), "extract should close the inserted range");
    expect(sequence.markers().front().position == 50,
           "marker should return to its original position after extract");
    expect(sequence.captions().front().timeline == FrameRange{40, 20},
           "caption should return to its original range after extract");
}

void testSlideRejectsExtremeDeltaWithoutOverflow() {
    using namespace videx::core;

    Sequence sequence({24, 1});
    const TrackId track = sequence.addTrack(TrackKind::Video);
    expect(sequence.overwriteClip(track, AssetId{1}, 0, 0, 10).succeeded(),
           "left fixture clip should be created");
    const auto middle = sequence.overwriteClip(track, AssetId{1}, 10, 100, 10);
    expect(middle.succeeded(), "middle fixture clip should be created");
    expect(sequence.overwriteClip(track, AssetId{1}, 20, 200, 10).succeeded(),
           "right fixture clip should be created");

    const auto slide =
        sequence.slideClip(middle.primaryClip, std::numeric_limits<Frame>::max());
    expect(!slide.succeeded(), "extreme slide delta must be rejected");
    const auto slideNegative =
        sequence.slideClip(middle.primaryClip, std::numeric_limits<Frame>::min());
    expect(!slideNegative.succeeded(), "extreme negative slide delta must be rejected");
}

void testPasteRejectsInvalidCropAndTransitions() {
    using namespace videx::core;

    Sequence sequence({24, 1});
    const TrackId track = sequence.addTrack(TrackKind::Video);
    Clip invalidCrop;
    invalidCrop.assetId = AssetId{1};
    invalidCrop.timeline = {.start = 0, .duration = 10};
    invalidCrop.cropLeft = 0.9;
    invalidCrop.cropRight = 0.9;
    const auto cropResult = sequence.pasteClips({{.targetTrack = track,
                                                  .kind = TrackKind::Video,
                                                  .clip = invalidCrop,
                                                  .timelineStart = 0}});
    expect(cropResult.error == EditError::InvalidRange, "invalid crop paste must be rejected");

    Clip invalidTransition;
    invalidTransition.assetId = AssetId{1};
    invalidTransition.timeline = {.start = 0, .duration = 10};
    invalidTransition.videoTransitionInFrames = 25;
    const auto transitionResult = sequence.pasteClips({{.targetTrack = track,
                                                        .kind = TrackKind::Video,
                                                        .clip = invalidTransition,
                                                        .timelineStart = 0}});
    expect(transitionResult.error == EditError::InvalidRange,
           "oversized pasted transition must be rejected");
    expect(sequence.findTrack(track)->clips.empty(),
           "rejected pastes must not add clips");
}

void testLoadedLinkIdsAreNotReminted() {
    using namespace videx::core;

    Track track{.id = TrackId{1}, .kind = TrackKind::Video};
    Clip clip;
    clip.id = ClipId{2};
    clip.assetId = AssetId{1};
    clip.timeline = {.start = 0, .duration = 10};
    clip.linkId = LinkId{500};
    track.clips.push_back(clip);
    Sequence sequence = Sequence::fromTracks({24, 1}, {track});
    const TrackId newTrack = sequence.addTrack(TrackKind::Video);
    expect(newTrack.value > 500U, "new entity IDs must not collide with loaded link IDs");
}

void testEffectReorderIsClampedAndUndoable() {
    using namespace videx::core;

    EditSession session({24, 1});
    const auto track = session.apply({.baseRevision = session.sequence().revision(),
                                      .label = "Add track",
                                      .command = AddTrackCommand{TrackKind::Video}});
    const auto clip = session.apply({.baseRevision = session.sequence().revision(),
                                     .label = "Add clip",
                                     .command = OverwriteClipCommand{
                                         track.primaryTrack, AssetId{1}, 0, 0, 50}});
    const auto first = session.apply({.baseRevision = session.sequence().revision(),
                                      .label = "Add brightness",
                                      .command = AddEffectCommand{clip.primaryClip,
                                                                  EffectType::Brightness}});
    const auto second = session.apply({.baseRevision = session.sequence().revision(),
                                       .label = "Add blur",
                                       .command = AddEffectCommand{clip.primaryClip,
                                                                   EffectType::Blur}});
    expect(first.succeeded() && second.succeeded(), "effects should be added");

    const auto move = session.apply({.baseRevision = session.sequence().revision(),
                                     .label = "Move effect",
                                     .command = MoveEffectCommand{clip.primaryClip,
                                                                  second.primaryEffect, -1}});
    expect(move.succeeded(), "effect reorder should succeed");
    const Clip* stored = session.sequence().findClip(clip.primaryClip);
    expect(stored != nullptr && stored->effects.size() == 2U &&
               stored->effects.front().type == EffectType::Blur,
           "blur should move before brightness");

    const auto clamped = session.apply({.baseRevision = session.sequence().revision(),
                                        .label = "Move effect",
                                        .command = MoveEffectCommand{clip.primaryClip,
                                                                     second.primaryEffect,
                                                                     -5}});
    expect(clamped.succeeded(), "clamped reorder should succeed as a no-op move");
    expect(session.undo().succeeded() && session.undo().succeeded(),
           "reorders should undo");
    stored = session.sequence().findClip(clip.primaryClip);
    expect(stored != nullptr && stored->effects.front().type == EffectType::Brightness,
           "undo should restore the original effect order");
}

int runTimelineTests() {
    testEffectReorderIsClampedAndUndoable();
    testRippleTrimShiftsUnsyncedTargetTrack();
    testSplitClampsFadesToPieceDurations();
    testInsertShiftsMarkersAndCaptions();
    testSlideRejectsExtremeDeltaWithoutOverflow();
    testPasteRejectsInvalidCropAndTransitions();
    testLoadedLinkIdsAreNotReminted();
    testInsertSplitsAndShiftsSyncLockedTracks();
    testOverwritePreservesOuterHandles();
    testFailedMoveIsAtomic();
    testSplitReplacesIdentityAndPreservesSourceTime();
    testRegularTrimPreservesTheOppositeEdge();
    testTrimRejectsCollisionAtomically();
    testExtractClosesSyncLockedTracks();
    testLockedTrackRejectsEdits();
    testTypedCommandsUndoAndRedo();
    testStaleCommandDoesNotMutateHistory();
    testTransactionIsAtomicAndCreatesOneUndoStep();
    testSequenceLoadsStableIdsAndRejectsOverlap();
    testLinkIdentitySurvivesSplit();
    testLiftRangeLeavesGapAndIsUndoable();
    testClipLinksAreTransactionalAndUndoable();
    testClipPropertiesAreValidatedAndUndoable();
    testSlipAndRollEditsAreAtomicAndUndoable();
    testClipFadesAreValidatedAndUndoable();
    testMoveBetweenCompatibleTracksIsAtomic();
    testPastePreservesPropertiesAndRemapsLinks();
    testTransformIsValidatedAndUndoable();
    testMotionKeyframesAreValidatedRetimedAndUndoable();
    testGainKeyframesAreValidatedRetimedAndUndoable();
    testRippleTrimAndSlidePreserveCoverage();
    testEffectsAreValidatedKeyframedRetimedAndUndoable();
    testTracksCanBeReorderedDeletedAndUndone();
    testTracksGroupByKindAndSoloToggles();
    testTransactionBindsPlacementToNewTrack();
    testFromTracksNormalizesKindGrouping();
    testTransitionsRequireAdjacentCutsAndAreUndoable();
    testCropIsValidatedAndUndoable();
    testSpeedKeyframesAreValidatedRetimedAndUndoable();
    testMasksAreValidatedAndUndoable();
    testMarkersCarryColorCommentAndAreEditable();
    testEasingPresetsAreAcceptedAndOutOfRangeRejected();
    testCaptionStylesAreValidatedAndUndoable();
    return failures;
}
