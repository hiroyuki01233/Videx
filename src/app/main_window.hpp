#pragma once

#include "project_file.hpp"

#include <videx/core/edit_session.hpp>

#include <QMainWindow>
#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

class QAction;
class QAudioSink;
class QAudioOutput;
class QBuffer;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QFontComboBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QProcess;
class QProgressDialog;
class QProgressBar;
class QMediaPlayer;
class QSlider;
class QSplitter;
class QString;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QVideoSink;

namespace videx::render {
class QtMonitorWidget;
struct MonitorTitleOverlay;
}

namespace videx::ui {

class ContextRail;
class PropertySection;
class SelectionModel;
class TimelineWidget;

// Why the program monitor is (or is not) using the direct-overlay fast path.
// The fast path plays the single base video clip through QMediaPlayer at its
// native resolution and paints static title rasters over it, so it avoids the
// FFmpeg frame server entirely. Every reason below forces the compositor
// fallback, which currently renders at 1280x720 (halved again during playback).
// Surfaced in the monitor diagnostics overlay and Help > Diagnostics so the
// quality drop is explainable instead of mysterious.
enum class PreviewPathReason {
    DirectOverlay,          // fast path active
    PreviewCache,           // playing a rendered In-to-Out cache
    Stopped,                // not playing; single-frame compositor render
    NoVideoClip,            // nothing to show at this frame
    MultipleVideoClips,     // more than one non-title video clip
    BaseSpeedKeyframes,     // base clip has a speed ramp
    BaseTransition,         // playhead is inside the base clip's transition
    BaseMediaMissing,       // base asset file is gone
    TitleBehindBase,        // title sits under the base video track
    TitleEffects,           // title has keyframed (time-varying) effects
    TitleTransition,        // title blends with an adjacent outgoing clip
    TitleRasterUnavailable, // title raster could not be read or regenerated
};
// Motion keyframes, crop, masks and static effects are deliberately absent:
// the overlay path handles them (motion/crop via QPainter, masks and static
// effects baked into a cached raster), so they no longer cost resolution.

// Short human-readable label for the monitor overlay and diagnostics.
[[nodiscard]] QString previewPathReasonText(PreviewPathReason reason);

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void createActions();
    void createPanels();
    void createStatusBar();
    void createProject();
    void importMedia();
    void importMediaFile(const QString& filePath);
    void exportReview(const QString& outputPathOverride = QString(),
                      bool previewRender = false);
    bool showExportDialog(QString& outputPath, core::Frame durationFrames, bool markedRange);
    void exportOtio();
    [[nodiscard]] bool previewCacheValid() const;
    void requestPreviewFrame(const QString& filePath, std::int64_t timestampMicroseconds = 0,
                             bool updateSource = true, bool updateProgram = true,
                             const QString& blendPath = {},
                             std::int64_t blendTimestampMicroseconds = 0,
                             double blendAmount = 0.0);
    void updateProgramFrame(core::Frame timelineFrame);
    void requestTimelineFrame(core::Frame timelineFrame);
    [[nodiscard]] QByteArray buildTimelineVideoManifest() const;
    void requestTrimTwoUp(core::Frame outgoingFrame, core::Frame incomingFrame);
    void startTrimTwoUpRender();
    void startTrimTwoUpPhase(core::Frame frame, int phase);
    void togglePlayback();
    void startPlayback(int direction);
    void pausePlayback();
    [[nodiscard]] bool startContinuousPlayback(core::Frame timelineFrame);
    [[nodiscard]] bool requestPlaybackAudio(core::Frame timelineFrame,
                                            bool prefetch = false);
    // Collects the title clips active at the frame as monitor overlays so the
    // continuous-playback path can keep running under them. Returns false when
    // a title needs the compositor, recording why in previewPathReason_.
    bool collectTitleOverlays(core::Frame frame, int baseTrackIndex,
                              std::vector<videx::render::MonitorTitleOverlay>& out);
    // Records why the program monitor is on the compositor or the fast path and
    // refreshes the monitor's diagnostics line.
    void setPreviewPathReason(PreviewPathReason reason);
    void startAudioSinkFromPcm(const QByteArray& pcm, int sampleRate, int channelCount,
                               core::Frame chunkStart);
    void startPlaybackClock();
    void stopPlaybackAudio();
    void updateTransportUi(core::Frame frame);
    [[nodiscard]] core::Frame sequenceEndFrame() const;
    void continuePendingPreview();
    void handleMediaProbe(const QString& filePath, const QJsonObject& metadata);
    void openProject();
    [[nodiscard]] bool saveProject();
    [[nodiscard]] bool saveProjectAs();
    [[nodiscard]] bool confirmDiscardChanges();
    void setDirty(bool dirty);
    void updateWindowTitle();
    void rebuildProjectTree();
    void refreshEditor();
    void handleFrameServerResponse();
    // Re-points stale track targets and mirrors them on the source patch
    // buttons (Premiere-style: the buttons read V2/A3 for the targeted rows).
    void updateSourcePatchUi();
    // Normalized (0..1) monitor content bounds of a clip: titles return their
    // text box so monitor selection and outlines hug the visible text.
    [[nodiscard]] QRectF clipMonitorContentRect(const videx::core::Clip& clip) const;
    void autosaveProject();
    void startAssetCacheJobs(core::AssetId assetId);
    void pruneAssetCache();
    void updateMonitorEditTarget();
    void toggleMonitorFullscreen();
    void createTextClipAtPlayhead();
    void createTitleClipAtPlayhead();
    bool addTitleClip(const QString& text, const QString& fontFamily, double positionX,
                      double positionY, double fontSize, std::uint32_t textColor,
                      std::uint32_t backgroundColor, bool bold, bool italic,
                      core::Frame start, core::Frame duration);
    [[nodiscard]] core::CaptionId captionAtPlayhead() const;
    void updateTextPanel();
    void applyTextPanel();
    void saveWorkspaceState();
    void restoreWorkspaceState();
    void applyWorkspacePreset(int preset);
    void ensureMediaPlayer();
    void loadWaveformCache(const ProjectAsset& asset);
    void updateInspector(core::ClipId clipId);
    void updateSelectionContext(const std::vector<core::ClipId>& clipIds);
    void applyInspectorProperties();
    void slipSelected(core::Frame sourceDelta);
    void rollSelected(core::Frame cutDelta);
    void copySelectedClips();
    void pasteClipsAt(core::Frame frame);
    void deleteSelectedClips(bool ripple);
    void splitSelectedClips(core::Frame frame);
    void setSelectedFades(core::Frame fadeIn, core::Frame fadeOut);
    void setSelectedTransitions(core::Frame videoFrames, core::Frame audioFrames);
    void editSequenceRange(bool ripple);
    void updateEffectsPanel();
    void updateHistoryPanel();
    void updateAudioMeters();
    void applySelectedEffect();
    void generateProxy(core::AssetId assetId);
    void updateCaptionOverlay(core::Frame frame);
    void writeCaptionSidecar(const QString& mp4Path);
    void openAssetInSource(core::AssetId assetId);
    void insertSourceSelection(bool overwrite);

