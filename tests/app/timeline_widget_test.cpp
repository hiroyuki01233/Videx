#include "timeline_widget.hpp"

#include <videx/core/timeline.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    using namespace videx;

    core::Sequence sequence({24, 1});
    const core::TrackId track = sequence.addTrack(core::TrackKind::Audio);
    const core::ClipId clip = sequence.overwriteClip(
        track, core::AssetId{1}, 0, 0, 20).primaryClip;
    if (!sequence.setGainKeyframe(clip, 0, 0.0,
            core::KeyframeInterpolation::Linear).succeeded() ||
        !sequence.setGainKeyframe(clip, 19, 0.0,
            core::KeyframeInterpolation::Linear).succeeded()) {
        std::cerr << "could not create gain automation fixture\n";
        return 1;
    }

    ui::TimelineWidget widget;
    widget.resize(520, 160);
    widget.setSequence(&sequence);
    widget.show();
    QCoreApplication::processEvents();

    const auto sendMouse = [&widget](const QEvent::Type type, const QPointF& position,
                                     const Qt::MouseButton button,
                                     const Qt::MouseButtons buttons,
                                     const Qt::KeyboardModifiers modifiers =
                                         Qt::NoModifier) {
        const QPointF global(widget.mapToGlobal(position.toPoint()));
        QMouseEvent event(type, position, position, global, button, buttons, modifiers);
        QApplication::sendEvent(&widget, &event);
    };

    core::Frame receivedFrame = -1;
    double receivedGain = 1000.0;
    widget.setGainHandler([&](const core::ClipId receivedClip,
                              const core::Frame timelineFrame,
                              const double gainDb) {
        if (receivedClip == clip) {
            receivedFrame = timelineFrame;
            receivedGain = gainDb;
        }
    });

    // Default geometry: labels occupy 160 px, the scale is 8 px/frame, and
    // tracks start below the ruler (28 px) plus the caption lane (24 px).
    // At 0 dB the gain line is approximately y=72 on the first track.
    const QPointF pressPosition(160.0 + 10.0 * 8.0, 72.0);
    const QPointF movePosition(pressPosition.x(), 84.0);
    sendMouse(QEvent::MouseButtonPress, pressPosition, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, movePosition, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, movePosition, Qt::LeftButton, Qt::NoButton);

    if (receivedFrame != 10 || !std::isfinite(receivedGain) ||
        receivedGain >= -10.0 || receivedGain <= -30.0) {
        std::cerr << "gain drag did not target the pointer frame: frame="
                  << receivedFrame << " gain=" << receivedGain << '\n';
        return 1;
    }

    core::Frame receivedFadeIn = -1;
    widget.setFadeHandler([&](const core::ClipId receivedClip, const core::Frame fadeIn,
                              const core::Frame) {
        if (receivedClip == clip) receivedFadeIn = fadeIn;
    });
    const QPointF fadeStart(160.0, 62.0);
    const QPointF fadeEnd(160.0 + 5.0 * 8.0, 62.0);
    sendMouse(QEvent::MouseButtonPress, fadeStart, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, fadeEnd, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, fadeEnd, Qt::LeftButton, Qt::NoButton);
    if (receivedFadeIn != 5) {
        std::cerr << "fade handle did not commit the pointer frame\n";
        return 1;
    }

    core::Frame splitFrame = -1;
    widget.setSplitClipHandler([&](const core::ClipId receivedClip,
                                   const core::Frame frame) {
        if (receivedClip == clip) splitFrame = frame;
    });
    widget.setTool(ui::TimelineWidget::Tool::Razor);
    const QPointF razorPosition(160.0 + 7.0 * 8.0, 92.0);
    sendMouse(QEvent::MouseButtonPress, razorPosition, Qt::LeftButton, Qt::LeftButton);
    if (splitFrame != 7) {
        std::cerr << "razor did not split at the pointer frame\n";
        return 1;
    }

    widget.setTool(ui::TimelineWidget::Tool::Selection);
    std::vector<core::ClipId> boxSelection;
    widget.setSelectionHandler(
        [&](const std::vector<core::ClipId>& ids) { boxSelection = ids; });
    const QPointF emptyPosition(160.0 + 22.0 * 8.0, 94.0);
    const QPointF boxEnd(162.0, 104.0);
    sendMouse(QEvent::MouseButtonPress, emptyPosition, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, boxEnd, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, boxEnd, Qt::LeftButton, Qt::NoButton);
    if (boxSelection.size() != 1U || boxSelection.front() != clip) {
        std::cerr << "box selection did not select the intersecting clip\n";
        return 1;
    }

    core::Frame playheadFrame = -1;
    widget.setPlayheadHandler([&](const core::Frame frame) { playheadFrame = frame; });
    const QPointF rulerPosition(160.0 + 12.0 * 8.0, 10.0);
    sendMouse(QEvent::MouseButtonPress, rulerPosition, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, rulerPosition, Qt::LeftButton, Qt::NoButton);
    if (playheadFrame != 12) {
        std::cerr << "ruler drag did not move the playhead to the pointer frame\n";
        return 1;
    }

    // Trim-end drag: previews stream the moving edit point and the drag ends
    // with a single inactive call before the commit is observable.
    std::vector<std::pair<core::Frame, bool>> trimPreviews;
    widget.setTrimPreviewHandler([&](const core::ClipId receivedClip, const core::Frame frame,
                                     const bool, const bool active) {
        if (receivedClip == clip) trimPreviews.emplace_back(frame, active);
    });
    core::Frame trimStartResult = -1;
    core::Frame trimEndResult = -1;
    widget.setTrimClipHandler([&](const core::ClipId receivedClip,
                                  const core::Frame newStart, const core::Frame newEnd) {
        if (receivedClip == clip) {
            trimStartResult = newStart;
            trimEndResult = newEnd;
        }
    });
    const QPointF trimEdge(160.0 + 20.0 * 8.0 - 2.0, 84.0);
    const QPointF trimTarget(160.0 + 15.0 * 8.0, 84.0);
    sendMouse(QEvent::MouseButtonPress, trimEdge, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, trimTarget, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, trimTarget, Qt::LeftButton, Qt::NoButton);
    if (trimStartResult != 0 || trimEndResult != 15) {
        std::cerr << "trim end drag did not commit the pointer frame: start="
                  << trimStartResult << " end=" << trimEndResult << '\n';
        return 1;
    }
    if (trimPreviews.size() < 2U || trimPreviews.back().second ||
        trimPreviews[trimPreviews.size() - 2].first != 14 ||
        !trimPreviews[trimPreviews.size() - 2].second) {
        std::cerr << "trim drag should stream active previews and end inactive\n";
        return 1;
    }

    // Captions live on their own lane below the ruler; dragging one there
    // commits a single move with the pointer offset preserved.
    if (!sequence.addCaption({.start = 0, .duration = 8}, "Lane").succeeded()) {
        std::cerr << "could not create caption fixture\n";
        return 1;
    }
    core::Frame captionMovedTo = -1;
    widget.setCaptionMoveHandler(
        [&](const core::CaptionId, const core::Frame start) { captionMovedTo = start; });
    const QPointF captionPress(160.0 + 4.0 * 8.0, 40.0);
    const QPointF captionTarget(160.0 + 14.0 * 8.0, 40.0);
    sendMouse(QEvent::MouseButtonPress, captionPress, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, captionTarget, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, captionTarget, Qt::LeftButton, Qt::NoButton);
    if (captionMovedTo != 10) {
        std::cerr << "caption drag did not commit the pointer frame: start="
                  << captionMovedTo << '\n';
        return 1;
    }

    // --- Multi-track section: two video tracks above the audio fixture. ---
    // Display rows become [V2, V1, A1] (front-most video on top).
    const core::TrackId v1 = sequence.addTrack(core::TrackKind::Video);
    const core::TrackId v2 = sequence.addTrack(core::TrackKind::Video);
    const core::ClipId videoClip =
        sequence.overwriteClip(v1, core::AssetId{2}, 0, 0, 20).primaryClip;
    widget.resize(520, 460);
    widget.setSequence(&sequence);
    QCoreApplication::processEvents();

    // T5: dragging a clip vertically must hand the target track to the host.
    core::TrackId movedToTrack{};
    core::Frame movedToFrame = -1;
    widget.setMoveClipHandler([&](const core::ClipId movedClip,
                                  const core::TrackId targetTrack,
                                  const core::Frame newStart) {
        if (movedClip == videoClip) {
            movedToTrack = targetTrack;
            movedToFrame = newStart;
        }
    });
    // Rows: V2 at y[52,114), V1 at y[114,176), A1 at y[176,238).
    const QPointF videoClipPress(160.0 + 5.0 * 8.0, 145.0);
    const QPointF videoClipTarget(videoClipPress.x(), 80.0);
    sendMouse(QEvent::MouseButtonPress, videoClipPress, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, videoClipTarget, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, videoClipTarget, Qt::LeftButton, Qt::NoButton);
    if (movedToTrack != v2) {
        std::cerr << "vertical drag did not target the front video track\n";
        return 1;
    }

    // T7: releasing over an invalid (locked) track must cancel the move.
    if (!sequence.setTrackLocked(v2, true).succeeded()) {
        std::cerr << "could not lock the target track fixture\n";
        return 1;
    }
    widget.setSequence(&sequence);
    movedToTrack = {};
    movedToFrame = -1;
    sendMouse(QEvent::MouseButtonPress, videoClipPress, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, videoClipTarget, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, videoClipTarget, Qt::LeftButton, Qt::NoButton);
    if (movedToTrack || movedToFrame != -1) {
        std::cerr << "dropping onto a locked track must cancel the move\n";
        return 1;
    }
    if (!sequence.setTrackLocked(v2, false).succeeded()) {
        std::cerr << "could not unlock the target track fixture\n";
        return 1;
    }
    widget.setSequence(&sequence);

    // T6: the new-audio drop zone is only the band right below the last row.
    // (QWidget only receives real drag-and-drop events through the window
    // system, so the zone resolution is asserted directly.)
    if (widget.dropTargetTrackIndex(QPointF(220.0, 250.0)) != -3) {
        std::cerr << "drop just below the last row must request a new audio track\n";
        return 1;
    }
    if (widget.dropTargetTrackIndex(QPointF(220.0, 330.0)) != -1) {
        std::cerr << "drop far below the tracks must not mint a track\n";
        return 1;
    }
    if (widget.dropTargetTrackIndex(QPointF(220.0, 40.0)) != -2) {
        std::cerr << "drop on the lane above the top row must request a new video track\n";
        return 1;
    }
    if (const core::Track* frontTrack =
            &sequence.tracks()[static_cast<std::size_t>(
                widget.dropTargetTrackIndex(QPointF(220.0, 80.0)))];
        frontTrack->id != v2) {
        std::cerr << "drop on the top row must resolve to the front video track\n";
        return 1;
    }

    // T8: hit tests must honour vertical scrolling.
    widget.resize(520, 200);
    QCoreApplication::processEvents();
    QScrollBar* verticalBar = nullptr;
    for (QScrollBar* bar : widget.findChildren<QScrollBar*>()) {
        if (bar->orientation() == Qt::Vertical) {
            verticalBar = bar;
        }
    }
    if (verticalBar == nullptr || verticalBar->maximum() <= 0) {
        std::cerr << "vertical scrollbar should be active with three tracks\n";
        return 1;
    }
    verticalBar->setValue(60);
    std::vector<core::ClipId> scrolledSelection;
    widget.setSelectionHandler(
        [&](const std::vector<core::ClipId>& ids) { scrolledSelection = ids; });
    // A1 row is [176,238) unscrolled; with scrollY=60 it maps to [116,178).
    sendMouse(QEvent::MouseButtonPress, QPointF(200.0, 140.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, QPointF(200.0, 140.0), Qt::LeftButton,
              Qt::NoButton);
    if (scrolledSelection.empty() ||
        std::ranges::find(scrolledSelection, clip) == scrolledSelection.end()) {
        std::cerr << "scrolled click did not hit the audio clip\n";
        return 1;
    }

    // T-B8: Alt-drop duplicates at the target instead of moving.
    widget.resize(520, 460);
    verticalBar->setValue(0);
    QCoreApplication::processEvents();
    core::TrackId duplicatedTrack{};
    core::Frame duplicatedStart = -1;
    widget.setDuplicateClipHandler([&](const core::ClipId sourceClip,
                                       const core::TrackId targetTrack,
                                       const core::Frame newStart) {
        if (sourceClip == videoClip) {
            duplicatedTrack = targetTrack;
            duplicatedStart = newStart;
        }
    });
    movedToTrack = {};
    sendMouse(QEvent::MouseButtonPress, videoClipPress, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, videoClipTarget, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, videoClipTarget, Qt::LeftButton, Qt::NoButton,
              Qt::AltModifier);
    if (duplicatedTrack != v2 || duplicatedStart != 0 || movedToTrack) {
        std::cerr << "alt-drop must duplicate onto the target track, not move\n";
        return 1;
    }

    // T-D3: dragging a row boundary in the label column resizes the track.
    core::TrackId heightTrack{};
    int heightAction = -1;
    widget.setTrackActionHandler([&](const ui::TimelineWidget::TrackAction action,
                                     const core::TrackId trackId) {
        heightTrack = trackId;
        heightAction = static_cast<int>(action);
    });
    sendMouse(QEvent::MouseButtonPress, QPointF(130.0, 114.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseMove, QPointF(130.0, 148.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, QPointF(130.0, 148.0), Qt::LeftButton,
              Qt::NoButton);
    if (heightTrack != v2 ||
        heightAction !=
            static_cast<int>(ui::TimelineWidget::TrackAction::HeightExpanded)) {
        std::cerr << "boundary drag did not request the expanded track height\n";
        return 1;
    }

    // T-B6: dragging near the right edge auto-scrolls the time axis. Content
    // must extend past the viewport for the scrollbar to have a range.
    const auto longClipResult = sequence.overwriteClip(v2, core::AssetId{3}, 0, 0, 400);
    if (!longClipResult.succeeded()) {
        std::cerr << "could not create the long clip fixture\n";
        return 1;
    }
    const core::ClipId longClip = longClipResult.primaryClip;
    widget.setSequence(&sequence);
    QCoreApplication::processEvents();
    QScrollBar* horizontalBar = nullptr;
    for (QScrollBar* bar : widget.findChildren<QScrollBar*>()) {
        if (bar->orientation() == Qt::Horizontal) {
            horizontalBar = bar;
        }
    }
    if (horizontalBar == nullptr) {
        std::cerr << "horizontal scrollbar must exist\n";
        return 1;
    }
    const int initialScroll = horizontalBar->value();
    sendMouse(QEvent::MouseButtonPress, videoClipPress, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, QPointF(widget.width() - 8.0, videoClipPress.y()),
              Qt::NoButton, Qt::LeftButton);
    bool autoScrolled = false;
    for (int attempt = 0; attempt < 40 && !autoScrolled; ++attempt) {
        QThread::msleep(25);
        QCoreApplication::processEvents();
        widget.update();
        QCoreApplication::processEvents();
        autoScrolled = horizontalBar->value() > initialScroll;
    }
    {
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&widget, &escape);
    }
    sendMouse(QEvent::MouseButtonRelease,
              QPointF(widget.width() - 8.0, videoClipPress.y()), Qt::LeftButton,
              Qt::NoButton);
    if (!autoScrolled) {
        std::cerr << "edge auto-scroll did not advance the timeline\n";
        return 1;
    }

    const auto sendKey = [&widget](const int key, const Qt::KeyboardModifiers modifiers) {
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        QApplication::sendEvent(&widget, &press);
    };

    // Reset the horizontal scroll the auto-scroll test advanced so the
    // pixel-to-frame math below is exact again.
    horizontalBar->setValue(0);
    QCoreApplication::processEvents();

    // S toggles snapping. Dragging the clip so its raw start would be frame
    // 11 (one frame from the playhead at 12) snaps with snapping on and
    // stays raw with snapping off.
    const QPointF snapDropPoint(160.0 + 16.0 * 8.0, videoClipPress.y());
    movedToFrame = -1;
    sendMouse(QEvent::MouseButtonPress, videoClipPress, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, snapDropPoint, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, snapDropPoint, Qt::LeftButton, Qt::NoButton);
    if (movedToFrame != 12) {
        std::cerr << "with snapping on the drop must snap to the playhead, got "
                  << movedToFrame << '\n';
        return 1;
    }
    sendKey(Qt::Key_S, Qt::NoModifier); // snapping off
    movedToFrame = -1;
    sendMouse(QEvent::MouseButtonPress, videoClipPress, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, snapDropPoint, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, snapDropPoint, Qt::LeftButton, Qt::NoButton);
    if (movedToFrame != 11) {
        std::cerr << "with snapping off the drop must land on the raw frame, got "
                  << movedToFrame << '\n';
        return 1;
    }
    sendKey(Qt::Key_S, Qt::NoModifier); // snapping back on

    // Shift+Z fits the whole sequence: afterwards the tail of the 400-frame
    // clip (which sat far off-screen at 8 px/frame) is clickable in-view.
    sendKey(Qt::Key_Z, Qt::ShiftModifier);
    QCoreApplication::processEvents();
    std::vector<core::ClipId> fitSelection;
    widget.setSelectionHandler(
        [&](const std::vector<core::ClipId>& ids) { fitSelection = ids; });
    const QPointF farRight(widget.width() - 30.0, 80.0); // V2 row, near frame 394
    sendMouse(QEvent::MouseButtonPress, farRight, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, farRight, Qt::LeftButton, Qt::NoButton);
    if (std::ranges::find(fitSelection, longClip) == fitSelection.end()) {
        std::cerr << "Shift+Z did not bring the sequence tail into view\n";
        return 1;
    }

    // Track header drag reorder: dragging V1's name area onto the V2 row
    // must request an array move of +1; dropping on an audio row is ignored.
    core::TrackId reorderedTrack{};
    int reorderDelta = 0;
    widget.setTrackReorderHandler([&](const core::TrackId trackId, const int delta) {
        reorderedTrack = trackId;
        reorderDelta = delta;
    });
    sendMouse(QEvent::MouseButtonPress, QPointF(130.0, 145.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseMove, QPointF(130.0, 80.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, QPointF(130.0, 80.0), Qt::LeftButton,
              Qt::NoButton);
    if (reorderedTrack != v1 || reorderDelta != 1) {
        std::cerr << "header drag did not request the +1 reorder, got delta "
                  << reorderDelta << '\n';
        return 1;
    }
    reorderedTrack = {};
    reorderDelta = 0;
    sendMouse(QEvent::MouseButtonPress, QPointF(130.0, 145.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseMove, QPointF(130.0, 210.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, QPointF(130.0, 210.0), Qt::LeftButton,
              Qt::NoButton);
    if (reorderedTrack || reorderDelta != 0) {
        std::cerr << "dropping a video header on an audio row must be ignored\n";
        return 1;
    }

    // Shortcut parity: Up/Down jump between edit points (cuts at 0/20/400),
    // Home/End jump to the sequence ends, +/- zoom around the playhead.
    widget.setPlayheadFrame(12, false);
    sendKey(Qt::Key_Up, Qt::NoModifier);
    if (widget.playheadFrame() != 0) {
        std::cerr << "Up must jump to the previous edit point, got "
                  << widget.playheadFrame() << '\n';
        return 1;
    }
    sendKey(Qt::Key_Down, Qt::NoModifier);
    if (widget.playheadFrame() != 20) {
        std::cerr << "Down must jump to the next edit point, got "
                  << widget.playheadFrame() << '\n';
        return 1;
    }
    sendKey(Qt::Key_End, Qt::NoModifier);
    if (widget.playheadFrame() != 400) {
        std::cerr << "End must jump to the sequence end, got "
                  << widget.playheadFrame() << '\n';
        return 1;
    }
    sendKey(Qt::Key_Home, Qt::NoModifier);
    if (widget.playheadFrame() != 0) {
        std::cerr << "Home must jump to the sequence start\n";
        return 1;
    }
    const int fitMaximum = horizontalBar->maximum();
    sendKey(Qt::Key_Plus, Qt::NoModifier);
    QCoreApplication::processEvents();
    widget.update();
    QCoreApplication::processEvents();
    if (horizontalBar->maximum() <= fitMaximum) {
        std::cerr << "keyboard zoom-in must extend the scroll range\n";
        return 1;
    }

    // Marker flags on the ruler edge: hit test, drag-to-move via the move
    // handler, right-press requesting the context menu, double-click edit,
    // and Escape cancelling an active drag.
    core::Sequence markerSequence({24, 1});
    const core::TrackId markerTrack = markerSequence.addTrack(core::TrackKind::Video);
    if (!markerSequence.overwriteClip(markerTrack, core::AssetId{1}, 0, 0, 100)
             .succeeded() ||
        !markerSequence.addMarker(10, "Cue").succeeded()) {
        std::cerr << "could not create marker fixture\n";
        return 1;
    }
    const core::MarkerId markerId = markerSequence.markers().front().id;
    ui::TimelineWidget markerWidget;
    markerWidget.resize(520, 200);
    markerWidget.setSequence(&markerSequence);
    markerWidget.show();
    QCoreApplication::processEvents();
    const auto sendMarkerMouse = [&markerWidget](const QEvent::Type type,
                                                 const QPointF& position,
                                                 const Qt::MouseButton button,
                                                 const Qt::MouseButtons buttons) {
        const QPointF global(markerWidget.mapToGlobal(position.toPoint()));
        QMouseEvent event(type, position, position, global, button, buttons,
                          Qt::NoModifier);
        QApplication::sendEvent(&markerWidget, &event);
    };
    // Default scale: labels 160 px, 8 px/frame; frame 10 sits at x=240 and the
    // flag straddles the ruler edge (y=28).
    const QPointF flag(240.0, 30.0);
    if (markerWidget.markerIdAt(flag) != markerId ||
        markerWidget.markerIdAt(QPointF(300.0, 30.0)) ||
        markerWidget.markerIdAt(QPointF(240.0, 60.0))) {
        std::cerr << "marker hit test did not isolate the flag zone\n";
        return 1;
    }
    core::MarkerId movedMarker{};
    core::Frame movedFrame = -1;
    markerWidget.setMarkerMoveHandler([&](const core::MarkerId id,
                                          const core::Frame frame) {
        movedMarker = id;
        movedFrame = frame;
    });
    sendMarkerMouse(QEvent::MouseButtonPress, flag, Qt::LeftButton, Qt::LeftButton);
    sendMarkerMouse(QEvent::MouseMove, QPointF(400.0, 30.0), Qt::NoButton,
                    Qt::LeftButton);
    sendMarkerMouse(QEvent::MouseButtonRelease, QPointF(400.0, 30.0), Qt::LeftButton,
                    Qt::NoButton);
    if (movedMarker != markerId || movedFrame != 30) {
        std::cerr << "marker drag did not commit the pointer frame, got "
                  << movedFrame << '\n';
        return 1;
    }
    core::MarkerId contextMarker{};
    markerWidget.setMarkerContextHandler(
        [&](const core::MarkerId id, const QPoint&) { contextMarker = id; });
    sendMarkerMouse(QEvent::MouseButtonPress, flag, Qt::RightButton, Qt::RightButton);
    sendMarkerMouse(QEvent::MouseButtonRelease, flag, Qt::RightButton, Qt::NoButton);
    if (contextMarker != markerId) {
        std::cerr << "right-press on the flag must request the marker menu\n";
        return 1;
    }
    core::MarkerId editedMarker{};
    markerWidget.setMarkerEditHandler(
        [&](const core::MarkerId id) { editedMarker = id; });
    {
        const QPointF global(markerWidget.mapToGlobal(flag.toPoint()));
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, flag, flag, global,
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&markerWidget, &doubleClick);
    }
    if (editedMarker != markerId) {
        std::cerr << "double-click on the flag must open the marker editor\n";
        return 1;
    }
    movedMarker = {};
    movedFrame = -1;
    sendMarkerMouse(QEvent::MouseButtonPress, flag, Qt::LeftButton, Qt::LeftButton);
    sendMarkerMouse(QEvent::MouseMove, QPointF(400.0, 30.0), Qt::NoButton,
                    Qt::LeftButton);
    {
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&markerWidget, &escape);
    }
    sendMarkerMouse(QEvent::MouseButtonRelease, QPointF(400.0, 30.0), Qt::LeftButton,
                    Qt::NoButton);
    if (movedMarker || movedFrame != -1) {
        std::cerr << "Escape must cancel an active marker drag\n";
        return 1;
    }

    // Edge trim: dragging a clip's right edge must resize that edge only —
    // live preview callbacks during the move, one commit with the far edge
    // untouched on release.
    core::Sequence trimSequence({24, 1});
    const core::TrackId trimTrack = trimSequence.addTrack(core::TrackKind::Video);
    const core::ClipId trimClip = trimSequence.overwriteClip(
        trimTrack, core::AssetId{1}, 0, 0, 40).primaryClip;
    ui::TimelineWidget trimWidget;
    trimWidget.resize(520, 200);
    trimWidget.setSequence(&trimSequence);
    trimWidget.show();
    QCoreApplication::processEvents();
    core::Frame trimStartReceived = -1;
    core::Frame trimEndReceived = -1;
    int trimCommits = 0;
    trimWidget.setTrimClipHandler([&](const core::ClipId id, const core::Frame newStart,
                                      const core::Frame newEnd) {
        if (id == trimClip) {
            ++trimCommits;
            trimStartReceived = newStart;
            trimEndReceived = newEnd;
        }
    });
    int trimPreviewCount = 0;
    core::Frame lastPreviewFrame = -1;
    trimWidget.setTrimPreviewHandler([&](const core::ClipId id, const core::Frame frame,
                                         const bool, const bool active) {
        if (id == trimClip && active) {
            ++trimPreviewCount;
            lastPreviewFrame = frame;
        }
    });
    const auto sendTrimMouse = [&trimWidget](const QEvent::Type type,
                                             const QPointF& position,
                                             const Qt::MouseButton button,
                                             const Qt::MouseButtons buttons) {
        const QPointF global(trimWidget.mapToGlobal(position.toPoint()));
        QMouseEvent event(type, position, position, global, button, buttons,
                          Qt::NoModifier);
        QApplication::sendEvent(&trimWidget, &event);
    };
    // Right edge of the 40-frame clip sits at x = 160 + 40*8 = 480.
    sendTrimMouse(QEvent::MouseButtonPress, QPointF(480.0, 80.0), Qt::LeftButton,
                  Qt::LeftButton);
    sendTrimMouse(QEvent::MouseMove, QPointF(400.0, 80.0), Qt::NoButton, Qt::LeftButton);
    sendTrimMouse(QEvent::MouseButtonRelease, QPointF(400.0, 80.0), Qt::LeftButton,
                  Qt::NoButton);
    if (trimCommits != 1 || trimStartReceived != 0 || trimEndReceived != 30) {
        std::cerr << "edge trim did not resize only the dragged edge: commits="
                  << trimCommits << " start=" << trimStartReceived << " end="
                  << trimEndReceived << '\n';
        return 1;
    }
    if (trimPreviewCount < 1 || lastPreviewFrame != 29) {
        std::cerr << "edge trim did not stream live previews, last frame "
                  << lastPreviewFrame << '\n';
        return 1;
    }

    // The header column (x < 160) must render identically at any horizontal
    // scroll: scrolled-in clips used to paint over the track labels.
    QScrollBar* markerHorizontalBar = nullptr;
    for (QScrollBar* bar : markerWidget.findChildren<QScrollBar*>()) {
        if (bar->orientation() == Qt::Horizontal) {
            markerHorizontalBar = bar;
        }
    }
    if (markerHorizontalBar == nullptr) {
        std::cerr << "missing horizontal scroll bar in marker fixture\n";
        return 1;
    }
    // Exclude the scroll bar itself: its handle legitimately moves.
    const int headerHeight = markerHorizontalBar->geometry().top() - 1;
    if (headerHeight <= 0) {
        std::cerr << "unexpected horizontal scroll bar geometry\n";
        return 1;
    }
    const QImage headerBefore =
        markerWidget.grab().toImage().copy(0, 0, 159, headerHeight);
    markerHorizontalBar->setValue(
        std::min(markerHorizontalBar->maximum(), 50));
    QCoreApplication::processEvents();
    if (markerHorizontalBar->value() == 0) {
        std::cerr << "could not scroll the marker fixture horizontally\n";
        return 1;
    }
    const QImage headerAfter =
        markerWidget.grab().toImage().copy(0, 0, 159, headerHeight);
    if (headerBefore != headerAfter) {
        for (int y = 0; y < headerBefore.height(); ++y) {
            for (int x = 0; x < headerBefore.width(); ++x) {
                if (headerBefore.pixel(x, y) != headerAfter.pixel(x, y)) {
                    std::cerr << "scrolled clips painted over the header column at ("
                              << x << ", " << y << "): "
                              << std::hex << headerBefore.pixel(x, y) << " -> "
                              << headerAfter.pixel(x, y) << std::dec << '\n';
                    return 1;
                }
            }
        }
        std::cerr << "scrolled clips painted over the header column\n";
        return 1;
    }
    return 0;
}
