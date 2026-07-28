#include "timeline_widget.hpp"
#include "theme_tokens.hpp"

#include <QColor>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMimeData>
#include <QKeySequence>
#include <QMouseEvent>
#include <QApplication>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QPolygon>
#include <QPolygonF>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QString>
#include <QToolTip>
#include <QWheelEvent>

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr int rulerHeight = 28;
// Captions live on their own lane between the ruler and the tracks so text
// never overlaps video clips; overlapping captions stack into extra sub-rows.
constexpr int captionLaneHeight = 24;
constexpr int trackHeight = 62;
constexpr int trackLabelWidth = 160;
constexpr int clipInset = 5;
constexpr double trimHandleWidth = 7.0;

int trackRowHeight(const videx::core::Track& track) {
    switch (track.heightMode) {
    case 0: return 34;
    case 2: return 96;
    default: return trackHeight;
    }
}

QColor clipColor(const videx::core::Clip& clip, const bool selected) {
    const int hue = static_cast<int>((clip.assetId.value * 47U) % 360U);
    return QColor::fromHsv(hue, selected ? 190 : 145, selected ? 230 : 190);
}

double gainAt(const videx::core::Clip& clip, const videx::core::Frame frame) {
    if (clip.gainKeyframes.empty()) return clip.audioGainDb;
    const auto next = std::ranges::lower_bound(
        clip.gainKeyframes, frame, {}, &videx::core::GainKeyframe::frameOffset);
    if (next == clip.gainKeyframes.begin()) return next->gainDb;
    if (next == clip.gainKeyframes.end()) return clip.gainKeyframes.back().gainDb;
    const auto previous = std::prev(next);
    if (previous->interpolation == videx::core::KeyframeInterpolation::Hold)
        return previous->gainDb;
    double ratio = static_cast<double>(frame - previous->frameOffset) /
                   static_cast<double>(next->frameOffset - previous->frameOffset);
    ratio = videx::core::interpolationProgress(previous->interpolation, ratio);
    return previous->gainDb + (next->gainDb - previous->gainDb) * ratio;
}

} // namespace

namespace videx::ui {

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    horizontalScroll_ = new QScrollBar(Qt::Horizontal, this);
    horizontalScroll_->setRange(0, 0);
    connect(horizontalScroll_, &QScrollBar::valueChanged, this, [this](const int value) {
        if (updatingScrollBar_) {
            return;
        }
        scrollFrame_ = std::max<core::Frame>(0, value);
        update();
    });
    verticalScroll_ = new QScrollBar(Qt::Vertical, this);
    verticalScroll_->setRange(0, 0);
    connect(verticalScroll_, &QScrollBar::valueChanged, this, [this](const int value) {
        if (updatingVerticalScrollBar_) {
            return;
        }
        scrollY_ = std::max(0, value);
        update();
    });
    autoScrollTimer_ = new QTimer(this);
    autoScrollTimer_->setInterval(50);
    connect(autoScrollTimer_, &QTimer::timeout, this, [this] {
        if (autoScrollStepX_ == 0 && autoScrollStepY_ == 0) {
            autoScrollTimer_->stop();
            return;
        }
        if (autoScrollStepX_ != 0) {
            const core::Frame step = std::max<core::Frame>(
                1, static_cast<core::Frame>(std::llround(16.0 / pixelsPerFrame_)));
            scrollFrame_ = std::max<core::Frame>(0, scrollFrame_ + autoScrollStepX_ * step);
        }
        if (autoScrollStepY_ != 0 && verticalScroll_ != nullptr &&
            verticalScroll_->maximum() > 0) {
            verticalScroll_->setValue(scrollY_ + autoScrollStepY_ * 16);
        }
        // Re-drive the active drag through the normal move path so previews
        // and snapping recompute against the shifted viewport.
        QMouseEvent synthetic(QEvent::MouseMove, lastDragPosition_, lastDragPosition_,
                              mapToGlobal(lastDragPosition_.toPoint()), Qt::NoButton,
                              Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(this, &synthetic);
        update();
    });
}

void TimelineWidget::updateAutoScroll(const QPointF& position) {
    constexpr double margin = 24.0;
    const bool dragging = dragMode_ != DragMode::None || captionDragging_ ||
                          markerDragging_ || boxSelecting_ || draggingPlayhead_;
    autoScrollStepX_ = 0;
    autoScrollStepY_ = 0;
    if (dragging) {
        const int barWidth =
            verticalScroll_ == nullptr ? 0 : verticalScroll_->sizeHint().width();
        const int barHeight =
            horizontalScroll_ == nullptr ? 0 : horizontalScroll_->sizeHint().height();
        if (position.x() < trackLabelWidth + margin) {
            autoScrollStepX_ = -1;
        } else if (position.x() > width() - barWidth - margin) {
            autoScrollStepX_ = 1;
        }
        if (position.y() < tracksTop() + margin && scrollY_ > 0) {
            autoScrollStepY_ = -1;
        } else if (position.y() > height() - barHeight - margin) {
            autoScrollStepY_ = 1;
        }
    }
    if (autoScrollStepX_ != 0 || autoScrollStepY_ != 0) {
        if (!autoScrollTimer_->isActive()) {
            autoScrollTimer_->start();
        }
    } else {
        autoScrollTimer_->stop();
    }
}

core::Frame TimelineWidget::visibleFrameCount() const noexcept {
    const double viewWidth = std::max(1.0, static_cast<double>(width() - trackLabelWidth));
    return std::max<core::Frame>(
        1, static_cast<core::Frame>(std::floor(viewWidth / pixelsPerFrame_)));
}

core::Frame TimelineWidget::contentEndFrame() const noexcept {
    core::Frame end = 0;
    if (sequence_ != nullptr) {
        for (const core::Track& track : sequence_->tracks()) {
            for (const core::Clip& clip : track.clips) {
                end = std::max(end, clip.timeline.end());
            }
        }
        for (const core::Caption& caption : sequence_->captions()) {
            end = std::max(end, caption.timeline.end());
        }
        for (const core::Marker& marker : sequence_->markers()) {
            end = std::max(end, marker.position);
        }
    }
    return std::max(end, playheadFrame_);
}

void TimelineWidget::updateHorizontalScrollBar() {
    if (horizontalScroll_ == nullptr) {
        return;
    }
    const core::Frame visible = visibleFrameCount();
    const core::Frame contentEnd = contentEndFrame() + visible / 4;
    const core::Frame maximum = std::max<core::Frame>(0, contentEnd - visible);
    const core::Frame clampedMaximum =
        std::min<core::Frame>(maximum, std::numeric_limits<int>::max());
    updatingScrollBar_ = true;
    horizontalScroll_->setRange(0, static_cast<int>(clampedMaximum));
    horizontalScroll_->setPageStep(static_cast<int>(
        std::min<core::Frame>(visible, std::numeric_limits<int>::max())));
    horizontalScroll_->setSingleStep(
        std::max(1, static_cast<int>(std::min<core::Frame>(visible / 20, 1000))));
    horizontalScroll_->setValue(static_cast<int>(
        std::min<core::Frame>(scrollFrame_, clampedMaximum)));
    updatingScrollBar_ = false;
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-videx-asset")) ||
        event->mimeData()->hasFormat(QStringLiteral("application/x-videx-effect"))) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    externalDragActive_ = false;
    update();
    QWidget::dragLeaveEvent(event);
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-videx-asset"))) {
        // Track the pointer so paint can ghost the drop target row / new
        // track band while an asset hovers over the timeline.
        externalDragActive_ = true;
        externalDragPosition_ = event->position();
        update();
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-videx-effect"))) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(event);
}

int TimelineWidget::dropTargetTrackIndex(const QPointF& position) const {
    const int row = displayRowAt(position.y());
    if (row >= 0) {
        return static_cast<int>(displayRows_[static_cast<std::size_t>(row)]);
    }
    const double y = position.y();
    if (y >= static_cast<double>(rulerHeight) && y < static_cast<double>(tracksTop())) {
        return -2; // drop zone above the top video row: new video track
    }
    if (y >= static_cast<double>(tracksTop())) {
        // New-audio zone is only the one-track-high band right below the last
        // row (and never the scrollbar strip); drops further down keep the
        // default targets instead of minting tracks by accident.
        const double contentBottom =
            tracksTop() - scrollY_ + (rowOffsets_.empty() ? 0 : rowOffsets_.back());
        const int barHeight = horizontalScroll_ == nullptr
                                  ? 0
                                  : horizontalScroll_->sizeHint().height();
        const double bandEnd =
            std::min<double>(contentBottom + trackHeight, height() - barHeight);
        if (y >= contentBottom && y < bandEnd) {
            return -3;
        }
    }
    return -1;
}

void TimelineWidget::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-videx-effect")) &&
        effectDropHandler_) {
        bool parsed = false;
        const int effectType = event->mimeData()
                                   ->data(QStringLiteral("application/x-videx-effect"))
                                   .toInt(&parsed);
        const core::Clip* clip = clipAt(event->position());
        if (parsed && clip != nullptr) {
            effectDropHandler_(effectType, clip->id);
            event->acceptProposedAction();
        }
        return;
    }
    if (!event->mimeData()->hasFormat(QStringLiteral("application/x-videx-asset")) ||
        !assetDropHandler_) {
        QWidget::dropEvent(event);
        return;
    }
    bool parsed = false;
    const std::uint64_t assetId =
        event->mimeData()
            ->data(QStringLiteral("application/x-videx-asset"))
            .toULongLong(&parsed);
    if (!parsed || assetId == 0) {
        return;
    }
    externalDragActive_ = false;
    const core::Frame frame = std::max<core::Frame>(0, xToFrame(event->position().x()));
    assetDropHandler_(assetId, dropTargetTrackIndex(event->position()), frame);
    event->acceptProposedAction();
}

void TimelineWidget::followPlayhead() {
    const core::Frame visible = visibleFrameCount();
    if (playheadFrame_ > scrollFrame_ + (visible * 9) / 10 ||
        playheadFrame_ < scrollFrame_) {
        scrollFrame_ = std::max<core::Frame>(0, playheadFrame_ - visible / 10);
    }
}

void TimelineWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const int verticalBarWidth =
        verticalScroll_ == nullptr ? 0 : verticalScroll_->sizeHint().width();
    if (horizontalScroll_ != nullptr) {
        const int barHeight = horizontalScroll_->sizeHint().height();
        horizontalScroll_->setGeometry(
            trackLabelWidth, height() - barHeight,
            std::max(0, width() - trackLabelWidth - verticalBarWidth), barHeight);
    }
    if (verticalScroll_ != nullptr) {
        const int barHeight = horizontalScroll_ == nullptr
                                  ? 0
                                  : horizontalScroll_->sizeHint().height();
        verticalScroll_->setGeometry(width() - verticalBarWidth, tracksTop(),
                                     verticalBarWidth,
                                     std::max(0, height() - tracksTop() - barHeight));
    }
    updateHorizontalScrollBar();
    updateVerticalScrollBar();
}

int TimelineWidget::tracksTop() const noexcept {
    return rulerHeight + captionLaneHeight * captionLaneRows_;
}

void TimelineWidget::rebuildCaptionRows() {
    captionRows_.clear();
    captionLaneRows_ = 1;
    if (sequence_ == nullptr) {
        return;
    }
    // Greedy interval colouring: overlapping captions stack into sub-rows so
    // each one stays visible and clickable (display capped at four rows).
    std::vector<const core::Caption*> ordered;
    ordered.reserve(sequence_->captions().size());
    for (const core::Caption& caption : sequence_->captions()) {
        ordered.push_back(&caption);
    }
    std::ranges::sort(ordered, [](const core::Caption* a, const core::Caption* b) {
        return a->timeline.start != b->timeline.start
                   ? a->timeline.start < b->timeline.start
                   : a->id.value < b->id.value;
    });
    std::vector<core::Frame> rowEnds;
    for (const core::Caption* caption : ordered) {
        int row = -1;
        for (std::size_t candidate = 0; candidate < rowEnds.size(); ++candidate) {
            if (rowEnds[candidate] <= caption->timeline.start) {
                row = static_cast<int>(candidate);
                break;
            }
        }
        if (row < 0) {
            row = static_cast<int>(rowEnds.size());
            rowEnds.push_back(0);
        }
        rowEnds[static_cast<std::size_t>(row)] = caption->timeline.end();
        captionRows_.insert_or_assign(caption->id.value, std::min(row, 3));
    }
    captionLaneRows_ =
        std::clamp(static_cast<int>(rowEnds.size()), 1, 4);
}

int TimelineWidget::captionSubRow(const core::CaptionId captionId) const noexcept {
    const auto iterator = captionRows_.find(captionId.value);
    return iterator == captionRows_.end() ? 0 : iterator->second;
}

void TimelineWidget::rebuildDisplayRows() {
    displayRows_.clear();
    rowOffsets_.assign(1U, 0);
    rebuildCaptionRows();
    if (sequence_ == nullptr) {
        return;
    }
    const auto& tracks = sequence_->tracks();
    std::vector<std::size_t> videoIndices;
    std::vector<std::size_t> audioIndices;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        (tracks[index].kind == core::TrackKind::Video ? videoIndices : audioIndices)
            .push_back(index);
    }
    displayRows_.reserve(tracks.size());
    displayRows_.insert(displayRows_.end(), videoIndices.rbegin(), videoIndices.rend());
    displayRows_.insert(displayRows_.end(), audioIndices.begin(), audioIndices.end());
    rowOffsets_.reserve(displayRows_.size() + 1U);
    int offset = 0;
    for (const std::size_t trackIndex : displayRows_) {
        offset += trackRowHeight(tracks[trackIndex]);
        rowOffsets_.push_back(offset);
    }
}

