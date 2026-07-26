#include <videx/render/qt_monitor_widget.hpp>

#include <QColor>
#include <QComboBox>
#include <QToolButton>
#include <QCursor>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QShortcut>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QString>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtGlobal>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace videx::render {

namespace {

constexpr double handleRadius = 6.0;
constexpr double rotationHandleOffset = 26.0;

// Mask coverage at a normalized point. Shared by the program monitor's own
// compositing pass and applyStillAdjustments so the two cannot drift apart.
double maskFactorAt(const int shape, const double centerX, const double centerY,
                    const double maskWidth, const double maskHeight, const double feather,
                    const bool inverted, const double normalizedX, const double normalizedY) {
    const double dx = std::abs(normalizedX - centerX) / (maskWidth * 0.5);
    const double dy = std::abs(normalizedY - centerY) / (maskHeight * 0.5);
    const double distance = shape == 1 ? std::max(dx, dy) : std::sqrt(dx * dx + dy * dy);
    double factor = distance <= 1.0 ? 1.0 : 0.0;
    if (feather > 0.0) {
        const double edge = feather * 2.0;
        factor = std::clamp((1.0 + edge - distance) / (2.0 * edge), 0.0, 1.0);
    }
    return inverted ? 1.0 - factor : factor;
}

// Brightness/contrast/saturation plus radial vignette for one pixel. nx/ny are
// the pixel's offset from the image centre, normalized to [-1, 1].
QRgb adjustPixel(const QRgb source, const double brightness, const double contrast,
                 const double saturation, const double vignette, const double nx,
                 const double ny) {
    QColor color = QColor::fromRgba(source);
    if (color.alpha() == 0) return source;
    double red = color.redF();
    double green = color.greenF();
    double blue = color.blueF();
    red = (red - 0.5) * (1.0 + contrast) + 0.5 + brightness;
    green = (green - 0.5) * (1.0 + contrast) + 0.5 + brightness;
    blue = (blue - 0.5) * (1.0 + contrast) + 0.5 + brightness;
    const double luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722;
    red = luminance + (red - luminance) * (1.0 + saturation);
    green = luminance + (green - luminance) * (1.0 + saturation);
    blue = luminance + (blue - luminance) * (1.0 + saturation);
    if (vignette > 0.0) {
        const double shade = std::clamp(1.0 - vignette * (nx * nx + ny * ny), 0.0, 1.0);
        red *= shade;
        green *= shade;
        blue *= shade;
    }
    color.setRgbF(std::clamp(red, 0.0, 1.0), std::clamp(green, 0.0, 1.0),
                  std::clamp(blue, 0.0, 1.0), color.alphaF());
    return color.rgba();
}

// Downscale/upscale blur, matching the monitor's approximation.
QImage blurApproximation(QImage image, const double blur) {
    if (blur <= 0.01) return image;
    const int divisor = std::clamp(static_cast<int>(std::lround(1.0 + blur * 0.35)), 1, 12);
    const QSize reduced(std::max(1, image.width() / divisor),
                        std::max(1, image.height() / divisor));
    return image.scaled(reduced, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
constexpr double pi = 3.14159265358979323846;

QCursor rotationCursor() {
    static const QCursor cursor = [] {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(20, 22, 26), 4.0));
        painter.drawArc(QRectF(5, 5, 14, 14), 40 * 16, 250 * 16);
        painter.setPen(QPen(QColor(240, 245, 255), 2.0));
        painter.drawArc(QRectF(5, 5, 14, 14), 40 * 16, 250 * 16);
        QPolygonF arrow;
        arrow << QPointF(19.5, 9.5) << QPointF(15.0, 4.5) << QPointF(14.0, 11.5);
        painter.setBrush(QColor(240, 245, 255));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(arrow);
        painter.end();
        return QCursor(pixmap, 12, 12);
    }();
    return cursor;
}

bool isCropHandle(const int handle, const int firstCropHandle) {
    return handle >= firstCropHandle;
}

Qt::CursorShape resizeCursorForAngle(double degrees) {
    double normalized = std::fmod(degrees, 180.0);
    if (normalized < 0.0) normalized += 180.0;
    if (normalized < 22.5 || normalized >= 157.5) return Qt::SizeHorCursor;
    if (normalized < 67.5) return Qt::SizeBDiagCursor;
    if (normalized < 112.5) return Qt::SizeVerCursor;
    return Qt::SizeFDiagCursor;
}

} // namespace

class MonitorViewport final : public QWidget {
  public:
    explicit MonitorViewport(QtMonitorWidget* owner) : QWidget(owner), owner_(owner) {
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(160, 90);
    }

  protected:
    bool event(QEvent* genericEvent) override {
        if (genericEvent->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(genericEvent);
            // In fullscreen the monitor is the whole UI: Space must stay a
            // transport shortcut, so only claim it for pan while windowed.
            if (keyEvent->key() == Qt::Key_Space && !owner_->fullscreenMode_ &&
                (spaceHeld_ || underMouse())) {
                genericEvent->accept();
                return true;
            }
        }
        return QWidget::event(genericEvent);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        owner_->paintViewport(painter);
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        owner_->showOverlayMenu(event->globalPos());
        event->accept();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton ||
            (event->button() == Qt::LeftButton && spaceHeld_)) {
            owner_->panning_ = true;
            owner_->panLastView_ = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            if (owner_->textDragHandler_ && owner_->overlayLabelHit(event->position())) {
                owner_->textDragging_ = true;
                owner_->textDragStartView_ = event->position();
                owner_->textDragStartX_ = owner_->overlayPositionX_;
                owner_->textDragStartY_ = owner_->overlayPositionY_;
                setCursor(Qt::ClosedHandCursor);
                event->accept();
                return;
            }
            const QtMonitorWidget::Handle handle = owner_->hitTest(event->position());
            if (handle != QtMonitorWidget::Handle::None) {
                owner_->beginDrag(handle, event->position());
                event->accept();
                return;
            }
            if (owner_->selectRequestHandler_ && !owner_->frame_.isNull()) {
                const QPointF canvas = owner_->viewToCanvas(event->position());
                const bool insideFrame =
                    canvas.x() >= 0.0 && canvas.x() <= owner_->frame_.width() &&
                    canvas.y() >= 0.0 && canvas.y() <= owner_->frame_.height();
                owner_->selectRequestHandler_(
                    insideFrame,
                    canvas.x() / std::max(1, owner_->frame_.width()),
                    canvas.y() / std::max(1, owner_->frame_.height()));
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (owner_->panning_) {
            const QPointF delta = event->position() - owner_->panLastView_;
            owner_->panLastView_ = event->position();
            owner_->panX_ += delta.x();
            owner_->panY_ += delta.y();
            update();
            event->accept();
            return;
        }
        if (owner_->textDragging_) {
            const QPointF delta = event->position() - owner_->textDragStartView_;
            const double newX = std::clamp(
                owner_->textDragStartX_ + delta.x() / std::max(1, owner_->width()),
                0.0, 1.0);
            const double newY = std::clamp(
                owner_->textDragStartY_ + delta.y() / std::max(1, owner_->height()),
                0.0, 1.0);
            owner_->overlayPositionX_ = newX;
            owner_->overlayPositionY_ = newY;
            owner_->updateOverlayGeometry();
            if (owner_->textDragHandler_) {
                owner_->textDragHandler_(newX, newY, false);
            }
            event->accept();
            return;
        }
        if (owner_->dragging_) {
            owner_->updateDrag(event->position(), event->modifiers());
            event->accept();
            return;
        }
        if (owner_->textDragHandler_ && owner_->overlayLabelHit(event->position())) {
            setCursor(Qt::SizeAllCursor);
            QWidget::mouseMoveEvent(event);
            return;
        }
        owner_->applyHoverCursor(owner_->hitTest(event->position()), event->position());
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if ((event->button() == Qt::MiddleButton ||
             event->button() == Qt::LeftButton) &&
            owner_->panning_) {
            owner_->panning_ = false;
            if (spaceHeld_) {
                setCursor(Qt::OpenHandCursor);
            } else {
                unsetCursor();
            }
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && owner_->textDragging_) {
            owner_->textDragging_ = false;
            unsetCursor();
            // Commit only when the position actually changed; a plain click
            // (or the first half of a double-click) must not push an undo step.
            const bool moved =
                owner_->overlayPositionX_ != owner_->textDragStartX_ ||
                owner_->overlayPositionY_ != owner_->textDragStartY_;
            if (moved && owner_->textDragHandler_) {
                owner_->textDragHandler_(owner_->overlayPositionX_,
                                         owner_->overlayPositionY_, true);
            } else if (!moved && owner_->textClickHandler_) {
                owner_->textClickHandler_();
            }
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && owner_->dragging_) {
            owner_->finishDrag();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && owner_->textEditRequestHandler_ &&
            owner_->overlayLabelHit(event->position())) {
            owner_->textDragging_ = false;
            owner_->textEditRequestHandler_();
            event->accept();
            return;
        }
        // Note: double-click no longer resets the zoom; the chosen percentage
        // must survive clicks (use Fit in the zoom combo or Shift+F instead).
        // Fullscreen is toggled via Ctrl+` / Ctrl+Shift+F only: a double-click
        // toggle would race the press-side clip selection.
        QWidget::mouseDoubleClickEvent(event);
    }
    void wheelEvent(QWheelEvent* event) override {
        if ((event->modifiers() & Qt::ControlModifier) != 0) {
            const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
            const double current =
                owner_->zoomFit_ ? owner_->displayScale() : owner_->zoomFactor_;
            owner_->setZoomFactor(std::clamp(current * factor, 0.05, 8.0));
            event->accept();
            return;
        }
        QWidget::wheelEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape && owner_->textDragging_) {
            owner_->textDragging_ = false;
            owner_->overlayPositionX_ = owner_->textDragStartX_;
            owner_->overlayPositionY_ = owner_->textDragStartY_;
            owner_->updateOverlayGeometry();
            if (owner_->textDragHandler_) {
                owner_->textDragHandler_(owner_->textDragStartX_,
                                         owner_->textDragStartY_, false);
            }
            unsetCursor();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && owner_->dragging_) {
            owner_->cancelDrag();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && owner_->escapeHandler_) {
            owner_->escapeHandler_();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
            spaceHeld_ = true;
            if (!owner_->dragging_) {
                setCursor(Qt::OpenHandCursor);
            }
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }
    void keyReleaseEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
            spaceHeld_ = false;
            if (!owner_->panning_) {
                unsetCursor();
            }
            event->accept();
            return;
        }
        QWidget::keyReleaseEvent(event);
    }

  private:
    friend class QtMonitorWidget;
    QtMonitorWidget* owner_ = nullptr;
    bool spaceHeld_ = false;
};

