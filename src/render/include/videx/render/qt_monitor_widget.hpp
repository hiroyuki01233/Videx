#pragma once

#include <QImage>
#include <QPointF>
#include <QRhiWidget>
#include <QString>
#include <functional>
#include <vector>
#include <cstdint>

class QRhiCommandBuffer;
class QComboBox;
class QLabel;
class QPainter;
class QResizeEvent;
class QMouseEvent;

namespace videx::render {

struct MonitorTransform final {
    double positionX = 0.0;
    double positionY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDegrees = 0.0;
    double anchorX = 0.5;
    double anchorY = 0.5;
};

// Full-canvas still (a title) painted over the video frame so title sections
// can keep the smooth continuous-playback path instead of CPU compositing.
// The static transform mirrors the compositor's placement semantics.
struct MonitorTitleOverlay final {
    QImage image;
    double opacity = 1.0;
    double positionX = 0.0;
    double positionY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDegrees = 0.0;
    double anchorX = 0.5;
    double anchorY = 0.5;
    // Crop is a fraction of the title raster removed from each edge. Applied as
    // a clip rectangle in the overlay's own (pre-transform) space so it matches
    // the compositor, which crops before transforming.
    double cropLeft = 0.0;
    double cropRight = 0.0;
    double cropTop = 0.0;
    double cropBottom = 0.0;
};

// Mask and colour/blur parameters, in the same units the compositor uses.
struct MonitorMask final {
    int shape = 0; // 0 none, 1 rectangle, 2 ellipse
    double centerX = 0.5;
    double centerY = 0.5;
    double width = 1.0;
    double height = 1.0;
    double feather = 0.0;
    bool inverted = false;
};

struct MonitorEffects final {
    double brightness = 0.0;
    double contrast = 0.0;
    double saturation = 0.0;
    double blur = 0.0;
    double vignette = 0.0;
};

// Applies a mask and colour/blur effects to a still image using the same pixel
// maths as the program monitor's own compositing pass. The result depends only
// on the inputs, so callers may cache it — which is what keeps title stills
// carrying static effects on the native-resolution overlay path instead of
// falling back to the frame server.
[[nodiscard]] QImage applyStillAdjustments(const QImage& source, const MonitorMask& mask,
                                           const MonitorEffects& effects);

struct MonitorCrop final {
    double left = 0.0;
    double right = 0.0;
    double top = 0.0;
    double bottom = 0.0;
};

class MonitorViewport;

class QtMonitorWidget final : public QRhiWidget {
  public:
    using TransformEditHandler = std::function<void(const MonitorTransform&, bool)>;
    using CropEditHandler = std::function<void(const MonitorCrop&, bool)>;

