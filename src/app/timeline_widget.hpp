#pragma once

#include <videx/core/timeline.hpp>

#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QRectF>

#include <functional>
#include <unordered_map>
#include <vector>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QScrollBar;
class QTimer;
class QWheelEvent;

namespace videx::ui {

class TimelineWidget final : public QWidget {
  public:
    enum class ContextAction {
        Cut,
        Copy,
        Paste,
        Duplicate,
        Split,
        Lift,
        RippleDelete,
        AddFadeIn,
        AddFadeOut,
        RemoveFades,
        AddCrossDissolve,
        AddAudioCrossfade,
        RemoveTransitions,
        AddGainKeyframe,
        RemoveGainKeyframe,
        AddMotionKeyframe,
        RemoveMotionKeyframe,
        LinkSelected,
        Unlink,
        ResetTransform,
        ResetEffects,
    };
    enum class Tool {
        Selection,
        Razor,
        Slip,
        Rolling,
        Ripple,
        Slide,
        Hand,
        Zoom,
    };
    enum class TrackAction {
        AddVideo,
        AddAudio,
        Delete,
        MoveUp,
        MoveDown,
        ToggleSolo,
        HeightMinimal,
        HeightStandard,
        HeightExpanded,
    };
    using MoveClipHandler = std::function<void(core::ClipId, core::TrackId, core::Frame)>;
    // Dragging a video clip into the band above the top video row creates a
    // new front-most track and moves the clip there.
    using MoveToNewTrackHandler = std::function<void(core::ClipId, core::Frame)>;
    // Alt+drag drops a duplicate at the target instead of moving the clip.
    using DuplicateClipHandler =
        std::function<void(core::ClipId, core::TrackId, core::Frame)>;
    using ClipHandler = std::function<void(core::ClipId)>;
    using SplitClipHandler = std::function<void(core::ClipId, core::Frame)>;
    using TrimClipHandler =
        std::function<void(core::ClipId, core::Frame, core::Frame)>;
    // Streams the timeline frame at the moving edit point while a trim-style
    // drag is active; twoUp=true asks for an outgoing/incoming pair (roll
    // edits). Called once with active=false when the drag ends.
    using TrimPreviewHandler =
        std::function<void(core::ClipId, core::Frame, bool, bool)>;
    using FadeHandler = std::function<void(core::ClipId, core::Frame, core::Frame)>;
    using TransitionHandler = std::function<void(core::ClipId, core::Frame)>;
    using GainHandler = std::function<void(core::ClipId, core::Frame, double)>;
    using DeltaEditHandler = std::function<void(core::ClipId, core::Frame)>;
    using PlayheadHandler = std::function<void(core::Frame)>;
    using TransportHandler = std::function<void()>;
    using TrackStateHandler = std::function<void(core::TrackId, bool)>;
    using TrackTargetHandler = std::function<void(core::TrackId)>;
    using SelectionHandler = std::function<void(const std::vector<core::ClipId>&)>;
    using ContextActionHandler =
        std::function<void(ContextAction, core::ClipId, core::Frame)>;
    using TrackActionHandler = std::function<void(TrackAction, core::TrackId)>;
    // Header drag reorder: array-position delta within the track's own kind.
    using TrackReorderHandler = std::function<void(core::TrackId, int)>;
    using AssetDropHandler = std::function<void(std::uint64_t, int, core::Frame)>;
    using EffectDropHandler = std::function<void(int, core::ClipId)>;
    using CaptionHandler = std::function<void(core::CaptionId)>;
    using CaptionMoveHandler = std::function<void(core::CaptionId, core::Frame)>;
    using MarkerHandler = std::function<void(core::MarkerId)>;
    using MarkerMoveHandler = std::function<void(core::MarkerId, core::Frame)>;
    using MarkerContextHandler = std::function<void(core::MarkerId, const QPoint&)>;

    explicit TimelineWidget(QWidget* parent = nullptr);