QtMonitorWidget::QtMonitorWidget(const QString& title, QWidget* parent) : QRhiWidget(parent) {
#if defined(Q_OS_WIN)
    setApi(Api::Direct3D11);
#elif defined(Q_OS_MACOS)
    setApi(Api::Metal);
#endif

    setAutoRenderTarget(true);
    setColorBufferFormat(TextureFormat::RGBA8);
    setMinimumSize(320, 180);

    auto* headerRow = new QWidget(this);
    headerRow_ = headerRow;
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(6, 2, 6, 2);
    auto* titleLabel = new QLabel(title, headerRow);
    titleLabel->setStyleSheet(QStringLiteral("color: #b9bec7; background: transparent;"));
    zoomCombo_ = new QComboBox(headerRow);
    zoomCombo_->setEditable(true);
    zoomCombo_->addItem(tr("Fit"), 0.0);
    for (const int percent : {10, 25, 50, 75, 100, 150, 200, 400}) {
        zoomCombo_->addItem(QStringLiteral("%1%").arg(percent), percent / 100.0);
    }
    zoomCombo_->setCurrentIndex(0);
    zoomCombo_->setFixedWidth(88);
    connect(zoomCombo_, &QComboBox::activated, this, [this](const int index) {
        const double factor = zoomCombo_->itemData(index).toDouble();
        if (factor <= 0.0) {
            setZoomFit();
        } else {
            setZoomFactor(factor);
        }
    });
    connect(zoomCombo_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        const QString text = zoomCombo_->currentText().trimmed();
        if (text.compare(tr("Fit"), Qt::CaseInsensitive) == 0) {
            setZoomFit();
            return;
        }
        bool parsed = false;
        const double percent = QString(text).remove(QLatin1Char('%')).toDouble(&parsed);
        if (parsed && percent >= 5.0 && percent <= 800.0) {
            setZoomFactor(percent / 100.0);
        } else {
            syncZoomCombo();
        }
    });
    headerLayout->addWidget(titleLabel, 1, Qt::AlignCenter);
    headerLayout->addWidget(zoomCombo_, 0);
    auto* fullscreenButton = new QToolButton(headerRow);
    fullscreenButton->setText(QStringLiteral("⛶"));
    fullscreenButton->setToolTip(tr("Fullscreen preview (Ctrl+`)"));
    fullscreenButton->setAutoRaise(true);
    connect(fullscreenButton, &QToolButton::clicked, this, [this] {
        if (fullscreenRequestHandler_) {
            fullscreenRequestHandler_();
        }
    });
    headerLayout->addWidget(fullscreenButton, 0);

    viewport_ = new MonitorViewport(this);

    overlayLabel_ = new QLabel(this);
    overlayLabel_->setAlignment(Qt::AlignCenter);
    overlayLabel_->setWordWrap(true);
    // Clicks on the caption overlay are handled by the viewport so text can be
    // dragged and double-clicked like any other monitor element.
    overlayLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    overlayLabel_->setStyleSheet(QStringLiteral(
        "color: white; background: rgba(0, 0, 0, 170); padding: 5px; font-size: 16px;"));
    overlayLabel_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(2);
    layout->addWidget(headerRow, 0);
    layout->addWidget(viewport_, 1);
    setLayout(layout);
    overlayLabel_->raise();
    updateOverlayGeometry();

    const auto addZoomShortcut = [this](const QString& sequence, auto action) {
        auto* shortcut = new QShortcut(QKeySequence(sequence), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, std::move(action));
    };
    addZoomShortcut(QStringLiteral("Shift+F"), [this] { setZoomFit(); });
    addZoomShortcut(QStringLiteral("Shift+5"), [this] { setZoomFactor(0.5); });
    addZoomShortcut(QStringLiteral("Shift+1"), [this] { setZoomFactor(1.0); });
}

void QtMonitorWidget::showOverlayMenu(const QPoint& globalPosition) {
    QMenu menu(this);
    const auto addToggle = [&menu](const QString& text, bool& flag) {
        QAction* action = menu.addAction(text);
        action->setCheckable(true);
        action->setChecked(flag);
        QObject::connect(action, &QAction::toggled, [&flag](const bool checked) {
            flag = checked;
        });
    };
    addToggle(tr("Show Overlays"), showOverlays_);
    menu.addSeparator();
    addToggle(tr("Center Cross"), showCenterCross_);
    addToggle(tr("Grid (Thirds)"), showGrid_);
    addToggle(tr("Safe Margins"), showSafeMargins_);
    addToggle(tr("Transparency Grid"), showTransparencyGrid_);
    addToggle(tr("Playback Diagnostics"), showDiagnostics_);
    menu.addSeparator();
    addToggle(tr("Snap"), snapUserEnabled_);
    menu.addSeparator();
    QAction* fitAction = menu.addAction(tr("Zoom: Fit\tShift+F"));
    QAction* halfAction = menu.addAction(tr("Zoom: 50%\tShift+5"));
    QAction* fullAction = menu.addAction(tr("Zoom: 100%\tShift+1"));
    QAction* chosen = menu.exec(globalPosition);
    if (chosen == fitAction) setZoomFit();
    else if (chosen == halfAction) setZoomFactor(0.5);
    else if (chosen == fullAction) setZoomFactor(1.0);
    viewport_->update();
}

void QtMonitorWidget::setFrame(const QImage& frame) {
    frame_ = frame.copy();
    composedDirty_ = true;
    viewport_->update();
}

void QtMonitorWidget::setTitleOverlays(std::vector<MonitorTitleOverlay> overlays) {
    titleOverlays_ = std::move(overlays);
    if (viewport_ != nullptr) viewport_->update();
}

void QtMonitorWidget::clearFrame() {
    frame_ = {};
    composedFrame_ = {};
    composedDirty_ = true;
    viewport_->update();
}