    core::EditSession editSession_;
    SelectionModel* selectionModel_ = nullptr;
    ContextRail* contextRail_ = nullptr;
    PropertySection* compositingSection_ = nullptr;
    PropertySection* audioSection_ = nullptr;
    PropertySection* timingSection_ = nullptr;
    PropertySection* transformSection_ = nullptr;
    PropertySection* cropMaskSection_ = nullptr;
    PropertySection* effectsSection_ = nullptr;
    PropertySection* addEffectSection_ = nullptr;
    PropertySection* textSection_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    QTreeWidget* projectTree_ = nullptr;
    QLineEdit* projectSearch_ = nullptr;
    QProcess* mediaWorker_ = nullptr;
    QStringList pendingImports_;
    QPoint assetDragStartPosition_;
    QProcess* previewWorker_ = nullptr;
    // Long-lived preview frame server ("timeline-frame-serve"): one request in
    // flight at a time; the newest queued frame supersedes older ones.
    // Live monitor drag: while a layered target is dragged, preview frames
    // render with this transform substituted for the clip so the element
    // itself follows the pointer, not just its outline.
    struct LiveDragTransform final {
        double positionX = 0.0;
        double positionY = 0.0;
        double scaleX = 1.0;
        double scaleY = 1.0;
        double rotationDegrees = 0.0;
        double anchorX = 0.5;
        double anchorY = 0.5;
    };
    videx::core::ClipId liveDragClip_;
    LiveDragTransform liveDragTransform_;
    bool monitorLayeredTarget_ = false;
    // Per-frame manifest rebuilds stat every clip's files; during playback the
    // sequence is unchanged, so serve the serialized manifest from this cache.
    mutable QByteArray manifestCache_;
    mutable std::uint64_t manifestCacheRevision_ = ~0ULL;
    QProcess* frameServer_ = nullptr;
    QByteArray frameServerBuffer_;
    bool frameServerBusy_ = false;
    bool frameServerDiscard_ = false;
    core::Frame frameServerFrame_ = 0;
    std::uint64_t frameServerRevision_ = 0;
    QProcess* audioWorker_ = nullptr;
    // Next audio chunk fetched while the current one still plays, so chunk
    // boundaries no longer stall the transport or drop audio.
    QByteArray pendingAudioPcm_;
    core::Frame pendingAudioStart_ = -1;
    int pendingAudioSampleRate_ = 0;
    int pendingAudioChannels_ = 0;
    // Timeline frame where the currently playing audio chunk's content ends
    // (independent of any sync trim at its start).
    core::Frame audioChunkEnd_ = -1;
    // .vxtitle rasters for the overlay path, keyed by path (mtime-validated).
    std::unordered_map<QString, std::pair<qint64, QImage>> titleOverlayCache_;
    // Title rasters with time-invariant effects/masks already applied, so an
    // affected title still plays on the native-resolution overlay path. Keyed
    // by raster path and holding the parameter signature that produced the
    // image, so one entry per title: changing a parameter replaces it rather
    // than accumulating a variant per value the user scrubs through.
    std::unordered_map<QString, std::pair<QString, QImage>> titleBakedCache_;
    QProcess* exportWorker_ = nullptr;
    QTimer* transportTimer_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    QAudioSink* audioSink_ = nullptr;
    QAudioOutput* mediaAudioOutput_ = nullptr;
    QMediaPlayer* mediaPlayer_ = nullptr;
    QVideoSink* mediaVideoSink_ = nullptr;
    QBuffer* audioBuffer_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* transportAction_ = nullptr;
    QMenu* windowMenu_ = nullptr;
    QToolButton* playPauseButton_ = nullptr;
    QSlider* seekSlider_ = nullptr;
    QSlider* sourceSeekSlider_ = nullptr;
    QLabel* sourceRangeLabel_ = nullptr;
    QLabel* timecodeLabel_ = nullptr;
    QListWidget* jobsList_ = nullptr;
    QListWidgetItem* exportJobItem_ = nullptr;
    QProgressDialog* exportProgress_ = nullptr;
    QDoubleSpinBox* opacitySpin_ = nullptr;
    QDoubleSpinBox* gainSpin_ = nullptr;
    QComboBox* gainInterpolationCombo_ = nullptr;
    QDoubleSpinBox* rateSpin_ = nullptr;
    QComboBox* speedInterpolationCombo_ = nullptr;
    QComboBox* motionInterpolationCombo_ = nullptr;
    QDoubleSpinBox* fadeInSpin_ = nullptr;
    QDoubleSpinBox* fadeOutSpin_ = nullptr;
    QDoubleSpinBox* positionXSpin_ = nullptr;
    QDoubleSpinBox* positionYSpin_ = nullptr;
    QDoubleSpinBox* scaleXSpin_ = nullptr;
    QDoubleSpinBox* scaleYSpin_ = nullptr;
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QDoubleSpinBox* anchorXSpin_ = nullptr;
    QDoubleSpinBox* anchorYSpin_ = nullptr;
    QDoubleSpinBox* cropLeftSpin_ = nullptr;
    QDoubleSpinBox* cropRightSpin_ = nullptr;
    QDoubleSpinBox* cropTopSpin_ = nullptr;
    QDoubleSpinBox* cropBottomSpin_ = nullptr;
    QComboBox* maskShapeCombo_ = nullptr;
    QDoubleSpinBox* maskCenterXSpin_ = nullptr;
    QDoubleSpinBox* maskCenterYSpin_ = nullptr;
    QDoubleSpinBox* maskWidthSpin_ = nullptr;
    QDoubleSpinBox* maskHeightSpin_ = nullptr;
    QDoubleSpinBox* maskFeatherSpin_ = nullptr;
    QCheckBox* maskInvertedCheck_ = nullptr;
    QWidget* inspectorEditSource_ = nullptr;
    QListWidget* effectsList_ = nullptr;
    QListWidget* historyList_ = nullptr;
    QProgressBar* leftAudioMeter_ = nullptr;
    QProgressBar* rightAudioMeter_ = nullptr;
    QComboBox* effectTypeCombo_ = nullptr;
    QComboBox* effectInterpolationCombo_ = nullptr;
    QDoubleSpinBox* effectAmountSpin_ = nullptr;
    QCheckBox* effectEnabledCheck_ = nullptr;
    core::ClipId effectsPanelClip_;
    QByteArray effectsPanelSignature_;
    core::ClipId inspectedClip_;
    core::TrackId videoTrack_;
    core::TrackId audioTrack_;
    core::AssetId currentSourceAsset_;
    core::Frame sourceInFrame_ = 0;
    core::Frame sourceOutFrame_ = 0;
    core::Frame sourceDurationFrames_ = 0;
    bool videoSourcePatched_ = true;
    bool audioSourcePatched_ = true;
    QToolButton* videoPatchButton_ = nullptr;
    QToolButton* audioPatchButton_ = nullptr;
    std::uint64_t nextAssetId_ = 1;
    std::uint64_t nextLinkId_ = 1;
    core::Frame nextInsertFrame_ = 0;
    core::Frame sequenceInFrame_ = -1;
    core::Frame sequenceOutFrame_ = -1;
    std::vector<ProjectAsset> assets_;
    QString projectPath_;
    bool dirty_ = false;
    render::QtMonitorWidget* sourceMonitor_ = nullptr;
    render::QtMonitorWidget* programMonitor_ = nullptr;
    QString pendingPreviewPath_;
    std::int64_t pendingPreviewTimestamp_ = 0;
    bool pendingPreviewSource_ = false;
    bool pendingPreviewProgram_ = false;
    QString pendingPreviewBlendPath_;
    std::int64_t pendingPreviewBlendTimestamp_ = 0;
    double pendingPreviewBlendAmount_ = 0.0;
    bool hasPendingPreview_ = false;
    core::Frame pendingTimelineFrame_ = -1;
    core::Frame playbackStartFrame_ = 0;
    QElapsedTimer playbackClockTimer_;
    core::Frame playbackClockFrame_ = 0;
    bool loopPlayback_ = false;
    int playbackResolutionDivisor_ = 0;
    QSplitter* monitorSplitter_ = nullptr;
    bool monitorFullscreen_ = false;
    QLineEdit* effectsBrowserSearch_ = nullptr;
    QListWidget* effectsBrowserList_ = nullptr;
    // Looping "what does this effect look like" previews for the browser:
    // frames per browser code (EffectType 0..4, pseudo codes 100+).
    std::unordered_map<int, std::vector<QImage>> effectPreviewFrames_;
    QTimer* effectPreviewTimer_ = nullptr;
    int effectPreviewIndex_ = 0;
    QWidget* keyframeLanesWidget_ = nullptr;
    QPlainTextEdit* textEditField_ = nullptr;
    QFontComboBox* textFontCombo_ = nullptr;
    QDoubleSpinBox* textSizeSpin_ = nullptr;
    QDoubleSpinBox* textPosXSpin_ = nullptr;
    QDoubleSpinBox* textPosYSpin_ = nullptr;
    QPushButton* textColorButton_ = nullptr;
    QPushButton* textBackgroundButton_ = nullptr;
    QCheckBox* textBoldCheck_ = nullptr;
    QCheckBox* textItalicCheck_ = nullptr;
    QLabel* textStatusLabel_ = nullptr;
    std::uint32_t textColorValue_ = 0xFFFFFFFFU;
    std::uint32_t textBackgroundValue_ = 0x99000000U;
    bool updatingTextPanel_ = false;
    core::CaptionId editingCaption_;
    core::ClipId editingTitleClip_;
    QByteArray defaultWorkspaceState_;
    std::function<void(int, core::ClipId)> applyBrowserEffect_;
    bool exportIsPreviewRender_ = false;
    std::uint64_t exportRevisionAtStart_ = 0;
    QString previewCachePath_;
    core::Frame previewCacheStart_ = 0;
    core::Frame previewCacheDuration_ = 0;
    std::uint64_t previewCacheRevision_ = 0;
    bool cachePlaybackActive_ = false;
    // Why the program monitor is on the compositor rather than direct overlay.
    PreviewPathReason previewPathReason_ = PreviewPathReason::Stopped;
    bool monitorAwaitingRender_ = false;
    QProcess* trimTwoUpWorker_ = nullptr;
    bool trimTwoUpActive_ = false;
    int trimTwoUpPhase_ = 0;
    core::Frame trimTwoUpOutgoing_ = -1;
    core::Frame trimTwoUpIncoming_ = -1;
    core::Frame trimTwoUpRenderedOutgoing_ = -1;
    core::Frame trimTwoUpRenderedIncoming_ = -1;
    QImage trimTwoUpOutgoingImage_;
    std::int32_t exportWidth_ = 1280;
    std::int32_t exportHeight_ = 720;
    std::int64_t exportBitrate_ = 5'000'000;
    core::ClipId mediaPlaybackClip_;
    int playbackDirection_ = 1;
    bool playbackRequested_ = false;
    bool updatingSeekSlider_ = false;
    bool exportCancelled_ = false;
    QString exportOutputPath_;
    core::Frame exportStartFrame_ = 0;
    core::Frame exportDurationFrames_ = 0;
    QStringList exportOverlayPaths_;
    QByteArray exportErrorBuffer_;
    QByteArray exportDiagnostics_;
    QSet<QString> activeCacheJobs_;
    struct TimelineThumbnail final {
        QString key;
        QImage image;
    };
    std::unordered_map<std::uint64_t, TimelineThumbnail> timelineThumbnails_;
    struct ClipboardClip final {
        core::TrackId sourceTrack;
        core::TrackKind kind = core::TrackKind::Video;
        core::Clip clip;
        core::Frame offset = 0;
    };
    std::vector<ClipboardClip> clipClipboard_;
};

} // namespace videx::ui