    void setSequence(const core::Sequence* sequence);
    void setMoveClipHandler(MoveClipHandler handler);
    void setMoveToNewTrackHandler(MoveToNewTrackHandler handler);
    void setDuplicateClipHandler(DuplicateClipHandler handler);
    void setSplitClipHandler(SplitClipHandler handler);
    void setLiftClipHandler(ClipHandler handler);
    void setRippleDeleteHandler(ClipHandler handler);
    void setTrimClipHandler(TrimClipHandler handler);
    void setTrimPreviewHandler(TrimPreviewHandler handler);
    void setFadeHandler(FadeHandler handler);
    void setTransitionHandler(TransitionHandler handler);
    void setGainHandler(GainHandler handler);
    void setSlipHandler(DeltaEditHandler handler);
    void setRollHandler(DeltaEditHandler handler);
    void setRippleTrimHandler(DeltaEditHandler handler);
    void setSlideHandler(DeltaEditHandler handler);
    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const noexcept;
    void setPlayheadHandler(PlayheadHandler handler);
    void setTransportHandler(TransportHandler handler);
    void setTrackLockHandler(TrackStateHandler handler);
    void setTrackSyncLockHandler(TrackStateHandler handler);
    void setTrackMuteHandler(TrackStateHandler handler);
    void setTrackEnabledHandler(TrackStateHandler handler);
    void setTrackTargetHandler(TrackTargetHandler handler);
    void setTargetTracks(core::TrackId videoTrack, core::TrackId audioTrack);
    void setSelectionHandler(SelectionHandler handler);
    void setContextActionHandler(ContextActionHandler handler);
    void setTrackActionHandler(TrackActionHandler handler);
    void setTrackReorderHandler(TrackReorderHandler handler);
    void setAssetDropHandler(AssetDropHandler handler);
    void setEffectDropHandler(EffectDropHandler handler);
    void setCaptionMoveHandler(CaptionMoveHandler handler);
    void setCaptionEditHandler(CaptionHandler handler);
    void setCaptionDeleteHandler(CaptionHandler handler);
    void setCaptionConvertHandler(CaptionHandler handler);
    void setMarkerMoveHandler(MarkerMoveHandler handler);
    void setMarkerContextHandler(MarkerContextHandler handler);
    void setMarkerEditHandler(MarkerHandler handler);
    // Marker flag hit test on the ruler edge; public for interaction tests.
    [[nodiscard]] core::MarkerId markerIdAt(const QPointF& position) const;
    void setPlayheadFrame(core::Frame frame, bool notify = true);
    void setInOutRange(core::Frame inFrame, core::Frame outFrame);
    [[nodiscard]] core::Frame playheadFrame() const noexcept;
    [[nodiscard]] const std::vector<core::ClipId>& selectedClipIds() const noexcept;
    void setSelectedClipIds(std::vector<core::ClipId> clipIds);
    // Resolves a drop position to a sequence track index, or -2 (new video
    // track above the top row) / -3 (new audio track below the last row) /
    // -1 (default targets). Public for interaction tests.
    [[nodiscard]] int dropTargetTrackIndex(const QPointF& position) const;
    // Human-readable clip labels (file base name, or title text), keyed by
    // asset id. Falls back to "Asset N" when missing.
    void setAssetLabels(std::unordered_map<std::uint64_t, QString> labels);
    // Head thumbnails per asset id, pre-scaled by the host; drawn at the left
    // edge of video clips like Premiere's head frame.
    void setAssetThumbnails(std::unordered_map<std::uint64_t, QImage> thumbnails);
    void setWaveform(core::AssetId assetId, core::Frame sourceDuration,
                     std::vector<float> peaks);
    void clearWaveforms();
    void setPreviewCacheState(core::Frame start, core::Frame duration, bool valid);

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

  private:
    enum class DragMode {
        None,
        Move,
        TrimStart,
        TrimEnd,
        FadeIn,
        FadeOut,
        TransitionIn,
        Gain,
        Slip,
        Roll,
        RippleEnd,
        Slide,
    };

    [[nodiscard]] double frameToX(core::Frame frame) const noexcept;
    [[nodiscard]] core::Frame xToFrame(double x) const noexcept;
    // Display rows group video tracks above audio tracks; within the video
    // group the front-most track (highest V number) is the top row while V1
    // sits directly above A1. displayRows_[row] is the sequence track index.
    void rebuildDisplayRows();
    void rebuildCaptionRows();
    // Top of the track rows; grows when overlapping captions stack into
    // additional sub-rows of the text lane.
    [[nodiscard]] int tracksTop() const noexcept;
    [[nodiscard]] int captionSubRow(core::CaptionId captionId) const noexcept;
    [[nodiscard]] int displayRowAt(double y) const noexcept;
    [[nodiscard]] double rowTop(int displayRow) const noexcept;
    [[nodiscard]] int rowHeight(int displayRow) const noexcept;
    [[nodiscard]] const core::Track* trackAtRow(int displayRow) const noexcept;
    [[nodiscard]] int rowOfTrackIndex(std::size_t trackIndex) const noexcept;
    [[nodiscard]] const core::Track* trackOfClip(core::ClipId clipId) const noexcept;
    [[nodiscard]] bool moveTargetValid(int displayRow) const noexcept;
    void updateVerticalScrollBar();
    [[nodiscard]] core::Frame snapFrame(core::Frame frame, core::ClipId ignoredClip) const noexcept;
    [[nodiscard]] const core::Clip* clipAt(const QPointF& position) const noexcept;
    [[nodiscard]] const core::Clip* clipEdgeAt(const QPointF& position) const noexcept;
    void notifySelection();
    [[nodiscard]] bool isSelected(core::ClipId clipId) const noexcept;
    [[nodiscard]] core::Frame visibleFrameCount() const noexcept;
    [[nodiscard]] core::Frame contentEndFrame() const noexcept;
    void updateHorizontalScrollBar();
    void followPlayhead();