void QtMonitorWidget::setFrameOpacity(const double opacity) {
    frameOpacity_ = std::clamp(opacity, 0.0, 1.0);
    viewport_->update();
}

void QtMonitorWidget::setOverlayText(const QString& text) {
    overlayLabel_->setText(text);
    overlayLabel_->setVisible(!text.isEmpty());
    if (text.isEmpty()) {
        textDragging_ = false;
    }
}

void QtMonitorWidget::setOverlayStyle(const double positionX, const double positionY,
                                      const double fontSize, const std::uint32_t textColor,
                                      const std::uint32_t backgroundColor, const bool bold,
                                      const bool italic) {
    overlayPositionX_ = std::clamp(positionX, 0.0, 1.0);
    overlayPositionY_ = std::clamp(positionY, 0.0, 1.0);
    overlayFontSize_ = std::clamp(fontSize, 8.0, 300.0);
    overlayTextColor_ = textColor;
    overlayBackgroundColor_ = backgroundColor;
    overlayBold_ = bold;
    overlayItalic_ = italic;
    applyOverlayStyleSheet();
    updateOverlayGeometry();
}

void QtMonitorWidget::applyOverlayStyleSheet() {
    if (overlayLabel_ == nullptr) return;
    const auto cssColor = [](const std::uint32_t rgba) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg((rgba >> 24U) & 0xFFU).arg((rgba >> 16U) & 0xFFU)
            .arg((rgba >> 8U) & 0xFFU).arg(rgba & 0xFFU);
    };
    overlayLabel_->setStyleSheet(
        QStringLiteral("color:%1; background:%2; padding:5px; font-size:%3px;"
                       "font-weight:%4; font-style:%5;")
            .arg(cssColor(overlayTextColor_), cssColor(overlayBackgroundColor_))
            .arg(overlayFontSize_ * std::max(0.25, width() / 1280.0), 0, 'f', 1)
            .arg(overlayBold_ ? QStringLiteral("bold") : QStringLiteral("normal"),
                 overlayItalic_ ? QStringLiteral("italic") : QStringLiteral("normal")));
}

void QtMonitorWidget::updateOverlayGeometry() {
    if (overlayLabel_ == nullptr) return;
    const int overlayWidth = std::max(120, static_cast<int>(width() * 0.8));
    overlayLabel_->setFixedWidth(overlayWidth);
    overlayLabel_->adjustSize();
    const int x = static_cast<int>(overlayPositionX_ * width() - overlayLabel_->width() * 0.5);
    const int y = static_cast<int>(overlayPositionY_ * height() - overlayLabel_->height() * 0.5);
    overlayLabel_->move(std::clamp(x, 0, std::max(0, width() - overlayLabel_->width())),
                        std::clamp(y, 0, std::max(0, height() - overlayLabel_->height())));
    overlayLabel_->raise();
}

void QtMonitorWidget::setFrameTransform(const double positionX, const double positionY,
                                        const double scaleX, const double scaleY,
                                        const double rotationDegrees, const double anchorX,
                                        const double anchorY) {
    positionX_ = positionX;
    positionY_ = positionY;
    scaleX_ = scaleX;
    scaleY_ = scaleY;
    rotationDegrees_ = rotationDegrees;
    anchorX_ = anchorX;
    anchorY_ = anchorY;
    composedDirty_ = true;
    viewport_->update();
}

void QtMonitorWidget::setFrameCrop(const double left, const double right, const double top,
                                   const double bottom) {
    cropLeft_ = std::clamp(left, 0.0, 1.0);
    cropRight_ = std::clamp(right, 0.0, 1.0);
    cropTop_ = std::clamp(top, 0.0, 1.0);
    cropBottom_ = std::clamp(bottom, 0.0, 1.0);
    composedDirty_ = true;
    viewport_->update();
}

void QtMonitorWidget::setFrameMask(const int shape, const double centerX, const double centerY,
                                   const double width, const double height, const double feather,
                                   const bool inverted) {
    maskShape_ = std::clamp(shape, 0, 2);
    maskCenterX_ = std::clamp(centerX, 0.0, 1.0);
    maskCenterY_ = std::clamp(centerY, 0.0, 1.0);
    maskWidth_ = std::max(width, 0.001);
    maskHeight_ = std::max(height, 0.001);
    maskFeather_ = std::clamp(feather, 0.0, 0.5);
    maskInverted_ = inverted;
    composedDirty_ = true;
    viewport_->update();
}

void QtMonitorWidget::setVideoEffects(const double brightness, const double contrast,
                                      const double saturation, const double blur,
                                      const double vignette) {
    brightness_ = brightness;
    contrast_ = contrast;
    saturation_ = saturation;
    blur_ = blur;
    vignette_ = vignette;
    composedDirty_ = true;
    viewport_->update();
}

void QtMonitorWidget::setEditTarget(const bool hasTarget, const MonitorTransform& committed,
                                    const MonitorCrop& committedCrop) {
    hasEditTarget_ = hasTarget;
    committedTransform_ = committed;
    committedCrop_ = committedCrop;
    bakedTransform_ = committed;
    if (!dragging_) {
        transientTransform_ = committed;
        transientCrop_ = committedCrop;
    }
    if (!hasTarget) {
        cropEditMode_ = false;
        dragging_ = false;
        activeHandle_ = Handle::None;
    }
    viewport_->update();
}

void QtMonitorWidget::setEditTargetKeepBaked(const MonitorTransform& committed,
                                             const MonitorCrop& committedCrop) {
    if (!hasEditTarget_) {
        setEditTarget(true, committed, committedCrop);
        return;
    }
    committedTransform_ = committed;
    committedCrop_ = committedCrop;
    if (!dragging_) {
        transientTransform_ = committed;
        transientCrop_ = committedCrop;
    }
    viewport_->update();
}

void QtMonitorWidget::setEditTargetContentRect(const double left, const double top,
                                               const double width, const double height) {
    contentRectLeft_ = std::clamp(left, 0.0, 1.0);
    contentRectTop_ = std::clamp(top, 0.0, 1.0);
    contentRectWidth_ = std::clamp(width, 0.01, 1.0 - contentRectLeft_);
    contentRectHeight_ = std::clamp(height, 0.01, 1.0 - contentRectTop_);
    if (viewport_ != nullptr) viewport_->update();
}

void QtMonitorWidget::setLayeredTarget(const bool layered) {
    if (layeredTarget_ == layered) return;
    layeredTarget_ = layered;
    if (viewport_ != nullptr) viewport_->update();
}

QtMonitorWidget::ContentExtents QtMonitorWidget::contentExtents(
    const MonitorCrop& crop) const {
    const double contentWidth = frame_.width();
    const double contentHeight = frame_.height();
    return {std::max(crop.left, contentRectLeft_) * contentWidth,
            std::min(1.0 - crop.right, contentRectLeft_ + contentRectWidth_) *
                contentWidth,
            std::max(crop.top, contentRectTop_) * contentHeight,
            std::min(1.0 - crop.bottom, contentRectTop_ + contentRectHeight_) *
                contentHeight};
}

void QtMonitorWidget::setTransformEditHandler(TransformEditHandler handler) {
    transformEditHandler_ = std::move(handler);
    if (transformEditHandler_) {
        setToolTip(tr("Drag inside: move | corners/edges: scale | outer handle: rotate |"
                      " cross: anchor | Esc: cancel"));
    }
}

void QtMonitorWidget::setCropEditHandler(CropEditHandler handler) {
    cropEditHandler_ = std::move(handler);
}

void QtMonitorWidget::setCropEditMode(const bool enabled) {
    cropEditMode_ = enabled && hasEditTarget_;
    viewport_->update();
}

bool QtMonitorWidget::cropEditMode() const noexcept {
    return cropEditMode_;
}

void QtMonitorWidget::setFullscreenMode(const bool fullscreen) {
    fullscreenMode_ = fullscreen;
    if (headerRow_ != nullptr) {
        headerRow_->setVisible(!fullscreen);
    }
    if (fullscreen && viewport_ != nullptr) {
        viewport_->setFocus(Qt::OtherFocusReason);
    }
}

void QtMonitorWidget::setFullscreenRequestHandler(std::function<void()> handler) {
    fullscreenRequestHandler_ = std::move(handler);
}