    explicit QtMonitorWidget(const QString& title, QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void clearFrame();
    void setTitleOverlays(std::vector<MonitorTitleOverlay> overlays);
    void setFrameOpacity(double opacity);
    void setOverlayText(const QString& text);
    void setOverlayStyle(double positionX, double positionY, double fontSize,
                         std::uint32_t textColor, std::uint32_t backgroundColor,
                         bool bold, bool italic);
    void setFrameTransform(double positionX, double positionY, double scaleX, double scaleY,
                           double rotationDegrees, double anchorX, double anchorY);
    void setFrameCrop(double left, double right, double top, double bottom);
    void setFrameMask(int shape, double centerX, double centerY, double width, double height,
                      double feather, bool inverted);
    void setVideoEffects(double brightness, double contrast, double saturation, double blur,
                         double vignette);

    // Direct-manipulation editing of the inspected clip. The committed values are
    // the model state currently baked into the displayed frame; drags preview a
    // delta on top of it and report transient values until the release commit.
    void setEditTarget(bool hasTarget, const MonitorTransform& committed,
                       const MonitorCrop& committedCrop);
    // Update the committed model values without re-baselining the displayed
    // frame: used right after a commit, while the re-rendered frame is still
    // in flight, so the preview does not jump back to the old position.
    void setEditTargetKeepBaked(const MonitorTransform& committed,
                                const MonitorCrop& committedCrop);
    // Confines the edit outline/handles to a sub-rect of the target's content
    // (normalized 0..1). Titles pass their text bounds so the selection box
    // hugs the text instead of framing the whole canvas.
    void setEditTargetContentRect(double left, double top, double width, double height);
    // Layered targets (one layer among several) keep the displayed frame
    // static during drags: only the outline follows the pointer, so the
    // background never appears to move with the dragged element.
    void setLayeredTarget(bool layered);
    void setTransformEditHandler(TransformEditHandler handler);
    void setCropEditHandler(CropEditHandler handler);
    void setCropEditMode(bool enabled);
    [[nodiscard]] bool cropEditMode() const noexcept;
    // Called when Escape is pressed while no drag is active (e.g. leave fullscreen).
    void setEscapeHandler(std::function<void()> handler);
    // Fullscreen mode: hides the header row and stops the viewport from
    // claiming Space, so transport shortcuts work while fullscreen.
    void setFullscreenMode(bool fullscreen);
    // Fired on a double-click that hits neither the caption overlay nor an
    // edit handle; the host toggles fullscreen (Premiere-style).
    void setFullscreenRequestHandler(std::function<void()> handler);
    // Called when the monitor is left-clicked without hitting an edit handle.
    // insideFrame reports whether the click landed on the video image, and the
    // normalized canvas position (0..1) lets the host hit-test layers so the
    // element under the pointer gets selected.
    void setSelectRequestHandler(std::function<void(bool, double, double)> handler);
    // Direct manipulation of the caption overlay: reports the normalized
    // overlay position while dragging (committed=false) and once on release
    // (committed=true). Double-clicking the overlay fires the edit request.
    using TextDragHandler = std::function<void(double, double, bool)>;
    void setTextDragHandler(TextDragHandler handler);
    void setTextEditRequestHandler(std::function<void()> handler);
    // Fired when the caption overlay is clicked without being moved, so the
    // host can focus the Text panel on it.
    void setTextClickHandler(std::function<void()> handler);
    // Reference resolution for percentage zoom: 100% shows one pixel of a
    // reference-sized frame per screen pixel even when a lower-resolution
    // playback frame is displayed.
    void setZoomReferenceSize(int width, int height);
    void setDiagnosticsText(const QString& text);
    // Optimistic preview driven from Inspector edits (no commit).
    void setPreviewTransform(const MonitorTransform& transform);
    void setPreviewCrop(const MonitorCrop& crop);

    // Exposed for UI interaction tests.
    [[nodiscard]] QWidget* viewportWidget() const noexcept;

    void setZoomFit();
    void setZoomFactor(double factor);
    [[nodiscard]] bool zoomIsFit() const noexcept;
    [[nodiscard]] double zoomFactor() const noexcept;

  protected:
    void initialize(QRhiCommandBuffer* commandBuffer) override;
    void render(QRhiCommandBuffer* commandBuffer) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    friend class MonitorViewport;

    enum class Handle {
        None,
        Position,
        ScaleTopLeft,
        ScaleTop,
        ScaleTopRight,
        ScaleLeft,
        ScaleRight,
        ScaleBottomLeft,
        ScaleBottom,
        ScaleBottomRight,
        Rotate,
        Anchor,
        CropLeft,
        CropRight,
        CropTop,
        CropBottom,
        CropTopLeft,
        CropTopRight,
        CropBottomLeft,
        CropBottomRight,
    };

    void ensureComposedFrame();
    void updateOverlayGeometry();
    void applyOverlayStyleSheet();
    void syncZoomCombo();

    [[nodiscard]] double displayScale() const;
    [[nodiscard]] QPointF displayTopLeft() const;
    [[nodiscard]] QPointF viewToCanvas(const QPointF& viewPoint) const;
    [[nodiscard]] QPointF canvasToView(const QPointF& canvasPoint) const;
    [[nodiscard]] QPointF anchorCanvasPoint(const MonitorTransform& transform) const;
    [[nodiscard]] QPointF contentToCanvas(const MonitorTransform& transform,
                                          const QPointF& contentPoint) const;
    [[nodiscard]] QPointF canvasToContent(const MonitorTransform& transform,
                                          const QPointF& canvasPoint) const;
    struct ContentExtents final {
        double left = 0.0;
        double right = 0.0;
        double top = 0.0;
        double bottom = 0.0;
    };
    [[nodiscard]] ContentExtents contentExtents(const MonitorCrop& crop) const;
    [[nodiscard]] Handle hitTest(const QPointF& viewPoint) const;
    void applyHoverCursor(Handle handle, const QPointF& viewPoint);
    void paintViewport(QPainter& painter);
    void beginDrag(Handle handle, const QPointF& viewPoint);
    void updateDrag(const QPointF& viewPoint, Qt::KeyboardModifiers modifiers);
    void finishDrag();
    void cancelDrag();

    MonitorViewport* viewport_ = nullptr;
    QComboBox* zoomCombo_ = nullptr;
    QImage frame_;
    QImage composedFrame_;
    std::vector<MonitorTitleOverlay> titleOverlays_;
    bool composedDirty_ = true;
    double frameOpacity_ = 1.0;
    QLabel* overlayLabel_ = nullptr;
    double overlayPositionX_ = 0.5;
    double overlayPositionY_ = 0.85;
    double overlayFontSize_ = 42.0;
    std::uint32_t overlayTextColor_ = 0xFFFFFFFFU;
    std::uint32_t overlayBackgroundColor_ = 0x99000000U;
    bool overlayBold_ = false;
    bool overlayItalic_ = false;
    double positionX_ = 0.0;
    double positionY_ = 0.0;
    double scaleX_ = 1.0;
    double scaleY_ = 1.0;
    double rotationDegrees_ = 0.0;
    double anchorX_ = 0.5;
    double anchorY_ = 0.5;
    double cropLeft_ = 0.0;
    double cropRight_ = 0.0;
    double cropTop_ = 0.0;
    double cropBottom_ = 0.0;
    int maskShape_ = 0;
    double maskCenterX_ = 0.5;
    double maskCenterY_ = 0.5;
    double maskWidth_ = 1.0;
    double maskHeight_ = 1.0;
    double maskFeather_ = 0.0;
    bool maskInverted_ = false;
    double brightness_ = 0.0;
    double contrast_ = 0.0;
    double saturation_ = 0.0;
    double blur_ = 0.0;
    double vignette_ = 0.0;

    bool hasEditTarget_ = false;
    double contentRectLeft_ = 0.0;
    double contentRectTop_ = 0.0;
    double contentRectWidth_ = 1.0;
    double contentRectHeight_ = 1.0;
    bool layeredTarget_ = false;
    MonitorTransform committedTransform_;
    MonitorCrop committedCrop_;
    MonitorTransform transientTransform_;
    MonitorCrop transientCrop_;
    // Transform baked into the displayed frame; the preview draws the delta
    // between transient and baked, so a commit does not snap the image back
    // while the fresh render is in flight.
    MonitorTransform bakedTransform_;
    TransformEditHandler transformEditHandler_;
    CropEditHandler cropEditHandler_;
    bool cropEditMode_ = false;

    Handle activeHandle_ = Handle::None;
    bool dragging_ = false;
    QPointF dragStartCanvas_;
    MonitorTransform dragStartTransform_;
    MonitorCrop dragStartCrop_;

    bool zoomFit_ = true;
    double zoomFactor_ = 1.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
    bool panning_ = false;
    QPointF panLastView_;

    bool showOverlays_ = true;
    bool showSafeMargins_ = false;
    bool showGrid_ = false;
    bool showCenterCross_ = true;
    bool showTransparencyGrid_ = false;
    bool snapUserEnabled_ = true;
    bool showDiagnostics_ = false;
    QString diagnosticsText_;
    std::function<void()> escapeHandler_;
    std::function<void()> fullscreenRequestHandler_;
    bool fullscreenMode_ = false;
    QWidget* headerRow_ = nullptr;
    std::function<void(bool, double, double)> selectRequestHandler_;
    TextDragHandler textDragHandler_;
    std::function<void()> textEditRequestHandler_;
    std::function<void()> textClickHandler_;
    int zoomReferenceWidth_ = 0;
    int zoomReferenceHeight_ = 0;
    bool textDragging_ = false;
    QPointF textDragStartView_;
    double textDragStartX_ = 0.5;
    double textDragStartY_ = 0.85;

    [[nodiscard]] bool overlayLabelHit(const QPointF& viewportPoint) const;

    void showOverlayMenu(const QPoint& globalPosition);
};

} // namespace videx::render