    const core::Sequence* sequence_ = nullptr;
    core::ClipId selectedClip_;
    std::vector<core::ClipId> selectedClips_;
    core::Frame playheadFrame_ = 0;
    core::Frame inFrame_ = -1;
    core::Frame outFrame_ = -1;
    core::Frame scrollFrame_ = 0;
    core::Frame dragStartFrame_ = 0;
    core::Frame dragPreviewFrame_ = 0;
    core::Frame dragPreviewEnd_ = 0;
    core::Frame dragPreviewFadeIn_ = 0;
    core::Frame dragPreviewFadeOut_ = 0;
    core::Frame dragPreviewTransitionIn_ = 0;
    core::Frame dragPreviewDelta_ = 0;
    core::Frame snapGuideFrame_ = -1;
    double pixelsPerFrame_ = 8.0;
    double dragPreviewGainDb_ = 0.0;
    DragMode dragMode_ = DragMode::None;
    bool draggingPlayhead_ = false;
    bool boxSelecting_ = false;
    bool preserveSelectionForBox_ = false;
    bool handDragging_ = false;
    double handLastX_ = 0.0;
    QPointF boxStart_;
    QRectF boxSelection_;
    MoveClipHandler moveClipHandler_;
    MoveToNewTrackHandler moveToNewTrackHandler_;
    DuplicateClipHandler duplicateClipHandler_;
    SplitClipHandler splitClipHandler_;
    ClipHandler liftClipHandler_;
    ClipHandler rippleDeleteHandler_;
    TrimClipHandler trimClipHandler_;
    TrimPreviewHandler trimPreviewHandler_;
    FadeHandler fadeHandler_;
    TransitionHandler transitionHandler_;
    GainHandler gainHandler_;
    DeltaEditHandler slipHandler_;
    DeltaEditHandler rollHandler_;
    DeltaEditHandler rippleTrimHandler_;
    DeltaEditHandler slideHandler_;
    PlayheadHandler playheadHandler_;
    TransportHandler transportHandler_;
    TrackStateHandler trackLockHandler_;
    TrackStateHandler trackSyncLockHandler_;
    TrackStateHandler trackMuteHandler_;
    TrackStateHandler trackEnabledHandler_;
    TrackTargetHandler trackTargetHandler_;
    core::TrackId targetedVideoTrack_;
    core::TrackId targetedAudioTrack_;
    SelectionHandler selectionHandler_;
    ContextActionHandler contextActionHandler_;
    TrackActionHandler trackActionHandler_;
    TrackReorderHandler trackReorderHandler_;
    bool trackDragging_ = false;
    int trackDragRow_ = -1;
    int trackDropRow_ = -1;
    AssetDropHandler assetDropHandler_;
    EffectDropHandler effectDropHandler_;
    CaptionMoveHandler captionMoveHandler_;
    CaptionHandler captionEditHandler_;
    CaptionHandler captionDeleteHandler_;
    CaptionHandler captionConvertHandler_;
    std::vector<std::size_t> displayRows_;
    std::vector<int> rowOffsets_;
    std::unordered_map<std::uint64_t, int> captionRows_;
    std::unordered_map<std::uint64_t, QString> assetLabels_;
    std::unordered_map<std::uint64_t, QImage> assetThumbnails_;
    bool snapEnabled_ = true;
    int captionLaneRows_ = 1;
    QScrollBar* verticalScroll_ = nullptr;
    int scrollY_ = 0;
    bool updatingVerticalScrollBar_ = false;
    int movePreviewRow_ = -1;
    QTimer* autoScrollTimer_ = nullptr;
    QPointF lastDragPosition_;
    int autoScrollStepX_ = 0;
    int autoScrollStepY_ = 0;
    void updateAutoScroll(const QPointF& position);
    bool heightDragging_ = false;
    int heightDragRow_ = -1;
    int heightDragMode_ = 1;
    bool externalDragActive_ = false;
    QPointF externalDragPosition_;
    core::CaptionId selectedCaption_;
    bool captionDragging_ = false;
    core::Frame captionGrabOffset_ = 0;
    core::Frame captionDragOrigin_ = 0;
    core::Frame captionPreviewStart_ = 0;
    MarkerMoveHandler markerMoveHandler_;
    MarkerContextHandler markerContextHandler_;
    MarkerHandler markerEditHandler_;
    bool markerDragging_ = false;
    core::MarkerId markerDragId_;
    core::Frame markerDragOrigin_ = -1;
    core::Frame markerPreviewFrame_ = -1;
    Tool tool_ = Tool::Selection;
    struct WaveformView final {
        core::Frame sourceDuration = 0;
        std::vector<float> peaks;
    };
    std::unordered_map<std::uint64_t, WaveformView> waveforms_;
    QScrollBar* horizontalScroll_ = nullptr;
    bool updatingScrollBar_ = false;
    core::Frame previewCacheStart_ = 0;
    core::Frame previewCacheDuration_ = 0;
    bool previewCacheValid_ = false;
};

} // namespace videx::ui