void QtMonitorWidget::setEscapeHandler(std::function<void()> handler) {
    escapeHandler_ = std::move(handler);
}

void QtMonitorWidget::setSelectRequestHandler(
    std::function<void(bool, double, double)> handler) {
    selectRequestHandler_ = std::move(handler);
}

void QtMonitorWidget::setTextDragHandler(TextDragHandler handler) {
    textDragHandler_ = std::move(handler);
}

void QtMonitorWidget::setTextEditRequestHandler(std::function<void()> handler) {
    textEditRequestHandler_ = std::move(handler);
}

void QtMonitorWidget::setTextClickHandler(std::function<void()> handler) {
    textClickHandler_ = std::move(handler);
}

void QtMonitorWidget::setZoomReferenceSize(const int width, const int height) {
    zoomReferenceWidth_ = std::max(0, width);
    zoomReferenceHeight_ = std::max(0, height);
    viewport_->update();
}

bool QtMonitorWidget::overlayLabelHit(const QPointF& viewportPoint) const {
    if (overlayLabel_ == nullptr || !overlayLabel_->isVisible() || viewport_ == nullptr) {
        return false;
    }
    const QPoint ownerPoint = viewport_->mapToParent(viewportPoint.toPoint());
    return overlayLabel_->geometry().contains(ownerPoint);
}

void QtMonitorWidget::setDiagnosticsText(const QString& text) {
    diagnosticsText_ = text;
    if (showDiagnostics_) {
        viewport_->update();
    }
}

void QtMonitorWidget::setPreviewTransform(const MonitorTransform& transform) {
    if (!hasEditTarget_ || dragging_) return;
    transientTransform_ = transform;
    viewport_->update();
}

void QtMonitorWidget::setPreviewCrop(const MonitorCrop& crop) {
    if (!hasEditTarget_ || dragging_) return;
    transientCrop_ = crop;
    viewport_->update();
}

QWidget* QtMonitorWidget::viewportWidget() const noexcept {
    return viewport_;
}

void QtMonitorWidget::setZoomFit() {
    zoomFit_ = true;
    panX_ = 0.0;
    panY_ = 0.0;
    syncZoomCombo();
    viewport_->update();
}

void QtMonitorWidget::setZoomFactor(const double factor) {
    zoomFit_ = false;
    zoomFactor_ = std::clamp(factor, 0.05, 8.0);
    syncZoomCombo();
    viewport_->update();
}

bool QtMonitorWidget::zoomIsFit() const noexcept {
    return zoomFit_;
}

double QtMonitorWidget::zoomFactor() const noexcept {
    return zoomFactor_;
}

void QtMonitorWidget::syncZoomCombo() {
    if (zoomCombo_ == nullptr) return;
    const QSignalBlocker blocker(zoomCombo_);
    if (zoomFit_) {
        zoomCombo_->setCurrentIndex(0);
        zoomCombo_->setEditText(tr("Fit"));
    } else {
        zoomCombo_->setEditText(
            QStringLiteral("%1%").arg(std::lround(zoomFactor_ * 100.0)));
    }
}

double QtMonitorWidget::displayScale() const {
    if (frame_.isNull() || viewport_ == nullptr) return 1.0;
    if (!zoomFit_) {
        // Anchor the percentage to the reference resolution so a half-res
        // playback frame is upscaled instead of shrinking the picture.
        if (zoomReferenceWidth_ > 0 && frame_.width() > 0) {
            return zoomFactor_ * static_cast<double>(zoomReferenceWidth_) / frame_.width();
        }
        return zoomFactor_;
    }
    const double sx = static_cast<double>(viewport_->width()) / frame_.width();
    const double sy = static_cast<double>(viewport_->height()) / frame_.height();
    return std::max(0.0001, std::min(sx, sy));
}

QPointF QtMonitorWidget::displayTopLeft() const {
    if (frame_.isNull() || viewport_ == nullptr) return {};
    const double scale = displayScale();
    const double displayWidth = frame_.width() * scale;
    const double displayHeight = frame_.height() * scale;
    double x = (viewport_->width() - displayWidth) * 0.5;
    double y = (viewport_->height() - displayHeight) * 0.5;
    if (!zoomFit_) {
        x += panX_;
        y += panY_;
    }
    return {x, y};
}

QPointF QtMonitorWidget::viewToCanvas(const QPointF& viewPoint) const {
    const double scale = displayScale();
    const QPointF topLeft = displayTopLeft();
    return {(viewPoint.x() - topLeft.x()) / scale, (viewPoint.y() - topLeft.y()) / scale};
}

QPointF QtMonitorWidget::canvasToView(const QPointF& canvasPoint) const {
    const double scale = displayScale();
    const QPointF topLeft = displayTopLeft();
    return {topLeft.x() + canvasPoint.x() * scale, topLeft.y() + canvasPoint.y() * scale};
}

QPointF QtMonitorWidget::anchorCanvasPoint(const MonitorTransform& transform) const {
    return {frame_.width() * 0.5 + transform.positionX,
            frame_.height() * 0.5 + transform.positionY};
}

QPointF QtMonitorWidget::contentToCanvas(const MonitorTransform& transform,
                                         const QPointF& contentPoint) const {
    const double radians = transform.rotationDegrees * pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double anchorPxX = transform.anchorX * frame_.width();
    const double anchorPxY = transform.anchorY * frame_.height();
    const double localX = (contentPoint.x() - anchorPxX) * transform.scaleX;
    const double localY = (contentPoint.y() - anchorPxY) * transform.scaleY;
    const QPointF anchor = anchorCanvasPoint(transform);
    return {anchor.x() + cosine * localX - sine * localY,
            anchor.y() + sine * localX + cosine * localY};
}

QPointF QtMonitorWidget::canvasToContent(const MonitorTransform& transform,
                                         const QPointF& canvasPoint) const {
    const double radians = transform.rotationDegrees * pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const QPointF anchor = anchorCanvasPoint(transform);
    const double dx = canvasPoint.x() - anchor.x();
    const double dy = canvasPoint.y() - anchor.y();
    const double localX = (cosine * dx + sine * dy) /
                          (transform.scaleX == 0.0 ? 1.0 : transform.scaleX);
    const double localY = (-sine * dx + cosine * dy) /
                          (transform.scaleY == 0.0 ? 1.0 : transform.scaleY);
    return {transform.anchorX * frame_.width() + localX,
            transform.anchorY * frame_.height() + localY};
}

QtMonitorWidget::Handle QtMonitorWidget::hitTest(const QPointF& viewPoint) const {
    if (!hasEditTarget_ || frame_.isNull()) {
        return Handle::None;
    }
    const MonitorTransform& transform = transientTransform_;
    const MonitorCrop& crop = transientCrop_;
    const ContentExtents extents = contentExtents(crop);
    const double left = extents.left;
    const double right = extents.right;
    const double top = extents.top;
    const double bottom = extents.bottom;
    const QPointF topLeft = canvasToView(contentToCanvas(transform, {left, top}));
    const QPointF topRight = canvasToView(contentToCanvas(transform, {right, top}));
    const QPointF bottomLeft = canvasToView(contentToCanvas(transform, {left, bottom}));
    const QPointF bottomRight = canvasToView(contentToCanvas(transform, {right, bottom}));
    const QPointF topMid = (topLeft + topRight) * 0.5;
    const QPointF bottomMid = (bottomLeft + bottomRight) * 0.5;
    const QPointF leftMid = (topLeft + bottomLeft) * 0.5;
    const QPointF rightMid = (topRight + bottomRight) * 0.5;
    const auto nearPoint = [&viewPoint](const QPointF& point) {
        return QLineF(viewPoint, point).length() <= handleRadius + 3.0;
    };

    if (cropEditMode_) {
        if (nearPoint(topLeft)) return Handle::CropTopLeft;
        if (nearPoint(topRight)) return Handle::CropTopRight;
        if (nearPoint(bottomLeft)) return Handle::CropBottomLeft;
        if (nearPoint(bottomRight)) return Handle::CropBottomRight;
        if (nearPoint(topMid)) return Handle::CropTop;
        if (nearPoint(bottomMid)) return Handle::CropBottom;
        if (nearPoint(leftMid)) return Handle::CropLeft;
        if (nearPoint(rightMid)) return Handle::CropRight;
        return Handle::None;
    }

    const QPointF anchorView = canvasToView(anchorCanvasPoint(transform));
    if (nearPoint(anchorView)) return Handle::Anchor;
    const QPointF rotationPoint =
        topMid + (topMid - bottomMid) / std::max(1.0, QLineF(topMid, bottomMid).length()) *
                     rotationHandleOffset;
    if (nearPoint(rotationPoint)) return Handle::Rotate;
    if (nearPoint(topLeft)) return Handle::ScaleTopLeft;
    if (nearPoint(topRight)) return Handle::ScaleTopRight;
    if (nearPoint(bottomLeft)) return Handle::ScaleBottomLeft;
    if (nearPoint(bottomRight)) return Handle::ScaleBottomRight;
    if (nearPoint(topMid)) return Handle::ScaleTop;
    if (nearPoint(bottomMid)) return Handle::ScaleBottom;
    if (nearPoint(leftMid)) return Handle::ScaleLeft;
    if (nearPoint(rightMid)) return Handle::ScaleRight;
    QPolygonF outline;
    outline << topLeft << topRight << bottomRight << bottomLeft;
    if (outline.containsPoint(viewPoint, Qt::OddEvenFill)) return Handle::Position;
    return Handle::None;
}