int TimelineWidget::displayRowAt(const double y) const noexcept {
    const double content = y - tracksTop() + scrollY_;
    if (content < 0.0 || displayRows_.empty()) {
        return -1;
    }
    for (std::size_t row = 0; row < displayRows_.size(); ++row) {
        if (content < static_cast<double>(rowOffsets_[row + 1U])) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

double TimelineWidget::rowTop(const int displayRow) const noexcept {
    if (displayRow < 0 || static_cast<std::size_t>(displayRow) >= displayRows_.size()) {
        return tracksTop();
    }
    return tracksTop() + rowOffsets_[static_cast<std::size_t>(displayRow)] - scrollY_;
}

int TimelineWidget::rowHeight(const int displayRow) const noexcept {
    if (displayRow < 0 || static_cast<std::size_t>(displayRow) >= displayRows_.size()) {
        return trackHeight;
    }
    return rowOffsets_[static_cast<std::size_t>(displayRow) + 1U] -
           rowOffsets_[static_cast<std::size_t>(displayRow)];
}

void TimelineWidget::updateVerticalScrollBar() {
    if (verticalScroll_ == nullptr) {
        return;
    }
    const int barHeight = horizontalScroll_ == nullptr
                              ? 0
                              : horizontalScroll_->sizeHint().height();
    const int viewport = std::max(1, height() - tracksTop() - barHeight);
    const int content = rowOffsets_.empty() ? 0 : rowOffsets_.back();
    const int maximum = std::max(0, content - viewport);
    updatingVerticalScrollBar_ = true;
    verticalScroll_->setRange(0, maximum);
    verticalScroll_->setPageStep(viewport);
    verticalScroll_->setSingleStep(trackHeight / 2);
    scrollY_ = std::min(scrollY_, maximum);
    verticalScroll_->setValue(scrollY_);
    verticalScroll_->setVisible(maximum > 0);
    updatingVerticalScrollBar_ = false;
}

const core::Track* TimelineWidget::trackAtRow(const int displayRow) const noexcept {
    if (sequence_ == nullptr || displayRow < 0 ||
        static_cast<std::size_t>(displayRow) >= displayRows_.size()) {
        return nullptr;
    }
    return &sequence_->tracks()[displayRows_[static_cast<std::size_t>(displayRow)]];
}

int TimelineWidget::rowOfTrackIndex(const std::size_t trackIndex) const noexcept {
    const auto iterator = std::ranges::find(displayRows_, trackIndex);
    return iterator == displayRows_.end()
               ? -1
               : static_cast<int>(std::distance(displayRows_.begin(), iterator));
}

const core::Track* TimelineWidget::trackOfClip(const core::ClipId clipId) const noexcept {
    if (sequence_ == nullptr || !clipId) {
        return nullptr;
    }
    for (const core::Track& track : sequence_->tracks()) {
        if (std::ranges::any_of(track.clips, [clipId](const core::Clip& clip) {
                return clip.id == clipId;
            })) {
            return &track;
        }
    }
    return nullptr;
}

bool TimelineWidget::moveTargetValid(const int displayRow) const noexcept {
    const core::Track* target = trackAtRow(displayRow);
    const core::Track* own = trackOfClip(selectedClip_);
    const core::Clip* dragged =
        sequence_ == nullptr ? nullptr : sequence_->findClip(selectedClip_);
    if (target == nullptr || own == nullptr || dragged == nullptr ||
        target->kind != own->kind || target->locked) {
        return false;
    }
    for (const core::Clip& clip : target->clips) {
        if (isSelected(clip.id) ||
            (dragged->linkId && clip.linkId == dragged->linkId)) {
            continue;
        }
        if (clip.timeline.start < dragPreviewEnd_ &&
            clip.timeline.end() > dragPreviewFrame_) {
            return false;
        }
    }
    return true;
}

void TimelineWidget::setSequence(const core::Sequence* sequence) {
    sequence_ = sequence;
    rebuildDisplayRows();
    std::erase_if(selectedClips_, [this](const core::ClipId clipId) {
        return sequence_ == nullptr || sequence_->findClip(clipId) == nullptr;
    });
    if (selectedClip_ && (sequence_ == nullptr || sequence_->findClip(selectedClip_) == nullptr)) {
        selectedClip_ = selectedClips_.empty() ? core::ClipId{} : selectedClips_.back();
    }
    if (selectedCaption_) {
        const bool captionExists =
            sequence_ != nullptr &&
            std::ranges::any_of(sequence_->captions(),
                                [this](const core::Caption& caption) {
                                    return caption.id == selectedCaption_;
                                });
        if (!captionExists) {
            selectedCaption_ = {};
            captionDragging_ = false;
        }
    }
    update();
}

void TimelineWidget::setMoveClipHandler(MoveClipHandler handler) {
    moveClipHandler_ = std::move(handler);
}

void TimelineWidget::setMoveToNewTrackHandler(MoveToNewTrackHandler handler) {
    moveToNewTrackHandler_ = std::move(handler);
}

void TimelineWidget::setDuplicateClipHandler(DuplicateClipHandler handler) {
    duplicateClipHandler_ = std::move(handler);
}

void TimelineWidget::setSplitClipHandler(SplitClipHandler handler) {
    splitClipHandler_ = std::move(handler);
}

void TimelineWidget::setLiftClipHandler(ClipHandler handler) {
    liftClipHandler_ = std::move(handler);
}

void TimelineWidget::setRippleDeleteHandler(ClipHandler handler) {
    rippleDeleteHandler_ = std::move(handler);
}

void TimelineWidget::setTrimClipHandler(TrimClipHandler handler) {
    trimClipHandler_ = std::move(handler);
}

void TimelineWidget::setTrimPreviewHandler(TrimPreviewHandler handler) {
    trimPreviewHandler_ = std::move(handler);
}

void TimelineWidget::setCaptionMoveHandler(CaptionMoveHandler handler) {
    captionMoveHandler_ = std::move(handler);
}

void TimelineWidget::setCaptionEditHandler(CaptionHandler handler) {
    captionEditHandler_ = std::move(handler);
}

void TimelineWidget::setCaptionDeleteHandler(CaptionHandler handler) {
    captionDeleteHandler_ = std::move(handler);
}

void TimelineWidget::setCaptionConvertHandler(CaptionHandler handler) {
    captionConvertHandler_ = std::move(handler);
}

void TimelineWidget::setMarkerMoveHandler(MarkerMoveHandler handler) {
    markerMoveHandler_ = std::move(handler);
}

void TimelineWidget::setMarkerContextHandler(MarkerContextHandler handler) {
    markerContextHandler_ = std::move(handler);
}

void TimelineWidget::setMarkerEditHandler(MarkerHandler handler) {
    markerEditHandler_ = std::move(handler);
}

core::MarkerId TimelineWidget::markerIdAt(const QPointF& position) const {
    if (sequence_ == nullptr || position.x() < static_cast<double>(trackLabelWidth) ||
        position.y() < static_cast<double>(rulerHeight) - 10.0 ||
        position.y() > static_cast<double>(rulerHeight) + 10.0) {
        return {};
    }
    for (const core::Marker& marker : sequence_->markers()) {
        const double x = frameToX(marker.position);
        if (std::abs(position.x() - x) <= 6.0) {
            return marker.id;
        }
    }
    return {};
}

void TimelineWidget::setAssetLabels(std::unordered_map<std::uint64_t, QString> labels) {
    assetLabels_ = std::move(labels);
    update();
}

void TimelineWidget::setAssetThumbnails(
    std::unordered_map<std::uint64_t, QImage> thumbnails) {
    assetThumbnails_ = std::move(thumbnails);
    update();
}

void TimelineWidget::setTrackReorderHandler(TrackReorderHandler handler) {
    trackReorderHandler_ = std::move(handler);
}

void TimelineWidget::setFadeHandler(FadeHandler handler) {
    fadeHandler_ = std::move(handler);
}

void TimelineWidget::setTransitionHandler(TransitionHandler handler) {
    transitionHandler_ = std::move(handler);
}

void TimelineWidget::setGainHandler(GainHandler handler) {
    gainHandler_ = std::move(handler);
}

void TimelineWidget::setSlipHandler(DeltaEditHandler handler) {
    slipHandler_ = std::move(handler);
}

void TimelineWidget::setRollHandler(DeltaEditHandler handler) {
    rollHandler_ = std::move(handler);
}

void TimelineWidget::setRippleTrimHandler(DeltaEditHandler handler) {
    rippleTrimHandler_ = std::move(handler);
}

void TimelineWidget::setSlideHandler(DeltaEditHandler handler) {
    slideHandler_ = std::move(handler);
}

void TimelineWidget::setTool(const Tool tool) {
    tool_ = tool;
    switch (tool_) {
    case Tool::Razor:
    case Tool::Zoom:
        setCursor(Qt::CrossCursor);
        break;
    case Tool::Slip:
    case Tool::Rolling:
    case Tool::Ripple:
    case Tool::Slide:
        setCursor(Qt::SizeHorCursor);
        break;
    case Tool::Hand:
        setCursor(Qt::OpenHandCursor);
        break;
    case Tool::Selection:
        unsetCursor();
        break;
    }
}

TimelineWidget::Tool TimelineWidget::tool() const noexcept {
    return tool_;
}

void TimelineWidget::setPlayheadHandler(PlayheadHandler handler) {
    playheadHandler_ = std::move(handler);
}

void TimelineWidget::setTransportHandler(TransportHandler handler) {
    transportHandler_ = std::move(handler);
}

void TimelineWidget::setTrackLockHandler(TrackStateHandler handler) {
    trackLockHandler_ = std::move(handler);
}

void TimelineWidget::setTrackSyncLockHandler(TrackStateHandler handler) {
    trackSyncLockHandler_ = std::move(handler);
}

void TimelineWidget::setTrackMuteHandler(TrackStateHandler handler) {
    trackMuteHandler_ = std::move(handler);
}

void TimelineWidget::setTrackEnabledHandler(TrackStateHandler handler) {
    trackEnabledHandler_ = std::move(handler);
}

void TimelineWidget::setTrackTargetHandler(TrackTargetHandler handler) {
    trackTargetHandler_ = std::move(handler);
}

void TimelineWidget::setTargetTracks(const core::TrackId videoTrack,
                                     const core::TrackId audioTrack) {
    targetedVideoTrack_ = videoTrack;
    targetedAudioTrack_ = audioTrack;
    update();
}

void TimelineWidget::setSelectionHandler(SelectionHandler handler) {
    selectionHandler_ = std::move(handler);
}

void TimelineWidget::setContextActionHandler(ContextActionHandler handler) {
    contextActionHandler_ = std::move(handler);
}

void TimelineWidget::setTrackActionHandler(TrackActionHandler handler) {
    trackActionHandler_ = std::move(handler);
}

void TimelineWidget::setAssetDropHandler(AssetDropHandler handler) {
    assetDropHandler_ = std::move(handler);
}

void TimelineWidget::setEffectDropHandler(EffectDropHandler handler) {
    effectDropHandler_ = std::move(handler);
}

const std::vector<core::ClipId>& TimelineWidget::selectedClipIds() const noexcept {
    return selectedClips_;
}

void TimelineWidget::setSelectedClipIds(std::vector<core::ClipId> clipIds) {
    std::erase_if(clipIds, [this](const core::ClipId id) {
        return !id || sequence_ == nullptr || sequence_->findClip(id) == nullptr;
    });
    selectedClips_ = std::move(clipIds);
    selectedClip_ = selectedClips_.empty() ? core::ClipId{} : selectedClips_.back();
    notifySelection();
    update();
}

bool TimelineWidget::isSelected(const core::ClipId clipId) const noexcept {
    return std::ranges::find(selectedClips_, clipId) != selectedClips_.end();
}

void TimelineWidget::notifySelection() {
    if (selectionHandler_) {
        selectionHandler_(selectedClips_);
    }
}

void TimelineWidget::setPreviewCacheState(const core::Frame start, const core::Frame duration,
                                          const bool valid) {
    previewCacheStart_ = start;
    previewCacheDuration_ = duration;
    previewCacheValid_ = valid;
    update();
}

void TimelineWidget::setPlayheadFrame(const core::Frame frame, const bool notify) {
    playheadFrame_ = std::max<core::Frame>(0, frame);
    followPlayhead();
    if (notify && playheadHandler_) {
        playheadHandler_(playheadFrame_);
    }
    update();
}

void TimelineWidget::setInOutRange(const core::Frame inFrame, const core::Frame outFrame) {
    inFrame_ = inFrame;
    outFrame_ = outFrame;
    update();
}

core::Frame TimelineWidget::playheadFrame() const noexcept {
    return playheadFrame_;
}

void TimelineWidget::setWaveform(const core::AssetId assetId,
                                 const core::Frame sourceDuration,
                                 std::vector<float> peaks) {
    if (!assetId || sourceDuration <= 0 || peaks.empty()) {
        return;
    }
    waveforms_[assetId.value] = {
        .sourceDuration = sourceDuration,
        .peaks = std::move(peaks),
    };
    update();
}

void TimelineWidget::clearWaveforms() {
    waveforms_.clear();
    update();
}

double TimelineWidget::frameToX(const core::Frame frame) const noexcept {
    return static_cast<double>(trackLabelWidth) +
           static_cast<double>(frame - scrollFrame_) * pixelsPerFrame_;
}

core::Frame TimelineWidget::xToFrame(const double x) const noexcept {
    const double frame = (x - static_cast<double>(trackLabelWidth)) / pixelsPerFrame_ +
                         static_cast<double>(scrollFrame_);
    return static_cast<core::Frame>(std::floor(frame));
}

core::Frame TimelineWidget::snapFrame(const core::Frame frame,
                                      const core::ClipId ignoredClip) const noexcept {
    if (sequence_ == nullptr) {
        return frame;
    }
    const double thresholdFrames = 8.0 / pixelsPerFrame_;
    const core::Frame threshold =
        thresholdFrames < 1.0 ? 0 : static_cast<core::Frame>(std::ceil(thresholdFrames));
    core::Frame closest = frame;
    core::Frame closestDistance = threshold + 1;
    auto consider = [&](const core::Frame candidate) {
        const core::Frame distance = std::abs(candidate - frame);
        if (distance <= threshold && distance < closestDistance) {
            closest = candidate;
            closestDistance = distance;
        }
    };
    consider(playheadFrame_);
    consider(0);
    if (inFrame_ >= 0) consider(inFrame_);
    if (outFrame_ >= 0) consider(outFrame_);
    core::Frame sequenceEnd = 0;
    for (const core::Marker& marker : sequence_->markers()) consider(marker.position);
    for (const core::Caption& caption : sequence_->captions()) {
        consider(caption.timeline.start);
        consider(caption.timeline.end());
        sequenceEnd = std::max(sequenceEnd, caption.timeline.end());
    }
    const core::Clip* ignored = sequence_->findClip(ignoredClip);
    const core::LinkId ignoredLink = ignored == nullptr ? core::LinkId{} : ignored->linkId;
    for (const core::Track& track : sequence_->tracks()) {
        for (const core::Clip& clip : track.clips) {
            sequenceEnd = std::max(sequenceEnd, clip.timeline.end());
            if (clip.id == ignoredClip || isSelected(clip.id) ||
                (ignoredLink && clip.linkId == ignoredLink)) {
                continue;
            }
            consider(clip.timeline.start);
            consider(clip.timeline.end());
            if (clip.videoTransitionInFrames > 0)
                consider(clip.timeline.start + clip.videoTransitionInFrames);
            if (clip.audioTransitionInFrames > 0)
                consider(clip.timeline.start + clip.audioTransitionInFrames);
        }
    }
    consider(sequenceEnd);
    return closest;
}

const core::Clip* TimelineWidget::clipAt(const QPointF& position) const noexcept {
    if (sequence_ == nullptr || position.x() < static_cast<double>(trackLabelWidth)) {
        return nullptr;
    }
    const core::Track* track = trackAtRow(displayRowAt(position.y()));
    if (track == nullptr) {
        return nullptr;
    }

    const core::Frame frame = xToFrame(position.x());
    const auto iterator = std::ranges::find_if(
        track->clips,
        [frame](const core::Clip& clip) { return clip.timeline.contains(frame); });
    return iterator == track->clips.end() ? nullptr : &*iterator;
}

const core::Clip* TimelineWidget::clipEdgeAt(const QPointF& position) const noexcept {
    // Edge grabs must work on the outside half of a clip boundary too: the
    // frame under the pointer is then past the clip, so clipAt() misses it.
    if (sequence_ == nullptr || position.x() < static_cast<double>(trackLabelWidth)) {
        return nullptr;
    }
    const core::Track* track = trackAtRow(displayRowAt(position.y()));
    if (track == nullptr) {
        return nullptr;
    }
    const core::Clip* nearest = nullptr;
    double nearestDistance = trimHandleWidth + 1.0;
    for (const core::Clip& clip : track->clips) {
        const double startDistance =
            std::abs(position.x() - frameToX(clip.timeline.start));
        const double endDistance =
            std::abs(position.x() - frameToX(clip.timeline.end()));
        const double distance = std::min(startDistance, endDistance);
        if (distance <= trimHandleWidth && distance < nearestDistance) {
            nearest = &clip;
            nearestDistance = distance;
        }
    }
    return nearest;
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::RightButton && sequence_ != nullptr &&
        event->position().x() < static_cast<double>(trackLabelWidth) &&
        event->position().y() >= static_cast<double>(tracksTop())) {
        const int row = displayRowAt(event->position().y());
        if (const core::Track* rowTrack = trackAtRow(row); rowTrack != nullptr) {
            const core::TrackId trackId = rowTrack->id;
            const std::size_t index = displayRows_[static_cast<std::size_t>(row)];
            QMenu menu(this);
            QAction* addVideo = menu.addAction(tr("Add Video Track"));
            QAction* addAudio = menu.addAction(tr("Add Audio Track"));
            menu.addSeparator();
            QAction* moveUp = menu.addAction(tr("Move Track Up"));
            QAction* moveDown = menu.addAction(tr("Move Track Down"));
            QAction* solo = nullptr;
            if (rowTrack->kind == core::TrackKind::Audio) {
                solo = menu.addAction(tr("Solo"));
                solo->setCheckable(true);
                solo->setChecked(rowTrack->solo);
            }
            menu.addSeparator();
            QAction* heightMinimal = menu.addAction(tr("Height: Minimal"));
            QAction* heightStandard = menu.addAction(tr("Height: Standard"));
            QAction* heightExpanded = menu.addAction(tr("Height: Expanded"));
            for (QAction* heightAction : {heightMinimal, heightStandard, heightExpanded}) {
                heightAction->setCheckable(true);
            }
            heightMinimal->setChecked(rowTrack->heightMode == 0);
            heightStandard->setChecked(rowTrack->heightMode == 1);
            heightExpanded->setChecked(rowTrack->heightMode == 2);
            menu.addSeparator();
            QAction* remove = menu.addAction(tr("Delete Track"));
            // Track moves stay inside the same kind group. The video group is
            // displayed reversed (front-most on top), so visual "up" maps to a
            // later array position for video and an earlier one for audio.
            const bool isVideo = rowTrack->kind == core::TrackKind::Video;
            const int upDelta = isVideo ? 1 : -1;
            const auto& allTracks = sequence_->tracks();
            const auto neighbourSameKind = [&](const int delta) {
                const std::int64_t neighbour =
                    static_cast<std::int64_t>(index) + delta;
                return neighbour >= 0 &&
                       neighbour < static_cast<std::int64_t>(allTracks.size()) &&
                       allTracks[static_cast<std::size_t>(neighbour)].kind ==
                           rowTrack->kind;
            };
            moveUp->setEnabled(neighbourSameKind(upDelta));
            moveDown->setEnabled(neighbourSameKind(-upDelta));
            QAction* chosen = menu.exec(event->globalPosition().toPoint());
            if (chosen != nullptr && trackActionHandler_) {
                TrackAction action = TrackAction::Delete;
                if (chosen == addVideo) action = TrackAction::AddVideo;
                else if (chosen == addAudio) action = TrackAction::AddAudio;
                else if (chosen == moveUp)
                    action = upDelta > 0 ? TrackAction::MoveDown : TrackAction::MoveUp;
                else if (chosen == moveDown)
                    action = upDelta > 0 ? TrackAction::MoveUp : TrackAction::MoveDown;
                else if (solo != nullptr && chosen == solo) action = TrackAction::ToggleSolo;
                else if (chosen == heightMinimal) action = TrackAction::HeightMinimal;
                else if (chosen == heightStandard) action = TrackAction::HeightStandard;
                else if (chosen == heightExpanded) action = TrackAction::HeightExpanded;
                else if (chosen == remove) action = TrackAction::Delete;
                trackActionHandler_(action, trackId);
            }
        }
        event->accept();
        return;
    }
    if (tool_ == Tool::Hand && event->button() == Qt::LeftButton) {
        handDragging_ = true;
        handLastX_ = event->position().x();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (tool_ == Tool::Zoom &&
        (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        const double mouseOffset = event->position().x() - static_cast<double>(trackLabelWidth);
        const double anchorFrame = mouseOffset / pixelsPerFrame_ + scrollFrame_;
        const double factor = event->button() == Qt::LeftButton ? 1.35 : 1.0 / 1.35;
        pixelsPerFrame_ = std::clamp(pixelsPerFrame_ * factor, 0.05, 80.0);
        scrollFrame_ = std::max<core::Frame>(0, static_cast<core::Frame>(std::floor(
            anchorFrame - mouseOffset / pixelsPerFrame_)));
        update();
        event->accept();
        return;
    }
    if (const core::MarkerId marker = markerIdAt(event->position()); marker) {
        if (event->button() == Qt::LeftButton) {
            markerDragging_ = true;
            markerDragId_ = marker;
            markerDragOrigin_ = -1;
            for (const core::Marker& candidate : sequence_->markers()) {
                if (candidate.id == marker) {
                    markerDragOrigin_ = candidate.position;
                    break;
                }
            }
            markerPreviewFrame_ = -1;
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton) {
            if (markerContextHandler_) {
                markerContextHandler_(marker, event->globalPosition().toPoint());
            }
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton &&
        event->position().x() >= static_cast<double>(trackLabelWidth) &&
        event->position().y() < static_cast<double>(rulerHeight)) {
        draggingPlayhead_ = true;
        setPlayheadFrame(xToFrame(event->position().x()));
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && sequence_ != nullptr &&
        event->position().x() >= static_cast<double>(trackLabelWidth) &&
        event->position().y() >= static_cast<double>(rulerHeight) &&
        event->position().y() < static_cast<double>(tracksTop())) {
        const core::Frame frame = xToFrame(event->position().x());
        const int subRow = std::clamp(
            static_cast<int>((event->position().y() - rulerHeight) / captionLaneHeight),
            0, captionLaneRows_ - 1);
        selectedCaption_ = {};
        for (const core::Caption& caption : sequence_->captions()) {
            if (caption.timeline.contains(frame) &&
                captionSubRow(caption.id) == subRow) {
                selectedCaption_ = caption.id;
                captionDragging_ = true;
                captionGrabOffset_ = frame - caption.timeline.start;
                captionDragOrigin_ = caption.timeline.start;
                captionPreviewStart_ = caption.timeline.start;
                break;
            }
        }
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton && sequence_ != nullptr &&
        event->position().x() >= static_cast<double>(trackLabelWidth) &&
        event->position().y() >= static_cast<double>(rulerHeight) &&
        event->position().y() < static_cast<double>(tracksTop())) {
        const core::Frame frame = xToFrame(event->position().x());
        const int subRow = std::clamp(
            static_cast<int>((event->position().y() - rulerHeight) / captionLaneHeight),
            0, captionLaneRows_ - 1);
        for (const core::Caption& caption : sequence_->captions()) {
            if (!caption.timeline.contains(frame) ||
                captionSubRow(caption.id) != subRow) {
                continue;
            }
            selectedCaption_ = caption.id;
            update();
            QMenu menu(this);
            QAction* convert = menu.addAction(tr("Convert to Title Clip"));
            QAction* editText = menu.addAction(tr("Edit Text..."));
            QAction* remove = menu.addAction(tr("Delete"));
            const core::CaptionId captionId = caption.id;
            QAction* chosen = menu.exec(event->globalPosition().toPoint());
            if (chosen == convert && captionConvertHandler_) {
                captionConvertHandler_(captionId);
            } else if (chosen == editText && captionEditHandler_) {
                captionEditHandler_(captionId);
            } else if (chosen == remove && captionDeleteHandler_) {
                captionDeleteHandler_(captionId);
                selectedCaption_ = {};
            }
            event->accept();
            return;
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && sequence_ != nullptr &&
        event->position().x() < static_cast<double>(trackLabelWidth)) {
        // Row-boundary drag in the label column resizes the track height.
        for (std::size_t row = 0; row < displayRows_.size(); ++row) {
            const double bottom = rowTop(static_cast<int>(row)) +
                                  rowHeight(static_cast<int>(row));
            if (std::abs(event->position().y() - bottom) <= 4.0) {
                heightDragging_ = true;
                heightDragRow_ = static_cast<int>(row);
                const core::Track* rowTrack = trackAtRow(static_cast<int>(row));
                heightDragMode_ = rowTrack == nullptr ? 1 : rowTrack->heightMode;
                setCursor(Qt::SizeVerCursor);
                event->accept();
                return;
            }
        }
    }
    if (event->button() == Qt::LeftButton && sequence_ != nullptr &&
        event->position().x() >= 110.0 &&
        event->position().x() < static_cast<double>(trackLabelWidth) &&
        event->position().y() >= static_cast<double>(tracksTop())) {
        // Dragging the name area of a track header reorders the track
        // within its own kind group.
        const int row = displayRowAt(event->position().y());
        if (trackAtRow(row) != nullptr && trackReorderHandler_) {
            trackDragging_ = true;
            trackDragRow_ = row;
            trackDropRow_ = row;
            setCursor(Qt::ClosedHandCursor);
            update();
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && sequence_ != nullptr &&
        event->position().x() < static_cast<double>(trackLabelWidth) &&
        event->position().y() >= static_cast<double>(tracksTop())) {
        if (const core::Track* rowTrack = trackAtRow(displayRowAt(event->position().y()));
            rowTrack != nullptr) {
            const core::Track& track = *rowTrack;
            if (event->position().x() < 28.0 && trackLockHandler_) {
                trackLockHandler_(track.id, !track.locked);
            } else if (event->position().x() < 56.0) {
                if (track.kind == core::TrackKind::Audio && trackMuteHandler_) {
                    trackMuteHandler_(track.id, !track.muted);
                } else if (track.kind == core::TrackKind::Video && trackEnabledHandler_) {
                    trackEnabledHandler_(track.id, !track.enabled);
                }
            } else if (event->position().x() < 82.0 && trackTargetHandler_) {
                trackTargetHandler_(track.id);
            } else if (event->position().x() < 110.0 && trackSyncLockHandler_) {
                trackSyncLockHandler_(track.id, !track.syncLocked);
            }
        }
        event->accept();
        return;
    }

    const core::Clip* clip = clipAt(event->position());
    if (clip == nullptr && tool_ == Tool::Selection) {
        clip = clipEdgeAt(event->position());
    }
    if (tool_ == Tool::Razor && event->button() == Qt::LeftButton && clip != nullptr) {
        selectedClips_ = {clip->id};
        selectedClip_ = clip->id;
        notifySelection();
        if (splitClipHandler_) {
            splitClipHandler_(clip->id, xToFrame(event->position().x()));
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        if (clip != nullptr && !isSelected(clip->id)) {
            selectedClips_ = {clip->id};
            selectedClip_ = clip->id;
            notifySelection();
        }
        QMenu menu(this);
        QAction* split = menu.addAction(tr("Split at Playhead"));
        QAction* cut = menu.addAction(tr("Cut"));
        QAction* copy = menu.addAction(tr("Copy"));
        QAction* paste = menu.addAction(tr("Paste at Playhead"));
        QAction* duplicate = menu.addAction(tr("Duplicate"));
        menu.addSeparator();
        QAction* addFadeIn = menu.addAction(tr("Add Fade In"));
        QAction* addFadeOut = menu.addAction(tr("Add Fade Out"));
        QAction* removeFades = menu.addAction(tr("Remove Fades"));
        QAction* addDissolve = menu.addAction(tr("Add Cross Dissolve"));
        QAction* addAudioCrossfade = menu.addAction(tr("Add Audio Crossfade"));
        QAction* removeTransitions = menu.addAction(tr("Remove Transitions"));
        menu.addSeparator();
        QAction* addGainKey = menu.addAction(tr("Add/Update Gain Keyframe at Playhead"));
        QAction* removeGainKey = menu.addAction(tr("Remove Gain Keyframe at Playhead"));
        QAction* addMotionKey = menu.addAction(tr("Add/Update Motion Keyframe at Playhead"));
        QAction* removeMotionKey = menu.addAction(tr("Remove Motion Keyframe at Playhead"));
        menu.addSeparator();
        QAction* linkSelected = menu.addAction(tr("Link Selected Clips"));
        QAction* unlink = menu.addAction(tr("Unlink Audio/Video"));
        QAction* resetTransform = menu.addAction(tr("Reset Transform/Crop/Mask"));
        QAction* resetEffects = menu.addAction(tr("Remove All Effects"));
        menu.addSeparator();
        QAction* lift = menu.addAction(tr("Delete (Lift)"));
        QAction* ripple = menu.addAction(tr("Ripple Delete"));
        const bool hasClip = clip != nullptr;
        for (QAction* action : {split, cut, copy, duplicate, addFadeIn, addFadeOut,
                                removeFades, addDissolve, addAudioCrossfade,
                                removeTransitions, unlink, resetTransform, resetEffects,
                                lift, ripple}) {
            action->setEnabled(hasClip);
        }
        linkSelected->setEnabled(selectedClips_.size() >= 2U);
        bool audioClip = false;
        if (hasClip && sequence_ != nullptr) {
            for (const core::Track& track : sequence_->tracks()) {
                if (std::ranges::any_of(track.clips, [clip](const core::Clip& candidate) {
                        return candidate.id == clip->id;
                    })) {
                    audioClip = track.kind == core::TrackKind::Audio;
                    break;
                }
            }
        }
        const bool playheadInsideClip = hasClip && clip->timeline.contains(playheadFrame_);
        addGainKey->setEnabled(playheadInsideClip && audioClip);
        removeGainKey->setEnabled(playheadInsideClip && audioClip &&
                                  !clip->gainKeyframes.empty());
        addMotionKey->setEnabled(playheadInsideClip && !audioClip);
        removeMotionKey->setEnabled(playheadInsideClip && !audioClip &&
                                    !clip->motionKeyframes.empty());
        // Copy the id before exec(): the modal loop can dispatch worker signals
        // that mutate the clips vector and dangle the raw pointer.
        const core::ClipId contextClipId = clip == nullptr ? core::ClipId{} : clip->id;
        QAction* chosen = menu.exec(event->globalPosition().toPoint());
        if (chosen != nullptr && contextActionHandler_) {
            ContextAction action = ContextAction::Paste;
            if (chosen == paste) action = ContextAction::Paste;
            else if (chosen == split) action = ContextAction::Split;
            else if (chosen == cut) action = ContextAction::Cut;
            else if (chosen == copy) action = ContextAction::Copy;
            else if (chosen == duplicate) action = ContextAction::Duplicate;
            else if (chosen == addFadeIn) action = ContextAction::AddFadeIn;
            else if (chosen == addFadeOut) action = ContextAction::AddFadeOut;
            else if (chosen == removeFades) action = ContextAction::RemoveFades;
            else if (chosen == addDissolve) action = ContextAction::AddCrossDissolve;
            else if (chosen == addAudioCrossfade) action = ContextAction::AddAudioCrossfade;
            else if (chosen == removeTransitions) action = ContextAction::RemoveTransitions;
            else if (chosen == addGainKey) action = ContextAction::AddGainKeyframe;
            else if (chosen == removeGainKey) action = ContextAction::RemoveGainKeyframe;
            else if (chosen == addMotionKey) action = ContextAction::AddMotionKeyframe;
            else if (chosen == removeMotionKey) action = ContextAction::RemoveMotionKeyframe;
            else if (chosen == linkSelected) action = ContextAction::LinkSelected;
            else if (chosen == unlink) action = ContextAction::Unlink;
            else if (chosen == resetTransform) action = ContextAction::ResetTransform;
            else if (chosen == resetEffects) action = ContextAction::ResetEffects;
            else if (chosen == lift) action = ContextAction::Lift;
            else if (chosen == ripple) action = ContextAction::RippleDelete;
            contextActionHandler_(action, contextClipId, playheadFrame_);
        }
        event->accept();
        return;
    }
    const Qt::KeyboardModifier selectionModifier =
#if defined(Q_OS_MACOS)
        Qt::MetaModifier;
#else
        Qt::ControlModifier;
#endif
    const bool additive =
        (event->modifiers() & (selectionModifier | Qt::ShiftModifier)) != 0;
    if (event->button() == Qt::LeftButton && clip == nullptr) {
        if (!additive) {
            selectedClips_.clear();
            selectedClip_ = {};
            notifySelection();
        }
        boxSelecting_ = true;
        preserveSelectionForBox_ = additive;
        boxStart_ = event->position();
        boxSelection_ = QRectF(boxStart_, boxStart_);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && clip != nullptr) {
        if (additive) {
            const auto existing = std::ranges::find(selectedClips_, clip->id);
            if (existing == selectedClips_.end()) {
                selectedClips_.push_back(clip->id);
                selectedClip_ = clip->id;
            } else if ((event->modifiers() & selectionModifier) != 0) {
                selectedClips_.erase(existing);
                selectedClip_ = selectedClips_.empty() ? core::ClipId{} : selectedClips_.back();
            }
        } else if (!isSelected(clip->id)) {
            selectedClips_ = {clip->id};
            selectedClip_ = clip->id;
            if ((event->modifiers() & Qt::AltModifier) == 0 && clip->linkId) {
                selectedClips_.clear();
                for (const core::Track& track : sequence_->tracks()) {
                    for (const core::Clip& linked : track.clips) {
                        if (linked.linkId == clip->linkId) selectedClips_.push_back(linked.id);
                    }
                }
                selectedClip_ = clip->id;
            }
        } else {
            selectedClip_ = clip->id;
        }
        notifySelection();
    }
    if (event->button() == Qt::LeftButton && clip != nullptr) {
        const double clipLeft = frameToX(clip->timeline.start);
        const double clipRight = frameToX(clip->timeline.end());
        const int row = displayRowAt(event->position().y());
        const double clipTop = rowTop(row) + clipInset;
        const double fadeInX = clipLeft + clip->fadeInFrames * pixelsPerFrame_;
        const double fadeOutX = clipRight - clip->fadeOutFrames * pixelsPerFrame_;
        const core::Track* rowTrack = trackAtRow(row);
        const bool audioClip =
            rowTrack != nullptr && rowTrack->kind == core::TrackKind::Audio;
        const double clipHeight = rowHeight(row) - clipInset * 2.0;
        const core::Frame clickedLocal = std::clamp<core::Frame>(
            xToFrame(event->position().x()) - clip->timeline.start, 0,
            clip->timeline.duration - 1);
        const double gainY = clipTop + clipHeight -
                             (gainAt(*clip, clickedLocal) + 60.0) / 84.0 * clipHeight;
        if (tool_ == Tool::Slip) {
            dragMode_ = DragMode::Slip;
        } else if (tool_ == Tool::Rolling) {
            dragMode_ = DragMode::Roll;
        } else if (tool_ == Tool::Ripple) {
            dragMode_ = DragMode::RippleEnd;
        } else if (tool_ == Tool::Slide) {
            dragMode_ = DragMode::Slide;
        } else {
            const core::Frame transitionFrames = audioClip ? clip->audioTransitionInFrames
                                                           : clip->videoTransitionInFrames;
            const double transitionEndX = clipLeft + transitionFrames * pixelsPerFrame_;
            if (transitionFrames > 0 &&
                std::abs(event->position().x() - transitionEndX) <= 8.0 &&
                event->position().y() <= clipTop + 18.0) {
                dragMode_ = DragMode::TransitionIn;
            } else if (audioClip && std::abs(event->position().y() - gainY) <= 5.0) {
                dragMode_ = DragMode::Gain;
            } else if (event->position().y() <= clipTop + 12.0 &&
                       std::abs(event->position().x() - fadeInX) <= 8.0) {
                dragMode_ = DragMode::FadeIn;
            } else if (event->position().y() <= clipTop + 12.0 &&
                       std::abs(event->position().x() - fadeOutX) <= 8.0) {
                dragMode_ = DragMode::FadeOut;
            } else if (std::abs(event->position().x() - clipLeft) <= trimHandleWidth) {
                dragMode_ = DragMode::TrimStart;
            } else if (std::abs(event->position().x() - clipRight) <= trimHandleWidth) {
                dragMode_ = DragMode::TrimEnd;
            } else {
                dragMode_ = DragMode::Move;
            }
        }
        const core::Frame transitionFrames = audioClip ? clip->audioTransitionInFrames
                                                       : clip->videoTransitionInFrames;
        dragStartFrame_ = xToFrame(event->position().x()) - clip->timeline.start;
        dragPreviewFrame_ = clip->timeline.start;
        dragPreviewEnd_ = clip->timeline.end();
        movePreviewRow_ = dragMode_ == DragMode::Move ? row : -1;
        dragPreviewFadeIn_ = clip->fadeInFrames;
        dragPreviewFadeOut_ = clip->fadeOutFrames;
        dragPreviewTransitionIn_ = transitionFrames;
        dragPreviewGainDb_ = gainAt(*clip, clickedLocal);
        dragPreviewDelta_ = 0;
    }
    update();
    event->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragMode_ != DragMode::None || captionDragging_ || markerDragging_ ||
        boxSelecting_ || draggingPlayhead_) {
        lastDragPosition_ = event->position();
        updateAutoScroll(event->position());
    }
    if (heightDragging_) {
        const double desired = event->position().y() - rowTop(heightDragRow_);
        heightDragMode_ = desired < 48.0 ? 0 : desired < 79.0 ? 1 : 2;
        setCursor(Qt::SizeVerCursor);
        update();
        event->accept();
        return;
    }
    if (trackDragging_) {
        // Snap the drop row to the nearest row of the dragged track's kind.
        const core::Track* dragged = trackAtRow(trackDragRow_);
        const int hovered = displayRowAt(event->position().y());
        if (dragged != nullptr && hovered >= 0) {
            const core::Track* hoveredTrack = trackAtRow(hovered);
            if (hoveredTrack != nullptr && hoveredTrack->kind == dragged->kind) {
                trackDropRow_ = hovered;
            }
        }
        update();
        event->accept();
        return;
    }
    if (handDragging_) {
        const double deltaPixels = event->position().x() - handLastX_;
        handLastX_ = event->position().x();
        scrollFrame_ = std::max<core::Frame>(
            0, scrollFrame_ - static_cast<core::Frame>(std::llround(deltaPixels /
                                                                     pixelsPerFrame_)));
        update();
        event->accept();
        return;
    }
    if (draggingPlayhead_) {
        setPlayheadFrame(xToFrame(event->position().x()));
        event->accept();
        return;
    }
    if (boxSelecting_) {
        boxSelection_ = QRectF(boxStart_, event->position()).normalized();
        update();
        event->accept();
        return;
    }
    if (markerDragging_) {
        const core::Frame raw = std::max<core::Frame>(0, xToFrame(event->position().x()));
        core::Frame desired = raw;
        snapGuideFrame_ = -1;
        if (snapEnabled_ && (event->modifiers() & Qt::AltModifier) == 0) {
            const core::Frame snapped = snapFrame(desired, core::ClipId{});
            if (snapped != desired) {
                desired = snapped;
                snapGuideFrame_ = snapped;
            }
        }
        // The dragged marker is itself a snap candidate: never snap back to
        // its origin or small moves would appear to do nothing.
        if (desired == markerDragOrigin_ && raw != desired) {
            desired = raw;
            snapGuideFrame_ = -1;
        }
        markerPreviewFrame_ = desired;
        update();
        event->accept();
        return;
    }
    if (captionDragging_) {
        const core::Frame raw = std::max<core::Frame>(
            0, xToFrame(event->position().x()) - captionGrabOffset_);
        core::Frame desired = raw;
        snapGuideFrame_ = -1;
        if (snapEnabled_ && (event->modifiers() & Qt::AltModifier) == 0) {
            const core::Frame snapped = snapFrame(desired, core::ClipId{});
            if (snapped != desired) {
                desired = snapped;
                snapGuideFrame_ = snapped;
            }
        }
        if (desired == captionDragOrigin_ && raw != desired) {
            desired = raw;
            snapGuideFrame_ = -1;
        }
        captionPreviewStart_ = desired;
        update();
        event->accept();
        return;
    }
    if (dragMode_ == DragMode::None || sequence_ == nullptr || !selectedClip_) {
        if (sequence_ != nullptr &&
            event->position().x() < static_cast<double>(trackLabelWidth) &&
            (event->buttons() & Qt::LeftButton) == 0) {
            // Track-height handle: hovering a row boundary in the label column.
            bool nearBoundary = false;
            for (std::size_t row = 0; row < displayRows_.size(); ++row) {
                const double bottom = rowTop(static_cast<int>(row)) +
                                      rowHeight(static_cast<int>(row));
                if (std::abs(event->position().y() - bottom) <= 4.0) {
                    nearBoundary = true;
                    break;
                }
            }
            if (nearBoundary) {
                setCursor(Qt::SizeVerCursor);
            } else {
                unsetCursor();
            }
            QWidget::mouseMoveEvent(event);
            return;
        }
        if (sequence_ != nullptr && tool_ == Tool::Selection &&
            (event->buttons() & Qt::LeftButton) == 0) {
            const core::Clip* hovered = clipAt(event->position());
            if (hovered != nullptr) {
                const double clipLeft = frameToX(hovered->timeline.start);
                const double clipRight = frameToX(hovered->timeline.end());
                if (std::abs(event->position().x() - clipLeft) <= trimHandleWidth ||
                    std::abs(event->position().x() - clipRight) <= trimHandleWidth) {
                    setCursor(Qt::SizeHorCursor);
                } else {
                    unsetCursor();
                }
            } else {
                unsetCursor();
            }
        }
        QWidget::mouseMoveEvent(event);
        return;
    }
    const core::Clip* selected = sequence_->findClip(selectedClip_);
    if (selected == nullptr) {
        if (trimPreviewHandler_ &&
            (dragMode_ == DragMode::TrimStart || dragMode_ == DragMode::TrimEnd ||
             dragMode_ == DragMode::Roll || dragMode_ == DragMode::RippleEnd)) {
            trimPreviewHandler_(selectedClip_, 0, false, false);
        }
        dragMode_ = DragMode::None;
        return;
    }

    const core::Frame mouseFrame = std::max<core::Frame>(0, xToFrame(event->position().x()));
    snapGuideFrame_ = -1;
    const bool snappingEnabled =
        snapEnabled_ && (event->modifiers() & Qt::AltModifier) == 0;
    if (dragMode_ == DragMode::Move) {
        const core::Frame duration = dragPreviewEnd_ - dragPreviewFrame_;
        const core::Frame rawStart = std::max<core::Frame>(0, mouseFrame - dragStartFrame_);
        core::Frame desiredStart = rawStart;
        if (snappingEnabled) {
            const core::Frame snappedStart = snapFrame(desiredStart, selectedClip_);
            const core::Frame snappedEnd = snapFrame(desiredStart + duration, selectedClip_);
            if (snappedStart != desiredStart) {
                desiredStart = snappedStart;
                snapGuideFrame_ = snappedStart;
            } else if (snappedEnd != desiredStart + duration) {
                desiredStart = std::max<core::Frame>(0, snappedEnd - duration);
                snapGuideFrame_ = snappedEnd;
            }
        }
        // Snapping must never pin a drag to its origin (after a split the old
        // edge and the playhead sit exactly there); prefer the raw position.
        if (desiredStart == selected->timeline.start && rawStart != desiredStart) {
            desiredStart = rawStart;
            snapGuideFrame_ = -1;
        }
        dragPreviewFrame_ = desiredStart;
        dragPreviewEnd_ = dragPreviewFrame_ + duration;
        // Live vertical preview: remember the hovered row and show whether the
        // clip may land there; other-kind rows get the forbidden cursor. The
        // band above the top row offers a new front-most video track.
        const int hoveredRow = displayRowAt(event->position().y());
        const core::Track* ownTrack = trackOfClip(selectedClip_);
        if (hoveredRow >= 0) {
            movePreviewRow_ = hoveredRow;
        } else if (event->position().y() >= static_cast<double>(rulerHeight) &&
                   event->position().y() < static_cast<double>(tracksTop()) &&
                   ownTrack != nullptr && ownTrack->kind == core::TrackKind::Video &&
                   moveToNewTrackHandler_) {
            movePreviewRow_ = -2;
        }
        const core::Track* hoverTrack = trackAtRow(movePreviewRow_);
        if (hoverTrack != nullptr && ownTrack != nullptr &&
            hoverTrack->kind != ownTrack->kind) {
            setCursor(Qt::ForbiddenCursor);
        } else {
            setCursor(Qt::ClosedHandCursor);
        }
    } else if (dragMode_ == DragMode::TrimStart) {
        const core::Frame snapped = snappingEnabled ? snapFrame(mouseFrame, selectedClip_) : mouseFrame;
        dragPreviewFrame_ = std::min(snapped, dragPreviewEnd_ - 1);
        snapGuideFrame_ = snapped != mouseFrame ? snapped : -1;
    } else if (dragMode_ == DragMode::TrimEnd) {
        const core::Frame snapped = snappingEnabled ? snapFrame(mouseFrame, selectedClip_) : mouseFrame;
        dragPreviewEnd_ = std::max(snapped, dragPreviewFrame_ + 1);
        snapGuideFrame_ = snapped != mouseFrame ? snapped : -1;
    } else if (dragMode_ == DragMode::FadeIn) {
        dragPreviewFadeIn_ = std::clamp<core::Frame>(
            mouseFrame - selected->timeline.start, 0, selected->timeline.duration);
    } else if (dragMode_ == DragMode::FadeOut) {
        dragPreviewFadeOut_ = std::clamp<core::Frame>(
            selected->timeline.end() - mouseFrame, 0, selected->timeline.duration);
    } else if (dragMode_ == DragMode::TransitionIn) {
        dragPreviewTransitionIn_ = std::clamp<core::Frame>(
            mouseFrame - selected->timeline.start, 1, selected->timeline.duration);
    } else if (dragMode_ == DragMode::Gain) {
        const int row = std::max(0, displayRowAt(event->position().y()));
        const double top = rowTop(row) + clipInset;
        const double clipHeight = rowHeight(row) - clipInset * 2.0;
        const double normalized = std::clamp((top + clipHeight - event->position().y()) /
                                                 clipHeight,
                                             0.0, 1.0);
        dragPreviewGainDb_ = normalized * 84.0 - 60.0;
    } else if (dragMode_ == DragMode::Slip || dragMode_ == DragMode::Roll ||
               dragMode_ == DragMode::RippleEnd || dragMode_ == DragMode::Slide) {
        const core::Frame initialMouseFrame = selected->timeline.start + dragStartFrame_;
        dragPreviewDelta_ = mouseFrame - initialMouseFrame;
    }
    if (dragMode_ == DragMode::Move) {
        QToolTip::showText(event->globalPosition().toPoint(),
                           tr("Start %1  End %2  Delta %3")
                               .arg(dragPreviewFrame_)
                               .arg(dragPreviewEnd_)
                               .arg(dragPreviewFrame_ - selected->timeline.start),
                           this);
    } else if (dragMode_ == DragMode::TrimStart || dragMode_ == DragMode::TrimEnd) {
        QToolTip::showText(event->globalPosition().toPoint(),
                           tr("Start %1  End %2  Duration %3")
                               .arg(dragPreviewFrame_)
                               .arg(dragPreviewEnd_)
                               .arg(dragPreviewEnd_ - dragPreviewFrame_),
                           this);
        if (trimPreviewHandler_) {
            trimPreviewHandler_(selectedClip_,
                                dragMode_ == DragMode::TrimStart ? dragPreviewFrame_
                                                                 : dragPreviewEnd_ - 1,
                                false, true);
        }
    } else if (dragMode_ == DragMode::Slip || dragMode_ == DragMode::Roll ||
               dragMode_ == DragMode::RippleEnd || dragMode_ == DragMode::Slide) {
        QToolTip::showText(event->globalPosition().toPoint(),
                           tr("Delta %1 frames").arg(dragPreviewDelta_), this);
        if (trimPreviewHandler_ &&
            (dragMode_ == DragMode::Roll || dragMode_ == DragMode::RippleEnd)) {
            trimPreviewHandler_(selectedClip_,
                                selected->timeline.end() - 1 + dragPreviewDelta_,
                                dragMode_ == DragMode::Roll, true);
        }
    }
    update();
    event->accept();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    autoScrollStepX_ = 0;
    autoScrollStepY_ = 0;
    if (autoScrollTimer_ != nullptr) {
        autoScrollTimer_->stop();
    }
    if (event->button() == Qt::LeftButton && trackDragging_) {
        trackDragging_ = false;
        unsetCursor();
        const core::Track* dragged = trackAtRow(trackDragRow_);
        const core::Track* target = trackAtRow(trackDropRow_);
        if (dragged != nullptr && target != nullptr && trackReorderHandler_ &&
            trackDropRow_ != trackDragRow_ && target->kind == dragged->kind) {
            const auto sourceIndex = static_cast<int>(
                displayRows_[static_cast<std::size_t>(trackDragRow_)]);
            const auto targetIndex = static_cast<int>(
                displayRows_[static_cast<std::size_t>(trackDropRow_)]);
            trackReorderHandler_(dragged->id, targetIndex - sourceIndex);
        }
        trackDragRow_ = -1;
        trackDropRow_ = -1;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && heightDragging_) {
        heightDragging_ = false;
        unsetCursor();
        const core::Track* rowTrack = trackAtRow(heightDragRow_);
        if (rowTrack != nullptr && trackActionHandler_ &&
            heightDragMode_ != rowTrack->heightMode) {
            const TrackAction action =
                heightDragMode_ == 0   ? TrackAction::HeightMinimal
                : heightDragMode_ == 1 ? TrackAction::HeightStandard
                                       : TrackAction::HeightExpanded;
            trackActionHandler_(action, rowTrack->id);
        }
        heightDragRow_ = -1;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && handDragging_) {
        handDragging_ = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && draggingPlayhead_) {
        draggingPlayhead_ = false;
        setPlayheadFrame(xToFrame(event->position().x()));
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && boxSelecting_) {
        boxSelecting_ = false;
        boxSelection_ = QRectF(boxStart_, event->position()).normalized();
        if (!preserveSelectionForBox_) {
            selectedClips_.clear();
        }
        if (sequence_ != nullptr && boxSelection_.width() > 2.0 && boxSelection_.height() > 2.0) {
            for (std::size_t row = 0; row < displayRows_.size(); ++row) {
                const core::Track& track = sequence_->tracks()[displayRows_[row]];
                if (track.locked || !track.enabled) continue;
                const int top = static_cast<int>(rowTop(static_cast<int>(row)));
                for (const core::Clip& candidate : track.clips) {
                    const QRectF candidateRect(frameToX(candidate.timeline.start), top + clipInset,
                        std::max(1.0, frameToX(candidate.timeline.end()) -
                                          frameToX(candidate.timeline.start)),
                        rowHeight(static_cast<int>(row)) - clipInset * 2);
                    if (boxSelection_.intersects(candidateRect) && !isSelected(candidate.id)) {
                        selectedClips_.push_back(candidate.id);
                    }
                }
            }
        }
        selectedClip_ = selectedClips_.empty() ? core::ClipId{} : selectedClips_.back();
        boxSelection_ = {};
        notifySelection();
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && markerDragging_) {
        markerDragging_ = false;
        snapGuideFrame_ = -1;
        if (markerDragId_ && markerPreviewFrame_ >= 0 && markerMoveHandler_) {
            markerMoveHandler_(markerDragId_, markerPreviewFrame_);
        }
        markerDragId_ = {};
        markerDragOrigin_ = -1;
        markerPreviewFrame_ = -1;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && captionDragging_) {
        captionDragging_ = false;
        snapGuideFrame_ = -1;
        if (selectedCaption_ && captionPreviewStart_ != captionDragOrigin_ &&
            captionMoveHandler_) {
            captionMoveHandler_(selectedCaption_, captionPreviewStart_);
        }
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && dragMode_ != DragMode::None) {
        const DragMode completedDrag = dragMode_;
        dragMode_ = DragMode::None;
        snapGuideFrame_ = -1;
        const core::Clip* clip =
            sequence_ == nullptr ? nullptr : sequence_->findClip(selectedClip_);
        if (clip != nullptr && completedDrag == DragMode::Move &&
            movePreviewRow_ == -2 && moveToNewTrackHandler_) {
            moveToNewTrackHandler_(selectedClip_, dragPreviewFrame_);
        } else if (clip != nullptr && completedDrag == DragMode::Move && moveClipHandler_) {
            core::TrackId targetTrack{};
            core::TrackId ownTrack{};
            if (const core::Track* rowTrack = trackAtRow(movePreviewRow_);
                rowTrack != nullptr) {
                const core::Track* origin = trackOfClip(selectedClip_);
                // Only land on a same-kind track; anything else keeps the
                // clip on its own track (time-only move).
                if (origin != nullptr && rowTrack->kind == origin->kind) {
                    if (rowTrack->id != origin->id && !moveTargetValid(movePreviewRow_)) {
                        // The preview showed red (locked/collision): cancel
                        // the whole drop so the clip snaps back unchanged.
                        movePreviewRow_ = -1;
                        unsetCursor();
                        update();
                        event->accept();
                        return;
                    }
                    targetTrack = rowTrack->id;
                }
            }
            if (sequence_ != nullptr) {
                for (const core::Track& track : sequence_->tracks()) {
                    if (std::ranges::any_of(track.clips, [this](const core::Clip& candidate) {
                            return candidate.id == selectedClip_;
                        })) {
                        ownTrack = track.id;
                        break;
                    }
                }
            }
            const bool trackChanged = targetTrack && targetTrack != ownTrack;
            if ((event->modifiers() & Qt::AltModifier) != 0 && duplicateClipHandler_) {
                // Alt-drop duplicates at the target and keeps the original.
                duplicateClipHandler_(selectedClip_, targetTrack, dragPreviewFrame_);
            } else if (trackChanged || dragPreviewFrame_ != clip->timeline.start) {
                moveClipHandler_(selectedClip_, targetTrack, dragPreviewFrame_);
            }
        } else if (clip != nullptr &&
                   (completedDrag == DragMode::TrimStart || completedDrag == DragMode::TrimEnd) &&
                   (clip->timeline.start != dragPreviewFrame_ ||
                    clip->timeline.end() != dragPreviewEnd_) &&
                   trimClipHandler_) {
            trimClipHandler_(selectedClip_, dragPreviewFrame_, dragPreviewEnd_);
        } else if (clip != nullptr &&
                   (completedDrag == DragMode::FadeIn || completedDrag == DragMode::FadeOut) &&
                   (clip->fadeInFrames != dragPreviewFadeIn_ ||
                    clip->fadeOutFrames != dragPreviewFadeOut_) && fadeHandler_) {
            fadeHandler_(selectedClip_, dragPreviewFadeIn_, dragPreviewFadeOut_);
        } else if (clip != nullptr && completedDrag == DragMode::TransitionIn &&
                   transitionHandler_) {
            bool audioClip = false;
            for (const core::Track& track : sequence_->tracks()) {
                if (std::ranges::any_of(track.clips, [this](const core::Clip& candidate) {
                        return candidate.id == selectedClip_;
                    })) {
                    audioClip = track.kind == core::TrackKind::Audio;
                    break;
                }
            }
            const core::Frame currentTransition = audioClip ? clip->audioTransitionInFrames
                                                            : clip->videoTransitionInFrames;
            if (dragPreviewTransitionIn_ != currentTransition) {
                transitionHandler_(selectedClip_, dragPreviewTransitionIn_);
            }
        } else if (clip != nullptr && completedDrag == DragMode::Slip &&
                   dragPreviewDelta_ != 0 && slipHandler_) {
            slipHandler_(selectedClip_, dragPreviewDelta_);
        } else if (clip != nullptr && completedDrag == DragMode::Gain &&
                   gainAt(*clip, std::clamp<core::Frame>(dragStartFrame_, 0,
                       clip->timeline.duration - 1)) !=
                       dragPreviewGainDb_ && gainHandler_) {
            gainHandler_(selectedClip_, clip->timeline.start +
                std::clamp<core::Frame>(dragStartFrame_, 0, clip->timeline.duration - 1),
                dragPreviewGainDb_);
        } else if (clip != nullptr && completedDrag == DragMode::Roll &&
                   dragPreviewDelta_ != 0 && rollHandler_) {
            rollHandler_(selectedClip_, dragPreviewDelta_);
        } else if (clip != nullptr && completedDrag == DragMode::RippleEnd &&
                   dragPreviewDelta_ != 0 && rippleTrimHandler_) {
            rippleTrimHandler_(selectedClip_, dragPreviewDelta_);
        } else if (clip != nullptr && completedDrag == DragMode::Slide &&
                   dragPreviewDelta_ != 0 && slideHandler_) {
            slideHandler_(selectedClip_, dragPreviewDelta_);
        }
        // After the edit handlers have committed, let the host restore the
        // program preview from the updated model.
        if (trimPreviewHandler_ &&
            (completedDrag == DragMode::TrimStart || completedDrag == DragMode::TrimEnd ||
             completedDrag == DragMode::Roll || completedDrag == DragMode::RippleEnd)) {
            trimPreviewHandler_(selectedClip_, 0, false, false);
        }
        movePreviewRow_ = -1;
        unsetCursor();
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && markerEditHandler_) {
        if (const core::MarkerId marker = markerIdAt(event->position()); marker) {
            markerDragging_ = false;
            markerDragId_ = {};
            markerPreviewFrame_ = -1;
            markerEditHandler_(marker);
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && sequence_ != nullptr &&
        event->position().x() >= static_cast<double>(trackLabelWidth) &&
        event->position().y() >= static_cast<double>(rulerHeight) &&
        event->position().y() < static_cast<double>(tracksTop()) && captionEditHandler_) {
        const core::Frame frame = xToFrame(event->position().x());
        const int subRow = std::clamp(
            static_cast<int>((event->position().y() - rulerHeight) / captionLaneHeight),
            0, captionLaneRows_ - 1);
        for (const core::Caption& caption : sequence_->captions()) {
            if (caption.timeline.contains(frame) &&
                captionSubRow(caption.id) == subRow) {
                selectedCaption_ = caption.id;
                captionDragging_ = false;
                update();
                captionEditHandler_(caption.id);
                event->accept();
                return;
            }
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TimelineWidget::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        selectedCaption_ && captionDeleteHandler_) {
        captionDeleteHandler_(selectedCaption_);
        selectedCaption_ = {};
        captionDragging_ = false;
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && heightDragging_) {
        heightDragging_ = false;
        heightDragRow_ = -1;
        unsetCursor();
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && trackDragging_) {
        trackDragging_ = false;
        trackDragRow_ = -1;
        trackDropRow_ = -1;
        unsetCursor();
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && captionDragging_) {
        captionDragging_ = false;
        captionPreviewStart_ = captionDragOrigin_;
        snapGuideFrame_ = -1;
        autoScrollTimer_->stop();
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && markerDragging_) {
        markerDragging_ = false;
        markerDragId_ = {};
        markerDragOrigin_ = -1;
        markerPreviewFrame_ = -1;
        snapGuideFrame_ = -1;
        autoScrollTimer_->stop();
        update();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selectedClip_ &&
        (event->modifiers() & Qt::ShiftModifier) != 0 && contextActionHandler_) {
        contextActionHandler_(ContextAction::RippleDelete, selectedClip_, playheadFrame_);
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selectedClip_ &&
        contextActionHandler_) {
        contextActionHandler_(ContextAction::Lift, selectedClip_, playheadFrame_);
        event->accept();
        return;
    }
    const Qt::KeyboardModifier primaryModifier =
#if defined(Q_OS_MACOS)
        Qt::MetaModifier;
#else
        Qt::ControlModifier;
#endif
    if (event->key() == Qt::Key_K && (event->modifiers() & primaryModifier) != 0 &&
        selectedClip_ && contextActionHandler_) {
        contextActionHandler_(ContextAction::Split, selectedClip_, playheadFrame_);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left) {
        setPlayheadFrame(playheadFrame_ - 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right) {
        setPlayheadFrame(playheadFrame_ + 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && dragMode_ != DragMode::None) {
        if (trimPreviewHandler_ &&
            (dragMode_ == DragMode::TrimStart || dragMode_ == DragMode::TrimEnd ||
             dragMode_ == DragMode::Roll || dragMode_ == DragMode::RippleEnd)) {
            trimPreviewHandler_(selectedClip_, 0, false, false);
        }
        dragMode_ = DragMode::None;
        snapGuideFrame_ = -1;
        movePreviewRow_ = -1;
        autoScrollTimer_->stop();
        unsetCursor();
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && transportHandler_) {
        transportHandler_();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Z &&
        (event->modifiers() & Qt::ShiftModifier) != 0) {
        // Fit the whole sequence into the visible width (Premiere: Fit).
        const int barWidth =
            verticalScroll_ == nullptr ? 0 : verticalScroll_->sizeHint().width();
        const double visibleWidth =
            std::max(1.0, static_cast<double>(width() - trackLabelWidth - barWidth));
        const core::Frame contentEnd = std::max<core::Frame>(1, contentEndFrame() + 10);
        pixelsPerFrame_ =
            std::clamp(visibleWidth / static_cast<double>(contentEnd), 0.05, 80.0);
        scrollFrame_ = 0;
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_S && event->modifiers() == Qt::NoModifier) {
        // Premiere-style snapping toggle.
        snapEnabled_ = !snapEnabled_;
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Home) {
        setPlayheadFrame(0);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_End) {
        setPlayheadFrame(contentEndFrame());
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) &&
        sequence_ != nullptr) {
        // Premiere Up/Down: jump to the previous/next edit point across the
        // enabled tracks.
        std::vector<core::Frame> cuts;
        cuts.push_back(0);
        for (const core::Track& track : sequence_->tracks()) {
            if (!track.enabled) {
                continue;
            }
            for (const core::Clip& clip : track.clips) {
                cuts.push_back(clip.timeline.start);
                cuts.push_back(clip.timeline.end());
            }
        }
        std::ranges::sort(cuts);
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        if (event->key() == Qt::Key_Up) {
            const auto iterator =
                std::lower_bound(cuts.begin(), cuts.end(), playheadFrame_);
            if (iterator != cuts.begin()) {
                setPlayheadFrame(*std::prev(iterator));
            }
        } else {
            const auto iterator =
                std::upper_bound(cuts.begin(), cuts.end(), playheadFrame_);
            if (iterator != cuts.end()) {
                setPlayheadFrame(*iterator);
            }
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal ||
        event->key() == Qt::Key_Minus) {
        // Keyboard zoom, keeping the playhead roughly centred.
        const double factor = event->key() == Qt::Key_Minus ? 1.0 / 1.25 : 1.25;
        const core::Frame anchor = playheadFrame_;
        pixelsPerFrame_ = std::clamp(pixelsPerFrame_ * factor, 0.05, 80.0);
        scrollFrame_ = std::max<core::Frame>(0, anchor - visibleFrameCount() / 2);
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    const Qt::KeyboardModifier zoomModifier =
#if defined(Q_OS_MACOS)
        Qt::MetaModifier;
#else
        Qt::ControlModifier;
#endif
    if ((event->modifiers() & zoomModifier) != 0) {
        const double oldPixelsPerFrame = pixelsPerFrame_;
        const double mouseOffset = event->position().x() - static_cast<double>(trackLabelWidth);
        const double anchorFrame =
            mouseOffset / oldPixelsPerFrame + static_cast<double>(scrollFrame_);
        const double factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
        pixelsPerFrame_ = std::clamp(pixelsPerFrame_ * factor, 0.05, 80.0);

        const double newScrollFrame = anchorFrame - mouseOffset / pixelsPerFrame_;
        scrollFrame_ =
            std::max<core::Frame>(0, static_cast<core::Frame>(std::floor(newScrollFrame)));
    } else if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        // Shift+wheel pans the time axis; Qt reports the delta on x for some
        // devices when Shift is held, so accept either axis.
        const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y()
                                                       : event->angleDelta().x();
        scrollFrame_ = std::max<core::Frame>(0, scrollFrame_ + (delta > 0 ? -6 : 6));
    } else if (verticalScroll_ != nullptr && verticalScroll_->maximum() > 0) {
        verticalScroll_->setValue(scrollY_ - event->angleDelta().y() / 3);
    }
    // Plain wheel with nothing to scroll vertically is a no-op by design:
    // horizontal panning is Shift+wheel, zooming is Ctrl+wheel.
    update();
    event->accept();
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    updateHorizontalScrollBar();
    updateVerticalScrollBar();
    QPainter painter(this);
    painter.fillRect(rect(), theme::surfaceTimeline);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(QRect(0, 0, width(), rulerHeight), theme::surfaceBase);
    painter.setPen(theme::textSecondary);

    const int frameStep = std::max(1, static_cast<int>(std::ceil(48.0 / pixelsPerFrame_)));
    const core::Frame visibleFrames =
        static_cast<core::Frame>(std::ceil(static_cast<double>(width()) / pixelsPerFrame_));
    const core::Frame firstTick = scrollFrame_ - (scrollFrame_ % frameStep);
    for (core::Frame frame = firstTick; frame <= scrollFrame_ + visibleFrames; frame += frameStep) {
        const int x = static_cast<int>(std::round(frameToX(frame)));
        painter.drawLine(x, rulerHeight - 8, x, rulerHeight);
        painter.drawText(x + 3, 17, QString::number(frame));
    }

    if (!snapEnabled_) {
        painter.setPen(theme::warning);
        painter.drawText(QRect(trackLabelWidth + 4, 0, 120, rulerHeight),
                         Qt::AlignVCenter | Qt::AlignLeft, tr("Snap off (S)"));
    }
    if (previewCacheDuration_ > 0) {
        const double cacheLeft = frameToX(previewCacheStart_);
        const double cacheRight = frameToX(previewCacheStart_ + previewCacheDuration_);
        painter.fillRect(QRectF(cacheLeft, rulerHeight - 3.0,
                                std::max(1.0, cacheRight - cacheLeft), 3.0),
                         previewCacheValid_ ? theme::success : theme::error);
    }

    painter.fillRect(QRect(0, 0, trackLabelWidth, height()), theme::surfacePanel);
    if (sequence_ == nullptr) {
        painter.drawText(rect(), Qt::AlignCenter, tr("No sequence open"));
        return;
    }
    const core::Clip* primarySelection = sequence_->findClip(selectedClip_);

    painter.save();
    painter.setClipRect(QRect(0, tracksTop(), width(), std::max(0, height() - tracksTop())));
    for (std::size_t row = 0; row < displayRows_.size(); ++row) {
        const std::size_t trackIndex = displayRows_[row];
        const auto& track = sequence_->tracks()[trackIndex];
        const int top = static_cast<int>(rowTop(static_cast<int>(row)));
        const int rowH = rowHeight(static_cast<int>(row));
        if (top + rowH < tracksTop() || top > height()) {
            continue;
        }
        const QRect trackRect(0, top, width(), rowH);
        painter.fillRect(trackRect,
                         row % 2U == 0U ? theme::surfaceTrackA : theme::surfaceTrackB);
        painter.setPen(theme::border);
        painter.drawLine(0, top + rowH - 1, width(), top + rowH - 1);

        painter.setPen(theme::textPrimary);
        // Kind-relative numbering: V1 is the first video track in the array
        // (the back layer), A1 the first audio track.
        int kindNumber = 0;
        for (std::size_t i = 0; i <= trackIndex; ++i) {
            if (sequence_->tracks()[i].kind == track.kind) {
                ++kindNumber;
            }
        }
        const QString trackName =
            !track.name.empty()
                ? QString::fromStdString(track.name)
                : QStringLiteral("%1%2")
                      .arg(track.kind == core::TrackKind::Video ? QStringLiteral("V")
                                                                : QStringLiteral("A"))
                      .arg(kindNumber);
        const int buttonTop = top + std::max(2, (rowH - 26) / 2);
        const QRect lockButton(4, buttonTop, 22, 26);
        const QRect stateButton(30, buttonTop, 22, 26);
        const QRect targetButton(56, buttonTop, 22, 26);
        const QRect syncButton(82, buttonTop, 24, 26);
        painter.fillRect(lockButton, track.locked ? QColor(168, 78, 70) : QColor(55, 59, 67));
        const bool stateDisabled = track.kind == core::TrackKind::Audio ? track.muted
                                                                       : !track.enabled;
        painter.fillRect(stateButton,
                         stateDisabled ? QColor(168, 112, 55) : QColor(55, 59, 67));
        const bool targeted = track.kind == core::TrackKind::Video
                                  ? track.id == targetedVideoTrack_
                                  : track.id == targetedAudioTrack_;
        painter.fillRect(targetButton, targeted ? theme::accentSoft
                                                : theme::surfaceRaised);
        painter.fillRect(syncButton, track.syncLocked ? theme::accent
                                                       : theme::surfaceRaised);
        painter.setPen(theme::textPrimary);
        painter.drawText(lockButton, Qt::AlignCenter, QStringLiteral("L"));
        painter.drawText(stateButton, Qt::AlignCenter,
                         track.kind == core::TrackKind::Audio ? QStringLiteral("M")
                                                              : QStringLiteral("V"));
        painter.drawText(targetButton, Qt::AlignCenter, QStringLiteral("T"));
        painter.drawText(syncButton, Qt::AlignCenter, QStringLiteral("S"));
        if (track.kind == core::TrackKind::Audio && track.solo) {
            painter.setPen(QColor(255, 214, 90));
        }
        painter.drawText(QRect(110, top, trackLabelWidth - 110, rowH), Qt::AlignCenter,
                         track.kind == core::TrackKind::Audio && track.solo
                             ? trackName + QStringLiteral(" [S]")
                             : trackName);

        // Clip bodies must never paint over the header column on the left —
        // when scrolled right a clip's rect starts at x < trackLabelWidth.
        painter.save();
        painter.setClipRect(QRect(trackLabelWidth, tracksTop(),
                                  std::max(0, width() - trackLabelWidth),
                                  std::max(0, height() - tracksTop())));
        for (const auto& clip : track.clips) {
            if (dragMode_ == DragMode::Move && clip.id == selectedClip_) {
                // The moving clip is painted as a translucent ghost at its
                // original position; the live preview rect is drawn after the
                // row loop at the hovered target row.
                const double ghostLeft = frameToX(clip.timeline.start);
                const double ghostRight = frameToX(clip.timeline.end());
                if (ghostRight >= trackLabelWidth && ghostLeft <= width()) {
                    const QRectF ghostRect(ghostLeft, top + clipInset,
                                           std::max(1.0, ghostRight - ghostLeft),
                                           rowH - clipInset * 2);
                    QColor ghost = clipColor(clip, false);
                    ghost.setAlpha(70);
                    painter.fillRect(ghostRect, ghost);
                    painter.setPen(QPen(QColor(255, 255, 255, 60), 1.0, Qt::DashLine));
                    painter.drawRect(ghostRect);
                }
                continue;
            }
            const core::Frame displayedStart =
                dragMode_ != DragMode::None && clip.id == selectedClip_ ? dragPreviewFrame_
                                                                        : clip.timeline.start;
            const core::Frame displayedEnd =
                dragMode_ != DragMode::None && clip.id == selectedClip_ ? dragPreviewEnd_
                                                                        : clip.timeline.end();
            const double left = frameToX(displayedStart);
            const double right = frameToX(displayedEnd);
            if (right < trackLabelWidth || left > width()) {
                continue;
            }

            const QRectF clipRect(left, top + clipInset, std::max(1.0, right - left),
                                  rowH - clipInset * 2);
            const bool selected = isSelected(clip.id) ||
                                  (primarySelection != nullptr && primarySelection->linkId &&
                                   clip.linkId == primarySelection->linkId);
            painter.fillRect(clipRect, clipColor(clip, selected));
            painter.setPen(selected ? QPen(QColor(246, 249, 255), 2.0)
                                    : QPen(QColor(17, 19, 22), 1.0));
            painter.drawRect(clipRect);
            core::Frame transitionFrames = track.kind == core::TrackKind::Video
                ? clip.videoTransitionInFrames : clip.audioTransitionInFrames;
            if (dragMode_ == DragMode::TransitionIn && clip.id == selectedClip_) {
                transitionFrames = dragPreviewTransitionIn_;
            }
            if (transitionFrames > 0) {
                const double transitionRight = std::min(
                    right, left + transitionFrames * pixelsPerFrame_);
                painter.setBrush(QColor(116, 220, 205, 105));
                painter.setPen(QPen(QColor(164, 246, 232), 1.2));
                painter.drawPolygon(QPolygonF({clipRect.topLeft(),
                                               QPointF(transitionRight, clipRect.top()),
                                               clipRect.bottomLeft()}));
                painter.drawText(QRectF(left + 3.0, clipRect.top(),
                                        std::max(1.0, transitionRight - left - 3.0), 15.0),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 track.kind == core::TrackKind::Video
                                     ? QStringLiteral("Dissolve")
                                     : QStringLiteral("XFade"));
                painter.setBrush(QColor(224, 255, 248));
                painter.drawEllipse(QPointF(transitionRight, clipRect.top() + 5.0), 3.5, 3.5);
            }
            double labelLeftInset = 6.0;
            if (track.kind == core::TrackKind::Video && clipRect.width() >= 40.0) {
                const auto thumbnailIterator = assetThumbnails_.find(clip.assetId.value);
                if (thumbnailIterator != assetThumbnails_.end() &&
                    !thumbnailIterator->second.isNull()) {
                    // Premiere-style head frame at the clip's left edge.
                    const QImage& thumbnail = thumbnailIterator->second;
                    const double thumbHeight = clipRect.height() - 2.0;
                    const double thumbWidth =
                        thumbHeight * thumbnail.width() /
                        std::max(1, thumbnail.height());
                    const QRectF thumbRect(clipRect.left() + 1.0, clipRect.top() + 1.0,
                                           std::min(thumbWidth, clipRect.width() - 2.0),
                                           thumbHeight);
                    painter.save();
                    painter.setClipRect(clipRect.adjusted(1.0, 1.0, -1.0, -1.0));
                    painter.drawImage(thumbRect, thumbnail);
                    painter.restore();
                    labelLeftInset = thumbWidth + 6.0;
                }
            }
            painter.setPen(QColor(16, 18, 20));
            const auto labelIterator = assetLabels_.find(clip.assetId.value);
            painter.drawText(clipRect.adjusted(labelLeftInset, 2, -4, -2),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             labelIterator != assetLabels_.end()
                                 ? labelIterator->second
                                 : QStringLiteral("Asset %1").arg(clip.assetId.value));
            if (!clip.effects.empty()) {
                painter.setPen(QColor(240, 224, 120));
                painter.drawText(clipRect.adjusted(4, 2, -4, -2),
                                 Qt::AlignRight | Qt::AlignTop,
                                 QStringLiteral("fx %1").arg(clip.effects.size()));
                painter.setBrush(QColor(255, 224, 92));
                painter.setPen(QPen(QColor(82, 66, 18), 1.0));
                std::vector<core::Frame> drawnOffsets;
                for (const core::ClipEffect& effect : clip.effects) {
                    for (const core::EffectKeyframe& keyframe : effect.keyframes) {
                        if (std::ranges::find(drawnOffsets, keyframe.frameOffset) !=
                            drawnOffsets.end()) {
                            continue;
                        }
                        drawnOffsets.push_back(keyframe.frameOffset);
                        const int keyX = static_cast<int>(std::round(
                            left + keyframe.frameOffset * pixelsPerFrame_));
                        const int keyY = static_cast<int>(std::round(clipRect.bottom() - 7.0));
                        painter.drawPolygon(QPolygon({QPoint(keyX, keyY - 4),
                                                     QPoint(keyX + 4, keyY),
                                                     QPoint(keyX, keyY + 4),
                                                     QPoint(keyX - 4, keyY)}));
                    }
                }
            }
            if (!clip.speedKeyframes.empty()) {
                painter.setBrush(QColor(82, 220, 255));
                painter.setPen(QPen(QColor(18, 76, 92), 1.0));
                for (const core::SpeedKeyframe& keyframe : clip.speedKeyframes) {
                    const int keyX = static_cast<int>(std::round(
                        left + keyframe.frameOffset * pixelsPerFrame_));
                    const int keyY = static_cast<int>(std::round(clipRect.bottom() - 17.0));
                    painter.drawPolygon(QPolygon({QPoint(keyX, keyY - 4),
                                                 QPoint(keyX + 4, keyY),
                                                 QPoint(keyX, keyY + 4),
                                                 QPoint(keyX - 4, keyY)}));
                }
            }
            if (!clip.motionKeyframes.empty()) {
                painter.setBrush(QColor(116, 255, 158));
                painter.setPen(QPen(QColor(22, 92, 46), 1.0));
                for (const core::MotionKeyframe& keyframe : clip.motionKeyframes) {
                    const int keyX = static_cast<int>(std::round(
                        left + keyframe.frameOffset * pixelsPerFrame_));
                    const int keyY = static_cast<int>(std::round(clipRect.bottom() - 27.0));
                    painter.drawPolygon(QPolygon({QPoint(keyX, keyY - 4),
                                                 QPoint(keyX + 4, keyY),
                                                 QPoint(keyX, keyY + 4),
                                                 QPoint(keyX - 4, keyY)}));
                }
            }
            painter.setPen(QPen(QColor(255, 255, 255, 190), 1.5));
            const core::Frame fadeIn = clip.id == selectedClip_ &&
                                                dragMode_ == DragMode::FadeIn
                                            ? dragPreviewFadeIn_
                                            : clip.fadeInFrames;
            const core::Frame fadeOut = clip.id == selectedClip_ &&
                                                 dragMode_ == DragMode::FadeOut
                                             ? dragPreviewFadeOut_
                                             : clip.fadeOutFrames;
            if (fadeIn > 0) {
                const double fadeX = std::min(right, left + fadeIn * pixelsPerFrame_);
                painter.drawLine(QPointF(left, clipRect.bottom()),
                                 QPointF(fadeX, clipRect.top()));
            }
            if (fadeOut > 0) {
                const double fadeX = std::max(left, right - fadeOut * pixelsPerFrame_);
                painter.drawLine(QPointF(fadeX, clipRect.top()),
                                 QPointF(right, clipRect.bottom()));
            }
            if (selected) {
                painter.setBrush(QColor(255, 255, 255));
                painter.setPen(QPen(QColor(20, 22, 25), 1.0));
                painter.drawEllipse(QPointF(std::min(right, left + fadeIn * pixelsPerFrame_),
                                            clipRect.top() + 3.0), 3.5, 3.5);
                painter.drawEllipse(QPointF(std::max(left, right - fadeOut * pixelsPerFrame_),
                                            clipRect.top() + 3.0), 3.5, 3.5);
            }
            if (track.kind == core::TrackKind::Audio) {
                painter.setPen(QPen(QColor(255, 225, 80, 220), 1.5));
                QPolygonF gainCurve;
                const int segments = std::clamp(static_cast<int>(clipRect.width() / 5.0), 2, 300);
                for (int index = 0; index <= segments; ++index) {
                    const double ratio = static_cast<double>(index) / segments;
                    const core::Frame local = std::clamp<core::Frame>(
                        static_cast<core::Frame>(std::llround(ratio * (clip.timeline.duration - 1))),
                        0, clip.timeline.duration - 1);
                    const double shownGain = clip.id == selectedClip_ &&
                                                     dragMode_ == DragMode::Gain
                                                 ? dragPreviewGainDb_ : gainAt(clip, local);
                    gainCurve << QPointF(left + ratio * clipRect.width(),
                        clipRect.bottom() - (shownGain + 60.0) / 84.0 * clipRect.height());
                }
                painter.drawPolyline(gainCurve);
                painter.setBrush(QColor(255, 225, 80));
                painter.setPen(QPen(QColor(92, 74, 12), 1.0));
                for (const core::GainKeyframe& keyframe : clip.gainKeyframes) {
                    const int keyX = static_cast<int>(std::round(
                        left + keyframe.frameOffset * pixelsPerFrame_));
                    const int keyY = static_cast<int>(std::round(clipRect.bottom() -
                        (keyframe.gainDb + 60.0) / 84.0 * clipRect.height()));
                    painter.drawPolygon(QPolygon({QPoint(keyX, keyY - 4),
                                                 QPoint(keyX + 4, keyY),
                                                 QPoint(keyX, keyY + 4),
                                                 QPoint(keyX - 4, keyY)}));
                }
                const auto waveform = waveforms_.find(clip.assetId.value);
                if (waveform != waveforms_.end() && waveform->second.sourceDuration > 0 &&
                    !waveform->second.peaks.empty()) {
                    painter.setPen(QPen(QColor(24, 35, 42, 185), 1.0));
                    const int firstX = std::max(static_cast<int>(std::ceil(left)), trackLabelWidth);
                    const int lastX = std::min(static_cast<int>(std::floor(right)), width());
                    const double center = clipRect.center().y();
                    const double amplitude = std::max(1.0, clipRect.height() * 0.38);
                    for (int x = firstX; x <= lastX; ++x) {
                        const core::Frame sourceFrame = clip.sourceStart +
                            static_cast<core::Frame>(std::floor((x - left) / pixelsPerFrame_));
                        const auto peakIndex = static_cast<std::size_t>(std::clamp<core::Frame>(
                            sourceFrame * static_cast<core::Frame>(waveform->second.peaks.size()) /
                                waveform->second.sourceDuration,
                            0, static_cast<core::Frame>(waveform->second.peaks.size() - 1U)));
                        const double peak = waveform->second.peaks[peakIndex] * amplitude;
                        painter.drawLine(QPointF(x, center - peak), QPointF(x, center + peak));
                    }
                }
            }
        }
        painter.restore();
    }
    if (dragMode_ == DragMode::Move && movePreviewRow_ >= 0 &&
        sequence_->findClip(selectedClip_) != nullptr) {
        const bool valid = moveTargetValid(movePreviewRow_);
        const double left = frameToX(dragPreviewFrame_);
        const double right = frameToX(dragPreviewEnd_);
        const QRectF previewRect(left, rowTop(movePreviewRow_) + clipInset,
                                 std::max(1.0, right - left),
                                 rowHeight(movePreviewRow_) - clipInset * 2);
        painter.save();
        painter.setClipRect(QRect(trackLabelWidth, tracksTop(),
                                  std::max(0, width() - trackLabelWidth),
                                  std::max(0, height() - tracksTop())));
        painter.fillRect(previewRect,
                         valid ? QColor(90, 200, 120, 90) : QColor(220, 80, 70, 90));
        painter.setPen(QPen(valid ? QColor(130, 240, 160) : QColor(255, 120, 110), 2.0));
        painter.drawRect(previewRect);
        painter.restore();
        painter.fillRect(QRectF(0.0, rowTop(movePreviewRow_), 4.0,
                                rowHeight(movePreviewRow_)),
                         valid ? QColor(90, 200, 120) : QColor(220, 80, 70));
    }
    painter.restore();

    const int laneHeightTotal = captionLaneHeight * captionLaneRows_;
    painter.fillRect(QRect(0, rulerHeight, width(), laneHeightTotal), QColor(30, 28, 40));
    painter.setPen(QColor(150, 140, 190));
    painter.drawText(QRectF(6.0, rulerHeight, trackLabelWidth - 12.0, laneHeightTotal),
                     Qt::AlignVCenter | Qt::AlignLeft, tr("Text"));
    painter.setPen(QColor(52, 50, 66));
    painter.drawLine(0, tracksTop() - 1, width(), tracksTop() - 1);
    painter.save();
    painter.setClipRect(QRect(trackLabelWidth, rulerHeight,
                              std::max(0, width() - trackLabelWidth), laneHeightTotal));
    for (const core::Caption& caption : sequence_->captions()) {
        core::Frame start = caption.timeline.start;
        if (captionDragging_ && caption.id == selectedCaption_) {
            start = captionPreviewStart_;
        }
        const double left = frameToX(start);
        const double right = frameToX(start + caption.timeline.duration);
        if (right < trackLabelWidth || left > width()) {
            continue;
        }
        const bool selected = caption.id == selectedCaption_;
        const double captionTop =
            rulerHeight + 2.0 + captionSubRow(caption.id) * captionLaneHeight;
        const QRectF captionRect(left, captionTop, std::max(2.0, right - left),
                                 captionLaneHeight - 4.0);
        painter.fillRect(captionRect,
                         selected ? QColor(168, 125, 255, 230) : QColor(139, 92, 246, 190));
        if (selected) {
            painter.setPen(QPen(Qt::white, 1.2));
            painter.drawRect(captionRect.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        painter.setPen(QColor(245, 240, 255));
        painter.drawText(captionRect.adjusted(3, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         QString::fromStdString(caption.text).simplified());
    }
    painter.restore();
    painter.save();
    painter.setClipRect(QRect(trackLabelWidth, 0,
                              std::max(0, width() - trackLabelWidth), height()));
    for (const core::Marker& marker : sequence_->markers()) {
        const bool dragged = markerDragging_ && marker.id == markerDragId_ &&
                             markerPreviewFrame_ >= 0;
        const core::Frame markerFrame = dragged ? markerPreviewFrame_ : marker.position;
        const int x = static_cast<int>(std::round(frameToX(markerFrame)));
        if (x < trackLabelWidth || x > width()) {
            continue;
        }
        const QColor flagColor = QColor::fromRgba(marker.color);
        painter.setPen(QPen(flagColor, 1.0, Qt::DashLine));
        painter.drawLine(x, rulerHeight, x, height());
        painter.setBrush(flagColor);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygon{{x - 5, rulerHeight - 1}, {x + 5, rulerHeight - 1},
                                     {x, rulerHeight + 7}});
        painter.setPen(flagColor.lighter(150));
        QString label = QString::fromStdString(marker.name);
        if (!marker.comment.empty()) {
            label += QStringLiteral(" •");
        }
        painter.drawText(x + 5, rulerHeight + 14, label);
    }
    painter.restore();

    if (inFrame_ >= 0 && outFrame_ > inFrame_) {
        const double left = frameToX(inFrame_);
        const double right = frameToX(outFrame_);
        painter.save();
        painter.setClipRect(QRect(trackLabelWidth, 0,
                                  std::max(0, width() - trackLabelWidth), height()));
        painter.fillRect(QRectF(left, rulerHeight, std::max(1.0, right - left),
                               height() - rulerHeight), QColor(74, 132, 186, 28));
        painter.fillRect(QRectF(left, 0, std::max(1.0, right - left), rulerHeight),
                         QColor(74, 132, 186, 80));
        painter.setPen(QPen(QColor(92, 178, 244), 2.0));
        painter.drawLine(QPointF(left, 0), QPointF(left, height()));
        painter.drawLine(QPointF(right, 0), QPointF(right, height()));
        painter.drawText(QRectF(left + 3.0, 0, 18.0, rulerHeight), Qt::AlignCenter,
                         QStringLiteral("I"));
        painter.drawText(QRectF(right - 21.0, 0, 18.0, rulerHeight), Qt::AlignCenter,
                         QStringLiteral("O"));
        painter.restore();
    }

    const int playheadX = static_cast<int>(std::round(frameToX(playheadFrame_)));
    if (playheadX >= trackLabelWidth && playheadX <= width()) {
        painter.save();
        painter.setClipRect(QRect(trackLabelWidth, 0,
                                  std::max(0, width() - trackLabelWidth), height()));
        painter.setPen(QPen(theme::playhead, 1.0));
        painter.drawLine(playheadX, 0, playheadX, height());
        painter.setBrush(theme::playhead);
        painter.drawPolygon(QPolygon{{playheadX - 5, 0}, {playheadX + 5, 0},
                                     {playheadX, rulerHeight / 2}});
        painter.restore();
    }
    if (snapGuideFrame_ >= 0) {
        const int guideX = static_cast<int>(std::round(frameToX(snapGuideFrame_)));
        if (guideX >= trackLabelWidth) {
            painter.setPen(QPen(QColor(64, 214, 231), 1.0, Qt::DashLine));
            painter.drawLine(guideX, rulerHeight, guideX, height());
        }
    }
    if (boxSelecting_) {
        painter.fillRect(boxSelection_, QColor(70, 150, 255, 45));
        painter.setPen(QPen(QColor(100, 180, 255), 1.0, Qt::DashLine));
        painter.drawRect(boxSelection_);
    }
    const auto drawNewTrackBand = [&painter, this](const QRectF& band,
                                                   const QString& label) {
        painter.fillRect(band, QColor(90, 200, 120, 60));
        painter.setPen(QPen(QColor(130, 240, 160), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(band.adjusted(0.5, 0.5, -0.5, -0.5));
        painter.setPen(QColor(190, 250, 205));
        painter.drawText(band, Qt::AlignCenter, label);
    };
    if (dragMode_ == DragMode::Move && movePreviewRow_ == -2) {
        // Ghost strip for the new front-most video track above the top row.
        const QRectF newTrackBand(0.0, rulerHeight, width(),
                                  tracksTop() - rulerHeight);
        drawNewTrackBand(newTrackBand, tr("New Video Track"));
        const double left = frameToX(dragPreviewFrame_);
        const double right = frameToX(dragPreviewEnd_);
        painter.fillRect(QRectF(left, rulerHeight + 2.0, std::max(1.0, right - left),
                                newTrackBand.height() - 4.0),
                         QColor(90, 200, 120, 130));
    }
    if (externalDragActive_) {
        // Ghost for external asset drags: highlight the target row, or the
        // new-track band the drop would create.
        const int target = dropTargetTrackIndex(externalDragPosition_);
        if (target >= 0) {
            const int row = rowOfTrackIndex(static_cast<std::size_t>(target));
            if (row >= 0) {
                painter.setPen(QPen(QColor(130, 240, 160), 2.0, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRectF(trackLabelWidth, rowTop(row) + 1.0,
                                        width() - trackLabelWidth - 1.0,
                                        rowHeight(row) - 2.0));
            }
        } else if (target == -2) {
            drawNewTrackBand(QRectF(0.0, rulerHeight, width(),
                                    tracksTop() - rulerHeight),
                             tr("New Video Track"));
        } else if (target == -3) {
            const double contentBottom =
                tracksTop() - scrollY_ +
                (rowOffsets_.empty() ? 0 : rowOffsets_.back());
            drawNewTrackBand(QRectF(0.0, contentBottom, width(), trackHeight),
                             tr("New Audio Track"));
        }
    }
    if (trackDragging_ && trackDropRow_ >= 0) {
        // Insertion highlight for the header reorder drag.
        painter.setPen(QPen(QColor(130, 240, 160), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(0.5, rowTop(trackDropRow_) + 0.5,
                                trackLabelWidth - 1.0,
                                rowHeight(trackDropRow_) - 1.0));
    }
    if (heightDragging_ && heightDragRow_ >= 0) {
        // Guide line for the boundary drag: where the chosen preset ends.
        const int preset = heightDragMode_ == 0 ? 34 : heightDragMode_ == 2 ? 96 : 62;
        const double guideY = rowTop(heightDragRow_) + preset;
        painter.setPen(QPen(QColor(130, 240, 160), 1.5, Qt::DashLine));
        painter.drawLine(QPointF(0.0, guideY), QPointF(width(), guideY));
    }
}

} // namespace videx::ui