void QtMonitorWidget::applyHoverCursor(const Handle handle, const QPointF& viewPoint) {
    if (viewport_ == nullptr) return;
    switch (handle) {
    case Handle::Position:
        viewport_->setCursor(Qt::SizeAllCursor);
        return;
    case Handle::Anchor:
        viewport_->setCursor(Qt::CrossCursor);
        return;
    case Handle::Rotate:
        viewport_->setCursor(rotationCursor());
        return;
    case Handle::ScaleTopLeft:
    case Handle::ScaleTopRight:
    case Handle::ScaleBottomLeft:
    case Handle::ScaleBottomRight:
    case Handle::ScaleTop:
    case Handle::ScaleBottom:
    case Handle::ScaleLeft:
    case Handle::ScaleRight:
    case Handle::CropTopLeft:
    case Handle::CropTopRight:
    case Handle::CropBottomLeft:
    case Handle::CropBottomRight:
    case Handle::CropTop:
    case Handle::CropBottom:
    case Handle::CropLeft:
    case Handle::CropRight: {
        const QPointF anchorView = canvasToView(anchorCanvasPoint(transientTransform_));
        const double angle =
            std::atan2(viewPoint.y() - anchorView.y(), viewPoint.x() - anchorView.x()) *
            180.0 / pi;
        viewport_->setCursor(resizeCursorForAngle(angle));
        return;
    }
    case Handle::None:
        break;
    }
    viewport_->unsetCursor();
}

void QtMonitorWidget::beginDrag(const Handle handle, const QPointF& viewPoint) {
    if (!hasEditTarget_) return;
    dragging_ = true;
    activeHandle_ = handle;
    dragStartCanvas_ = viewToCanvas(viewPoint);
    dragStartTransform_ = transientTransform_;
    dragStartCrop_ = transientCrop_;
    viewport_->setFocus(Qt::MouseFocusReason);
    if (handle == Handle::Position) {
        viewport_->setCursor(Qt::ClosedHandCursor);
    }
}

void QtMonitorWidget::updateDrag(const QPointF& viewPoint, const Qt::KeyboardModifiers modifiers) {
    if (!dragging_) return;
    const QPointF canvasPoint = viewToCanvas(viewPoint);
    const bool snapEnabled =
        snapUserEnabled_ && (modifiers & Qt::ControlModifier) == 0;
    MonitorTransform next = dragStartTransform_;
    MonitorCrop nextCrop = dragStartCrop_;

    const auto anchorStart = anchorCanvasPoint(dragStartTransform_);
    switch (activeHandle_) {
    case Handle::Position: {
        double deltaX = canvasPoint.x() - dragStartCanvas_.x();
        double deltaY = canvasPoint.y() - dragStartCanvas_.y();
        if ((modifiers & Qt::ShiftModifier) != 0) {
            if (std::abs(deltaX) >= std::abs(deltaY)) deltaY = 0.0;
            else deltaX = 0.0;
        }
        next.positionX = dragStartTransform_.positionX + deltaX;
        next.positionY = dragStartTransform_.positionY + deltaY;
        if (snapEnabled) {
            const double snapThreshold = 8.0 / displayScale();
            if (std::abs(next.positionX) <= snapThreshold) next.positionX = 0.0;
            if (std::abs(next.positionY) <= snapThreshold) next.positionY = 0.0;
        }
        break;
    }
    case Handle::ScaleTopLeft:
    case Handle::ScaleTopRight:
    case Handle::ScaleBottomLeft:
    case Handle::ScaleBottomRight: {
        const double startDistance =
            std::max(1.0, QLineF(anchorStart, dragStartCanvas_).length());
        const double currentDistance = QLineF(anchorStart, canvasPoint).length();
        double ratio = currentDistance / startDistance;
        ratio = std::clamp(ratio, 0.001, 1000.0);
        const bool uniform = (modifiers & Qt::ShiftModifier) == 0;
        if (uniform) {
            next.scaleX = std::clamp(dragStartTransform_.scaleX * ratio, 0.01, 100.0);
            next.scaleY = std::clamp(dragStartTransform_.scaleY * ratio, 0.01, 100.0);
        } else {
            const QPointF startLocal = canvasToContent(dragStartTransform_, dragStartCanvas_);
            const QPointF currentLocal =
                canvasToContent(dragStartTransform_, canvasPoint);
            const double anchorPxX = dragStartTransform_.anchorX * frame_.width();
            const double anchorPxY = dragStartTransform_.anchorY * frame_.height();
            const double ratioX =
                std::abs(startLocal.x() - anchorPxX) < 1.0
                    ? 1.0
                    : (currentLocal.x() - anchorPxX) / (startLocal.x() - anchorPxX);
            const double ratioY =
                std::abs(startLocal.y() - anchorPxY) < 1.0
                    ? 1.0
                    : (currentLocal.y() - anchorPxY) / (startLocal.y() - anchorPxY);
            next.scaleX = std::clamp(dragStartTransform_.scaleX * std::abs(ratioX), 0.01, 100.0);
            next.scaleY = std::clamp(dragStartTransform_.scaleY * std::abs(ratioY), 0.01, 100.0);
        }
        break;
    }
    case Handle::ScaleLeft:
    case Handle::ScaleRight: {
        const QPointF startLocal = canvasToContent(dragStartTransform_, dragStartCanvas_);
        const QPointF currentLocal = canvasToContent(dragStartTransform_, canvasPoint);
        const double anchorPxX = dragStartTransform_.anchorX * frame_.width();
        if (std::abs(startLocal.x() - anchorPxX) >= 1.0) {
            const double ratio = (currentLocal.x() - anchorPxX) / (startLocal.x() - anchorPxX);
            next.scaleX = std::clamp(dragStartTransform_.scaleX * std::abs(ratio), 0.01, 100.0);
        }
        break;
    }
    case Handle::ScaleTop:
    case Handle::ScaleBottom: {
        const QPointF startLocal = canvasToContent(dragStartTransform_, dragStartCanvas_);
        const QPointF currentLocal = canvasToContent(dragStartTransform_, canvasPoint);
        const double anchorPxY = dragStartTransform_.anchorY * frame_.height();
        if (std::abs(startLocal.y() - anchorPxY) >= 1.0) {
            const double ratio = (currentLocal.y() - anchorPxY) / (startLocal.y() - anchorPxY);
            next.scaleY = std::clamp(dragStartTransform_.scaleY * std::abs(ratio), 0.01, 100.0);
        }
        break;
    }
    case Handle::Rotate: {
        const double startAngle = std::atan2(dragStartCanvas_.y() - anchorStart.y(),
                                             dragStartCanvas_.x() - anchorStart.x());
        const double currentAngle = std::atan2(canvasPoint.y() - anchorStart.y(),
                                               canvasPoint.x() - anchorStart.x());
        double degrees = dragStartTransform_.rotationDegrees +
                         (currentAngle - startAngle) * 180.0 / pi;
        if ((modifiers & Qt::ShiftModifier) != 0) {
            degrees = std::round(degrees / 15.0) * 15.0;
        } else if (snapEnabled) {
            const double nearest = std::round(degrees / 90.0) * 90.0;
            if (std::abs(degrees - nearest) <= 3.0) degrees = nearest;
        }
        next.rotationDegrees = std::clamp(degrees, -36'000.0, 36'000.0);
        break;
    }
    case Handle::Anchor: {
        const QPointF contentPoint = canvasToContent(dragStartTransform_, canvasPoint);
        double anchorX = std::clamp(contentPoint.x() / frame_.width(), 0.0, 1.0);
        double anchorY = std::clamp(contentPoint.y() / frame_.height(), 0.0, 1.0);
        if (snapEnabled) {
            if (std::abs(anchorX - 0.5) <= 0.02) anchorX = 0.5;
            if (std::abs(anchorY - 0.5) <= 0.02) anchorY = 0.5;
        }
        next.anchorX = anchorX;
        next.anchorY = anchorY;
        // Compensate position so the image does not jump when the pivot moves.
        const double radians = dragStartTransform_.rotationDegrees * pi / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        const double deltaPxX =
            (anchorX - dragStartTransform_.anchorX) * frame_.width() *
            dragStartTransform_.scaleX;
        const double deltaPxY =
            (anchorY - dragStartTransform_.anchorY) * frame_.height() *
            dragStartTransform_.scaleY;
        next.positionX = dragStartTransform_.positionX + cosine * deltaPxX - sine * deltaPxY;
        next.positionY = dragStartTransform_.positionY + sine * deltaPxX + cosine * deltaPxY;
        break;
    }
    case Handle::CropLeft:
    case Handle::CropRight:
    case Handle::CropTop:
    case Handle::CropBottom:
    case Handle::CropTopLeft:
    case Handle::CropTopRight:
    case Handle::CropBottomLeft:
    case Handle::CropBottomRight: {
        const QPointF contentPoint = canvasToContent(dragStartTransform_, canvasPoint);
        const double normalizedX = std::clamp(contentPoint.x() / frame_.width(), 0.0, 1.0);
        const double normalizedY = std::clamp(contentPoint.y() / frame_.height(), 0.0, 1.0);
        const bool symmetric = (modifiers & Qt::AltModifier) != 0;
        const bool editsLeft = activeHandle_ == Handle::CropLeft ||
                               activeHandle_ == Handle::CropTopLeft ||
                               activeHandle_ == Handle::CropBottomLeft;
        const bool editsRight = activeHandle_ == Handle::CropRight ||
                                activeHandle_ == Handle::CropTopRight ||
                                activeHandle_ == Handle::CropBottomRight;
        const bool editsTop = activeHandle_ == Handle::CropTop ||
                              activeHandle_ == Handle::CropTopLeft ||
                              activeHandle_ == Handle::CropTopRight;
        const bool editsBottom = activeHandle_ == Handle::CropBottom ||
                                 activeHandle_ == Handle::CropBottomLeft ||
                                 activeHandle_ == Handle::CropBottomRight;
        // Keep the clamp bounds ordered even if a project carries crops > 0.99.
        const auto limit = [](const double other) { return std::max(0.0, 0.99 - other); };
        if (editsLeft) {
            nextCrop.left = std::clamp(normalizedX, 0.0, limit(dragStartCrop_.right));
            if (symmetric) nextCrop.right = std::clamp(nextCrop.left, 0.0, limit(nextCrop.left));
        }
        if (editsRight) {
            nextCrop.right = std::clamp(1.0 - normalizedX, 0.0, limit(dragStartCrop_.left));
            if (symmetric) nextCrop.left = std::clamp(nextCrop.right, 0.0, limit(nextCrop.right));
        }
        if (editsTop) {
            nextCrop.top = std::clamp(normalizedY, 0.0, limit(dragStartCrop_.bottom));
            if (symmetric) nextCrop.bottom = std::clamp(nextCrop.top, 0.0, limit(nextCrop.top));
        }
        if (editsBottom) {
            nextCrop.bottom = std::clamp(1.0 - normalizedY, 0.0, limit(dragStartCrop_.top));
            if (symmetric) nextCrop.top = std::clamp(nextCrop.bottom, 0.0, limit(nextCrop.bottom));
        }
        break;
    }
    case Handle::None:
        break;
    }

    transientTransform_ = next;
    transientCrop_ = nextCrop;
    const bool cropHandle = isCropHandle(static_cast<int>(activeHandle_),
                                         static_cast<int>(Handle::CropLeft));
    if (cropHandle) {
        if (cropEditHandler_) cropEditHandler_(transientCrop_, false);
    } else {
        if (transformEditHandler_) transformEditHandler_(transientTransform_, false);
    }
    viewport_->update();
}

void QtMonitorWidget::finishDrag() {
    if (!dragging_) return;
    dragging_ = false;
    const Handle handle = activeHandle_;
    activeHandle_ = Handle::None;
    viewport_->unsetCursor();
    const bool cropHandle = isCropHandle(static_cast<int>(handle),
                                         static_cast<int>(Handle::CropLeft));
    if (cropHandle) {
        if (cropEditHandler_) cropEditHandler_(transientCrop_, true);
    } else if (handle != Handle::None) {
        if (transformEditHandler_) transformEditHandler_(transientTransform_, true);
    }
    viewport_->update();
}

void QtMonitorWidget::cancelDrag() {
    if (!dragging_) return;
    dragging_ = false;
    activeHandle_ = Handle::None;
    transientTransform_ = dragStartTransform_;
    transientCrop_ = dragStartCrop_;
    viewport_->unsetCursor();
    if (transformEditHandler_) transformEditHandler_(transientTransform_, false);
    if (cropEditHandler_) cropEditHandler_(transientCrop_, false);
    viewport_->update();
}

QImage applyStillAdjustments(const QImage& source, const MonitorMask& mask,
                             const MonitorEffects& effects) {
    if (source.isNull()) return source;
    const bool hasMask = mask.shape != 0;
    const bool hasColor = effects.brightness != 0.0 || effects.contrast != 0.0 ||
                          effects.saturation != 0.0 || effects.vignette != 0.0;
    if (!hasMask && !hasColor && effects.blur <= 0.01) {
        return source;
    }
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    const double halfWidth = std::max(1.0, result.width() * 0.5);
    const double halfHeight = std::max(1.0, result.height() * 0.5);
    for (int row = 0; row < result.height(); ++row) {
        auto* pixels = reinterpret_cast<QRgb*>(result.scanLine(row));
        for (int column = 0; column < result.width(); ++column) {
            if (hasColor) {
                pixels[column] = adjustPixel(
                    pixels[column], effects.brightness, effects.contrast, effects.saturation,
                    effects.vignette, (column - halfWidth) / halfWidth,
                    (row - halfHeight) / halfHeight);
            }
            if (hasMask) {
                const double factor = maskFactorAt(
                    mask.shape, mask.centerX, mask.centerY, std::max(mask.width, 0.001),
                    std::max(mask.height, 0.001), mask.feather, mask.inverted,
                    (column + 0.5) / result.width(), (row + 0.5) / result.height());
                const QRgb pixel = pixels[column];
                pixels[column] = qRgba(static_cast<int>(qRed(pixel) * factor),
                                       static_cast<int>(qGreen(pixel) * factor),
                                       static_cast<int>(qBlue(pixel) * factor),
                                       static_cast<int>(qAlpha(pixel) * factor));
            }
        }
    }
    return blurApproximation(result.convertToFormat(QImage::Format_ARGB32_Premultiplied),
                             effects.blur);
}

void QtMonitorWidget::ensureComposedFrame() {
    if (!composedDirty_) return;
    composedDirty_ = false;
    if (frame_.isNull()) {
        composedFrame_ = {};
        return;
    }
    // The compositing worker already applied crop/mask/effects for timeline
    // frames; this CPU pass only matters for the direct media-player path.
    // Skipping it during playback saves a full-frame pixel walk per frame.
    if (cropLeft_ == 0.0 && cropRight_ == 0.0 && cropTop_ == 0.0 &&
        cropBottom_ == 0.0 && maskShape_ == 0 && brightness_ == 0.0 &&
        contrast_ == 0.0 && saturation_ == 0.0 && blur_ == 0.0 &&
        vignette_ == 0.0) {
        composedFrame_ = frame_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        return;
    }
    QImage cropped = frame_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int leftPixel = static_cast<int>(std::floor(cropLeft_ * cropped.width()));
    const int rightPixel = static_cast<int>(std::ceil((1.0 - cropRight_) * cropped.width()));
    const int topPixel = static_cast<int>(std::floor(cropTop_ * cropped.height()));
    const int bottomPixel = static_cast<int>(std::ceil((1.0 - cropBottom_) * cropped.height()));
    for (int row = 0; row < cropped.height(); ++row) {
        auto* pixels = reinterpret_cast<QRgb*>(cropped.scanLine(row));
        for (int column = 0; column < cropped.width(); ++column) {
            if (column < leftPixel || column >= rightPixel || row < topPixel ||
                row >= bottomPixel) {
                pixels[column] = qRgba(0, 0, 0, 0);
                continue;
            }
            if (maskShape_ != 0) {
                const double factor = maskFactorAt(
                    maskShape_, maskCenterX_, maskCenterY_, maskWidth_, maskHeight_,
                    maskFeather_, maskInverted_, (column + 0.5) / cropped.width(),
                    (row + 0.5) / cropped.height());
                const QRgb source = pixels[column];
                pixels[column] = qRgba(
                    static_cast<int>(qRed(source) * factor),
                    static_cast<int>(qGreen(source) * factor),
                    static_cast<int>(qBlue(source) * factor),
                    static_cast<int>(qAlpha(source) * factor));
            }
        }
    }
    QImage transformed(frame_.size(), QImage::Format_ARGB32_Premultiplied);
    transformed.fill(Qt::transparent);
    QPainter painter(&transformed);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(transformed.width() * 0.5 + positionX_ * transformed.width() / 1280.0,
                      transformed.height() * 0.5 + positionY_ * transformed.height() / 720.0);
    painter.rotate(rotationDegrees_);
    painter.scale(scaleX_, scaleY_);
    painter.translate(-anchorX_ * frame_.width(), -anchorY_ * frame_.height());
    painter.drawImage(0, 0, cropped);
    painter.end();
    if (brightness_ != 0.0 || contrast_ != 0.0 || saturation_ != 0.0 || vignette_ != 0.0) {
        transformed = transformed.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < transformed.height(); ++y) {
            auto* pixels = reinterpret_cast<QRgb*>(transformed.scanLine(y));
            for (int x = 0; x < transformed.width(); ++x) {
                pixels[x] = adjustPixel(
                    pixels[x], brightness_, contrast_, saturation_, vignette_,
                    (x - transformed.width() * 0.5) /
                        std::max(1.0, transformed.width() * 0.5),
                    (y - transformed.height() * 0.5) /
                        std::max(1.0, transformed.height() * 0.5));
            }
        }
        transformed = transformed.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    composedFrame_ = blurApproximation(std::move(transformed), blur_);
}

void QtMonitorWidget::paintViewport(QPainter& painter) {
    painter.fillRect(viewport_->rect(), QColor(16, 17, 20));
    if (frame_.isNull()) {
        painter.setPen(QColor(140, 146, 156));
        painter.drawText(viewport_->rect(), Qt::AlignCenter, tr("No frame"));
        return;
    }
    ensureComposedFrame();
    const double scale = displayScale();
    const QPointF topLeft = displayTopLeft();
    const QRectF displayRect(topLeft.x(), topLeft.y(), frame_.width() * scale,
                             frame_.height() * scale);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (showTransparencyGrid_) {
        const int cell = 12;
        for (int y = static_cast<int>(displayRect.top());
             y < static_cast<int>(displayRect.bottom()); y += cell) {
            for (int x = static_cast<int>(displayRect.left());
                 x < static_cast<int>(displayRect.right()); x += cell) {
                const bool dark = ((x / cell) + (y / cell)) % 2 == 0;
                painter.fillRect(
                    QRectF(x, y, cell, cell).intersected(displayRect),
                    dark ? QColor(70, 72, 78) : QColor(96, 99, 106));
            }
        }
    }
    painter.setOpacity(frameOpacity_);

    const bool transformDelta =
        hasEditTarget_ && !layeredTarget_ &&
        (transientTransform_.positionX != bakedTransform_.positionX ||
         transientTransform_.positionY != bakedTransform_.positionY ||
         transientTransform_.scaleX != bakedTransform_.scaleX ||
         transientTransform_.scaleY != bakedTransform_.scaleY ||
         transientTransform_.rotationDegrees != bakedTransform_.rotationDegrees);
    if (transformDelta) {
        const QPointF anchorCanvas = anchorCanvasPoint(bakedTransform_);
        const QPointF anchorViewPoint = canvasToView(anchorCanvas);
        painter.save();
        painter.translate(anchorViewPoint +
                          QPointF((transientTransform_.positionX -
                                   bakedTransform_.positionX) * scale,
                                  (transientTransform_.positionY -
                                   bakedTransform_.positionY) * scale));
        painter.rotate(transientTransform_.rotationDegrees -
                       bakedTransform_.rotationDegrees);
        painter.scale(bakedTransform_.scaleX == 0.0
                          ? 1.0
                          : transientTransform_.scaleX / bakedTransform_.scaleX,
                      bakedTransform_.scaleY == 0.0
                          ? 1.0
                          : transientTransform_.scaleY / bakedTransform_.scaleY);
        painter.translate(-anchorViewPoint);
        painter.drawImage(displayRect, composedFrame_);
        painter.restore();
    } else {
        painter.drawImage(displayRect, composedFrame_);
    }
    // Title stills ride on top of the continuous-playback frame; one scaled
    // drawImage per title keeps the smooth path instead of CPU compositing.
    // The transform mirrors the compositor: the content anchor lands at the
    // canvas centre plus the position offset, scaled/rotated about it.
    for (const MonitorTitleOverlay& overlay : titleOverlays_) {
        if (overlay.image.isNull() || overlay.opacity <= 0.0) continue;
        painter.setOpacity(std::clamp(overlay.opacity, 0.0, 1.0));
        // Crop keeps the content in place and hides the edges, matching the
        // compositor (which clears the cropped pixels to transparent). A clip
        // rectangle produces the same result without walking pixels. The rect
        // is expressed in the content's own space so it survives the transform.
        const double cropWidthFraction =
            1.0 - overlay.cropLeft - overlay.cropRight;
        const double cropHeightFraction =
            1.0 - overlay.cropTop - overlay.cropBottom;
        const bool hasCrop = overlay.cropLeft > 0.0 || overlay.cropRight > 0.0 ||
                             overlay.cropTop > 0.0 || overlay.cropBottom > 0.0;
        if (hasCrop && (cropWidthFraction <= 0.0 || cropHeightFraction <= 0.0)) {
            continue; // fully cropped away
        }
        const auto contentCropRect = [&](const QRectF& content) {
            return QRectF(content.x() + overlay.cropLeft * content.width(),
                          content.y() + overlay.cropTop * content.height(),
                          content.width() * cropWidthFraction,
                          content.height() * cropHeightFraction);
        };
        if (overlay.positionX == 0.0 && overlay.positionY == 0.0 &&
            overlay.scaleX == 1.0 && overlay.scaleY == 1.0 &&
            overlay.rotationDegrees == 0.0) {
            if (hasCrop) {
                painter.save();
                painter.setClipRect(contentCropRect(displayRect));
                painter.drawImage(displayRect, overlay.image);
                painter.restore();
            } else {
                painter.drawImage(displayRect, overlay.image);
            }
            continue;
        }
        // Position offsets are stored in reference-canvas pixels (the paused
        // compositor's 1280x720); the live frame may be the media's native or
        // proxy resolution, so normalize the offset before mapping to view.
        const double referenceWidth = zoomReferenceWidth_ > 0
                                          ? static_cast<double>(zoomReferenceWidth_)
                                          : static_cast<double>(frame_.width());
        const double referenceHeight = zoomReferenceHeight_ > 0
                                           ? static_cast<double>(zoomReferenceHeight_)
                                           : static_cast<double>(frame_.height());
        const QPointF anchorView = canvasToView(
            {frame_.width() * (0.5 + overlay.positionX / referenceWidth),
             frame_.height() * (0.5 + overlay.positionY / referenceHeight)});
        painter.save();
        painter.translate(anchorView);
        painter.rotate(overlay.rotationDegrees);
        painter.scale(overlay.scaleX, overlay.scaleY);
        painter.translate(-overlay.anchorX * displayRect.width(),
                          -overlay.anchorY * displayRect.height());
        const QRectF contentRect(0.0, 0.0, displayRect.width(), displayRect.height());
        if (hasCrop) {
            painter.setClipRect(contentCropRect(contentRect));
        }
        painter.drawImage(contentRect, overlay.image);
        painter.restore();
    }
    painter.setOpacity(1.0);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    if (showDiagnostics_ && !diagnosticsText_.isEmpty()) {
        painter.setPen(QColor(180, 220, 180));
        painter.drawText(QRectF(0.0, 0.0, viewport_->width() - 6.0, 18.0),
                         Qt::AlignRight | Qt::AlignVCenter, diagnosticsText_);
    }
    painter.setPen(QPen(QColor(255, 255, 255, 90), 1.0));
    painter.drawRect(displayRect);
    if (showGrid_) {
        painter.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
        for (int third = 1; third <= 2; ++third) {
            const double x = displayRect.left() + displayRect.width() * third / 3.0;
            const double y = displayRect.top() + displayRect.height() * third / 3.0;
            painter.drawLine(QPointF(x, displayRect.top()), QPointF(x, displayRect.bottom()));
            painter.drawLine(QPointF(displayRect.left(), y), QPointF(displayRect.right(), y));
        }
    }
    if (showSafeMargins_) {
        painter.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
        const auto marginRect = [&displayRect](const double fraction) {
            const double insetX = displayRect.width() * (1.0 - fraction) * 0.5;
            const double insetY = displayRect.height() * (1.0 - fraction) * 0.5;
            return displayRect.adjusted(insetX, insetY, -insetX, -insetY);
        };
        painter.drawRect(marginRect(0.9));
        painter.drawRect(marginRect(0.8));
    }

    if (!hasEditTarget_ || !showOverlays_) {
        if (!hasEditTarget_ && transformEditHandler_) {
            painter.setPen(QColor(150, 156, 168));
            painter.drawText(QPointF(8.0, viewport_->height() - 8.0),
                             tr("Select a clip in the timeline to edit it here"));
        }
        return;
    }

    const MonitorTransform& transform = transientTransform_;
    const MonitorCrop& crop = transientCrop_;
    const ContentExtents extents = contentExtents(crop);
    const double left = extents.left;
    const double right = extents.right;
    const double top = extents.top;
    const double bottom = extents.bottom;
    const QPointF topLeftHandle = canvasToView(contentToCanvas(transform, {left, top}));
    const QPointF topRightHandle = canvasToView(contentToCanvas(transform, {right, top}));
    const QPointF bottomLeftHandle = canvasToView(contentToCanvas(transform, {left, bottom}));
    const QPointF bottomRightHandle = canvasToView(contentToCanvas(transform, {right, bottom}));
    const QPointF topMid = (topLeftHandle + topRightHandle) * 0.5;
    const QPointF bottomMid = (bottomLeftHandle + bottomRightHandle) * 0.5;
    const QPointF leftMid = (topLeftHandle + bottomLeftHandle) * 0.5;
    const QPointF rightMid = (topRightHandle + bottomRightHandle) * 0.5;

    if (cropEditMode_) {
        QPainterPath outside;
        outside.addRect(displayRect);
        QPolygonF inner;
        inner << topLeftHandle << topRightHandle << bottomRightHandle << bottomLeftHandle;
        QPainterPath innerPath;
        innerPath.addPolygon(inner);
        outside = outside.subtracted(innerPath);
        painter.fillPath(outside, QColor(0, 0, 0, 120));
        painter.setPen(QPen(QColor(255, 210, 90), 1.6));
    } else {
        painter.setPen(QPen(QColor(80, 190, 255), 1.6));
    }
    QPolygonF outline;
    outline << topLeftHandle << topRightHandle << bottomRightHandle << bottomLeftHandle
            << topLeftHandle;
    painter.drawPolyline(outline);

    const auto drawHandle = [&painter](const QPointF& point) {
        painter.drawRect(QRectF(point.x() - handleRadius * 0.5,
                                point.y() - handleRadius * 0.5, handleRadius, handleRadius));
    };
    painter.setBrush(QColor(245, 250, 255));
    for (const QPointF& point : {topLeftHandle, topRightHandle, bottomLeftHandle,
                                 bottomRightHandle, topMid, bottomMid, leftMid, rightMid}) {
        drawHandle(point);
    }

    if (!cropEditMode_) {
        const QLineF vertical(topMid, bottomMid);
        const QPointF rotationPoint =
            topMid + (topMid - bottomMid) / std::max(1.0, vertical.length()) *
                         rotationHandleOffset;
        painter.setPen(QPen(QColor(80, 190, 255), 1.2));
        painter.drawLine(topMid, rotationPoint);
        painter.setBrush(QColor(245, 250, 255));
        painter.drawEllipse(rotationPoint, handleRadius * 0.6, handleRadius * 0.6);

        const QPointF anchorView = canvasToView(anchorCanvasPoint(transform));
        painter.setPen(QPen(QColor(255, 170, 70), 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(anchorView, handleRadius, handleRadius);
        painter.drawLine(anchorView - QPointF(handleRadius * 1.6, 0.0),
                         anchorView + QPointF(handleRadius * 1.6, 0.0));
        painter.drawLine(anchorView - QPointF(0.0, handleRadius * 1.6),
                         anchorView + QPointF(0.0, handleRadius * 1.6));
    }

    if (showCenterCross_) {
        painter.setPen(QPen(QColor(255, 255, 255, 45), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(displayRect.center().x(), displayRect.top()),
                         QPointF(displayRect.center().x(), displayRect.bottom()));
        painter.drawLine(QPointF(displayRect.left(), displayRect.center().y()),
                         QPointF(displayRect.right(), displayRect.center().y()));
    }

    if (dragging_) {
        painter.setPen(QColor(235, 240, 248));
        QString status;
        if (activeHandle_ == Handle::Position) {
            status = QStringLiteral("X %1  Y %2")
                         .arg(transform.positionX, 0, 'f', 1)
                         .arg(transform.positionY, 0, 'f', 1);
        } else if (activeHandle_ == Handle::Rotate) {
            status = QStringLiteral("%1°").arg(transform.rotationDegrees, 0, 'f', 1);
        } else if (activeHandle_ == Handle::Anchor) {
            status = QStringLiteral("Anchor %1%% / %2%%")
                         .arg(transform.anchorX * 100.0, 0, 'f', 1)
                         .arg(transform.anchorY * 100.0, 0, 'f', 1);
        } else if (isCropHandle(static_cast<int>(activeHandle_),
                                static_cast<int>(Handle::CropLeft))) {
            status = QStringLiteral("L %1%%  R %2%%  T %3%%  B %4%%")
                         .arg(crop.left * 100.0, 0, 'f', 1)
                         .arg(crop.right * 100.0, 0, 'f', 1)
                         .arg(crop.top * 100.0, 0, 'f', 1)
                         .arg(crop.bottom * 100.0, 0, 'f', 1);
        } else {
            status = QStringLiteral("Scale %1%% / %2%%")
                         .arg(transform.scaleX * 100.0, 0, 'f', 1)
                         .arg(transform.scaleY * 100.0, 0, 'f', 1);
        }
        painter.drawText(QPointF(8.0, viewport_->height() - 8.0), status);
    }
}

void QtMonitorWidget::resizeEvent(QResizeEvent* event) {
    QRhiWidget::resizeEvent(event);
    applyOverlayStyleSheet();
    updateOverlayGeometry();
    if (viewport_ != nullptr) {
        viewport_->update();
    }
}

void QtMonitorWidget::initialize(QRhiCommandBuffer* commandBuffer) {
    Q_UNUSED(commandBuffer);
}

void QtMonitorWidget::render(QRhiCommandBuffer* commandBuffer) {
    const QColor background = QColor::fromRgb(23, 25, 28);
    commandBuffer->beginPass(renderTarget(), background, QRhiDepthStencilClearValue{1.0F, 0},
                             nullptr);
    commandBuffer->endPass();
}

} // namespace videx::render
