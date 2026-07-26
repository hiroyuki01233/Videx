#include "main_window.hpp"
#include "timeline_widget.hpp"
#include "title_store.hpp"

#include <videx/render/qt_monitor_widget.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioOutput>
#include <QBuffer>
#include <QCloseEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QPlainTextEdit>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHash>
#include <QMimeData>
#include <QMouseEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSize>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QImage>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMap>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMediaDevices>
#include <QProcess>
#include <QPainter>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QPoint>
#include <QProgressDialog>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QStandardPaths>
#include <QUuid>
#include <QSysInfo>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QVideoFrame>
#include <QVideoSink>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <cstring>

namespace {

QDockWidget* createDock(const QString& title, QWidget* content, QWidget* parent) {
    auto* dock = new QDockWidget(title, parent);
    dock->setObjectName(title);
    dock->setWidget(content);
    return dock;
}

struct KeyframeLaneEntry final {
    QString name;
    int laneType = 0; // 0 motion, 1 gain, 2 speed, 3 effect
    std::uint64_t effectId = 0;
    std::vector<videx::core::Frame> offsets;
    std::vector<int> interpolations;
};

// Compact per-clip keyframe map: one lane per animated property group, diamonds
// colored by interpolation, drag to retime (one undo per drag), Delete removes,
// right-click changes interpolation, clicking empty lane space moves the playhead.
class KeyframeLaneWidget final : public QWidget {
  public:
    using SeekHandler = std::function<void(videx::core::Frame)>;
    using MoveHandler =
        std::function<void(const KeyframeLaneEntry&, videx::core::Frame, videx::core::Frame)>;
    using DeleteHandler = std::function<void(const KeyframeLaneEntry&, videx::core::Frame)>;
    using InterpolationHandler =
        std::function<void(const KeyframeLaneEntry&, videx::core::Frame, int)>;

    explicit KeyframeLaneWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::ClickFocus);
        setMinimumHeight(36);
        setToolTip(QObject::tr("Keyframe map: drag to retime, Delete to remove, "
                               "right-click for interpolation, click empty space to seek"));
    }

    void setHandlers(SeekHandler seek, MoveHandler move, DeleteHandler remove,
                     InterpolationHandler interpolation) {
        seekHandler_ = std::move(seek);
        moveHandler_ = std::move(move);
        deleteHandler_ = std::move(remove);
        interpolationHandler_ = std::move(interpolation);
    }

    void setLanes(std::vector<KeyframeLaneEntry> lanes, const videx::core::Frame clipStart,
                  const videx::core::Frame clipDuration, const videx::core::Frame playhead) {
        lanes_ = std::move(lanes);
        clipStart_ = clipStart;
        clipDuration_ = std::max<videx::core::Frame>(1, clipDuration);
        playhead_ = playhead;
        if (!dragging_ || selectedLane_ < 0 ||
            static_cast<std::size_t>(selectedLane_) >= lanes_.size() ||
            selectedIndex_ < 0 ||
            static_cast<std::size_t>(selectedIndex_) >=
                lanes_[static_cast<std::size_t>(selectedLane_)].offsets.size()) {
            dragging_ = false;
            selectedLane_ = -1;
            selectedIndex_ = -1;
        }
        setMinimumHeight(static_cast<int>(lanes_.size()) * laneHeight + 22);
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(28, 30, 35));
        painter.setPen(QColor(120, 126, 136));
        if (lanes_.empty()) {
            painter.drawText(rect(), Qt::AlignCenter,
                             QObject::tr("No keyframes on this clip"));
            return;
        }
        const double areaLeft = nameWidth;
        const double areaWidth = std::max(10.0, width() - areaLeft - 8.0);
        painter.drawLine(QPointF(areaLeft, 0.0), QPointF(areaLeft, height()));
        const double playheadX =
            areaLeft + areaWidth * std::clamp<double>(
                static_cast<double>(playhead_ - clipStart_) / clipDuration_, 0.0, 1.0);
        painter.setPen(QColor(230, 80, 80));
        painter.drawLine(QPointF(playheadX, 0.0), QPointF(playheadX, height()));
        for (std::size_t laneIndex = 0; laneIndex < lanes_.size(); ++laneIndex) {
            const KeyframeLaneEntry& lane = lanes_[laneIndex];
            const double top = 18.0 + laneIndex * laneHeight;
            const double mid = top + laneHeight * 0.5;
            painter.setPen(QColor(170, 176, 186));
            painter.drawText(QRectF(4.0, top, nameWidth - 8.0, laneHeight),
                             Qt::AlignVCenter | Qt::AlignLeft, lane.name);
            painter.setPen(QColor(60, 64, 72));
            painter.drawLine(QPointF(areaLeft, mid), QPointF(areaLeft + areaWidth, mid));
            for (std::size_t keyIndex = 0; keyIndex < lane.offsets.size(); ++keyIndex) {
                videx::core::Frame offset = lane.offsets[keyIndex];
                if (dragging_ && static_cast<int>(laneIndex) == selectedLane_ &&
                    static_cast<int>(keyIndex) == selectedIndex_) {
                    offset = dragOffset_;
                }
                const double x = areaLeft + areaWidth * (static_cast<double>(offset) /
                                                        clipDuration_);
                QColor color(110, 190, 250);
                if (keyIndex < lane.interpolations.size()) {
                    if (lane.interpolations[keyIndex] == 1) color = QColor(250, 170, 90);
                    else if (lane.interpolations[keyIndex] == 2) color = QColor(140, 220, 120);
                    else if (lane.interpolations[keyIndex] == 3) color = QColor(200, 150, 250);
                    else if (lane.interpolations[keyIndex] == 4) color = QColor(250, 140, 190);
                    else if (lane.interpolations[keyIndex] == 5) color = QColor(120, 230, 220);
                }
                QPolygonF diamond;
                diamond << QPointF(x, mid - 5.0) << QPointF(x + 5.0, mid)
                        << QPointF(x, mid + 5.0) << QPointF(x - 5.0, mid);
                painter.setBrush(color);
                painter.setPen(static_cast<int>(laneIndex) == selectedLane_ &&
                                       static_cast<int>(keyIndex) == selectedIndex_
                                   ? QPen(Qt::white, 1.5)
                                   : QPen(QColor(30, 32, 36), 1.0));
                painter.drawPolygon(diamond);
            }
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        const auto hit = hitTest(event->position());
        selectedLane_ = hit.first;
        selectedIndex_ = hit.second;
        if (event->button() == Qt::LeftButton) {
            if (selectedLane_ >= 0) {
                dragging_ = true;
                dragOffset_ = lanes_[static_cast<std::size_t>(selectedLane_)]
                                  .offsets[static_cast<std::size_t>(selectedIndex_)];
                dragStartOffset_ = dragOffset_;
            } else if (seekHandler_ && event->position().x() >= nameWidth) {
                seekHandler_(clipStart_ + xToOffset(event->position().x()));
            }
        }
        update();
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ && (event->buttons() & Qt::LeftButton) != 0) {
            dragOffset_ = xToOffset(event->position().x());
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            if (selectedLane_ >= 0 &&
                static_cast<std::size_t>(selectedLane_) < lanes_.size() &&
                dragOffset_ != dragStartOffset_ && moveHandler_) {
                moveHandler_(lanes_[static_cast<std::size_t>(selectedLane_)],
                             dragStartOffset_, dragOffset_);
            }
            update();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
            selectedLane_ >= 0 &&
            static_cast<std::size_t>(selectedLane_) < lanes_.size() &&
            selectedIndex_ >= 0 &&
            static_cast<std::size_t>(selectedIndex_) <
                lanes_[static_cast<std::size_t>(selectedLane_)].offsets.size() &&
            deleteHandler_) {
            deleteHandler_(lanes_[static_cast<std::size_t>(selectedLane_)],
                           lanes_[static_cast<std::size_t>(selectedLane_)]
                               .offsets[static_cast<std::size_t>(selectedIndex_)]);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && dragging_) {
            dragging_ = false;
            dragOffset_ = dragStartOffset_;
            update();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        const auto hit = hitTest(QPointF(event->pos()));
        if (hit.first < 0 || !interpolationHandler_) {
            return;
        }
        selectedLane_ = hit.first;
        selectedIndex_ = hit.second;
        update();
        // Copy the target before exec(): the modal loop can dispatch worker
        // signals that call setLanes() and reallocate lanes_.
        const KeyframeLaneEntry laneCopy = lanes_[static_cast<std::size_t>(hit.first)];
        const videx::core::Frame offsetCopy =
            laneCopy.offsets[static_cast<std::size_t>(hit.second)];
        QMenu menu(this);
        QAction* linear = menu.addAction(QObject::tr("Linear"));
        QAction* hold = menu.addAction(QObject::tr("Hold"));
        QAction* ease = menu.addAction(QObject::tr("Ease"));
        QAction* easeIn = menu.addAction(QObject::tr("Ease In"));
        QAction* easeOut = menu.addAction(QObject::tr("Ease Out"));
        QAction* easeInOut = menu.addAction(QObject::tr("Ease In-Out"));
        QAction* chosen = menu.exec(event->globalPos());
        int interpolation = -1;
        if (chosen == linear) interpolation = 0;
        else if (chosen == hold) interpolation = 1;
        else if (chosen == ease) interpolation = 2;
        else if (chosen == easeIn) interpolation = 3;
        else if (chosen == easeOut) interpolation = 4;
        else if (chosen == easeInOut) interpolation = 5;
        if (interpolation >= 0) {
            interpolationHandler_(laneCopy, offsetCopy, interpolation);
        }
    }

  private:
    static constexpr double laneHeight = 18.0;
    static constexpr double nameWidth = 96.0;

    [[nodiscard]] videx::core::Frame xToOffset(const double x) const {
        const double areaLeft = nameWidth;
        const double areaWidth = std::max(10.0, width() - areaLeft - 8.0);
        const double ratio = std::clamp((x - areaLeft) / areaWidth, 0.0, 1.0);
        return std::clamp<videx::core::Frame>(
            static_cast<videx::core::Frame>(std::llround(ratio * clipDuration_)), 0,
            clipDuration_ - 1);
    }
    [[nodiscard]] std::pair<int, int> hitTest(const QPointF& position) const {
        const double areaLeft = nameWidth;
        const double areaWidth = std::max(10.0, width() - areaLeft - 8.0);
        for (std::size_t laneIndex = 0; laneIndex < lanes_.size(); ++laneIndex) {
            const double mid = 18.0 + laneIndex * laneHeight + laneHeight * 0.5;
            if (std::abs(position.y() - mid) > laneHeight * 0.5) {
                continue;
            }
            const KeyframeLaneEntry& lane = lanes_[laneIndex];
            for (std::size_t keyIndex = 0; keyIndex < lane.offsets.size(); ++keyIndex) {
                const double x =
                    areaLeft + areaWidth * (static_cast<double>(lane.offsets[keyIndex]) /
                                            clipDuration_);
                if (std::abs(position.x() - x) <= 6.0) {
                    return {static_cast<int>(laneIndex), static_cast<int>(keyIndex)};
                }
            }
        }
        return {-1, -1};
    }

    std::vector<KeyframeLaneEntry> lanes_;
    videx::core::Frame clipStart_ = 0;
    videx::core::Frame clipDuration_ = 1;
    videx::core::Frame playhead_ = 0;
    int selectedLane_ = -1;
    int selectedIndex_ = -1;
    bool dragging_ = false;
    videx::core::Frame dragOffset_ = 0;
    videx::core::Frame dragStartOffset_ = 0;
    SeekHandler seekHandler_;
    MoveHandler moveHandler_;
    DeleteHandler deleteHandler_;
    InterpolationHandler interpolationHandler_;
};

// Label-drag scrubbing for numeric parameters: horizontal drag adjusts the spin
// value (Shift fine, Ctrl finer, Alt coarse), Esc cancels back to the start
// value, and the commit callback runs once on release so a whole scrub is one
// undo step.
class ParameterScrubController final : public QObject {
  public:
    using Commit = std::function<void()>;
    explicit ParameterScrubController(QObject* parent) : QObject(parent) {}

    void attach(QLabel* label, QDoubleSpinBox* spin, Commit commit) {
        if (label == nullptr || spin == nullptr) {
            return;
        }
        label->setCursor(Qt::SizeHorCursor);
        label->setToolTip(
            QObject::tr("Drag to change | Shift: fine | Ctrl: finer | Alt: coarse | Esc: cancel"));
        entries_.insert(label, Entry{spin, std::move(commit)});
        label->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        const auto entry = entries_.find(watched);
        if (entry == entries_.end()) {
            return QObject::eventFilter(watched, event);
        }
        auto* label = static_cast<QLabel*>(watched);
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && entry->spin->isEnabled()) {
                active_ = label;
                startValue_ = entry->spin->value();
                lastGlobalX_ = mouse->globalPosition().x();
                scrubbing_ = false;
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (active_ != label) {
                break;
            }
            auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->buttons() & Qt::LeftButton) == 0) {
                break;
            }
            const double x = mouse->globalPosition().x();
            const double deltaPixels = x - lastGlobalX_;
            lastGlobalX_ = x;
            if (!scrubbing_) {
                scrubbing_ = true;
                label->grabKeyboard();
            }
            double step = entry->spin->singleStep();
            const Qt::KeyboardModifiers modifiers = mouse->modifiers();
            if ((modifiers & Qt::ControlModifier) != 0) {
                step *= 0.02;
            } else if ((modifiers & Qt::ShiftModifier) != 0) {
                step *= 0.1;
            } else if ((modifiers & Qt::AltModifier) != 0) {
                step *= 10.0;
            }
            entry->spin->setValue(entry->spin->value() + deltaPixels * step * 0.5);
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (active_ != label) {
                break;
            }
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                const bool wasScrubbing = scrubbing_;
                const bool changed = entry->spin->value() != startValue_;
                endScrub(label);
                if (wasScrubbing && changed && entry->commit) {
                    entry->commit();
                }
                return true;
            }
            break;
        }
        case QEvent::KeyPress: {
            if (active_ == label && scrubbing_) {
                auto* key = static_cast<QKeyEvent*>(event);
                if (key->key() == Qt::Key_Escape) {
                    entry->spin->setValue(startValue_);
                    endScrub(label);
                    return true;
                }
            }
            break;
        }
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    struct Entry final {
        QDoubleSpinBox* spin = nullptr;
        Commit commit;
    };

    void endScrub(QLabel* label) {
        if (scrubbing_) {
            label->releaseKeyboard();
        }
        active_ = nullptr;
        scrubbing_ = false;
    }

    QHash<QObject*, Entry> entries_;
    QObject* active_ = nullptr;
    double startValue_ = 0.0;
    double lastGlobalX_ = 0.0;
    bool scrubbing_ = false;
};

double clipEnvelope(const videx::core::Clip& clip, const videx::core::Frame frame) {
    double envelope = 1.0;
    const videx::core::Frame local = frame - clip.timeline.start;
    if (clip.fadeInFrames > 0) {
        envelope = std::min(envelope, static_cast<double>(local + 1) /
                                         static_cast<double>(clip.fadeInFrames));
    }
    const videx::core::Frame remaining = clip.timeline.end() - frame;
    if (clip.fadeOutFrames > 0) {
        envelope = std::min(envelope, static_cast<double>(remaining) /
                                         static_cast<double>(clip.fadeOutFrames));
    }
    return std::clamp(envelope, 0.0, 1.0);
}

QString effectName(const videx::core::EffectType type) {
    using videx::core::EffectType;
    switch (type) {
    case EffectType::Brightness: return QStringLiteral("Brightness");
    case EffectType::Contrast: return QStringLiteral("Contrast");
    case EffectType::Saturation: return QStringLiteral("Saturation");
    case EffectType::Blur: return QStringLiteral("Blur");
    case EffectType::Vignette: return QStringLiteral("Vignette");
    }
    return QStringLiteral("Effect");
}

double effectValueAt(const videx::core::ClipEffect& effect,
                     const videx::core::Frame localFrame) {
    if (effect.keyframes.empty()) {
        return effect.amount;
    }
    const auto next = std::ranges::lower_bound(effect.keyframes, localFrame, {},
                                                &videx::core::EffectKeyframe::frameOffset);
    if (next == effect.keyframes.begin()) {
        return next->value;
    }
    if (next == effect.keyframes.end()) {
        return effect.keyframes.back().value;
    }
    const auto previous = std::prev(next);
    if (previous->interpolation == videx::core::KeyframeInterpolation::Hold) {
        return previous->value;
    }
    double ratio = static_cast<double>(localFrame - previous->frameOffset) /
                   static_cast<double>(next->frameOffset - previous->frameOffset);
    ratio = videx::core::interpolationProgress(previous->interpolation, ratio);
    return previous->value + (next->value - previous->value) * ratio;
}

bool audioTrackAudible(const videx::core::Sequence& sequence,
                       const videx::core::Track& track) {
    if (track.kind != videx::core::TrackKind::Audio || !track.enabled || track.muted) {
        return false;
    }
    const bool anySolo = std::ranges::any_of(
        sequence.tracks(), [](const videx::core::Track& candidate) {
            return candidate.kind == videx::core::TrackKind::Audio && candidate.solo;
        });
    return !anySolo || track.solo;
}

// Text bounds of a rendered title, normalized to the canvas. Mirrors the
// layout in title_store's writeTitleImage (1080p raster, 720p-relative font).
QRectF titleTextRectNormalized(const videx::ui::TitleStyle& style) {
    constexpr int canvasWidth = 1920;
    constexpr int canvasHeight = 1080;
    constexpr double uiScale = canvasHeight / 720.0;
    QFont font;
    if (!style.fontFamily.isEmpty()) {
        font.setFamily(style.fontFamily);
    }
    font.setPixelSize(
        std::max(8, static_cast<int>(std::lround(style.fontSize * uiScale))));
    font.setBold(style.bold);
    font.setItalic(style.italic);
    const QFontMetricsF metrics(font);
    const double padX = 12.0 * uiScale;
    const double padY = 8.0 * uiScale;
    const QRectF bounds = metrics.boundingRect(
        QRectF(0.0, 0.0, canvasWidth - padX * 4.0, canvasHeight - padY * 4.0),
        Qt::AlignCenter | Qt::TextWordWrap, style.text);
    QRectF positioned = bounds.adjusted(-padX, -padY, padX, padY);
    positioned.moveCenter(
        QPointF(style.positionX * canvasWidth, style.positionY * canvasHeight));
    return {positioned.left() / canvasWidth, positioned.top() / canvasHeight,
            positioned.width() / canvasWidth, positioned.height() / canvasHeight};
}

// Whether a normalized canvas point lands inside a clip's transformed content
// rect. Inverse of the monitor's contentToCanvas, evaluated at the reference
// canvas so the math matches the on-screen handles.
bool monitorPointInClip(const videx::core::MotionKeyframe& motion,
                        const videx::core::Clip& clip, const QRectF& contentRect,
                        const double normalizedX, const double normalizedY) {
    constexpr double canvasWidth = 1280.0;
    constexpr double canvasHeight = 720.0;
    constexpr double pi = 3.14159265358979323846;
    const double pointX = normalizedX * canvasWidth;
    const double pointY = normalizedY * canvasHeight;
    const double anchorCanvasX = canvasWidth * 0.5 + motion.positionX;
    const double anchorCanvasY = canvasHeight * 0.5 + motion.positionY;
    const double radians = motion.rotationDegrees * pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double deltaX = pointX - anchorCanvasX;
    const double deltaY = pointY - anchorCanvasY;
    const double localX = (cosine * deltaX + sine * deltaY) /
                          (motion.scaleX == 0.0 ? 1.0 : motion.scaleX);
    const double localY = (-sine * deltaX + cosine * deltaY) /
                          (motion.scaleY == 0.0 ? 1.0 : motion.scaleY);
    const double contentX = motion.anchorX * canvasWidth + localX;
    const double contentY = motion.anchorY * canvasHeight + localY;
    const double left = std::max(clip.cropLeft, contentRect.left()) * canvasWidth;
    const double right =
        std::min(1.0 - clip.cropRight, contentRect.right()) * canvasWidth;
    const double top = std::max(clip.cropTop, contentRect.top()) * canvasHeight;
    const double bottom =
        std::min(1.0 - clip.cropBottom, contentRect.bottom()) * canvasHeight;
    return contentX >= left && contentX <= right && contentY >= top &&
           contentY <= bottom;
}

// Effects-browser pseudo codes: values below 100 are core::EffectType, the
// rest map to clip properties applied on drop (fades, transitions).
constexpr int browserFadeIn = 100;
constexpr int browserFadeOut = 101;
constexpr int browserDissolve = 102;
constexpr int browserCrossfade = 103;

// One frame of the synthetic clip used to demonstrate effects in the browser:
// a moving diagonal gradient with a travelling white dot.
QImage effectPreviewBase(const int frameIndex, const int frameCount, const int width,
                         const int height) {
    QImage frame(width, height, QImage::Format_RGB32);
    const double phase = static_cast<double>(frameIndex) / frameCount;
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(frame.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const double diagonal =
                std::fmod((static_cast<double>(x) / width +
                           static_cast<double>(y) / height) * 0.5 + phase, 1.0);
            const int red = static_cast<int>(90 + 120 * diagonal);
            const int green = static_cast<int>(60 + 100 * (1.0 - diagonal));
            const int blue = static_cast<int>(140 + 90 * diagonal);
            line[x] = qRgb(std::clamp(red, 0, 255), std::clamp(green, 0, 255),
                           std::clamp(blue, 0, 255));
        }
    }
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(250, 250, 250));
    const double dotX = 8.0 + phase * (width - 16.0);
    painter.drawEllipse(QPointF(dotX, height * 0.55), 6.0, 6.0);
    return frame;
}

QImage effectPreviewFrame(const int code, const int frameIndex, const int frameCount,
                          const int width, const int height) {
    QImage frame = effectPreviewBase(frameIndex, frameCount, width, height);
    const double phase = static_cast<double>(frameIndex) / frameCount;
    const auto forEachPixel = [&frame, width, height](const auto& transform) {
        for (int y = 0; y < height; ++y) {
            auto* line = reinterpret_cast<QRgb*>(frame.scanLine(y));
            for (int x = 0; x < width; ++x) {
                line[x] = transform(x, y, line[x]);
            }
        }
    };
    switch (code) {
    case static_cast<int>(videx::core::EffectType::Brightness):
        forEachPixel([](int, int, const QRgb pixel) {
            return qRgb(std::min(255, qRed(pixel) + 80),
                        std::min(255, qGreen(pixel) + 80),
                        std::min(255, qBlue(pixel) + 80));
        });
        break;
    case static_cast<int>(videx::core::EffectType::Contrast):
        forEachPixel([](int, int, const QRgb pixel) {
            const auto stretch = [](const int value) {
                return std::clamp(
                    static_cast<int>((value - 128) * 1.9 + 128), 0, 255);
            };
            return qRgb(stretch(qRed(pixel)), stretch(qGreen(pixel)),
                        stretch(qBlue(pixel)));
        });
        break;
    case static_cast<int>(videx::core::EffectType::Saturation):
        forEachPixel([](int, int, const QRgb pixel) {
            const int gray = qGray(pixel);
            const auto toward = [gray](const int value) {
                return gray + (value - gray) / 8;
            };
            return qRgb(toward(qRed(pixel)), toward(qGreen(pixel)),
                        toward(qBlue(pixel)));
        });
        break;
    case static_cast<int>(videx::core::EffectType::Blur): {
        const QImage small = frame.scaled(width / 5, height / 5,
                                          Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
        frame = small.scaled(width, height, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
        break;
    }
    case static_cast<int>(videx::core::EffectType::Vignette):
        forEachPixel([width, height](const int x, const int y, const QRgb pixel) {
            const double nx = (x - width * 0.5) / (width * 0.5);
            const double ny = (y - height * 0.5) / (height * 0.5);
            const double shade =
                std::clamp(1.0 - 0.85 * (nx * nx + ny * ny), 0.0, 1.0);
            return qRgb(static_cast<int>(qRed(pixel) * shade),
                        static_cast<int>(qGreen(pixel) * shade),
                        static_cast<int>(qBlue(pixel) * shade));
        });
        break;
    case browserFadeIn:
    case browserFadeOut: {
        const double opacity = code == browserFadeIn ? phase : 1.0 - phase;
        forEachPixel([opacity](int, int, const QRgb pixel) {
            return qRgb(static_cast<int>(qRed(pixel) * opacity),
                        static_cast<int>(qGreen(pixel) * opacity),
                        static_cast<int>(qBlue(pixel) * opacity));
        });
        break;
    }
    case browserDissolve:
    case browserCrossfade: {
        // Crossfade between the clip and a second look (swapped channels).
        const QImage other = effectPreviewBase(
            (frameIndex + frameCount / 2) % frameCount, frameCount, width,
            height).rgbSwapped();
        forEachPixel([&other, phase](const int x, const int y, const QRgb pixel) {
            const QRgb second = other.pixel(x, y);
            const auto mix = [phase](const int a, const int b) {
                return static_cast<int>(a * (1.0 - phase) + b * phase);
            };
            return qRgb(mix(qRed(pixel), qRed(second)),
                        mix(qGreen(pixel), qGreen(second)),
                        mix(qBlue(pixel), qBlue(second)));
        });
        break;
    }
    default:
        break;
    }
    return frame;
}

// Kind-relative display label for a track (custom name, else V2/A3 style).
// Mirrors the timeline header numbering: V1/A1 are the first of their kind.
QString trackPatchLabel(const videx::core::Sequence& sequence,
                        const videx::core::TrackId trackId,
                        const videx::core::TrackKind kind) {
    int kindNumber = 0;
    for (const videx::core::Track& track : sequence.tracks()) {
        if (track.kind == kind) {
            ++kindNumber;
        }
        if (track.id == trackId) {
            if (track.kind != kind) {
                break;
            }
            if (!track.name.empty()) {
                return QString::fromStdString(track.name);
            }
            return QStringLiteral("%1%2")
                .arg(kind == videx::core::TrackKind::Video ? QStringLiteral("V")
                                                           : QStringLiteral("A"))
                .arg(kindNumber);
        }
    }
    return QStringLiteral("—");
}

QString timecodeString(const videx::core::FrameRate& rate, const videx::core::Frame frame) {
    const int nominalRate =
        std::max(1, static_cast<int>(std::lround(rate.framesPerSecond())));
    const std::int64_t frameIndex = std::max<videx::core::Frame>(0, frame);
    const std::int64_t wholeSeconds = frameIndex * rate.denominator / rate.numerator;
    const std::int64_t secondStartFrame =
        (wholeSeconds * rate.numerator + rate.denominator - 1) / rate.denominator;
    const int displayFrame = std::clamp(
        static_cast<int>(frameIndex - secondStartFrame), 0, nominalRate - 1);
    return QStringLiteral("%1:%2:%3:%4")
        .arg(wholeSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((wholeSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(wholeSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(displayFrame, 2, 10, QLatin1Char('0'));
}

videx::core::MotionKeyframe motionAt(const videx::core::Clip& clip,
                                     const videx::core::Frame localFrame) {
    videx::core::MotionKeyframe base{.opacity = clip.opacity,
                                     .positionX = clip.positionX,
                                     .positionY = clip.positionY,
                                     .scaleX = clip.scaleX,
                                     .scaleY = clip.scaleY,
                                     .rotationDegrees = clip.rotationDegrees,
                                     .anchorX = clip.anchorX,
                                     .anchorY = clip.anchorY};
    if (clip.motionKeyframes.empty()) return base;
    const auto next = std::ranges::lower_bound(
        clip.motionKeyframes, localFrame, {}, &videx::core::MotionKeyframe::frameOffset);
    if (next == clip.motionKeyframes.begin()) return *next;
    if (next == clip.motionKeyframes.end()) return clip.motionKeyframes.back();
    const auto previous = std::prev(next);
    if (previous->interpolation == videx::core::KeyframeInterpolation::Hold) return *previous;
    double ratio = static_cast<double>(localFrame - previous->frameOffset) /
                   static_cast<double>(next->frameOffset - previous->frameOffset);
    ratio = videx::core::interpolationProgress(previous->interpolation, ratio);
    const auto mix = [ratio](const double left, const double right) {
        return left + (right - left) * ratio;
    };
    return {.frameOffset = localFrame,
            .opacity = mix(previous->opacity, next->opacity),
            .positionX = mix(previous->positionX, next->positionX),
            .positionY = mix(previous->positionY, next->positionY),
            .scaleX = mix(previous->scaleX, next->scaleX),
            .scaleY = mix(previous->scaleY, next->scaleY),
            .rotationDegrees = mix(previous->rotationDegrees, next->rotationDegrees),
            .anchorX = mix(previous->anchorX, next->anchorX),
            .anchorY = mix(previous->anchorY, next->anchorY),
            .interpolation = previous->interpolation};
}

double gainAt(const videx::core::Clip& clip, const videx::core::Frame localFrame) {
    if (clip.gainKeyframes.empty()) return clip.audioGainDb;
    const auto next = std::ranges::lower_bound(
        clip.gainKeyframes, localFrame, {}, &videx::core::GainKeyframe::frameOffset);
    if (next == clip.gainKeyframes.begin()) return next->gainDb;
    if (next == clip.gainKeyframes.end()) return clip.gainKeyframes.back().gainDb;
    const auto previous = std::prev(next);
    if (previous->interpolation == videx::core::KeyframeInterpolation::Hold)
        return previous->gainDb;
    double ratio = static_cast<double>(localFrame - previous->frameOffset) /
                   static_cast<double>(next->frameOffset - previous->frameOffset);
    ratio = videx::core::interpolationProgress(previous->interpolation, ratio);
    return previous->gainDb + (next->gainDb - previous->gainDb) * ratio;
}

videx::core::Frame sourceOffsetAt(const videx::core::Clip& clip,
                                  const videx::core::Frame localFrame) {
    if (localFrame <= 0) return 0;
    if (clip.speedKeyframes.empty()) {
        return static_cast<videx::core::Frame>(std::llround(
            static_cast<double>(localFrame) * clip.playbackRate));
    }
    const double requested = static_cast<double>(localFrame);
    double offset = std::min(requested,
        static_cast<double>(clip.speedKeyframes.front().frameOffset)) *
        clip.speedKeyframes.front().rate;
    for (std::size_t index = 0; index + 1U < clip.speedKeyframes.size(); ++index) {
        const auto& previous = clip.speedKeyframes[index];
        const auto& next = clip.speedKeyframes[index + 1U];
        if (requested <= previous.frameOffset) break;
        const double segment = static_cast<double>(next.frameOffset - previous.frameOffset);
        const double ratio = std::clamp((requested - previous.frameOffset) / segment, 0.0, 1.0);
        const double curveIntegral =
            videx::core::interpolationIntegral(previous.interpolation, ratio);
        offset += segment * (previous.rate * ratio +
            (next.rate - previous.rate) * curveIntegral);
    }
    const double lastFrame = static_cast<double>(clip.speedKeyframes.back().frameOffset);
    if (requested > lastFrame)
        offset += (requested - lastFrame) * clip.speedKeyframes.back().rate;
    return static_cast<videx::core::Frame>(std::llround(offset));
}

QByteArray timelineManifestLine(const videx::core::TrackKind kind,
                                const videx::core::Clip& clip, const QString& path,
                                const int trackOrder = 0) {
    QByteArray line;
    line += kind == videx::core::TrackKind::Video ? 'V' : 'A';
    const auto field = [&line](const QByteArray& value) { line += '\t'; line += value; };
    field(QByteArray::number(clip.timeline.start));
    field(QByteArray::number(clip.timeline.duration));
    field(QByteArray::number(clip.sourceStart));
    field(QByteArray::number(clip.opacity, 'g', 17));
    field(QByteArray::number(clip.audioGainDb, 'g', 17));
    field(QByteArray::number(clip.playbackRate, 'g', 17));
    field(QByteArray::number(clip.fadeInFrames));
    field(QByteArray::number(clip.fadeOutFrames));
    field(QByteArray::number(clip.videoTransitionInFrames));
    field(QByteArray::number(clip.audioTransitionInFrames));
    field(QByteArray::number(clip.positionX, 'g', 17));
    field(QByteArray::number(clip.positionY, 'g', 17));
    field(QByteArray::number(clip.scaleX, 'g', 17));
    field(QByteArray::number(clip.scaleY, 'g', 17));
    field(QByteArray::number(clip.rotationDegrees, 'g', 17));
    field(QByteArray::number(clip.anchorX, 'g', 17));
    field(QByteArray::number(clip.anchorY, 'g', 17));
    field(QByteArray::number(clip.cropLeft, 'g', 17));
    field(QByteArray::number(clip.cropRight, 'g', 17));
    field(QByteArray::number(clip.cropTop, 'g', 17));
    field(QByteArray::number(clip.cropBottom, 'g', 17));
    QByteArray speedSpec;
    for (const videx::core::SpeedKeyframe& key : clip.speedKeyframes) {
        if (!speedSpec.isEmpty()) speedSpec += ';';
        speedSpec += QByteArray::number(key.frameOffset) + ':' +
                     QByteArray::number(key.rate, 'g', 17) + ':' +
                     QByteArray::number(static_cast<int>(key.interpolation));
    }
    field(speedSpec.isEmpty() ? QByteArray("-") : speedSpec);
    field(QByteArray::number(static_cast<int>(clip.maskShape)));
    field(QByteArray::number(clip.maskCenterX, 'g', 17));
    field(QByteArray::number(clip.maskCenterY, 'g', 17));
    field(QByteArray::number(clip.maskWidth, 'g', 17));
    field(QByteArray::number(clip.maskHeight, 'g', 17));
    field(QByteArray::number(clip.maskFeather, 'g', 17));
    field(clip.maskInverted ? QByteArray("1") : QByteArray("0"));
    QByteArray effectSpec;
    for (const videx::core::ClipEffect& effect : clip.effects) {
        if (!effectSpec.isEmpty()) effectSpec += '|';
        effectSpec += QByteArray::number(static_cast<int>(effect.type)) + ',' +
                      (effect.enabled ? QByteArray("1") : QByteArray("0")) + ',' +
                      QByteArray::number(effect.amount, 'g', 17) + ',';
        if (effect.keyframes.empty()) {
            effectSpec += '-';
        } else {
            bool first = true;
            for (const videx::core::EffectKeyframe& key : effect.keyframes) {
                if (!first) effectSpec += ';';
                first = false;
                effectSpec += QByteArray::number(key.frameOffset) + ':' +
                              QByteArray::number(key.value, 'g', 17) + ':' +
                              QByteArray::number(static_cast<int>(key.interpolation));
            }
        }
    }
    field(effectSpec.isEmpty() ? QByteArray("-") : effectSpec);
    QByteArray motionSpec;
    for (const videx::core::MotionKeyframe& key : clip.motionKeyframes) {
        if (!motionSpec.isEmpty()) motionSpec += ';';
        motionSpec += QByteArray::number(key.frameOffset) + ',' +
                      QByteArray::number(key.opacity, 'g', 17) + ',' +
                      QByteArray::number(key.positionX, 'g', 17) + ',' +
                      QByteArray::number(key.positionY, 'g', 17) + ',' +
                      QByteArray::number(key.scaleX, 'g', 17) + ',' +
                      QByteArray::number(key.scaleY, 'g', 17) + ',' +
                      QByteArray::number(key.rotationDegrees, 'g', 17) + ',' +
                      QByteArray::number(key.anchorX, 'g', 17) + ',' +
                      QByteArray::number(key.anchorY, 'g', 17) + ',' +
                      QByteArray::number(static_cast<int>(key.interpolation));
    }
    field(motionSpec.isEmpty() ? QByteArray("-") : motionSpec);
    QByteArray gainSpec;
    for (const videx::core::GainKeyframe& key : clip.gainKeyframes) {
        if (!gainSpec.isEmpty()) gainSpec += ';';
        gainSpec += QByteArray::number(key.frameOffset) + ',' +
                    QByteArray::number(key.gainDb, 'g', 17) + ',' +
                    QByteArray::number(static_cast<int>(key.interpolation));
    }
    field(gainSpec.isEmpty() ? QByteArray("-") : gainSpec);
    field(QByteArray::number(trackOrder));
    field(path.toUtf8());
    line += '\n';
    return line;
}

void applyClipEffects(videx::render::QtMonitorWidget* monitor,
                      const videx::core::Clip& clip, const videx::core::Frame frame) {
    double brightness = 0.0;
    double contrast = 0.0;
    double saturation = 0.0;
    double blur = 0.0;
    double vignette = 0.0;
    const videx::core::Frame local = frame - clip.timeline.start;
    for (const videx::core::ClipEffect& effect : clip.effects) {
        if (!effect.enabled) {
            continue;
        }
        const double value = effectValueAt(effect, local);
        switch (effect.type) {
        case videx::core::EffectType::Brightness: brightness += value; break;
        case videx::core::EffectType::Contrast: contrast += value; break;
        case videx::core::EffectType::Saturation: saturation += value; break;
        case videx::core::EffectType::Blur: blur = std::max(blur, value); break;
        case videx::core::EffectType::Vignette: vignette = std::max(vignette, value); break;
        }
    }
    monitor->setVideoEffects(brightness, contrast, saturation, blur, vignette);
}

template <typename Integer>
std::optional<Integer> readLittleEndian(const QByteArray& data, qsizetype& offset) {
    if (offset < 0 || data.size() - offset < static_cast<qsizetype>(sizeof(Integer))) {
        return std::nullopt;
    }
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<Unsigned>(
                     static_cast<unsigned char>(data[offset + static_cast<qsizetype>(index)]))
                 << (index * 8U);
    }
    offset += static_cast<qsizetype>(sizeof(Integer));
    return static_cast<Integer>(value);
}

std::optional<QImage> parseVideoFrame(const QByteArray& data) {
    constexpr qsizetype headerSize = 28;
    if (data.size() < headerSize || data.first(4) != QByteArrayLiteral("VXF1")) {
        return std::nullopt;
    }

    qsizetype offset = 4;
    const auto width = readLittleEndian<std::uint32_t>(data, offset);
    const auto height = readLittleEndian<std::uint32_t>(data, offset);
    const auto timestamp = readLittleEndian<std::int64_t>(data, offset);
    const auto payloadSize = readLittleEndian<std::uint64_t>(data, offset);
    if (!width || !height || !timestamp || !payloadSize || *width == 0 || *height == 0 ||
        *width > 16'384U || *height > 16'384U) {
        return std::nullopt;
    }

    const std::uint64_t expectedSize =
        static_cast<std::uint64_t>(*width) * static_cast<std::uint64_t>(*height) * 4U;
    if (*payloadSize != expectedSize || expectedSize > static_cast<std::uint64_t>(data.size()) ||
        offset != headerSize || data.size() - offset != static_cast<qsizetype>(expectedSize)) {
        return std::nullopt;
    }

    const auto* pixels = reinterpret_cast<const uchar*>(data.constData() + offset);
    return QImage(pixels, static_cast<int>(*width), static_cast<int>(*height),
                  static_cast<int>(*width * 4U), QImage::Format_RGBA8888)
        .copy();
}

struct ParsedAudio final {
    std::uint32_t sampleRate = 0;
    std::uint32_t channelCount = 0;
    QByteArray pcm;
};

std::optional<ParsedAudio> parseAudioBuffer(const QByteArray& data) {
    constexpr qsizetype headerSize = 28;
    if (data.size() <= headerSize || data.first(4) != QByteArrayLiteral("VXA1")) {
        return std::nullopt;
    }
    qsizetype offset = 4;
    const auto sampleRate = readLittleEndian<std::uint32_t>(data, offset);
    const auto channelCount = readLittleEndian<std::uint32_t>(data, offset);
    const auto timestamp = readLittleEndian<std::int64_t>(data, offset);
    const auto payloadSize = readLittleEndian<std::uint64_t>(data, offset);
    if (!sampleRate || !channelCount || !timestamp || !payloadSize || *sampleRate == 0 ||
        *channelCount == 0 || *channelCount > 32U || offset != headerSize ||
        *payloadSize != static_cast<std::uint64_t>(data.size() - offset) ||
        *payloadSize % (sizeof(float) * *channelCount) != 0U) {
        return std::nullopt;
    }
    return ParsedAudio{
        .sampleRate = *sampleRate,
        .channelCount = *channelCount,
        .pcm = data.mid(offset),
    };
}

std::optional<std::vector<float>> parseWaveform(const QByteArray& data) {
    if (data.size() < 8 || data.first(4) != QByteArrayLiteral("VXW1")) {
        return std::nullopt;
    }
    qsizetype offset = 4;
    const auto count = readLittleEndian<std::uint32_t>(data, offset);
    if (!count || *count == 0 || *count > 100'000U ||
        data.size() - offset != static_cast<qsizetype>(*count * sizeof(float))) {
        return std::nullopt;
    }
    std::vector<float> peaks(*count);
    std::memcpy(peaks.data(), data.constData() + offset, peaks.size() * sizeof(float));
    if (!std::ranges::all_of(peaks, [](const float value) {
            return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
        })) {
        return std::nullopt;
    }
    return peaks;
}

QString cacheKeyForPath(const QString& path) {
    const QFileInfo info(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(info.absoluteFilePath().toUtf8());
    hash.addData(QByteArray::number(info.size()));
    hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    return QString::fromLatin1(hash.result().toHex());
}

QKeySequence primaryShortcut(const Qt::Key key,
                             const Qt::KeyboardModifiers additional = {}) {
#if defined(Q_OS_MACOS)
    return QKeySequence(QKeyCombination(Qt::MetaModifier | additional, key));
#else
    return QKeySequence(QKeyCombination(Qt::ControlModifier | additional, key));
#endif
}

} // namespace

namespace videx::ui {

QString previewPathReasonText(const PreviewPathReason reason) {
    switch (reason) {
    case PreviewPathReason::DirectOverlay:
        return QCoreApplication::translate("MainWindow", "direct");
    case PreviewPathReason::PreviewCache:
        return QCoreApplication::translate("MainWindow", "render cache");
    case PreviewPathReason::Stopped:
        return QCoreApplication::translate("MainWindow", "stopped");
    case PreviewPathReason::NoVideoClip:
        return QCoreApplication::translate("MainWindow", "no video clip");
    case PreviewPathReason::MultipleVideoClips:
        return QCoreApplication::translate("MainWindow", "multiple video clips");
    case PreviewPathReason::BaseSpeedKeyframes:
        return QCoreApplication::translate("MainWindow", "clip speed ramp");
    case PreviewPathReason::BaseTransition:
        return QCoreApplication::translate("MainWindow", "clip transition");
    case PreviewPathReason::BaseMediaMissing:
        return QCoreApplication::translate("MainWindow", "media missing");
    case PreviewPathReason::TitleBehindBase:
        return QCoreApplication::translate("MainWindow", "title behind video");
    case PreviewPathReason::TitleEffects:
        return QCoreApplication::translate("MainWindow", "title effect keyframes");
    case PreviewPathReason::TitleTransition:
        return QCoreApplication::translate("MainWindow", "title transition blend");
    case PreviewPathReason::TitleRasterUnavailable:
        return QCoreApplication::translate("MainWindow", "title raster unavailable");
    }
    return QCoreApplication::translate("MainWindow", "unknown");
}

void MainWindow::setPreviewPathReason(const PreviewPathReason reason) {
    previewPathReason_ = reason;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Videx"));
    resize(1440, 900);
    setDockNestingEnabled(true);
    setAcceptDrops(true);

    createActions();
    createPanels();
    createStatusBar();
    createProject();
    defaultWorkspaceState_ = saveState();
    restoreWorkspaceState();
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(60'000);
    connect(autosaveTimer_, &QTimer::timeout, this, [this] { autosaveProject(); });
    autosaveTimer_->start();
}

void MainWindow::createActions() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* newProject = fileMenu->addAction(tr("New Project"));
    newProject->setShortcut(QKeySequence::New);
    connect(newProject, &QAction::triggered, this, [this] { createProject(); });

    auto* openProjectAction = fileMenu->addAction(tr("Open Project..."));
    openProjectAction->setShortcut(QKeySequence::Open);
    connect(openProjectAction, &QAction::triggered, this, [this] { openProject(); });

    auto* importMediaAction = fileMenu->addAction(tr("Import Media..."));
    importMediaAction->setShortcut(primaryShortcut(Qt::Key_I));
#if defined(VIDEX_HAS_MEDIA_WORKER)
    connect(importMediaAction, &QAction::triggered, this, [this] { importMedia(); });
#else
    importMediaAction->setEnabled(false);
    importMediaAction->setToolTip(tr("Build Videx with the FFmpeg media worker to import media"));
#endif

    auto* saveProjectAction = fileMenu->addAction(tr("Save Project"));
    saveProjectAction->setShortcut(QKeySequence::Save);
    connect(saveProjectAction, &QAction::triggered, this,
            [this] { static_cast<void>(saveProject()); });

    auto* saveProjectAsAction = fileMenu->addAction(tr("Save Project As..."));
    saveProjectAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveProjectAsAction, &QAction::triggered, this,
            [this] { static_cast<void>(saveProjectAs()); });

    auto* exportAction = fileMenu->addAction(tr("Export Review MP4..."));
    exportAction->setShortcut(primaryShortcut(Qt::Key_M));
#if defined(VIDEX_HAS_MEDIA_WORKER)
    connect(exportAction, &QAction::triggered, this, [this] { exportReview(); });
#else
    exportAction->setEnabled(false);
#endif

    auto* exportOtioAction = fileMenu->addAction(tr("Export OpenTimelineIO..."));
    connect(exportOtioAction, &QAction::triggered, this, [this] { exportOtio(); });

    fileMenu->addSeparator();

    auto* quit = fileMenu->addAction(tr("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = editMenu->addAction(tr("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] {
        if (editSession_.undo().succeeded()) {
            setDirty(true);
        }
        refreshEditor();
    });

    redoAction_ = editMenu->addAction(tr("Redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] {
        if (editSession_.redo().succeeded()) {
            setDirty(true);
        }
        refreshEditor();
    });
    editMenu->addSeparator();
    auto* cutAction = editMenu->addAction(tr("Cut Clips"));
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, [this] {
        copySelectedClips();
        deleteSelectedClips(false);
    });
    auto* copyAction = editMenu->addAction(tr("Copy Clips"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, [this] { copySelectedClips(); });
    auto* pasteAction = editMenu->addAction(tr("Paste Clips at Playhead"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this,
            [this] { pasteClipsAt(timeline_ == nullptr ? 0 : timeline_->playheadFrame()); });
    auto* duplicateAction = editMenu->addAction(tr("Duplicate Clips"));
    duplicateAction->setShortcut(primaryShortcut(Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, this, [this] {
        if (timeline_ == nullptr || timeline_->selectedClipIds().empty()) {
            return;
        }
        core::Frame end = 0;
        for (const core::ClipId id : timeline_->selectedClipIds()) {
            if (const core::Clip* clip = editSession_.sequence().findClip(id)) {
                end = std::max(end, clip->timeline.end());
            }
        }
        copySelectedClips();
        pasteClipsAt(end);
    });

    auto* sequenceMenu = menuBar()->addMenu(tr("&Sequence"));
    transportAction_ = sequenceMenu->addAction(tr("Play/Pause"));
    transportAction_->setShortcut(QKeySequence(Qt::Key_Space));
    transportAction_->setShortcutContext(Qt::ApplicationShortcut);
    connect(transportAction_, &QAction::triggered, this, [this] { togglePlayback(); });

    auto addTransportShortcut = [this, sequenceMenu](const QString& label,
                                                     const QKeySequence& shortcut,
                                                     auto handler) {
        QAction* action = sequenceMenu->addAction(label);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::ApplicationShortcut);
        connect(action, &QAction::triggered, this, std::move(handler));
    };
    addTransportShortcut(tr("Play Reverse"), QKeySequence(Qt::Key_J),
                         [this] { startPlayback(-1); });
    addTransportShortcut(tr("Stop"), QKeySequence(Qt::Key_K), [this] { pausePlayback(); });
    addTransportShortcut(tr("Play Forward"), QKeySequence(Qt::Key_L),
                         [this] { startPlayback(1); });
    addTransportShortcut(tr("Fullscreen Program Monitor"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+F")),
                         [this] { toggleMonitorFullscreen(); });
    // Premiere-style alias for the same toggle.
    addTransportShortcut(tr("Fullscreen Program Monitor (Ctrl+`)"),
                         QKeySequence(Qt::ControlModifier | Qt::Key_QuoteLeft),
                         [this] { toggleMonitorFullscreen(); });
    addTransportShortcut(tr("Reset Workspace"), QKeySequence(), [this] {
        if (!defaultWorkspaceState_.isEmpty()) {
            restoreState(defaultWorkspaceState_);
            statusBar()->showMessage(tr("Workspace layout reset to default"), 3000);
        }
    });
    addTransportShortcut(tr("Render In to Out"), QKeySequence(QStringLiteral("Ctrl+R")),
                         [this] {
                             if (sequenceInFrame_ < 0 ||
                                 sequenceOutFrame_ <= sequenceInFrame_) {
                                 statusBar()->showMessage(
                                     tr("Mark a sequence In/Out range first "
                                        "(Shift+I / Shift+O)"),
                                     4000);
                                 return;
                             }
                             const QString cacheRoot =
                                 QStandardPaths::writableLocation(
                                     QStandardPaths::CacheLocation) +
                                 QStringLiteral("/previews");
                             QDir().mkpath(cacheRoot);
                             exportReview(
                                 cacheRoot +
                                     QStringLiteral("/preview_%1_%2.mp4")
                                         .arg(sequenceInFrame_)
                                         .arg(editSession_.sequence().revision()),
                                 true);
                         });
    addTransportShortcut(tr("Previous Frame"), QKeySequence(Qt::Key_Left), [this] {
        pausePlayback();
        timeline_->setPlayheadFrame(timeline_->playheadFrame() - 1);
    });
    addTransportShortcut(tr("Next Frame"), QKeySequence(Qt::Key_Right), [this] {
        pausePlayback();
        timeline_->setPlayheadFrame(timeline_->playheadFrame() + 1);
    });
    addTransportShortcut(tr("Go to Start"), QKeySequence(Qt::Key_Home), [this] {
        pausePlayback();
        timeline_->setPlayheadFrame(0);
    });
    addTransportShortcut(tr("Go to End"), QKeySequence(Qt::Key_End), [this] {
        pausePlayback();
        timeline_->setPlayheadFrame(std::max<core::Frame>(0, sequenceEndFrame() - 1));
    });
    addTransportShortcut(tr("Slip Source Left"),
                         QKeySequence(QStringLiteral("Alt+Left")),
                         [this] { slipSelected(-1); });
    addTransportShortcut(tr("Slip Source Right"),
                         QKeySequence(QStringLiteral("Alt+Right")),
                         [this] { slipSelected(1); });
    addTransportShortcut(tr("Roll Cut Left"),
                         primaryShortcut(Qt::Key_Left, Qt::AltModifier),
                         [this] { rollSelected(-1); });
    addTransportShortcut(tr("Roll Cut Right"),
                         primaryShortcut(Qt::Key_Right, Qt::AltModifier),
                         [this] { rollSelected(1); });
    addTransportShortcut(tr("Mark Source In"), QKeySequence(Qt::Key_I), [this] {
        if (sourceSeekSlider_ != nullptr && currentSourceAsset_) {
            sourceInFrame_ = sourceSeekSlider_->value();
            sourceOutFrame_ = std::max(sourceOutFrame_, sourceInFrame_ + 1);
            sourceRangeLabel_->setText(
                tr("Source In %1 / Out %2").arg(sourceInFrame_).arg(sourceOutFrame_));
        }
    });
    addTransportShortcut(tr("Mark Source Out"), QKeySequence(Qt::Key_O), [this] {
        if (sourceSeekSlider_ != nullptr && currentSourceAsset_) {
            sourceOutFrame_ = std::min(sourceDurationFrames_, std::max<core::Frame>(
                sourceInFrame_ + 1, sourceSeekSlider_->value() + 1));
            sourceRangeLabel_->setText(
                tr("Source In %1 / Out %2").arg(sourceInFrame_).arg(sourceOutFrame_));
        }
    });
    addTransportShortcut(tr("Mark Sequence In"), QKeySequence(QStringLiteral("Shift+I")),
                         [this] {
        sequenceInFrame_ = timeline_->playheadFrame();
        if (sequenceOutFrame_ <= sequenceInFrame_)
            sequenceOutFrame_ = std::max(sequenceInFrame_ + 1, sequenceEndFrame());
        timeline_->setInOutRange(sequenceInFrame_, sequenceOutFrame_);
        statusBar()->showMessage(
            tr("Sequence In %1 / Out %2").arg(sequenceInFrame_).arg(sequenceOutFrame_), 2500);
    });
    addTransportShortcut(tr("Mark Sequence Out"), QKeySequence(QStringLiteral("Shift+O")),
                         [this] {
        sequenceOutFrame_ = std::max(sequenceInFrame_ + 1, timeline_->playheadFrame() + 1);
        if (sequenceInFrame_ < 0) sequenceInFrame_ = 0;
        timeline_->setInOutRange(sequenceInFrame_, sequenceOutFrame_);
        statusBar()->showMessage(
            tr("Sequence In %1 / Out %2").arg(sequenceInFrame_).arg(sequenceOutFrame_), 2500);
    });
    addTransportShortcut(tr("Clear Sequence In/Out"),
                         QKeySequence(QStringLiteral("Shift+X")), [this] {
        sequenceInFrame_ = -1;
        sequenceOutFrame_ = -1;
        timeline_->setInOutRange(-1, -1);
        statusBar()->showMessage(tr("Sequence In/Out cleared"), 2000);
    });
    addTransportShortcut(tr("Lift Sequence In/Out"), QKeySequence(Qt::Key_Semicolon),
                         [this] { editSequenceRange(false); });
    addTransportShortcut(tr("Extract Sequence In/Out"), QKeySequence(Qt::Key_Apostrophe),
                         [this] { editSequenceRange(true); });
    addTransportShortcut(tr("Insert Source"), QKeySequence(Qt::Key_Comma),
                         [this] { insertSourceSelection(false); });
    addTransportShortcut(tr("Overwrite Source"), QKeySequence(Qt::Key_Period),
                         [this] { insertSourceSelection(true); });
    auto* addMarkerAction = sequenceMenu->addAction(tr("Add Marker..."));
    addMarkerAction->setShortcut(QKeySequence(Qt::Key_M));
    connect(addMarkerAction, &QAction::triggered, this, [this] {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Add Marker"), tr("Name"),
                                                   QLineEdit::Normal, tr("Marker"), &accepted);
        if (!accepted || name.trimmed().isEmpty()) {
            return;
        }
        const auto result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Add marker",
            .command = core::AddMarkerCommand{timeline_->playheadFrame(),
                                               name.trimmed().toStdString()},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
        }
    });
    addTransportShortcut(tr("Go to Next Marker"),
                         QKeySequence(Qt::ShiftModifier | Qt::Key_M), [this] {
        const core::Frame playhead = timeline_->playheadFrame();
        for (const core::Marker& marker : editSession_.sequence().markers()) {
            if (marker.position > playhead) {
                timeline_->setPlayheadFrame(marker.position);
                return;
            }
        }
    });
    addTransportShortcut(tr("Go to Previous Marker"),
                         QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_M),
                         [this] {
        const core::Frame playhead = timeline_->playheadFrame();
        const auto& markers = editSession_.sequence().markers();
        for (auto it = markers.rbegin(); it != markers.rend(); ++it) {
            if (it->position < playhead) {
                timeline_->setPlayheadFrame(it->position);
                return;
            }
        }
    });
    auto* addCaptionAction = sequenceMenu->addAction(tr("Add Caption..."));
    addCaptionAction->setShortcut(primaryShortcut(Qt::Key_T, Qt::ShiftModifier));
    connect(addCaptionAction, &QAction::triggered, this, [this] {
        bool accepted = false;
        const QString text = QInputDialog::getMultiLineText(
            this, tr("Add Caption"), tr("Caption text"), {}, &accepted);
        if (!accepted || text.trimmed().isEmpty()) {
            return;
        }
        const core::Frame duration = std::max<core::Frame>(
            1, static_cast<core::Frame>(std::llround(
                   editSession_.sequence().frameRate().framesPerSecond() * 3.0)));
        const auto result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Add caption",
            .command = core::AddCaptionCommand{{timeline_->playheadFrame(), duration},
                                                text.trimmed().toStdString()},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateCaptionOverlay(timeline_->playheadFrame());
        }
    });
    auto* editCaptionAction = sequenceMenu->addAction(tr("Edit Caption at Playhead..."));
    connect(editCaptionAction, &QAction::triggered, this, [this] {
        const core::Frame frame = timeline_->playheadFrame();
        const auto& captions = editSession_.sequence().captions();
        const auto found = std::ranges::find_if(captions, [frame](const core::Caption& caption) {
            return caption.timeline.contains(frame);
        });
        if (found == captions.end()) {
            statusBar()->showMessage(tr("No caption at the playhead"), 3000);
            return;
        }
        bool accepted = false;
        const QString text = QInputDialog::getMultiLineText(
            this, tr("Edit Caption"), tr("Text"), QString::fromStdString(found->text),
            &accepted);
        if (!accepted || text.trimmed().isEmpty()) return;
        const double x = QInputDialog::getDouble(this, tr("Caption Position"), tr("X (%)"),
            found->positionX * 100.0, 0.0, 100.0, 1, &accepted);
        if (!accepted) return;
        const double y = QInputDialog::getDouble(this, tr("Caption Position"), tr("Y (%)"),
            found->positionY * 100.0, 0.0, 100.0, 1, &accepted);
        if (!accepted) return;
        const double size = QInputDialog::getDouble(this, tr("Caption Style"), tr("Font size"),
            found->fontSize, 8.0, 300.0, 1, &accepted);
        if (!accepted) return;
        const auto rgbaText = [](const std::uint32_t rgba) {
            return QStringLiteral("#%1").arg(rgba, 8, 16, QLatin1Char('0')).toUpper();
        };
        const QString foreground = QInputDialog::getText(
            this, tr("Caption Style"), tr("Text color (#RRGGBBAA)"), QLineEdit::Normal,
            rgbaText(found->textColor), &accepted);
        if (!accepted) return;
        const QString background = QInputDialog::getText(
            this, tr("Caption Style"), tr("Background color (#RRGGBBAA)"), QLineEdit::Normal,
            rgbaText(found->backgroundColor), &accepted);
        if (!accepted) return;
        bool foregroundOk = false;
        bool backgroundOk = false;
        const std::uint32_t foregroundRgba = foreground.trimmed().remove(QLatin1Char('#'))
            .toUInt(&foregroundOk, 16);
        const std::uint32_t backgroundRgba = background.trimmed().remove(QLatin1Char('#'))
            .toUInt(&backgroundOk, 16);
        if (!foregroundOk || !backgroundOk) {
            statusBar()->showMessage(tr("Colors must use #RRGGBBAA"), 4000);
            return;
        }
        const QStringList weights{tr("Regular"), tr("Bold")};
        const QString weight = QInputDialog::getItem(
            this, tr("Caption Style"), tr("Weight"), weights, found->bold ? 1 : 0,
            false, &accepted);
        if (!accepted) return;
        const QStringList slants{tr("Normal"), tr("Italic")};
        const QString slant = QInputDialog::getItem(
            this, tr("Caption Style"), tr("Slant"), slants, found->italic ? 1 : 0,
            false, &accepted);
        if (!accepted) return;
        const auto result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Edit caption",
            .command = core::SetCaptionCommand{
                found->id, found->timeline, text.trimmed().toStdString(), x / 100.0, y / 100.0,
                size, foregroundRgba, backgroundRgba, weight == weights[1], slant == slants[1]},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateCaptionOverlay(frame);
        }
    });
    auto* deleteAnnotationAction = sequenceMenu->addAction(tr("Delete Marker/Caption at Playhead"));
    deleteAnnotationAction->setShortcut(
        primaryShortcut(Qt::Key_Delete, Qt::ShiftModifier));
    connect(deleteAnnotationAction, &QAction::triggered, this, [this] {
        std::vector<core::EditCommand> commands;
        const core::Frame frame = timeline_->playheadFrame();
        for (const core::Marker& marker : editSession_.sequence().markers()) {
            if (marker.position == frame) {
                commands.push_back(core::RemoveMarkerCommand{marker.id});
            }
        }
        for (const core::Caption& caption : editSession_.sequence().captions()) {
            if (caption.timeline.contains(frame)) {
                commands.push_back(core::RemoveCaptionCommand{caption.id});
            }
        }
        if (!commands.empty() && editSession_.apply(core::TransactionEnvelope{
                                     .baseRevision = editSession_.sequence().revision(),
                                     .label = "Delete annotation",
                                     .commands = std::move(commands),
                                 }).succeeded()) {
            setDirty(true);
            refreshEditor();
            updateCaptionOverlay(frame);
        }
    });
    menuBar()->addMenu(tr("&Window"));
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* diagnosticsAction = helpMenu->addAction(tr("Diagnostics..."));
    connect(diagnosticsAction, &QAction::triggered, this, [this] {
        qsizetype clipCount = 0;
        for (const core::Track& track : editSession_.sequence().tracks()) {
            clipCount += static_cast<qsizetype>(track.clips.size());
        }
        const QString workerPath =
            QCoreApplication::applicationDirPath() + QStringLiteral("/videx-media-worker") +
#if defined(Q_OS_WIN)
            QStringLiteral(".exe");
#else
            QString{};
#endif
        const QString details =
            tr("Videx diagnostics\n\n"
               "OS: %1 (%2)\n"
               "CPU: %3\n"
               "Qt: %4\n"
               "Build: %5\n"
               "Media worker: %6\n"
               "Project: %7\n"
               "Tracks / clips / assets: %8 / %9 / %10\n"
               "Cache: %11\n"
               "Preview path: %12")
                .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                     QSysInfo::buildCpuArchitecture(), QString::fromLatin1(qVersion()),
#if defined(NDEBUG)
                     tr("Release"),
#else
                     tr("Debug"),
#endif
                     QFileInfo::exists(workerPath) ? workerPath : tr("not found"),
                     projectPath_.isEmpty() ? tr("unsaved") : projectPath_)
                .arg(editSession_.sequence().tracks().size())
                .arg(clipCount)
                .arg(assets_.size())
                .arg(QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
                     previewPathReason_ == PreviewPathReason::DirectOverlay ||
                             previewPathReason_ == PreviewPathReason::PreviewCache
                         ? previewPathReasonText(previewPathReason_)
                         : tr("compositor (%1)")
                               .arg(previewPathReasonText(previewPathReason_)));
        QMessageBox box(QMessageBox::Information, tr("Videx Diagnostics"),
                        tr("Runtime and project information"), QMessageBox::Ok, this);
        box.setDetailedText(details);
        box.exec();
    });

    auto* tools = addToolBar(tr("Editing Tools"));
    tools->setObjectName(QStringLiteral("editingTools"));
    tools->setMovable(true);
    auto* toolGroup = new QActionGroup(tools);
    toolGroup->setExclusive(true);
    auto addTool = [this, tools, toolGroup](const QString& label, const QString& shortcut,
                                            const TimelineWidget::Tool tool, const bool checked) {
        QAction* action = tools->addAction(label);
        action->setCheckable(true);
        action->setChecked(checked);
        action->setShortcut(QKeySequence(shortcut));
        action->setShortcutContext(Qt::ApplicationShortcut);
        toolGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool] {
            if (timeline_ != nullptr) {
                timeline_->setTool(tool);
                statusBar()->showMessage(tr("Editing tool changed"), 1200);
            }
        });
    };
    addTool(tr("Select"), QStringLiteral("V"), TimelineWidget::Tool::Selection, true);
    addTool(tr("Razor"), QStringLiteral("C"), TimelineWidget::Tool::Razor, false);
    addTool(tr("Slip"), QStringLiteral("Y"), TimelineWidget::Tool::Slip, false);
    addTool(tr("Roll"), QStringLiteral("N"), TimelineWidget::Tool::Rolling, false);
    addTool(tr("Ripple"), QStringLiteral("B"), TimelineWidget::Tool::Ripple, false);
    addTool(tr("Slide"), QStringLiteral("U"), TimelineWidget::Tool::Slide, false);
    addTool(tr("Hand"), QStringLiteral("H"), TimelineWidget::Tool::Hand, false);
    addTool(tr("Zoom"), QStringLiteral("Z"), TimelineWidget::Tool::Zoom, false);

    auto* textToolAction = tools->addAction(tr("Title"));
    textToolAction->setShortcut(QKeySequence(Qt::Key_T));
    textToolAction->setShortcutContext(Qt::ApplicationShortcut);
    textToolAction->setToolTip(
        tr("Create a title clip on a video track at the playhead (T)"));
    connect(textToolAction, &QAction::triggered, this,
            [this] { createTitleClipAtPlayhead(); });
}

void MainWindow::createPanels() {
    auto* centralContainer = new QWidget;
    auto* centralLayout = new QVBoxLayout(centralContainer);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(4);

    monitorTabs_ = new QTabWidget;
    sourceMonitor_ = new render::QtMonitorWidget(tr("Source Monitor"));
    programMonitor_ = new render::QtMonitorWidget(tr("Program Monitor"));
    monitorTabs_->addTab(programMonitor_, tr("Program"));
    monitorTabs_->addTab(sourceMonitor_, tr("Source"));
    monitorTabs_->setCurrentWidget(programMonitor_);
    // Header button (and only it) requests fullscreen; the shortcut path is
    // Ctrl+` / Ctrl+Shift+F.
    programMonitor_->setFullscreenRequestHandler(
        [this] { toggleMonitorFullscreen(); });
    programMonitor_->setEscapeHandler([this] {
        if (monitorFullscreen_) {
            toggleMonitorFullscreen();
        }
    });
    centralLayout->addWidget(monitorTabs_, 1);

    auto* sourceControls = new QWidget;
    auto* sourceLayout = new QHBoxLayout(sourceControls);
    sourceLayout->setContentsMargins(8, 2, 8, 2);
    sourceRangeLabel_ = new QLabel(tr("Source: no clip"));
    sourceSeekSlider_ = new QSlider(Qt::Horizontal);
    sourceSeekSlider_->setRange(0, 0);
    sourceSeekSlider_->setEnabled(false);
    sourceLayout->addWidget(sourceRangeLabel_);
    sourceLayout->addWidget(sourceSeekSlider_, 1);
    auto addSourceButton = [this, sourceLayout](const QString& text, const QString& tip,
                                                auto handler) {
        auto* button = new QToolButton;
        button->setText(text);
        button->setToolTip(tip);
        connect(button, &QToolButton::clicked, this, std::move(handler));
        sourceLayout->addWidget(button);
    };
    videoPatchButton_ = new QToolButton(sourceControls);
    videoPatchButton_->setText(tr("V1"));
    videoPatchButton_->setToolTip(tr("Patch source video to the targeted video track"));
    videoPatchButton_->setCheckable(true);
    videoPatchButton_->setChecked(videoSourcePatched_);
    sourceLayout->addWidget(videoPatchButton_);
    connect(videoPatchButton_, &QToolButton::toggled, this,
            [this](const bool enabled) { videoSourcePatched_ = enabled; });
    audioPatchButton_ = new QToolButton(sourceControls);
    audioPatchButton_->setText(tr("A1"));
    audioPatchButton_->setToolTip(tr("Patch source audio to the targeted audio track"));
    audioPatchButton_->setCheckable(true);
    audioPatchButton_->setChecked(audioSourcePatched_);
    sourceLayout->addWidget(audioPatchButton_);
    connect(audioPatchButton_, &QToolButton::toggled, this,
            [this](const bool enabled) { audioSourcePatched_ = enabled; });
    addSourceButton(tr("In"), tr("Mark source In (I)"), [this] {
        if (sourceSeekSlider_ != nullptr && currentSourceAsset_) {
            sourceInFrame_ = sourceSeekSlider_->value();
            sourceOutFrame_ = std::max(sourceOutFrame_, sourceInFrame_ + 1);
            sourceRangeLabel_->setText(
                tr("Source In %1 / Out %2").arg(sourceInFrame_).arg(sourceOutFrame_));
        }
    });
    addSourceButton(tr("Out"), tr("Mark source Out (O)"), [this] {
        if (sourceSeekSlider_ != nullptr && currentSourceAsset_) {
            sourceOutFrame_ = std::min(sourceDurationFrames_, std::max<core::Frame>(
                sourceInFrame_ + 1, sourceSeekSlider_->value() + 1));
            sourceRangeLabel_->setText(
                tr("Source In %1 / Out %2").arg(sourceInFrame_).arg(sourceOutFrame_));
        }
    });
    addSourceButton(tr("Insert"), tr("Insert marked source at playhead (, )"),
                    [this] { insertSourceSelection(false); });
    addSourceButton(tr("Overwrite"), tr("Overwrite marked source at playhead (.)"),
                    [this] { insertSourceSelection(true); });
    connect(sourceSeekSlider_, &QSlider::valueChanged, this, [this](const int frame) {
        const auto asset = std::ranges::find_if(assets_, [this](const ProjectAsset& candidate) {
            return candidate.id == currentSourceAsset_;
        });
        if (asset == assets_.end()) {
            return;
        }
        const double fps = editSession_.sequence().frameRate().framesPerSecond();
        requestPreviewFrame(asset->path, static_cast<std::int64_t>(std::llround(
                                           frame * 1'000'000.0 / fps)), true, false);
    });
    centralLayout->addWidget(sourceControls, 0);

    auto* transportBar = new QWidget;
    auto* transportLayout = new QHBoxLayout(transportBar);
    transportLayout->setContentsMargins(8, 4, 8, 6);
    auto addTransportButton = [this, transportLayout](const QString& name, const QString& text,
                                                       const QString& tooltip, auto handler) {
        auto* button = new QToolButton;
        button->setObjectName(name);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setFixedSize(38, 30);
        connect(button, &QToolButton::clicked, this, std::move(handler));
        transportLayout->addWidget(button);
    };
    addTransportButton(QStringLiteral("transportStart"), QStringLiteral("|<"),
                       tr("Go to start (Home)"), [this] {
                           pausePlayback();
                           timeline_->setPlayheadFrame(0);
                       });
    addTransportButton(QStringLiteral("transportPrevious"), QStringLiteral("<"),
                       tr("Previous frame (Left)"), [this] {
                           pausePlayback();
                           timeline_->setPlayheadFrame(timeline_->playheadFrame() - 1);
                       });
    playPauseButton_ = new QToolButton;
    playPauseButton_->setObjectName(QStringLiteral("transportPlayPause"));
    playPauseButton_->setText(tr("Play"));
    playPauseButton_->setToolTip(tr("Play/Pause (Space)"));
    playPauseButton_->setFixedSize(58, 30);
    connect(playPauseButton_, &QToolButton::clicked, this, [this] { togglePlayback(); });
    transportLayout->addWidget(playPauseButton_);
    addTransportButton(QStringLiteral("transportStop"), tr("Stop"), tr("Stop (K)"), [this] {
        pausePlayback();
    });
    addTransportButton(QStringLiteral("transportNext"), QStringLiteral(">"),
                       tr("Next frame (Right)"), [this] {
                           pausePlayback();
                           timeline_->setPlayheadFrame(timeline_->playheadFrame() + 1);
                       });
    addTransportButton(QStringLiteral("transportEnd"), QStringLiteral(">|"),
                       tr("Go to end (End)"), [this] {
                           pausePlayback();
                           timeline_->setPlayheadFrame(
                               std::max<core::Frame>(0, sequenceEndFrame() - 1));
                       });
    addTransportButton(QStringLiteral("transportPlayInOut"), QStringLiteral("[>]"),
                       tr("Play In to Out"), [this] {
                           if (sequenceInFrame_ < 0 ||
                               sequenceOutFrame_ <= sequenceInFrame_) {
                               statusBar()->showMessage(
                                   tr("Mark a sequence In/Out range first (Shift+I / Shift+O)"),
                                   4000);
                               return;
                           }
                           pausePlayback();
                           timeline_->setPlayheadFrame(sequenceInFrame_);
                           startPlayback(1);
                       });
    auto* loopButton = new QToolButton;
    loopButton->setObjectName(QStringLiteral("transportLoop"));
    loopButton->setText(tr("Loop"));
    loopButton->setCheckable(true);
    loopButton->setToolTip(tr("Loop playback (restarts from In or 0 when the end is reached)"));
    loopButton->setFixedSize(44, 30);
    connect(loopButton, &QToolButton::toggled, this,
            [this](const bool enabled) { loopPlayback_ = enabled; });
    transportLayout->addWidget(loopButton);
    auto* resolutionCombo = new QComboBox;
    resolutionCombo->setObjectName(QStringLiteral("playbackResolution"));
    resolutionCombo->addItem(tr("Auto"), 0);
    resolutionCombo->addItem(tr("Full"), 1);
    resolutionCombo->addItem(QStringLiteral("1/2"), 2);
    resolutionCombo->addItem(QStringLiteral("1/4"), 4);
    resolutionCombo->setToolTip(
        tr("Playback preview resolution. Export always renders at full quality."));
    connect(resolutionCombo, &QComboBox::currentIndexChanged, this,
            [this, resolutionCombo](const int) {
                playbackResolutionDivisor_ = resolutionCombo->currentData().toInt();
            });
    transportLayout->addWidget(resolutionCombo);

    seekSlider_ = new QSlider(Qt::Horizontal);
    seekSlider_->setObjectName(QStringLiteral("transportSeek"));
    seekSlider_->setRange(0, 0);
    seekSlider_->setTracking(true);
    connect(seekSlider_, &QSlider::sliderPressed, this, [this] {
        if (playbackRequested_) {
            togglePlayback();
        }
    });
    connect(seekSlider_, &QSlider::valueChanged, this, [this](const int value) {
        if (updatingSeekSlider_ || timeline_ == nullptr) {
            return;
        }
        timeline_->setPlayheadFrame(static_cast<core::Frame>(value));
    });
    transportLayout->addWidget(seekSlider_, 1);

    timecodeLabel_ = new QLabel(QStringLiteral("00:00:00:00"));
    timecodeLabel_->setObjectName(QStringLiteral("transportTimecode"));
    timecodeLabel_->setMinimumWidth(96);
    timecodeLabel_->setAlignment(Qt::AlignCenter);
    transportLayout->addWidget(timecodeLabel_);
    centralLayout->addWidget(transportBar, 0);
    setCentralWidget(centralContainer);

    auto* projectPanel = new QWidget;
    auto* projectLayout = new QVBoxLayout(projectPanel);
    projectLayout->setContentsMargins(2, 2, 2, 2);
    projectSearch_ = new QLineEdit(projectPanel);
    projectSearch_->setPlaceholderText(tr("Search media..."));
    projectSearch_->setClearButtonEnabled(true);
    projectLayout->addWidget(projectSearch_);
    projectTree_ = new QTreeWidget(projectPanel);
    projectTree_->setHeaderLabels({tr("Name"), tr("Type"), tr("Duration")});
    projectTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    projectLayout->addWidget(projectTree_, 1);
    connect(projectSearch_, &QLineEdit::textChanged, this,
            [this] { rebuildProjectTree(); });
    projectTree_->viewport()->installEventFilter(this);
    connect(projectTree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, const int) {
                const std::uint64_t assetId = item->data(0, Qt::UserRole).toULongLong();
                const auto asset = std::ranges::find_if(
                    assets_, [assetId](const ProjectAsset& candidate) {
                        return candidate.id.value == assetId;
                    });
                if (asset != assets_.end()) {
                    openAssetInSource(asset->id);
                }
            });
    connect(projectTree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                QTreeWidgetItem* item = projectTree_->itemAt(position);
                if (item == nullptr || item->data(0, Qt::UserRole).isNull()) {
                    return;
                }
                const std::uint64_t assetId = item->data(0, Qt::UserRole).toULongLong();
                auto asset = std::ranges::find_if(assets_, [assetId](const ProjectAsset& candidate) {
                    return candidate.id.value == assetId;
                });
                if (asset == assets_.end()) {
                    return;
                }
                QMenu menu(this);
                QAction* rename = menu.addAction(tr("Rename..."));
                QAction* moveToBin = menu.addAction(tr("Move to Bin..."));
                QAction* removeMedia = menu.addAction(tr("Remove from Project"));
                menu.addSeparator();
                QAction* relink = menu.addAction(tr("Relink Media..."));
                QAction* generateProxyAction = menu.addAction(tr("Generate 540p Proxy"));
                const QString existingProxy =
                    asset->metadata.value(QStringLiteral("proxy_cache")).toString();
                QAction* removeProxyAction = nullptr;
                if (QFileInfo::exists(existingProxy)) {
                    removeProxyAction = menu.addAction(tr("Remove Proxy"));
                }
                QAction* chosen = menu.exec(projectTree_->viewport()->mapToGlobal(position));
                if (chosen == rename) {
                    bool accepted = false;
                    const QString current = asset->metadata
                        .value(QStringLiteral("display_name"))
                        .toString(QFileInfo(asset->path).fileName());
                    const QString name = QInputDialog::getText(
                        this, tr("Rename Media"), tr("Name"), QLineEdit::Normal,
                        current, &accepted).trimmed();
                    if (accepted && !name.isEmpty()) {
                        asset->metadata.insert(QStringLiteral("display_name"), name);
                        setDirty(true);
                        rebuildProjectTree();
                    }
                    return;
                }
                if (chosen == moveToBin) {
                    bool accepted = false;
                    const QString current = asset->metadata
                        .value(QStringLiteral("bin")).toString(QStringLiteral("Media"));
                    const QString bin = QInputDialog::getText(
                        this, tr("Move Media"), tr("Bin name"), QLineEdit::Normal,
                        current, &accepted).trimmed();
                    if (accepted) {
                        asset->metadata.insert(QStringLiteral("bin"),
                                               bin.isEmpty() ? QStringLiteral("Media") : bin);
                        setDirty(true);
                        rebuildProjectTree();
                    }
                    return;
                }
                if (chosen == removeMedia) {
                    const bool inUse = std::ranges::any_of(
                        editSession_.sequence().tracks(), [assetId](const core::Track& track) {
                            return std::ranges::any_of(track.clips, [assetId](const core::Clip& clip) {
                                return clip.assetId.value == assetId;
                            });
                        });
                    if (inUse) {
                        statusBar()->showMessage(
                            tr("Media is used in the timeline and cannot be removed"), 4500);
                        return;
                    }
                    if (QMessageBox::question(this, tr("Remove Media"),
                            tr("Remove this media from the project? The source file is kept.")) !=
                        QMessageBox::Yes) return;
                    if (currentSourceAsset_.value == assetId) {
                        currentSourceAsset_ = {};
                        sourceMonitor_->clearFrame();
                        sourceSeekSlider_->setEnabled(false);
                        sourceRangeLabel_->setText(tr("Source: no clip"));
                    }
                    assets_.erase(asset);
                    editSession_.clearHistory();
                    setDirty(true);
                    rebuildProjectTree();
                    refreshEditor();
                    statusBar()->showMessage(
                        tr("Media removed; edit history was cleared"), 4500);
                    return;
                }
                if (chosen == generateProxyAction) {
                    generateProxy(asset->id);
                    return;
                }
                if (chosen == removeProxyAction) {
                    QFile::remove(existingProxy);
                    asset->metadata.remove(QStringLiteral("proxy_cache"));
                    rebuildProjectTree();
                    statusBar()->showMessage(tr("Proxy removed; using original media"), 4000);
                    return;
                }
                if (chosen != relink) {
                    return;
                }
                const QString replacement = QFileDialog::getOpenFileName(
                    this, tr("Relink %1").arg(QFileInfo(asset->path).fileName()),
                    QFileInfo(asset->path).absolutePath(), tr("Media files (*.*)"));
                if (replacement.isEmpty()) {
                    return;
                }
                asset->path = replacement;
                asset->metadata.remove(QStringLiteral("proxy_cache"));
                asset->metadata.remove(QStringLiteral("waveform_cache"));
                asset->metadata.remove(QStringLiteral("thumbnail_cache"));
                asset->metadata.remove(QStringLiteral("cache_key"));
                const core::AssetId relinkAssetId = asset->id;
                setDirty(true);
                rebuildProjectTree();
                updateProgramFrame(timeline_->playheadFrame());
                statusBar()->showMessage(tr("Relinked %1").arg(QFileInfo(replacement).fileName()),
                                         4000);
                QString relinkWorkerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
                relinkWorkerName += QStringLiteral(".exe");
#endif
                const QString relinkWorkerPath =
                    QCoreApplication::applicationDirPath() + '/' + relinkWorkerName;
                if (QFileInfo::exists(relinkWorkerPath)) {
                    auto* probeWorker = new QProcess(this);
                    connect(probeWorker,
                            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                            [this, probeWorker, relinkAssetId](
                                const int exitCode, const QProcess::ExitStatus exitStatus) {
                                const QByteArray output = probeWorker->readAllStandardOutput();
                                probeWorker->deleteLater();
                                const auto current = std::ranges::find_if(
                                    assets_, [relinkAssetId](const ProjectAsset& candidate) {
                                        return candidate.id == relinkAssetId;
                                    });
                                if (current == assets_.end()) {
                                    return;
                                }
                                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                                    QJsonParseError parseError;
                                    const QJsonDocument document =
                                        QJsonDocument::fromJson(output, &parseError);
                                    if (parseError.error == QJsonParseError::NoError &&
                                        document.isObject()) {
                                        const QJsonObject probed = document.object();
                                        current->metadata.insert(
                                            QStringLiteral("duration_us"),
                                            probed.value(QStringLiteral("duration_us")));
                                        current->metadata.insert(
                                            QStringLiteral("streams"),
                                            probed.value(QStringLiteral("streams")));
                                        setDirty(true);
                                        rebuildProjectTree();
                                    }
                                }
                                startAssetCacheJobs(relinkAssetId);
                            });
                    probeWorker->start(relinkWorkerPath,
                                       {QStringLiteral("probe"), replacement});
                } else {
                    startAssetCacheJobs(relinkAssetId);
                }
            });
    addDockWidget(Qt::LeftDockWidgetArea, createDock(tr("Project"), projectPanel, this));

    auto* inspector = new QWidget;
    auto* inspectorLayout = new QFormLayout(inspector);
    opacitySpin_ = new QDoubleSpinBox;
    opacitySpin_->setRange(0.0, 100.0);
    opacitySpin_->setDecimals(1);
    opacitySpin_->setSuffix(tr(" %"));
    gainSpin_ = new QDoubleSpinBox;
    gainSpin_->setRange(-60.0, 24.0);
    gainSpin_->setDecimals(1);
    gainSpin_->setSuffix(tr(" dB"));
    gainInterpolationCombo_ = new QComboBox(inspector);
    gainInterpolationCombo_->addItem(tr("Linear"),
        static_cast<int>(core::KeyframeInterpolation::Linear));
    gainInterpolationCombo_->addItem(tr("Hold"),
        static_cast<int>(core::KeyframeInterpolation::Hold));
    gainInterpolationCombo_->addItem(tr("Ease"),
        static_cast<int>(core::KeyframeInterpolation::Ease));
    gainInterpolationCombo_->addItem(tr("Ease In"),
        static_cast<int>(core::KeyframeInterpolation::EaseIn));
    gainInterpolationCombo_->addItem(tr("Ease Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseOut));
    gainInterpolationCombo_->addItem(tr("Ease In-Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseInOut));
    rateSpin_ = new QDoubleSpinBox;
    rateSpin_->setRange(0.25, 4.0);
    rateSpin_->setDecimals(2);
    rateSpin_->setSingleStep(0.05);
    rateSpin_->setSuffix(QStringLiteral("x"));
    speedInterpolationCombo_ = new QComboBox(inspector);
    speedInterpolationCombo_->addItem(tr("Linear"),
        static_cast<int>(core::KeyframeInterpolation::Linear));
    speedInterpolationCombo_->addItem(tr("Hold"),
        static_cast<int>(core::KeyframeInterpolation::Hold));
    speedInterpolationCombo_->addItem(tr("Ease"),
        static_cast<int>(core::KeyframeInterpolation::Ease));
    speedInterpolationCombo_->addItem(tr("Ease In"),
        static_cast<int>(core::KeyframeInterpolation::EaseIn));
    speedInterpolationCombo_->addItem(tr("Ease Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseOut));
    speedInterpolationCombo_->addItem(tr("Ease In-Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseInOut));
    fadeInSpin_ = new QDoubleSpinBox;
    fadeInSpin_->setRange(0.0, 100'000.0);
    fadeInSpin_->setDecimals(0);
    fadeInSpin_->setSuffix(tr(" frames"));
    fadeOutSpin_ = new QDoubleSpinBox;
    fadeOutSpin_->setRange(0.0, 100'000.0);
    fadeOutSpin_->setDecimals(0);
    fadeOutSpin_->setSuffix(tr(" frames"));
    auto makeTransformSpin = [inspector](const double minimum, const double maximum,
                                         const double value, const QString& suffix) {
        auto* spin = new QDoubleSpinBox(inspector);
        spin->setRange(minimum, maximum);
        spin->setDecimals(2);
        spin->setValue(value);
        spin->setSuffix(suffix);
        return spin;
    };
    positionXSpin_ = makeTransformSpin(-100'000.0, 100'000.0, 0.0, tr(" px"));
    positionYSpin_ = makeTransformSpin(-100'000.0, 100'000.0, 0.0, tr(" px"));
    scaleXSpin_ = makeTransformSpin(1.0, 10'000.0, 100.0, tr(" %"));
    scaleYSpin_ = makeTransformSpin(1.0, 10'000.0, 100.0, tr(" %"));
    rotationSpin_ = makeTransformSpin(-36'000.0, 36'000.0, 0.0, tr(" deg"));
    anchorXSpin_ = makeTransformSpin(0.0, 100.0, 50.0, tr(" %"));
    anchorYSpin_ = makeTransformSpin(0.0, 100.0, 50.0, tr(" %"));
    motionInterpolationCombo_ = new QComboBox(inspector);
    motionInterpolationCombo_->addItem(tr("Linear"),
        static_cast<int>(core::KeyframeInterpolation::Linear));
    motionInterpolationCombo_->addItem(tr("Hold"),
        static_cast<int>(core::KeyframeInterpolation::Hold));
    motionInterpolationCombo_->addItem(tr("Ease"),
        static_cast<int>(core::KeyframeInterpolation::Ease));
    motionInterpolationCombo_->addItem(tr("Ease In"),
        static_cast<int>(core::KeyframeInterpolation::EaseIn));
    motionInterpolationCombo_->addItem(tr("Ease Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseOut));
    motionInterpolationCombo_->addItem(tr("Ease In-Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseInOut));
    cropLeftSpin_ = makeTransformSpin(0.0, 99.0, 0.0, tr(" %"));
    cropRightSpin_ = makeTransformSpin(0.0, 99.0, 0.0, tr(" %"));
    cropTopSpin_ = makeTransformSpin(0.0, 99.0, 0.0, tr(" %"));
    cropBottomSpin_ = makeTransformSpin(0.0, 99.0, 0.0, tr(" %"));
    maskShapeCombo_ = new QComboBox(inspector);
    maskShapeCombo_->addItem(tr("None"), static_cast<int>(core::MaskShape::None));
    maskShapeCombo_->addItem(tr("Rectangle"), static_cast<int>(core::MaskShape::Rectangle));
    maskShapeCombo_->addItem(tr("Ellipse"), static_cast<int>(core::MaskShape::Ellipse));
    maskCenterXSpin_ = makeTransformSpin(0.0, 100.0, 50.0, tr(" %"));
    maskCenterYSpin_ = makeTransformSpin(0.0, 100.0, 50.0, tr(" %"));
    maskWidthSpin_ = makeTransformSpin(0.1, 200.0, 100.0, tr(" %"));
    maskHeightSpin_ = makeTransformSpin(0.1, 200.0, 100.0, tr(" %"));
    maskFeatherSpin_ = makeTransformSpin(0.0, 50.0, 0.0, tr(" %"));
    maskInvertedCheck_ = new QCheckBox(tr("Invert mask"), inspector);
    inspectorLayout->addRow(tr("Opacity"), opacitySpin_);
    inspectorLayout->addRow(tr("Audio gain"), gainSpin_);
    inspectorLayout->addRow(tr("Gain interpolation"), gainInterpolationCombo_);
    auto* setGainKeyframe = new QPushButton(tr("Add/Update Gain Keyframe"), inspector);
    auto* removeGainKeyframe = new QPushButton(tr("Remove Gain Keyframe Here"), inspector);
    inspectorLayout->addRow(setGainKeyframe);
    inspectorLayout->addRow(removeGainKeyframe);
    auto* gainNavRow = new QWidget(inspector);
    auto* gainNavLayout = new QHBoxLayout(gainNavRow);
    gainNavLayout->setContentsMargins(0, 0, 0, 0);
    auto* previousGainKey = new QPushButton(tr("< Key"), gainNavRow);
    auto* nextGainKey = new QPushButton(tr("Key >"), gainNavRow);
    auto* clearGainKeys = new QPushButton(tr("Clear All"), gainNavRow);
    previousGainKey->setToolTip(tr("Jump to the previous gain keyframe"));
    nextGainKey->setToolTip(tr("Jump to the next gain keyframe"));
    clearGainKeys->setToolTip(tr("Remove every gain keyframe"));
    gainNavLayout->addWidget(previousGainKey);
    gainNavLayout->addWidget(nextGainKey);
    gainNavLayout->addWidget(clearGainKeys);
    inspectorLayout->addRow(tr("Gain keys"), gainNavRow);
    const auto jumpGainKey = [this](const bool forward) {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr || clip->gainKeyframes.empty()) {
            statusBar()->showMessage(tr("The clip has no gain keyframes"), 2500);
            return;
        }
        const core::Frame local = timeline_->playheadFrame() - clip->timeline.start;
        core::Frame target = -1;
        for (const core::GainKeyframe& key : clip->gainKeyframes) {
            if (forward && key.frameOffset > local) {
                target = key.frameOffset;
                break;
            }
            if (!forward && key.frameOffset < local) {
                target = key.frameOffset;
            }
        }
        if (target < 0) {
            statusBar()->showMessage(tr("No keyframe in that direction"), 2000);
            return;
        }
        timeline_->setPlayheadFrame(clip->timeline.start + target);
    };
    connect(previousGainKey, &QPushButton::clicked, this,
            [jumpGainKey] { jumpGainKey(false); });
    connect(nextGainKey, &QPushButton::clicked, this,
            [jumpGainKey] { jumpGainKey(true); });
    connect(clearGainKeys, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || clip->gainKeyframes.empty()) {
            return;
        }
        if (QMessageBox::question(
                this, tr("Clear Gain Automation"),
                tr("Remove all %1 gain keyframes from this clip?")
                    .arg(clip->gainKeyframes.size())) != QMessageBox::Yes) {
            return;
        }
        std::vector<core::EditCommand> commands;
        for (const core::GainKeyframe& key : clip->gainKeyframes) {
            commands.push_back(core::RemoveGainKeyframeCommand{clip->id, key.frameOffset});
        }
        const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
            .baseRevision = editSession_.sequence().revision(),
            .label = "Clear gain automation",
            .commands = std::move(commands),
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateInspector(inspectedClip_);
        }
    });
    inspectorLayout->addRow(tr("Playback rate"), rateSpin_);
    inspectorLayout->addRow(tr("Speed interpolation"), speedInterpolationCombo_);
    auto* setSpeedKeyframe = new QPushButton(tr("Add/Update Speed Keyframe"), inspector);
    auto* removeSpeedKeyframe = new QPushButton(tr("Remove Speed Keyframe Here"), inspector);
    inspectorLayout->addRow(setSpeedKeyframe);
    inspectorLayout->addRow(removeSpeedKeyframe);
    inspectorLayout->addRow(tr("Fade in"), fadeInSpin_);
    inspectorLayout->addRow(tr("Fade out"), fadeOutSpin_);
    inspectorLayout->addRow(tr("Position X"), positionXSpin_);
    inspectorLayout->addRow(tr("Position Y"), positionYSpin_);
    inspectorLayout->addRow(tr("Scale X"), scaleXSpin_);
    inspectorLayout->addRow(tr("Scale Y"), scaleYSpin_);
    inspectorLayout->addRow(tr("Rotation"), rotationSpin_);
    inspectorLayout->addRow(tr("Anchor X"), anchorXSpin_);
    inspectorLayout->addRow(tr("Anchor Y"), anchorYSpin_);
    inspectorLayout->addRow(tr("Motion interpolation"), motionInterpolationCombo_);
    auto* setMotionKeyframe = new QPushButton(tr("Add/Update Motion Keyframe"), inspector);
    auto* removeMotionKeyframe = new QPushButton(tr("Remove Motion Keyframe Here"), inspector);
    inspectorLayout->addRow(setMotionKeyframe);
    inspectorLayout->addRow(removeMotionKeyframe);
    auto* motionNavRow = new QWidget(inspector);
    auto* motionNavLayout = new QHBoxLayout(motionNavRow);
    motionNavLayout->setContentsMargins(0, 0, 0, 0);
    auto* previousMotionKey = new QPushButton(tr("< Key"), motionNavRow);
    auto* nextMotionKey = new QPushButton(tr("Key >"), motionNavRow);
    auto* clearMotionKeys = new QPushButton(tr("Clear All"), motionNavRow);
    previousMotionKey->setToolTip(tr("Jump to the previous motion keyframe"));
    nextMotionKey->setToolTip(tr("Jump to the next motion keyframe"));
    clearMotionKeys->setToolTip(tr("Remove every motion keyframe (like turning the "
                                   "stopwatch off)"));
    motionNavLayout->addWidget(previousMotionKey);
    motionNavLayout->addWidget(nextMotionKey);
    motionNavLayout->addWidget(clearMotionKeys);
    inspectorLayout->addRow(tr("Motion keys"), motionNavRow);
    const auto jumpMotionKey = [this](const bool forward) {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr || clip->motionKeyframes.empty()) {
            statusBar()->showMessage(tr("The clip has no motion keyframes"), 2500);
            return;
        }
        const core::Frame local = timeline_->playheadFrame() - clip->timeline.start;
        core::Frame target = -1;
        for (const core::MotionKeyframe& key : clip->motionKeyframes) {
            if (forward && key.frameOffset > local) {
                target = key.frameOffset;
                break;
            }
            if (!forward && key.frameOffset < local) {
                target = key.frameOffset;
            }
        }
        if (target < 0) {
            statusBar()->showMessage(tr("No keyframe in that direction"), 2000);
            return;
        }
        timeline_->setPlayheadFrame(clip->timeline.start + target);
    };
    connect(previousMotionKey, &QPushButton::clicked, this,
            [jumpMotionKey] { jumpMotionKey(false); });
    connect(nextMotionKey, &QPushButton::clicked, this,
            [jumpMotionKey] { jumpMotionKey(true); });
    connect(clearMotionKeys, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || clip->motionKeyframes.empty()) {
            return;
        }
        if (QMessageBox::question(
                this, tr("Clear Motion Animation"),
                tr("Remove all %1 motion keyframes from this clip?")
                    .arg(clip->motionKeyframes.size())) != QMessageBox::Yes) {
            return;
        }
        std::vector<core::EditCommand> commands;
        for (const core::MotionKeyframe& key : clip->motionKeyframes) {
            commands.push_back(
                core::RemoveMotionKeyframeCommand{clip->id, key.frameOffset});
        }
        const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
            .baseRevision = editSession_.sequence().revision(),
            .label = "Clear motion animation",
            .commands = std::move(commands),
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateInspector(inspectedClip_);
            updateProgramFrame(timeline_->playheadFrame());
        }
    });
    inspectorLayout->addRow(tr("Crop Left"), cropLeftSpin_);
    inspectorLayout->addRow(tr("Crop Right"), cropRightSpin_);
    inspectorLayout->addRow(tr("Crop Top"), cropTopSpin_);
    inspectorLayout->addRow(tr("Crop Bottom"), cropBottomSpin_);
    inspectorLayout->addRow(tr("Mask Shape"), maskShapeCombo_);
    inspectorLayout->addRow(tr("Mask Center X"), maskCenterXSpin_);
    inspectorLayout->addRow(tr("Mask Center Y"), maskCenterYSpin_);
    inspectorLayout->addRow(tr("Mask Width"), maskWidthSpin_);
    inspectorLayout->addRow(tr("Mask Height"), maskHeightSpin_);
    inspectorLayout->addRow(tr("Mask Feather"), maskFeatherSpin_);
    inspectorLayout->addRow(maskInvertedCheck_);
    auto* resetTransform = new QPushButton(tr("Reset Transform"), inspector);
    inspectorLayout->addRow(resetTransform);
    auto* resetCrop = new QPushButton(tr("Reset Crop"), inspector);
    inspectorLayout->addRow(resetCrop);
    auto* resetMask = new QPushButton(tr("Reset Mask"), inspector);
    inspectorLayout->addRow(resetMask);
    for (QDoubleSpinBox* spin : {opacitySpin_, gainSpin_, rateSpin_, fadeInSpin_, fadeOutSpin_,
                                 positionXSpin_, positionYSpin_, scaleXSpin_, scaleYSpin_,
                                 rotationSpin_, anchorXSpin_, anchorYSpin_, cropLeftSpin_,
                                 cropRightSpin_, cropTopSpin_, cropBottomSpin_, maskCenterXSpin_,
                                 maskCenterYSpin_, maskWidthSpin_, maskHeightSpin_,
                                 maskFeatherSpin_}) {
        spin->setEnabled(false);
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                [this] { applyInspectorProperties(); });
    }
    const auto livePreviewTransform = [this] {
        if (programMonitor_ == nullptr) {
            return;
        }
        programMonitor_->setPreviewTransform(render::MonitorTransform{
            positionXSpin_->value(), positionYSpin_->value(),
            scaleXSpin_->value() / 100.0, scaleYSpin_->value() / 100.0,
            rotationSpin_->value(), anchorXSpin_->value() / 100.0,
            anchorYSpin_->value() / 100.0});
    };
    for (QDoubleSpinBox* spin : {positionXSpin_, positionYSpin_, scaleXSpin_, scaleYSpin_,
                                 rotationSpin_, anchorXSpin_, anchorYSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, livePreviewTransform);
    }
    const auto livePreviewCrop = [this] {
        if (programMonitor_ == nullptr) {
            return;
        }
        programMonitor_->setPreviewCrop(render::MonitorCrop{
            cropLeftSpin_->value() / 100.0, cropRightSpin_->value() / 100.0,
            cropTopSpin_->value() / 100.0, cropBottomSpin_->value() / 100.0});
    };
    for (QDoubleSpinBox* spin :
         {cropLeftSpin_, cropRightSpin_, cropTopSpin_, cropBottomSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, livePreviewCrop);
    }
    auto* editCropButton = new QPushButton(tr("Edit Crop in Monitor"), inspector);
    editCropButton->setCheckable(true);
    inspectorLayout->addRow(editCropButton);
    connect(editCropButton, &QPushButton::toggled, this, [this](const bool enabled) {
        if (programMonitor_ != nullptr) {
            programMonitor_->setCropEditMode(enabled);
        }
    });
    auto* scrubController = new ParameterScrubController(this);
    for (QDoubleSpinBox* spin : {opacitySpin_, gainSpin_, rateSpin_, fadeInSpin_,
                                 fadeOutSpin_, positionXSpin_, positionYSpin_, scaleXSpin_,
                                 scaleYSpin_, rotationSpin_, anchorXSpin_, anchorYSpin_,
                                 cropLeftSpin_, cropRightSpin_, cropTopSpin_,
                                 cropBottomSpin_, maskCenterXSpin_, maskCenterYSpin_,
                                 maskWidthSpin_, maskHeightSpin_, maskFeatherSpin_}) {
        if (spin == nullptr) {
            continue;
        }
        if (auto* label = qobject_cast<QLabel*>(inspectorLayout->labelForField(spin))) {
            scrubController->attach(label, spin, [this] { applyInspectorProperties(); });
        }
    }
    connect(resetTransform, &QPushButton::clicked, this, [this] {
        positionXSpin_->setValue(0.0);
        positionYSpin_->setValue(0.0);
        scaleXSpin_->setValue(100.0);
        scaleYSpin_->setValue(100.0);
        rotationSpin_->setValue(0.0);
        anchorXSpin_->setValue(50.0);
        anchorYSpin_->setValue(50.0);
        applyInspectorProperties();
    });
    connect(resetCrop, &QPushButton::clicked, this, [this] {
        cropLeftSpin_->setValue(0.0);
        cropRightSpin_->setValue(0.0);
        cropTopSpin_->setValue(0.0);
        cropBottomSpin_->setValue(0.0);
        applyInspectorProperties();
    });
    connect(maskShapeCombo_, &QComboBox::currentIndexChanged, this,
            [this] { applyInspectorProperties(); });
    connect(maskInvertedCheck_, &QCheckBox::toggled, this,
            [this] { applyInspectorProperties(); });
    connect(resetMask, &QPushButton::clicked, this, [this] {
        maskShapeCombo_->setCurrentIndex(0);
        maskCenterXSpin_->setValue(50.0);
        maskCenterYSpin_->setValue(50.0);
        maskWidthSpin_->setValue(100.0);
        maskHeightSpin_->setValue(100.0);
        maskFeatherSpin_->setValue(0.0);
        maskInvertedCheck_->setChecked(false);
        applyInspectorProperties();
    });
    connect(setSpeedKeyframe, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr) return;
        if (!clip->timeline.contains(timeline_->playheadFrame())) {
            statusBar()->showMessage(tr("Move the playhead inside the clip first"), 3000);
            return;
        }
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Set speed keyframe",
            .command = core::SetSpeedKeyframeCommand{
                clip->id, offset, rateSpin_->value(),
                static_cast<core::KeyframeInterpolation>(
                    speedInterpolationCombo_->currentData().toInt())},
        });
        if (result.succeeded()) { setDirty(true); refreshEditor(); }
    });
    connect(removeSpeedKeyframe, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr) return;
        if (!clip->timeline.contains(timeline_->playheadFrame())) return;
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Remove speed keyframe",
            .command = core::RemoveSpeedKeyframeCommand{clip->id, offset},
        });
        if (result.succeeded()) { setDirty(true); refreshEditor(); }
    });
    connect(setGainKeyframe, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr) return;
        if (!clip->timeline.contains(timeline_->playheadFrame())) {
            statusBar()->showMessage(tr("Move the playhead inside the clip first"), 3000);
            return;
        }
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Set gain keyframe",
            .command = core::SetGainKeyframeCommand{
                clip->id, offset, gainSpin_->value(),
                static_cast<core::KeyframeInterpolation>(
                    gainInterpolationCombo_->currentData().toInt())},
        });
        if (result.succeeded()) { setDirty(true); refreshEditor(); }
    });
    connect(removeGainKeyframe, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr) return;
        if (!clip->timeline.contains(timeline_->playheadFrame())) return;
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Remove gain keyframe",
            .command = core::RemoveGainKeyframeCommand{clip->id, offset},
        });
        if (result.succeeded()) { setDirty(true); refreshEditor(); }
    });
    connect(setMotionKeyframe, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr) return;
        if (!clip->timeline.contains(timeline_->playheadFrame())) {
            statusBar()->showMessage(tr("Move the playhead inside the clip first"), 3000);
            return;
        }
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Set motion keyframe",
            .command = core::SetMotionKeyframeCommand{
                clip->id, offset, opacitySpin_->value() / 100.0,
                positionXSpin_->value(), positionYSpin_->value(),
                scaleXSpin_->value() / 100.0, scaleYSpin_->value() / 100.0,
                rotationSpin_->value(), anchorXSpin_->value() / 100.0,
                anchorYSpin_->value() / 100.0,
                static_cast<core::KeyframeInterpolation>(
                    motionInterpolationCombo_->currentData().toInt())},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateProgramFrame(timeline_->playheadFrame());
        }
    });
    connect(removeMotionKeyframe, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        if (clip == nullptr || timeline_ == nullptr) return;
        if (!clip->timeline.contains(timeline_->playheadFrame())) return;
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Remove motion keyframe",
            .command = core::RemoveMotionKeyframeCommand{clip->id, offset},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateProgramFrame(timeline_->playheadFrame());
        }
    });
    programMonitor_->setTransformEditHandler(
        [this](const render::MonitorTransform& state, const bool committed) {
            if (!inspectedClip_) {
                return;
            }
            const auto setSpin = [](QDoubleSpinBox* spin, const double value) {
                const QSignalBlocker blocker(spin);
                spin->setValue(value);
            };
            setSpin(positionXSpin_, state.positionX);
            setSpin(positionYSpin_, state.positionY);
            setSpin(scaleXSpin_, state.scaleX * 100.0);
            setSpin(scaleYSpin_, state.scaleY * 100.0);
            setSpin(rotationSpin_, state.rotationDegrees);
            setSpin(anchorXSpin_, state.anchorX * 100.0);
            setSpin(anchorYSpin_, state.anchorY * 100.0);
            if (!committed) {
                // Layered targets keep the displayed frame static, so render
                // live frames with the transient transform substituted: the
                // dragged element itself follows the pointer. The busy gate in
                // requestTimelineFrame coalesces these to the worker's pace.
                if (monitorLayeredTarget_ && timeline_ != nullptr) {
                    liveDragClip_ = inspectedClip_;
                    liveDragTransform_ = {state.positionX, state.positionY,
                                          state.scaleX, state.scaleY,
                                          state.rotationDegrees, state.anchorX,
                                          state.anchorY};
                    requestTimelineFrame(timeline_->playheadFrame());
                }
                return;
            }
            liveDragClip_ = {};
            const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
            if (clip == nullptr) {
                return;
            }
            core::EditResult result;
            if (!clip->motionKeyframes.empty() && timeline_ != nullptr) {
                if (!clip->timeline.contains(timeline_->playheadFrame())) {
                    statusBar()->showMessage(
                        tr("Move the playhead inside the clip to edit automation"), 3000);
                    updateInspector(clip->id);
                    updateMonitorEditTarget();
                    return;
                }
                const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
                result = editSession_.apply({
                    .baseRevision = editSession_.sequence().revision(),
                    .label = "Transform clip in monitor",
                    .command = core::SetMotionKeyframeCommand{
                        clip->id, offset, opacitySpin_->value() / 100.0,
                        state.positionX, state.positionY, state.scaleX, state.scaleY,
                        state.rotationDegrees, state.anchorX, state.anchorY,
                        static_cast<core::KeyframeInterpolation>(
                            motionInterpolationCombo_->currentData().toInt())},
                });
            } else {
                result = editSession_.apply({
                    .baseRevision = editSession_.sequence().revision(),
                    .label = "Transform clip in monitor",
                    .command = core::SetClipTransformCommand{
                        clip->id, state.positionX, state.positionY, state.scaleX,
                        state.scaleY, state.rotationDegrees, state.anchorX, state.anchorY},
                });
            }
            if (result.succeeded()) {
                monitorAwaitingRender_ = true;
                setDirty(true);
                refreshEditor();
                updateProgramFrame(timeline_->playheadFrame());
            } else {
                statusBar()->showMessage(
                    tr("Could not apply transform: %1")
                        .arg(QString::fromStdString(result.message)),
                    5000);
                updateInspector(inspectedClip_);
                updateMonitorEditTarget();
            }
        });
    programMonitor_->setCropEditHandler(
        [this](const render::MonitorCrop& crop, const bool committed) {
            if (!inspectedClip_) {
                return;
            }
            const auto setSpin = [](QDoubleSpinBox* spin, const double value) {
                const QSignalBlocker blocker(spin);
                spin->setValue(value);
            };
            setSpin(cropLeftSpin_, crop.left * 100.0);
            setSpin(cropRightSpin_, crop.right * 100.0);
            setSpin(cropTopSpin_, crop.top * 100.0);
            setSpin(cropBottomSpin_, crop.bottom * 100.0);
            if (!committed) {
                return;
            }
            const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
            if (clip == nullptr) {
                return;
            }
            const core::EditResult result = editSession_.apply({
                .baseRevision = editSession_.sequence().revision(),
                .label = "Crop clip in monitor",
                .command = core::SetClipCropCommand{clip->id, crop.left, crop.right,
                                                    crop.top, crop.bottom},
            });
            if (result.succeeded()) {
                monitorAwaitingRender_ = true;
                setDirty(true);
                refreshEditor();
                updateProgramFrame(timeline_->playheadFrame());
            } else {
                statusBar()->showMessage(
                    tr("Could not apply crop: %1")
                        .arg(QString::fromStdString(result.message)),
                    5000);
                updateInspector(inspectedClip_);
                updateMonitorEditTarget();
            }
        });
    programMonitor_->setSelectRequestHandler([this](const bool insideFrame,
                                                    const double normalizedX,
                                                    const double normalizedY) {
        if (timeline_ == nullptr) {
            return;
        }
        if (!insideFrame) {
            // Clicking the dark canvas outside the picture clears the focus.
            timeline_->setSelectedClipIds({});
            return;
        }
        // Hit-test layers front-to-back at the click point so the element
        // under the pointer is selected: a title only when the click lands on
        // its text box, otherwise the video behind it.
        const core::Frame frame = timeline_->playheadFrame();
        const auto& tracks = editSession_.sequence().tracks();
        core::ClipId hit;
        core::ClipId frontmost;
        for (auto track = tracks.rbegin(); track != tracks.rend() && !hit; ++track) {
            if (track->kind != core::TrackKind::Video || !track->enabled) {
                continue;
            }
            for (const core::Clip& clip : track->clips) {
                if (!clip.timeline.contains(frame)) {
                    continue;
                }
                if (!frontmost) {
                    frontmost = clip.id;
                }
                const core::Frame local = std::clamp<core::Frame>(
                    frame - clip.timeline.start, 0, clip.timeline.duration - 1);
                if (monitorPointInClip(motionAt(clip, local), clip,
                                       clipMonitorContentRect(clip), normalizedX,
                                       normalizedY)) {
                    hit = clip.id;
                    break;
                }
            }
        }
        // Leave the current selection alone when nothing sits under the
        // playhead, so a click on the picture never discards focus by accident.
        if (!hit) {
            hit = frontmost;
        }
        if (hit) {
            timeline_->setSelectedClipIds({hit});
        }
    });
    programMonitor_->setTextDragHandler(
        [this](const double positionX, const double positionY, const bool committed) {
            if (!committed) {
                return; // The monitor already previews the overlay position.
            }
            const core::CaptionId captionId = captionAtPlayhead();
            const core::Caption* caption = nullptr;
            for (const core::Caption& candidate : editSession_.sequence().captions()) {
                if (candidate.id == captionId) {
                    caption = &candidate;
                    break;
                }
            }
            if (caption == nullptr) {
                return;
            }
            const core::EditResult result = editSession_.apply({
                .baseRevision = editSession_.sequence().revision(),
                .label = "Move text",
                .command = core::SetCaptionCommand{caption->id, caption->timeline,
                                                   caption->text, positionX, positionY,
                                                   caption->fontSize, caption->textColor,
                                                   caption->backgroundColor, caption->bold,
                                                   caption->italic},
            });
            if (result.succeeded()) {
                setDirty(true);
                refreshEditor();
                updateTextPanel();
                updateCaptionOverlay(timeline_->playheadFrame());
            }
        });
    const auto raiseTextDock = [this] {
        if (textEditField_ == nullptr) {
            return;
        }
        updateTextPanel();
        QWidget* ancestor = textEditField_->parentWidget();
        while (ancestor != nullptr && qobject_cast<QDockWidget*>(ancestor) == nullptr) {
            ancestor = ancestor->parentWidget();
        }
        if (auto* dock = qobject_cast<QDockWidget*>(ancestor)) {
            dock->show();
            dock->raise();
        }
    };
    programMonitor_->setTextClickHandler(raiseTextDock);
    programMonitor_->setTextEditRequestHandler([this, raiseTextDock] {
        raiseTextDock();
        if (textEditField_ != nullptr) {
            textEditField_->setFocus(Qt::OtherFocusReason);
            textEditField_->selectAll();
        }
    });
    programMonitor_->setZoomReferenceSize(1280, 720);
    auto* inspectorScroll = new QScrollArea;
    inspectorScroll->setWidget(inspector);
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    QDockWidget* inspectorDock = createDock(tr("Inspector"), inspectorScroll, this);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    auto* effectsPanel = new QWidget;
    auto* effectsLayout = new QVBoxLayout(effectsPanel);
    effectTypeCombo_ = new QComboBox(effectsPanel);
    for (const core::EffectType type : {core::EffectType::Brightness,
                                        core::EffectType::Contrast,
                                        core::EffectType::Saturation,
                                        core::EffectType::Blur,
                                        core::EffectType::Vignette}) {
        effectTypeCombo_->addItem(effectName(type), static_cast<int>(type));
    }
    auto* addEffectButton = new QPushButton(tr("Add Effect"), effectsPanel);
    effectsList_ = new QListWidget(effectsPanel);
    effectEnabledCheck_ = new QCheckBox(tr("Enabled"), effectsPanel);
    effectAmountSpin_ = new QDoubleSpinBox(effectsPanel);
    effectAmountSpin_->setDecimals(3);
    effectAmountSpin_->setRange(-1.0, 3.0);
    effectAmountSpin_->setSingleStep(0.05);
    effectInterpolationCombo_ = new QComboBox(effectsPanel);
    effectInterpolationCombo_->addItem(tr("Linear"),
        static_cast<int>(core::KeyframeInterpolation::Linear));
    effectInterpolationCombo_->addItem(tr("Hold"),
        static_cast<int>(core::KeyframeInterpolation::Hold));
    effectInterpolationCombo_->addItem(tr("Ease"),
        static_cast<int>(core::KeyframeInterpolation::Ease));
    effectInterpolationCombo_->addItem(tr("Ease In"),
        static_cast<int>(core::KeyframeInterpolation::EaseIn));
    effectInterpolationCombo_->addItem(tr("Ease Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseOut));
    effectInterpolationCombo_->addItem(tr("Ease In-Out"),
        static_cast<int>(core::KeyframeInterpolation::EaseInOut));
    auto* removeEffectButton = new QPushButton(tr("Remove Effect"), effectsPanel);
    auto* addKeyframeButton = new QPushButton(tr("Add/Update Keyframe"), effectsPanel);
    auto* removeKeyframeButton = new QPushButton(tr("Remove Keyframe Here"), effectsPanel);
    effectsLayout->addWidget(effectTypeCombo_);
    effectsLayout->addWidget(addEffectButton);
    effectsLayout->addWidget(effectsList_, 1);
    effectsLayout->addWidget(effectEnabledCheck_);
    effectsLayout->addWidget(effectAmountSpin_);
    effectsLayout->addWidget(effectInterpolationCombo_);
    effectsLayout->addWidget(addKeyframeButton);
    effectsLayout->addWidget(removeKeyframeButton);
    effectsLayout->addWidget(removeEffectButton);
    connect(addEffectButton, &QPushButton::clicked, this, [this] {
        if (!inspectedClip_) {
            return;
        }
        const auto type = static_cast<core::EffectType>(effectTypeCombo_->currentData().toInt());
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Add effect",
            .command = core::AddEffectCommand{inspectedClip_, type},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateEffectsPanel();
        }
    });
    connect(removeEffectButton, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = effectsList_->currentItem();
        if (item == nullptr || !inspectedClip_) {
            return;
        }
        const core::EffectId effectId{item->data(Qt::UserRole).toULongLong()};
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Remove effect",
            .command = core::RemoveEffectCommand{inspectedClip_, effectId},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateEffectsPanel();
        }
    });
    connect(effectsList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { updateEffectsPanel(); });
    connect(effectEnabledCheck_, &QCheckBox::toggled, this,
            [this](const bool) { applySelectedEffect(); });
    connect(effectAmountSpin_, &QDoubleSpinBox::editingFinished, this,
            [this] { applySelectedEffect(); });
    connect(addKeyframeButton, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        QListWidgetItem* item = effectsList_->currentItem();
        if (clip == nullptr || item == nullptr || timeline_ == nullptr) {
            return;
        }
        if (!clip->timeline.contains(timeline_->playheadFrame())) {
            statusBar()->showMessage(tr("Move the playhead inside the clip first"), 3000);
            return;
        }
        const core::EffectId effectId{item->data(Qt::UserRole).toULongLong()};
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Set effect keyframe",
            .command = core::SetEffectKeyframeCommand{
                inspectedClip_, effectId, offset, effectAmountSpin_->value(),
                static_cast<core::KeyframeInterpolation>(
                    effectInterpolationCombo_->currentData().toInt())},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateEffectsPanel();
        }
    });
    connect(removeKeyframeButton, &QPushButton::clicked, this, [this] {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        QListWidgetItem* item = effectsList_->currentItem();
        if (clip == nullptr || item == nullptr || timeline_ == nullptr) {
            return;
        }
        if (!clip->timeline.contains(timeline_->playheadFrame())) return;
        const core::EffectId effectId{item->data(Qt::UserRole).toULongLong()};
        const core::Frame offset = timeline_->playheadFrame() - clip->timeline.start;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Remove effect keyframe",
            .command = core::RemoveEffectKeyframeCommand{inspectedClip_, effectId, offset},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateEffectsPanel();
        }
    });
    auto* lanesWidget = new KeyframeLaneWidget(effectsPanel);
    keyframeLanesWidget_ = lanesWidget;
    effectsLayout->addWidget(lanesWidget);
    lanesWidget->setHandlers(
        [this](const core::Frame frame) {
            if (timeline_ != nullptr) {
                timeline_->setPlayheadFrame(frame);
            }
        },
        [this](const KeyframeLaneEntry& lane, const core::Frame from, const core::Frame to) {
            const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
            if (clip == nullptr || from == to) {
                return;
            }
            std::vector<core::EditCommand> commands;
            if (lane.laneType == 0) {
                const auto key = std::ranges::find(clip->motionKeyframes, from,
                                                   &core::MotionKeyframe::frameOffset);
                if (key == clip->motionKeyframes.end()) return;
                if (std::ranges::find(clip->motionKeyframes, to,
                                      &core::MotionKeyframe::frameOffset) !=
                    clip->motionKeyframes.end()) {
                    statusBar()->showMessage(
                        tr("A keyframe already exists at that frame"), 3000);
                    return;
                }
                commands.push_back(core::RemoveMotionKeyframeCommand{clip->id, from});
                commands.push_back(core::SetMotionKeyframeCommand{
                    clip->id, to, key->opacity, key->positionX, key->positionY,
                    key->scaleX, key->scaleY, key->rotationDegrees, key->anchorX,
                    key->anchorY, key->interpolation});
            } else if (lane.laneType == 1) {
                const auto key = std::ranges::find(clip->gainKeyframes, from,
                                                   &core::GainKeyframe::frameOffset);
                if (key == clip->gainKeyframes.end()) return;
                if (std::ranges::find(clip->gainKeyframes, to,
                                      &core::GainKeyframe::frameOffset) !=
                    clip->gainKeyframes.end()) {
                    statusBar()->showMessage(
                        tr("A keyframe already exists at that frame"), 3000);
                    return;
                }
                commands.push_back(core::RemoveGainKeyframeCommand{clip->id, from});
                commands.push_back(core::SetGainKeyframeCommand{clip->id, to, key->gainDb,
                                                                key->interpolation});
            } else if (lane.laneType == 2) {
                const auto key = std::ranges::find(clip->speedKeyframes, from,
                                                   &core::SpeedKeyframe::frameOffset);
                if (key == clip->speedKeyframes.end()) return;
                if (std::ranges::find(clip->speedKeyframes, to,
                                      &core::SpeedKeyframe::frameOffset) !=
                    clip->speedKeyframes.end()) {
                    statusBar()->showMessage(
                        tr("A keyframe already exists at that frame"), 3000);
                    return;
                }
                commands.push_back(core::RemoveSpeedKeyframeCommand{clip->id, from});
                commands.push_back(core::SetSpeedKeyframeCommand{clip->id, to, key->rate,
                                                                 key->interpolation});
            } else {
                const auto effect = std::ranges::find(clip->effects,
                                                      core::EffectId{lane.effectId},
                                                      &core::ClipEffect::id);
                if (effect == clip->effects.end()) return;
                const auto key = std::ranges::find(effect->keyframes, from,
                                                   &core::EffectKeyframe::frameOffset);
                if (key == effect->keyframes.end()) return;
                if (std::ranges::find(effect->keyframes, to,
                                      &core::EffectKeyframe::frameOffset) !=
                    effect->keyframes.end()) {
                    statusBar()->showMessage(
                        tr("A keyframe already exists at that frame"), 3000);
                    return;
                }
                commands.push_back(core::RemoveEffectKeyframeCommand{
                    clip->id, core::EffectId{lane.effectId}, from});
                commands.push_back(core::SetEffectKeyframeCommand{
                    clip->id, core::EffectId{lane.effectId}, to, key->value,
                    key->interpolation});
            }
            const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                .baseRevision = editSession_.sequence().revision(),
                .label = "Move keyframe",
                .commands = std::move(commands),
            });
            if (result.succeeded()) {
                setDirty(true);
                refreshEditor();
                updateInspector(inspectedClip_);
                updateProgramFrame(timeline_->playheadFrame());
            }
        },
        [this](const KeyframeLaneEntry& lane, const core::Frame offset) {
            const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
            if (clip == nullptr) {
                return;
            }
            core::EditCommand command = core::RemoveMotionKeyframeCommand{clip->id, offset};
            if (lane.laneType == 1) {
                command = core::RemoveGainKeyframeCommand{clip->id, offset};
            } else if (lane.laneType == 2) {
                command = core::RemoveSpeedKeyframeCommand{clip->id, offset};
            } else if (lane.laneType == 3) {
                command = core::RemoveEffectKeyframeCommand{
                    clip->id, core::EffectId{lane.effectId}, offset};
            }
            const core::EditResult result = editSession_.apply({
                .baseRevision = editSession_.sequence().revision(),
                .label = "Delete keyframe",
                .command = std::move(command),
            });
            if (result.succeeded()) {
                setDirty(true);
                refreshEditor();
                updateInspector(inspectedClip_);
                updateProgramFrame(timeline_->playheadFrame());
            }
        },
        [this](const KeyframeLaneEntry& lane, const core::Frame offset,
               const int interpolation) {
            const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
            if (clip == nullptr) {
                return;
            }
            const auto interp = static_cast<core::KeyframeInterpolation>(interpolation);
            std::optional<core::EditCommand> command;
            if (lane.laneType == 0) {
                const auto key = std::ranges::find(clip->motionKeyframes, offset,
                                                   &core::MotionKeyframe::frameOffset);
                if (key != clip->motionKeyframes.end()) {
                    command = core::SetMotionKeyframeCommand{
                        clip->id, offset, key->opacity, key->positionX, key->positionY,
                        key->scaleX, key->scaleY, key->rotationDegrees, key->anchorX,
                        key->anchorY, interp};
                }
            } else if (lane.laneType == 1) {
                const auto key = std::ranges::find(clip->gainKeyframes, offset,
                                                   &core::GainKeyframe::frameOffset);
                if (key != clip->gainKeyframes.end()) {
                    command = core::SetGainKeyframeCommand{clip->id, offset, key->gainDb,
                                                           interp};
                }
            } else if (lane.laneType == 2) {
                const auto key = std::ranges::find(clip->speedKeyframes, offset,
                                                   &core::SpeedKeyframe::frameOffset);
                if (key != clip->speedKeyframes.end()) {
                    command = core::SetSpeedKeyframeCommand{clip->id, offset, key->rate,
                                                            interp};
                }
            } else {
                const auto effect = std::ranges::find(clip->effects,
                                                      core::EffectId{lane.effectId},
                                                      &core::ClipEffect::id);
                if (effect != clip->effects.end()) {
                    const auto key = std::ranges::find(effect->keyframes, offset,
                                                       &core::EffectKeyframe::frameOffset);
                    if (key != effect->keyframes.end()) {
                        command = core::SetEffectKeyframeCommand{
                            clip->id, core::EffectId{lane.effectId}, offset, key->value,
                            interp};
                    }
                }
            }
            if (!command.has_value()) {
                return;
            }
            const core::EditResult result = editSession_.apply({
                .baseRevision = editSession_.sequence().revision(),
                .label = "Change keyframe interpolation",
                .command = std::move(*command),
            });
            if (result.succeeded()) {
                setDirty(true);
                refreshEditor();
                updateInspector(inspectedClip_);
                updateProgramFrame(timeline_->playheadFrame());
            }
        });
    auto* reorderRow = new QWidget(effectsPanel);
    auto* reorderLayout = new QHBoxLayout(reorderRow);
    reorderLayout->setContentsMargins(0, 0, 0, 0);
    auto* moveEffectUp = new QPushButton(tr("Move Up"), reorderRow);
    auto* moveEffectDown = new QPushButton(tr("Move Down"), reorderRow);
    moveEffectUp->setToolTip(tr("Render this effect earlier (order = render order)"));
    moveEffectDown->setToolTip(tr("Render this effect later (order = render order)"));
    reorderLayout->addWidget(moveEffectUp);
    reorderLayout->addWidget(moveEffectDown);
    auto* keyNavRow = new QWidget(effectsPanel);
    auto* keyNavLayout = new QHBoxLayout(keyNavRow);
    keyNavLayout->setContentsMargins(0, 0, 0, 0);
    auto* previousKeyButton = new QPushButton(tr("< Prev Key"), keyNavRow);
    auto* nextKeyButton = new QPushButton(tr("Next Key >"), keyNavRow);
    previousKeyButton->setToolTip(tr("Jump the playhead to the previous keyframe"));
    nextKeyButton->setToolTip(tr("Jump the playhead to the next keyframe"));
    keyNavLayout->addWidget(previousKeyButton);
    keyNavLayout->addWidget(nextKeyButton);
    effectsLayout->addWidget(reorderRow);
    effectsLayout->addWidget(keyNavRow);
    const auto moveSelectedEffect = [this](const int delta) {
        QListWidgetItem* item = effectsList_->currentItem();
        if (item == nullptr || !inspectedClip_) {
            return;
        }
        const core::EffectId effectId{item->data(Qt::UserRole).toULongLong()};
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = delta < 0 ? "Move effect up" : "Move effect down",
            .command = core::MoveEffectCommand{inspectedClip_, effectId, delta},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateEffectsPanel();
            for (int index = 0; index < effectsList_->count(); ++index) {
                if (effectsList_->item(index)->data(Qt::UserRole).toULongLong() ==
                    effectId.value) {
                    effectsList_->setCurrentRow(index);
                    break;
                }
            }
        }
    };
    connect(moveEffectUp, &QPushButton::clicked, this,
            [moveSelectedEffect] { moveSelectedEffect(-1); });
    connect(moveEffectDown, &QPushButton::clicked, this,
            [moveSelectedEffect] { moveSelectedEffect(1); });
    const auto jumpToEffectKeyframe = [this](const bool forward) {
        const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
        QListWidgetItem* item = effectsList_->currentItem();
        if (clip == nullptr || item == nullptr || timeline_ == nullptr) {
            return;
        }
        const core::EffectId effectId{item->data(Qt::UserRole).toULongLong()};
        const auto effect =
            std::ranges::find(clip->effects, effectId, &core::ClipEffect::id);
        if (effect == clip->effects.end() || effect->keyframes.empty()) {
            statusBar()->showMessage(tr("The selected effect has no keyframes"), 3000);
            return;
        }
        const core::Frame local = timeline_->playheadFrame() - clip->timeline.start;
        core::Frame target = -1;
        if (forward) {
            for (const core::EffectKeyframe& key : effect->keyframes) {
                if (key.frameOffset > local) {
                    target = key.frameOffset;
                    break;
                }
            }
        } else {
            for (const core::EffectKeyframe& key : effect->keyframes) {
                if (key.frameOffset < local) {
                    target = key.frameOffset;
                }
            }
        }
        if (target < 0) {
            statusBar()->showMessage(tr("No keyframe in that direction"), 2000);
            return;
        }
        timeline_->setPlayheadFrame(clip->timeline.start + target);
    };
    connect(previousKeyButton, &QPushButton::clicked, this,
            [jumpToEffectKeyframe] { jumpToEffectKeyframe(false); });
    connect(nextKeyButton, &QPushButton::clicked, this,
            [jumpToEffectKeyframe] { jumpToEffectKeyframe(true); });
    QDockWidget* effectsDock = createDock(tr("Effect Controls"), effectsPanel, this);
    addDockWidget(Qt::RightDockWidgetArea, effectsDock);

    auto* browserPanel = new QWidget;
    auto* browserLayout = new QVBoxLayout(browserPanel);
    effectsBrowserSearch_ = new QLineEdit(browserPanel);
    effectsBrowserSearch_->setPlaceholderText(tr("Search effects..."));
    effectsBrowserSearch_->setClearButtonEnabled(true);
    effectsBrowserList_ = new QListWidget(browserPanel);
    effectsBrowserList_->setToolTip(
        tr("Double-click to apply to the selected clip, or drag onto a timeline clip"));
    effectsBrowserList_->setIconSize(QSize(96, 54));
    browserLayout->addWidget(effectsBrowserSearch_);
    browserLayout->addWidget(effectsBrowserList_, 1);
    constexpr int previewFrameCount = 18;
    for (const int code :
         {static_cast<int>(core::EffectType::Brightness),
          static_cast<int>(core::EffectType::Contrast),
          static_cast<int>(core::EffectType::Saturation),
          static_cast<int>(core::EffectType::Blur),
          static_cast<int>(core::EffectType::Vignette), browserFadeIn,
          browserFadeOut, browserDissolve, browserCrossfade}) {
        std::vector<QImage> frames;
        frames.reserve(previewFrameCount);
        for (int frame = 0; frame < previewFrameCount; ++frame) {
            frames.push_back(effectPreviewFrame(code, frame, previewFrameCount, 96, 54));
        }
        effectPreviewFrames_[code] = std::move(frames);
    }
    const auto rebuildEffectsBrowser = [this] {
        const QString filter = effectsBrowserSearch_->text().trimmed();
        effectsBrowserList_->clear();
        const struct {
            int code;
            QString name;
            QString description;
        } entries[] = {
            {static_cast<int>(core::EffectType::Brightness),
             effectName(core::EffectType::Brightness),
             tr("Video Effects - lift or lower luminance")},
            {static_cast<int>(core::EffectType::Contrast),
             effectName(core::EffectType::Contrast),
             tr("Video Effects - expand or flatten contrast")},
            {static_cast<int>(core::EffectType::Saturation),
             effectName(core::EffectType::Saturation),
             tr("Video Effects - color intensity")},
            {static_cast<int>(core::EffectType::Blur),
             effectName(core::EffectType::Blur), tr("Video Effects - box blur")},
            {static_cast<int>(core::EffectType::Vignette),
             effectName(core::EffectType::Vignette),
             tr("Video Effects - darken frame edges")},
            {browserFadeIn, tr("Fade In"),
             tr("Transitions - fade the clip in from black (1 second)")},
            {browserFadeOut, tr("Fade Out"),
             tr("Transitions - fade the clip out to black (1 second)")},
            {browserDissolve, tr("Cross Dissolve"),
             tr("Transitions - dissolve from the previous video clip (1 second)")},
            {browserCrossfade, tr("Audio Crossfade"),
             tr("Transitions - crossfade from the previous audio clip (1 second)")},
        };
        for (const auto& entry : entries) {
            if (!filter.isEmpty() && !entry.name.contains(filter, Qt::CaseInsensitive) &&
                !entry.description.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            auto* item = new QListWidgetItem(entry.name, effectsBrowserList_);
            item->setToolTip(entry.description);
            item->setData(Qt::UserRole, entry.code);
            const auto preview = effectPreviewFrames_.find(entry.code);
            if (preview != effectPreviewFrames_.end() && !preview->second.empty()) {
                item->setIcon(QIcon(QPixmap::fromImage(preview->second.front())));
            }
        }
        if (effectsBrowserList_->count() == 0) {
            auto* empty = new QListWidgetItem(tr("No effects match \"%1\"").arg(filter),
                                              effectsBrowserList_);
            empty->setFlags(Qt::NoItemFlags);
        }
    };
    connect(effectsBrowserSearch_, &QLineEdit::textChanged, this, rebuildEffectsBrowser);
    rebuildEffectsBrowser();
    effectPreviewTimer_ = new QTimer(this);
    effectPreviewTimer_->setInterval(90);
    connect(effectPreviewTimer_, &QTimer::timeout, this, [this] {
        if (effectsBrowserList_ == nullptr || !effectsBrowserList_->isVisible()) {
            return;
        }
        ++effectPreviewIndex_;
        for (int row = 0; row < effectsBrowserList_->count(); ++row) {
            QListWidgetItem* item = effectsBrowserList_->item(row);
            if (item == nullptr || item->data(Qt::UserRole).isNull()) {
                continue;
            }
            const auto preview =
                effectPreviewFrames_.find(item->data(Qt::UserRole).toInt());
            if (preview != effectPreviewFrames_.end() && !preview->second.empty()) {
                item->setIcon(QIcon(QPixmap::fromImage(preview->second[
                    static_cast<std::size_t>(effectPreviewIndex_) %
                    preview->second.size()])));
            }
        }
    });
    effectPreviewTimer_->start();
    const auto applyBrowserEffect = [this](const int effectType, const core::ClipId clipId) {
        if (!clipId) {
            statusBar()->showMessage(tr("Select a clip to apply the effect to"), 3000);
            return;
        }
        const core::Clip* clip = editSession_.sequence().findClip(clipId);
        if (clip == nullptr) {
            return;
        }
        const core::Frame second = std::max<core::Frame>(
            1, static_cast<core::Frame>(std::llround(
                   editSession_.sequence().frameRate().framesPerSecond())));
        core::EditCommand command = core::AddEffectCommand{
            clipId, static_cast<core::EffectType>(effectType)};
        std::string label = "Add effect";
        if (effectType == browserFadeIn || effectType == browserFadeOut) {
            const core::Frame fade =
                std::min<core::Frame>(second, clip->timeline.duration);
            command = core::SetClipFadesCommand{
                clipId,
                effectType == browserFadeIn ? fade : clip->fadeInFrames,
                effectType == browserFadeOut ? fade : clip->fadeOutFrames};
            label = effectType == browserFadeIn ? "Add fade in" : "Add fade out";
        } else if (effectType == browserDissolve || effectType == browserCrossfade) {
            const core::Track* track = nullptr;
            for (const core::Track& candidate : editSession_.sequence().tracks()) {
                if (std::ranges::any_of(candidate.clips,
                                        [clipId](const core::Clip& entry) {
                                            return entry.id == clipId;
                                        })) {
                    track = &candidate;
                    break;
                }
            }
            const bool wantsAudio = effectType == browserCrossfade;
            if (track == nullptr ||
                (track->kind == core::TrackKind::Audio) != wantsAudio) {
                statusBar()->showMessage(
                    wantsAudio
                        ? tr("Audio Crossfade applies to audio clips")
                        : tr("Cross Dissolve applies to video clips"),
                    3500);
                return;
            }
            const core::Frame transition =
                std::min<core::Frame>(second, clip->timeline.duration);
            command = core::SetClipTransitionsCommand{
                clipId, wantsAudio ? clip->videoTransitionInFrames : transition,
                wantsAudio ? transition : clip->audioTransitionInFrames};
            label = wantsAudio ? "Add audio crossfade" : "Add cross dissolve";
        }
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = std::move(label),
            .command = std::move(command),
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateInspector(clipId);
            updateEffectsPanel();
            updateProgramFrame(timeline_->playheadFrame());
        } else {
            statusBar()->showMessage(
                tr("Could not add effect: %1")
                    .arg(QString::fromStdString(result.message)),
                4000);
        }
    };
    connect(effectsBrowserList_, &QListWidget::itemDoubleClicked, this,
            [this, applyBrowserEffect](QListWidgetItem* item) {
                if (item == nullptr || item->data(Qt::UserRole).isNull()) {
                    return;
                }
                applyBrowserEffect(item->data(Qt::UserRole).toInt(), inspectedClip_);
            });
    applyBrowserEffect_ = applyBrowserEffect;
    effectsBrowserList_->viewport()->installEventFilter(this);
    QDockWidget* browserDock = createDock(tr("Effects Browser"), browserPanel, this);
    addDockWidget(Qt::RightDockWidgetArea, browserDock);

    historyList_ = new QListWidget;
    historyList_->setToolTip(tr("Double-click a state to move through undo/redo history"));
    connect(historyList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
        int steps = item == nullptr ? 0 : item->data(Qt::UserRole).toInt();
        bool changed = false;
        while (steps < 0 && editSession_.canUndo()) {
            changed = editSession_.undo().succeeded() || changed;
            ++steps;
        }
        while (steps > 0 && editSession_.canRedo()) {
            changed = editSession_.redo().succeeded() || changed;
            --steps;
        }
        if (changed) {
            setDirty(true);
            refreshEditor();
        }
    });
    QDockWidget* historyDock = createDock(tr("History"), historyList_, this);
    addDockWidget(Qt::RightDockWidgetArea, historyDock);

    auto* metersPanel = new QWidget;
    auto* metersLayout = new QFormLayout(metersPanel);
    leftAudioMeter_ = new QProgressBar(metersPanel);
    rightAudioMeter_ = new QProgressBar(metersPanel);
    for (QProgressBar* meter : {leftAudioMeter_, rightAudioMeter_}) {
        meter->setRange(0, 100);
        meter->setTextVisible(false);
        meter->setOrientation(Qt::Vertical);
        meter->setMinimumHeight(130);
    }
    auto* meterRow = new QWidget(metersPanel);
    auto* meterRowLayout = new QHBoxLayout(meterRow);
    meterRowLayout->addWidget(leftAudioMeter_);
    meterRowLayout->addWidget(rightAudioMeter_);
    metersLayout->addRow(tr("L / R"), meterRow);
    QDockWidget* metersDock = createDock(tr("Audio Meters"), metersPanel, this);
    addDockWidget(Qt::RightDockWidgetArea, metersDock);

    auto* textPanel = new QWidget;
    auto* textLayout = new QFormLayout(textPanel);
    textStatusLabel_ = new QLabel(tr("No text at the playhead"), textPanel);
    textStatusLabel_->setWordWrap(true);
    textLayout->addRow(textStatusLabel_);
    auto* newTitleButton = new QPushButton(tr("New Title on Video Track (T)"), textPanel);
    textLayout->addRow(newTitleButton);
    auto* newTextButton = new QPushButton(tr("New Subtitle at Playhead"), textPanel);
    textLayout->addRow(newTextButton);
    connect(newTitleButton, &QPushButton::clicked, this,
            [this] { createTitleClipAtPlayhead(); });
    textEditField_ = new QPlainTextEdit(textPanel);
    textEditField_->setPlaceholderText(tr("Text content"));
    textEditField_->setFixedHeight(70);
    textLayout->addRow(tr("Text"), textEditField_);
    textFontCombo_ = new QFontComboBox(textPanel);
    textLayout->addRow(tr("Font"), textFontCombo_);
    textSizeSpin_ = new QDoubleSpinBox(textPanel);
    textSizeSpin_->setRange(8.0, 300.0);
    textSizeSpin_->setValue(42.0);
    textLayout->addRow(tr("Font Size"), textSizeSpin_);
    textPosXSpin_ = new QDoubleSpinBox(textPanel);
    textPosXSpin_->setRange(0.0, 100.0);
    textPosXSpin_->setSuffix(tr(" %"));
    textPosXSpin_->setValue(50.0);
    textPosYSpin_ = new QDoubleSpinBox(textPanel);
    textPosYSpin_->setRange(0.0, 100.0);
    textPosYSpin_->setSuffix(tr(" %"));
    textPosYSpin_->setValue(85.0);
    textLayout->addRow(tr("Position X"), textPosXSpin_);
    textLayout->addRow(tr("Position Y"), textPosYSpin_);
    textColorButton_ = new QPushButton(tr("Fill Color..."), textPanel);
    textBackgroundButton_ = new QPushButton(tr("Background Color..."), textPanel);
    textLayout->addRow(textColorButton_);
    textLayout->addRow(textBackgroundButton_);
    textBoldCheck_ = new QCheckBox(tr("Bold"), textPanel);
    textItalicCheck_ = new QCheckBox(tr("Italic"), textPanel);
    textLayout->addRow(textBoldCheck_);
    textLayout->addRow(textItalicCheck_);
    auto* deleteTextButton = new QPushButton(tr("Delete Text at Playhead"), textPanel);
    textLayout->addRow(deleteTextButton);
    connect(newTextButton, &QPushButton::clicked, this,
            [this] { createTextClipAtPlayhead(); });
    // Debounce typing so a burst of keystrokes becomes one undoable edit
    // instead of one edit (and one undo entry) per key press.
    auto* textApplyTimer = new QTimer(this);
    textApplyTimer->setSingleShot(true);
    textApplyTimer->setInterval(350);
    connect(textApplyTimer, &QTimer::timeout, this, [this] { applyTextPanel(); });
    connect(textEditField_, &QPlainTextEdit::textChanged, this, [this, textApplyTimer] {
        if (!updatingTextPanel_) {
            textApplyTimer->start();
        }
    });
    connect(textFontCombo_, &QFontComboBox::currentFontChanged, this,
            [this] { applyTextPanel(); });
    for (QDoubleSpinBox* spin : {textSizeSpin_, textPosXSpin_, textPosYSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { applyTextPanel(); });
    }
    connect(textBoldCheck_, &QCheckBox::toggled, this, [this] { applyTextPanel(); });
    connect(textItalicCheck_, &QCheckBox::toggled, this, [this] { applyTextPanel(); });
    connect(textColorButton_, &QPushButton::clicked, this, [this] {
        const QColor initial = QColor::fromRgba(textColorValue_);
        const QColor chosen = QColorDialog::getColor(
            initial, this, tr("Fill Color"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid()) {
            textColorValue_ = chosen.rgba();
            applyTextPanel();
        }
    });
    connect(textBackgroundButton_, &QPushButton::clicked, this, [this] {
        const QColor initial = QColor::fromRgba(textBackgroundValue_);
        const QColor chosen = QColorDialog::getColor(
            initial, this, tr("Background Color"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid()) {
            textBackgroundValue_ = chosen.rgba();
            applyTextPanel();
        }
    });
    connect(deleteTextButton, &QPushButton::clicked, this, [this] {
        const core::CaptionId caption = captionAtPlayhead();
        if (!caption) {
            return;
        }
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Delete text",
            .command = core::RemoveCaptionCommand{caption},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateTextPanel();
            updateCaptionOverlay(timeline_->playheadFrame());
        }
    });
    auto* textScroll = new QScrollArea;
    textScroll->setWidget(textPanel);
    textScroll->setWidgetResizable(true);
    textScroll->setFrameShape(QFrame::NoFrame);
    QDockWidget* textDock = createDock(tr("Text"), textScroll, this);
    addDockWidget(Qt::RightDockWidgetArea, textDock);

    tabifyDockWidget(inspectorDock, effectsDock);
    tabifyDockWidget(effectsDock, browserDock);
    tabifyDockWidget(browserDock, textDock);
    tabifyDockWidget(textDock, historyDock);
    tabifyDockWidget(historyDock, metersDock);
    inspectorDock->raise();

    timeline_ = new TimelineWidget;
    timeline_->setMinimumHeight(280);
    timeline_->setSelectionHandler([this](const std::vector<core::ClipId>& clipIds) {
        // Prefer the most recently selected clip on a video track so linked A/V
        // selections keep the Program Monitor transform overlay active.
        core::ClipId inspect;
        for (auto it = clipIds.rbegin(); it != clipIds.rend() && !inspect; ++it) {
            for (const core::Track& track : editSession_.sequence().tracks()) {
                if (track.kind != core::TrackKind::Video) {
                    continue;
                }
                if (std::ranges::any_of(track.clips, [id = *it](const core::Clip& clip) {
                        return clip.id == id;
                    })) {
                    inspect = *it;
                    break;
                }
            }
        }
        if (!inspect && !clipIds.empty()) {
            inspect = clipIds.back();
        }
        updateInspector(inspect);
    });
    timeline_->setContextActionHandler(
        [this](const TimelineWidget::ContextAction action, const core::ClipId clipId,
               const core::Frame frame) {
            switch (action) {
            case TimelineWidget::ContextAction::Cut:
                copySelectedClips();
                deleteSelectedClips(false);
                break;
            case TimelineWidget::ContextAction::Copy:
                copySelectedClips();
                break;
            case TimelineWidget::ContextAction::Paste:
                pasteClipsAt(frame);
                break;
            case TimelineWidget::ContextAction::Duplicate: {
                core::Frame end = frame;
                for (const core::ClipId id : timeline_->selectedClipIds()) {
                    if (const core::Clip* clip = editSession_.sequence().findClip(id)) {
                        end = std::max(end, clip->timeline.end());
                    }
                }
                copySelectedClips();
                pasteClipsAt(end);
                break;
            }
            case TimelineWidget::ContextAction::Split:
                splitSelectedClips(frame);
                break;
            case TimelineWidget::ContextAction::Lift:
                deleteSelectedClips(false);
                break;
            case TimelineWidget::ContextAction::RippleDelete:
                deleteSelectedClips(true);
                break;
            case TimelineWidget::ContextAction::AddFadeIn:
                setSelectedFades(12, -1);
                break;
            case TimelineWidget::ContextAction::AddFadeOut:
                setSelectedFades(-1, 12);
                break;
            case TimelineWidget::ContextAction::RemoveFades:
                setSelectedFades(0, 0);
                break;
            case TimelineWidget::ContextAction::AddCrossDissolve:
                setSelectedTransitions(12, -1);
                break;
            case TimelineWidget::ContextAction::AddAudioCrossfade:
                setSelectedTransitions(-1, 12);
                break;
            case TimelineWidget::ContextAction::RemoveTransitions:
                setSelectedTransitions(0, 0);
                break;
            case TimelineWidget::ContextAction::AddGainKeyframe:
            case TimelineWidget::ContextAction::RemoveGainKeyframe: {
                const core::Clip* clip = editSession_.sequence().findClip(clipId);
                if (clip == nullptr || !clip->timeline.contains(frame)) break;
                const core::Frame local = frame - clip->timeline.start;
                const bool remove = action == TimelineWidget::ContextAction::RemoveGainKeyframe;
                const core::EditResult result = editSession_.apply({
                    .baseRevision = editSession_.sequence().revision(),
                    .label = remove ? "Remove gain keyframe" : "Set gain keyframe",
                    .command = remove
                        ? core::EditCommand{core::RemoveGainKeyframeCommand{clipId, local}}
                        : core::EditCommand{core::SetGainKeyframeCommand{
                              clipId, local, gainAt(*clip, local),
                              core::KeyframeInterpolation::Linear}},
                });
                if (result.succeeded()) { setDirty(true); refreshEditor(); }
                break;
            }
            case TimelineWidget::ContextAction::AddMotionKeyframe:
            case TimelineWidget::ContextAction::RemoveMotionKeyframe: {
                const core::Clip* clip = editSession_.sequence().findClip(clipId);
                if (clip == nullptr || !clip->timeline.contains(frame)) break;
                const core::Frame local = frame - clip->timeline.start;
                const bool remove = action == TimelineWidget::ContextAction::RemoveMotionKeyframe;
                const core::MotionKeyframe value = motionAt(*clip, local);
                const core::EditResult result = editSession_.apply({
                    .baseRevision = editSession_.sequence().revision(),
                    .label = remove ? "Remove motion keyframe" : "Set motion keyframe",
                    .command = remove
                        ? core::EditCommand{core::RemoveMotionKeyframeCommand{clipId, local}}
                        : core::EditCommand{core::SetMotionKeyframeCommand{
                              clipId, local, value.opacity, value.positionX, value.positionY,
                              value.scaleX, value.scaleY, value.rotationDegrees,
                              value.anchorX, value.anchorY,
                              core::KeyframeInterpolation::Linear}},
                });
                if (result.succeeded()) {
                    setDirty(true); refreshEditor(); updateProgramFrame(frame);
                }
                break;
            }
            case TimelineWidget::ContextAction::LinkSelected: {
                const std::vector<core::ClipId> ids = timeline_->selectedClipIds();
                if (ids.size() < 2U) break;
                const core::LinkId link{nextLinkId_++};
                std::vector<core::EditCommand> commands;
                commands.reserve(ids.size());
                for (const core::ClipId id : ids) {
                    commands.push_back(core::SetClipLinkCommand{id, link});
                }
                const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                    .baseRevision = editSession_.sequence().revision(),
                    .label = "Link selected clips",
                    .commands = std::move(commands),
                });
                if (!result.succeeded()) {
                    --nextLinkId_;
                    statusBar()->showMessage(
                        tr("Could not link clips: %1")
                            .arg(QString::fromStdString(result.message)), 4000);
                    break;
                }
                setDirty(true);
                refreshEditor();
                break;
            }
            case TimelineWidget::ContextAction::Unlink: {
                const core::Clip* clicked = editSession_.sequence().findClip(clipId);
                if (clicked == nullptr || !clicked->linkId) break;
                const core::LinkId link = clicked->linkId;
                std::vector<core::EditCommand> commands;
                for (const core::Track& track : editSession_.sequence().tracks()) {
                    for (const core::Clip& candidate : track.clips) {
                        if (candidate.linkId == link) {
                            commands.push_back(core::SetClipLinkCommand{candidate.id, {}});
                        }
                    }
                }
                const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                    .baseRevision = editSession_.sequence().revision(),
                    .label = "Unlink clips",
                    .commands = std::move(commands),
                });
                if (!result.succeeded()) {
                    statusBar()->showMessage(
                        tr("Could not unlink clips: %1")
                            .arg(QString::fromStdString(result.message)), 4000);
                    break;
                }
                setDirty(true);
                refreshEditor();
                break;
            }
            case TimelineWidget::ContextAction::ResetTransform: {
                std::vector<core::EditCommand> commands;
                for (const core::ClipId id : timeline_->selectedClipIds()) {
                    const core::Clip* selected = editSession_.sequence().findClip(id);
                    if (selected == nullptr) continue;
                    commands.push_back(core::SetClipTransformCommand{.clipId = id});
                    commands.push_back(core::SetClipCropCommand{.clipId = id});
                    commands.push_back(core::SetClipMaskCommand{.clipId = id});
                    for (const core::MotionKeyframe& keyframe : selected->motionKeyframes)
                        commands.push_back(
                            core::RemoveMotionKeyframeCommand{id, keyframe.frameOffset});
                }
                if (commands.empty()) break;
                const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                    .baseRevision = editSession_.sequence().revision(),
                    .label = "Reset transform crop and mask",
                    .commands = std::move(commands),
                });
                if (!result.succeeded()) {
                    statusBar()->showMessage(
                        tr("Could not reset transform: %1")
                            .arg(QString::fromStdString(result.message)), 4000);
                    break;
                }
                setDirty(true);
                refreshEditor();
                updateProgramFrame(timeline_->playheadFrame());
                break;
            }
            case TimelineWidget::ContextAction::ResetEffects: {
                std::vector<core::EditCommand> commands;
                for (const core::ClipId id : timeline_->selectedClipIds()) {
                    if (const core::Clip* selected = editSession_.sequence().findClip(id)) {
                        for (const core::ClipEffect& effect : selected->effects) {
                            commands.push_back(core::RemoveEffectCommand{id, effect.id});
                        }
                    }
                }
                if (commands.empty()) break;
                const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                    .baseRevision = editSession_.sequence().revision(),
                    .label = "Remove all clip effects",
                    .commands = std::move(commands),
                });
                if (!result.succeeded()) {
                    statusBar()->showMessage(
                        tr("Could not remove effects: %1")
                            .arg(QString::fromStdString(result.message)), 4000);
                    break;
                }
                setDirty(true);
                refreshEditor();
                updateProgramFrame(timeline_->playheadFrame());
                break;
            }
            }
        });
    timeline_->setAssetDropHandler([this](const std::uint64_t assetIdValue,
                                          const int trackIndex, const core::Frame frame) {
        const auto asset = std::ranges::find_if(
            assets_, [assetIdValue](const ProjectAsset& candidate) {
                return candidate.id.value == assetIdValue;
            });
        if (asset == assets_.end()) {
            return;
        }
        bool hasVideo = false;
        bool hasAudio = false;
        for (const QJsonValue& stream :
             asset->metadata.value(QStringLiteral("streams")).toArray()) {
            const QString kind = stream.toObject().value(QStringLiteral("kind")).toString();
            hasVideo = hasVideo || kind == QStringLiteral("video");
            hasAudio = hasAudio || kind == QStringLiteral("audio");
        }
        if (!hasVideo && !hasAudio) {
            statusBar()->showMessage(tr("The dropped media has no usable streams"), 4000);
            return;
        }
        const qint64 durationMicroseconds =
            asset->metadata.value(QStringLiteral("duration_us")).toInteger(0);
        const core::Frame durationFrames = std::max<core::Frame>(
            1, static_cast<core::Frame>(std::ceil(
                   std::max(0.0, static_cast<double>(durationMicroseconds) / 1'000'000.0) *
                   editSession_.sequence().frameRate().framesPerSecond())));
        core::TrackId videoTarget = videoTrack_;
        core::TrackId audioTarget = audioTrack_;
        std::vector<core::EditCommand> commands;
        if (trackIndex == -2 && hasVideo) {
            // Drop above the top row: stack onto a brand-new front-most track.
            // The null target below binds to this track inside the same
            // transaction, so create+place is one atomic undo step.
            commands.push_back(core::AddTrackCommand{core::TrackKind::Video});
            videoTarget = {};
        } else if (trackIndex == -3 && hasAudio) {
            commands.push_back(core::AddTrackCommand{core::TrackKind::Audio});
            audioTarget = {};
        }
        const auto& tracks = editSession_.sequence().tracks();
        if (trackIndex >= 0 && static_cast<std::size_t>(trackIndex) < tracks.size()) {
            const core::Track& dropTrack = tracks[static_cast<std::size_t>(trackIndex)];
            if (dropTrack.kind == core::TrackKind::Video) {
                videoTarget = dropTrack.id;
            } else {
                audioTarget = dropTrack.id;
            }
        }
        const core::LinkId dropLink =
            hasVideo && hasAudio ? core::LinkId{nextLinkId_++} : core::LinkId{};
        if (hasVideo) {
            commands.push_back(core::OverwriteClipCommand{
                .targetTrack = videoTarget,
                .assetId = asset->id,
                .timelineStart = frame,
                .sourceStart = 0,
                .duration = durationFrames,
                .linkId = dropLink,
            });
        }
        if (hasAudio) {
            commands.push_back(core::OverwriteClipCommand{
                .targetTrack = audioTarget,
                .assetId = asset->id,
                .timelineStart = frame,
                .sourceStart = 0,
                .duration = durationFrames,
                .linkId = dropLink,
            });
        }
        const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
            .baseRevision = editSession_.sequence().revision(),
            .label = "Drop media on timeline",
            .commands = std::move(commands),
        });
        if (!result.succeeded()) {
            if (dropLink) {
                --nextLinkId_;
            }
            statusBar()->showMessage(
                tr("Could not place media: %1").arg(QString::fromStdString(result.message)),
                5000);
            return;
        }
        setDirty(true);
        refreshEditor();
        timeline_->setSelectedClipIds(result.createdClips);
    });
    timeline_->setEffectDropHandler([this](const int effectType, const core::ClipId clipId) {
        if (applyBrowserEffect_) {
            applyBrowserEffect_(effectType, clipId);
        }
    });
    timeline_->setTrackReorderHandler([this](const core::TrackId trackId,
                                             const int delta) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Reorder track",
            .command = core::MoveTrackCommand{trackId, delta},
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not reorder the track: %1")
                    .arg(QString::fromStdString(result.message)), 4000);
            return;
        }
        setDirty(true);
        refreshEditor();
        updateProgramFrame(timeline_->playheadFrame());
    });
    timeline_->setTrackActionHandler(
        [this](const TimelineWidget::TrackAction action, const core::TrackId trackId) {
            core::EditCommand command = core::MoveTrackCommand{trackId, 0};
            std::string label;
            switch (action) {
            case TimelineWidget::TrackAction::AddVideo:
                command = core::AddTrackCommand{core::TrackKind::Video};
                label = "Add video track";
                break;
            case TimelineWidget::TrackAction::AddAudio:
                command = core::AddTrackCommand{core::TrackKind::Audio};
                label = "Add audio track";
                break;
            case TimelineWidget::TrackAction::Delete: {
                const core::Track* target = editSession_.sequence().findTrack(trackId);
                if (target == nullptr) return;
                const auto sameKind = std::ranges::count(editSession_.sequence().tracks(),
                                                         target->kind, &core::Track::kind);
                if (sameKind <= 1) {
                    statusBar()->showMessage(tr("The last track of a type cannot be deleted"),
                                             3500);
                    return;
                }
                command = core::RemoveTrackCommand{trackId};
                label = "Delete track";
                break;
            }
            case TimelineWidget::TrackAction::MoveUp:
                command = core::MoveTrackCommand{trackId, -1};
                label = "Move track up";
                break;
            case TimelineWidget::TrackAction::MoveDown:
                command = core::MoveTrackCommand{trackId, 1};
                label = "Move track down";
                break;
            case TimelineWidget::TrackAction::ToggleSolo: {
                const core::Track* target = editSession_.sequence().findTrack(trackId);
                if (target == nullptr) {
                    return;
                }
                command = core::SetTrackSoloCommand{trackId, !target->solo};
                label = target->solo ? "Unsolo track" : "Solo track";
                break;
            }
            case TimelineWidget::TrackAction::HeightMinimal:
                command = core::SetTrackHeightModeCommand{trackId, 0};
                label = "Set track height";
                break;
            case TimelineWidget::TrackAction::HeightStandard:
                command = core::SetTrackHeightModeCommand{trackId, 1};
                label = "Set track height";
                break;
            case TimelineWidget::TrackAction::HeightExpanded:
                command = core::SetTrackHeightModeCommand{trackId, 2};
                label = "Set track height";
                break;
            }
            const core::EditResult result = editSession_.apply({
                .baseRevision = editSession_.sequence().revision(),
                .label = std::move(label),
                .command = std::move(command),
            });
            if (!result.succeeded()) {
                statusBar()->showMessage(
                    tr("Could not change track: %1")
                        .arg(QString::fromStdString(result.message)), 4000);
                return;
            }
            if (editSession_.sequence().findTrack(videoTrack_) == nullptr ||
                editSession_.sequence().findTrack(audioTrack_) == nullptr) {
                videoTrack_ = {};
                audioTrack_ = {};
                for (const core::Track& track : editSession_.sequence().tracks()) {
                    if (!videoTrack_ && track.kind == core::TrackKind::Video) videoTrack_ = track.id;
                    if (!audioTrack_ && track.kind == core::TrackKind::Audio) audioTrack_ = track.id;
                }
            }
            setDirty(true);
            refreshEditor();
        });
    timeline_->setFadeHandler([this](const core::ClipId clipId, const core::Frame fadeIn,
                                     const core::Frame fadeOut) {
        if (!timeline_->selectedClipIds().empty() &&
            std::ranges::find(timeline_->selectedClipIds(), clipId) ==
                timeline_->selectedClipIds().end()) {
            timeline_->setSelectedClipIds({clipId});
        }
        setSelectedFades(fadeIn, fadeOut);
    });
    timeline_->setTransitionHandler([this](const core::ClipId clipId,
                                            const core::Frame duration) {
        const core::Clip* clip = editSession_.sequence().findClip(clipId);
        if (clip == nullptr) return;
        core::TrackKind kind = core::TrackKind::Video;
        bool found = false;
        for (const core::Track& track : editSession_.sequence().tracks()) {
            if (std::ranges::any_of(track.clips,
                    [clipId](const core::Clip& candidate) { return candidate.id == clipId; })) {
                kind = track.kind;
                found = true;
                break;
            }
        }
        if (!found) return;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Resize transition",
            .command = core::SetClipTransitionsCommand{
                clipId,
                kind == core::TrackKind::Video ? duration : 0,
                kind == core::TrackKind::Audio ? duration : 0},
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not resize transition: %1")
                    .arg(QString::fromStdString(result.message)), 4000);
            refreshEditor();
            return;
        }
        setDirty(true);
        refreshEditor();
        updateProgramFrame(timeline_->playheadFrame());
    });
    timeline_->setGainHandler([this](const core::ClipId clipId,
                                     const core::Frame timelineFrame,
                                     const double gainDb) {
        const core::Clip* clip = editSession_.sequence().findClip(clipId);
        if (clip == nullptr) return;
        const bool animated = !clip->gainKeyframes.empty();
        const core::Frame offset = animated ? std::clamp<core::Frame>(
            timelineFrame - clip->timeline.start, 0, clip->timeline.duration - 1) : 0;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Adjust clip gain",
            .command = animated
                ? core::EditCommand{core::SetGainKeyframeCommand{
                      clipId, offset, gainDb, core::KeyframeInterpolation::Linear}}
                : core::EditCommand{core::SetClipPropertiesCommand{
                      clipId, clip->opacity, gainDb, clip->playbackRate}},
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not change gain: %1").arg(QString::fromStdString(result.message)),
                4000);
            return;
        }
        setDirty(true);
        refreshEditor();
    });
    timeline_->setSlipHandler([this](const core::ClipId clipId, const core::Frame delta) {
        updateInspector(clipId);
        slipSelected(delta);
    });
    timeline_->setRollHandler([this](const core::ClipId clipId, const core::Frame delta) {
        updateInspector(clipId);
        rollSelected(delta);
    });
    timeline_->setRippleTrimHandler(
        [this](const core::ClipId clipId, const core::Frame delta) {
            const core::Clip* selected = editSession_.sequence().findClip(clipId);
            if (selected == nullptr) {
                return;
            }
            std::vector<core::EditCommand> commands{
                core::RippleTrimEndCommand{clipId, selected->timeline.end() + delta}};
            for (const core::Track& track : editSession_.sequence().tracks()) {
                for (const core::Clip& clip : track.clips) {
                    if (clip.id != clipId && selected->linkId &&
                        clip.linkId == selected->linkId) {
                        commands.push_back(core::TrimClipCommand{
                            clip.id, clip.timeline.start, clip.timeline.end() + delta});
                    }
                }
            }
            const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                .baseRevision = editSession_.sequence().revision(),
                .label = "Ripple trim linked clips",
                .commands = std::move(commands),
            });
            if (!result.succeeded()) {
                statusBar()->showMessage(
                    tr("Could not ripple trim: %1")
                        .arg(QString::fromStdString(result.message)), 5000);
                return;
            }
            setDirty(true);
            refreshEditor();
        });
    timeline_->setSlideHandler([this](const core::ClipId clipId, const core::Frame delta) {
        const core::Clip* selected = editSession_.sequence().findClip(clipId);
        if (selected == nullptr) {
            return;
        }
        std::vector<core::EditCommand> commands;
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                if (clip.id == clipId || (selected->linkId && clip.linkId == selected->linkId)) {
                    commands.push_back(core::SlideClipCommand{clip.id, delta});
                }
            }
        }
        const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
            .baseRevision = editSession_.sequence().revision(),
            .label = "Slide linked clips",
            .commands = std::move(commands),
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not slide clip: %1").arg(QString::fromStdString(result.message)),
                5000);
            return;
        }
        setDirty(true);
        refreshEditor();
    });
    timeline_->setMoveToNewTrackHandler([this](const core::ClipId clipId,
                                               const core::Frame newStart) {
        const core::Clip* selected = editSession_.sequence().findClip(clipId);
        if (selected == nullptr) {
            return;
        }
        const core::Frame delta = newStart - selected->timeline.start;
        // One transaction: the null target binds to the track added first, so
        // create+move is a single undo step and failure leaves no empty track.
        std::vector<core::EditCommand> commands;
        commands.push_back(core::AddTrackCommand{core::TrackKind::Video});
        commands.push_back(core::MoveClipToTrackCommand{clipId, core::TrackId{}, newStart});
        // Linked partners stay on their own tracks and only shift in time.
        if (selected->linkId) {
            for (const core::Track& track : editSession_.sequence().tracks()) {
                for (const core::Clip& clip : track.clips) {
                    if (clip.id != clipId && clip.linkId == selected->linkId) {
                        commands.push_back(core::MoveClipCommand{
                            clip.id, clip.timeline.start + delta});
                    }
                }
            }
        }
        const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
            .baseRevision = editSession_.sequence().revision(),
            .label = "Move clip to new track",
            .commands = std::move(commands),
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not move the clip: %1")
                    .arg(QString::fromStdString(result.message)), 4000);
            return;
        }
        setDirty(true);
        refreshEditor();
        updateProgramFrame(timeline_->playheadFrame());
    });
    timeline_->setDuplicateClipHandler([this](const core::ClipId clipId,
                                              const core::TrackId targetTrack,
                                              const core::Frame newStart) {
        const core::Clip* selected = editSession_.sequence().findClip(clipId);
        if (selected == nullptr) {
            return;
        }
        const core::Frame delta = newStart - selected->timeline.start;
        std::vector<core::ClipPlacement> placements;
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                const bool isDragged = clip.id == clipId;
                const bool isLinked = selected->linkId && clip.id != clipId &&
                                      clip.linkId == selected->linkId;
                if (!isDragged && !isLinked) {
                    continue;
                }
                placements.push_back(core::ClipPlacement{
                    .targetTrack = isDragged && targetTrack ? targetTrack : track.id,
                    .kind = track.kind,
                    .clip = clip,
                    .timelineStart = clip.timeline.start + delta,
                });
            }
        }
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Duplicate clip",
            .command = core::PasteClipsCommand{std::move(placements)},
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not duplicate: %1")
                    .arg(QString::fromStdString(result.message)), 4000);
            return;
        }
        setDirty(true);
        refreshEditor();
        timeline_->setSelectedClipIds(result.createdClips);
        updateProgramFrame(timeline_->playheadFrame());
    });
    timeline_->setMoveClipHandler([this](const core::ClipId clipId,
                                         const core::TrackId targetTrack,
                                         const core::Frame newStart) {
        const core::Clip* selected = editSession_.sequence().findClip(clipId);
        if (selected == nullptr) {
            return;
        }
        const core::Frame delta = newStart - selected->timeline.start;
        std::vector<core::ClipId> affected = timeline_->selectedClipIds();
        if (std::ranges::find(affected, clipId) == affected.end()) {
            affected.push_back(clipId);
        }
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                if (selected->linkId && clip.linkId == selected->linkId &&
                    std::ranges::find(affected, clip.id) == affected.end()) {
                    affected.push_back(clip.id);
                }
            }
        }
        // When the drag lands on another track, shift every affected clip by
        // the same kind-relative amount so linked audio follows the video
        // (V1->V2 also moves A1->A2 when that track exists).
        const auto& tracks = editSession_.sequence().tracks();
        const auto kindPosition = [&tracks](const std::size_t index) {
            int position = 0;
            for (std::size_t i = 0; i < index; ++i) {
                if (tracks[i].kind == tracks[index].kind) {
                    ++position;
                }
            }
            return position;
        };
        const auto trackAtKindPosition =
            [&tracks](const core::TrackKind kind,
                      const int position) -> const core::Track* {
            int seen = 0;
            for (const core::Track& track : tracks) {
                if (track.kind != kind) {
                    continue;
                }
                if (seen == position) {
                    return &track;
                }
                ++seen;
            }
            return nullptr;
        };
        std::size_t ownIndex = tracks.size();
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (std::ranges::any_of(tracks[i].clips, [clipId](const core::Clip& c) {
                    return c.id == clipId;
                })) {
                ownIndex = i;
                break;
            }
        }
        int kindShift = 0;
        if (targetTrack && ownIndex < tracks.size()) {
            const auto targetIterator =
                std::ranges::find(tracks, targetTrack, &core::Track::id);
            if (targetIterator != tracks.end() &&
                targetIterator->kind == tracks[ownIndex].kind) {
                const auto targetIndex = static_cast<std::size_t>(
                    std::distance(tracks.begin(), targetIterator));
                kindShift = kindPosition(targetIndex) - kindPosition(ownIndex);
            }
        }
        std::vector<core::EditCommand> commands;
        for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
            const core::Track& track = tracks[trackIndex];
            for (const core::Clip& clip : track.clips) {
                if (std::ranges::find(affected, clip.id) == affected.end()) {
                    continue;
                }
                core::TrackId destination = track.id;
                if (kindShift != 0) {
                    if (const core::Track* shifted = trackAtKindPosition(
                            track.kind, kindPosition(trackIndex) + kindShift);
                        shifted != nullptr && !shifted->locked) {
                        destination = shifted->id;
                    }
                    // Without a corresponding track the clip stays on its own.
                }
                if (destination != track.id) {
                    commands.push_back(core::MoveClipToTrackCommand{
                        clip.id, destination, clip.timeline.start + delta});
                } else {
                    commands.push_back(
                        core::MoveClipCommand{clip.id, clip.timeline.start + delta});
                }
            }
        }
        const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
            .baseRevision = editSession_.sequence().revision(),
            .label = selected->linkId ? "Move linked clips" : "Move clip",
            .commands = std::move(commands),
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not move clip: %1").arg(QString::fromStdString(result.message)), 5000);
        }
        if (result.succeeded()) {
            setDirty(true);
        }
        refreshEditor();
    });
    timeline_->setSplitClipHandler(
        [this](const core::ClipId clipId, const core::Frame splitPosition) {
            const core::Clip* selected = editSession_.sequence().findClip(clipId);
            if (selected == nullptr) {
                return;
            }
            std::vector<core::EditCommand> commands;
            const core::LinkId rightLink =
                selected->linkId ? core::LinkId{nextLinkId_++} : core::LinkId{};
            for (const core::Track& track : editSession_.sequence().tracks()) {
                for (const core::Clip& clip : track.clips) {
                    if ((clip.id == clipId ||
                         (selected->linkId && clip.linkId == selected->linkId)) &&
                        splitPosition > clip.timeline.start && splitPosition < clip.timeline.end()) {
                        commands.push_back(
                            core::SplitClipCommand{clip.id, splitPosition, rightLink});
                    }
                }
            }
            const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                .baseRevision = editSession_.sequence().revision(),
                .label = selected->linkId ? "Split linked clips" : "Split clip",
                .commands = std::move(commands),
            });
            if (!result.succeeded()) {
                statusBar()->showMessage(
                    tr("Could not split clip: %1").arg(QString::fromStdString(result.message)),
                    5000);
            }
            if (result.succeeded()) {
                setDirty(true);
            }
            refreshEditor();
        });
    timeline_->setLiftClipHandler([this](const core::ClipId clipId) {
        const core::Clip* selected = editSession_.sequence().findClip(clipId);
        if (selected == nullptr) {
            return;
        }
        std::vector<core::EditCommand> commands;
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                if (clip.id == clipId || (selected->linkId && clip.linkId == selected->linkId)) {
                    commands.push_back(core::LiftClipCommand{clip.id});
                }
            }
        }
        const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
            .baseRevision = editSession_.sequence().revision(),
            .label = selected->linkId ? "Delete linked clips" : "Delete clip",
            .commands = std::move(commands),
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not delete clip: %1").arg(QString::fromStdString(result.message)), 5000);
        }
        if (result.succeeded()) {
            setDirty(true);
        }
        refreshEditor();
    });
    timeline_->setRippleDeleteHandler([this](const core::ClipId clipId) {
        const core::Clip* clip = editSession_.sequence().findClip(clipId);
        if (clip == nullptr) {
            return;
        }
        const core::FrameRange range = clip->timeline;
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Ripple delete",
            .command = core::ExtractRangeCommand{range},
        });
        if (!result.succeeded()) {
            statusBar()->showMessage(
                tr("Could not ripple delete: %1").arg(QString::fromStdString(result.message)),
                5000);
        } else {
            setDirty(true);
        }
        refreshEditor();
    });
    timeline_->setTrimClipHandler(
        [this](const core::ClipId clipId, const core::Frame newStart,
               const core::Frame newEnd) {
            const core::Clip* selected = editSession_.sequence().findClip(clipId);
            if (selected == nullptr) {
                return;
            }
            const core::Frame startDelta = newStart - selected->timeline.start;
            const core::Frame endDelta = newEnd - selected->timeline.end();
            std::vector<core::EditCommand> commands;
            for (const core::Track& track : editSession_.sequence().tracks()) {
                for (const core::Clip& clip : track.clips) {
                    if (clip.id == clipId || (selected->linkId && clip.linkId == selected->linkId)) {
                        commands.push_back(core::TrimClipCommand{
                            clip.id, clip.timeline.start + startDelta,
                            clip.timeline.end() + endDelta});
                    }
                }
            }
            const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                .baseRevision = editSession_.sequence().revision(),
                .label = selected->linkId ? "Trim linked clips" : "Trim clip",
                .commands = std::move(commands),
            });
            if (!result.succeeded()) {
                statusBar()->showMessage(
                    tr("Could not trim clip: %1").arg(QString::fromStdString(result.message)),
                    5000);
            }
            if (result.succeeded()) {
                setDirty(true);
            }
            refreshEditor();
        });
    timeline_->setTrimPreviewHandler(
        [this](const core::ClipId clipId, const core::Frame editFrame, const bool twoUp,
               const bool active) {
            if (programMonitor_ == nullptr || timeline_ == nullptr) {
                return;
            }
            if (!active) {
                trimTwoUpActive_ = false;
                updateProgramFrame(timeline_->playheadFrame());
                updateCaptionOverlay(timeline_->playheadFrame());
                return;
            }
            if (playbackRequested_) {
                return;
            }
            const core::Clip* clip = editSession_.sequence().findClip(clipId);
            if (clip == nullptr) {
                return;
            }
            const core::FrameRate rate = editSession_.sequence().frameRate();
            programMonitor_->setOverlayStyle(0.5, 0.06, 20.0, 0xFFFFFFFFU, 0xB0000000U,
                                             true, false);
            if (twoUp) {
                // Roll edit: show the outgoing/incoming pair around the cut.
                const core::Frame outgoing = std::max<core::Frame>(0, editFrame);
                requestTrimTwoUp(outgoing, outgoing + 1);
                programMonitor_->setOverlayText(
                    tr("Roll %1 | %2").arg(timecodeString(rate, outgoing),
                                           timecodeString(rate, outgoing + 1)));
                return;
            }
            trimTwoUpActive_ = false;
            // Show the frame at the moving edit point; requestTimelineFrame
            // coalesces while a worker is busy, so mouse moves do not flood it.
            const core::Frame clamped = std::clamp<core::Frame>(
                editFrame, clip->timeline.start, clip->timeline.end() - 1);
            requestTimelineFrame(clamped);
            programMonitor_->setOverlayText(
                tr("Trim %1").arg(timecodeString(rate,
                                                 std::max<core::Frame>(0, editFrame))));
        });
    timeline_->setCaptionMoveHandler(
        [this](const core::CaptionId captionId, const core::Frame newStart) {
            const core::Caption* caption = nullptr;
            for (const core::Caption& candidate : editSession_.sequence().captions()) {
                if (candidate.id == captionId) {
                    caption = &candidate;
                    break;
                }
            }
            if (caption == nullptr) {
                return;
            }
            const core::EditResult result = editSession_.apply({
                .baseRevision = editSession_.sequence().revision(),
                .label = "Move text",
                .command = core::SetCaptionCommand{
                    caption->id,
                    core::FrameRange{.start = newStart,
                                     .duration = caption->timeline.duration},
                    caption->text, caption->positionX, caption->positionY,
                    caption->fontSize, caption->textColor, caption->backgroundColor,
                    caption->bold, caption->italic},
            });
            if (result.succeeded()) {
                setDirty(true);
                refreshEditor();
                updateTextPanel();
                updateCaptionOverlay(timeline_->playheadFrame());
            } else {
                statusBar()->showMessage(
                    tr("Could not move text: %1")
                        .arg(QString::fromStdString(result.message)), 4000);
            }
        });
    timeline_->setCaptionEditHandler([this, raiseTextDock](const core::CaptionId captionId) {
        for (const core::Caption& caption : editSession_.sequence().captions()) {
            if (caption.id == captionId) {
                timeline_->setPlayheadFrame(caption.timeline.start);
                break;
            }
        }
        raiseTextDock();
        if (textEditField_ != nullptr) {
            textEditField_->setFocus(Qt::OtherFocusReason);
            textEditField_->selectAll();
        }
    });
    timeline_->setCaptionDeleteHandler([this](const core::CaptionId captionId) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Delete text",
            .command = core::RemoveCaptionCommand{captionId},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateTextPanel();
            updateCaptionOverlay(timeline_->playheadFrame());
        }
    });
    timeline_->setCaptionConvertHandler([this](const core::CaptionId captionId) {
        const core::Caption* caption = nullptr;
        for (const core::Caption& candidate : editSession_.sequence().captions()) {
            if (candidate.id == captionId) {
                caption = &candidate;
                break;
            }
        }
        if (caption == nullptr) {
            return;
        }
        // Copy before addTitleClip: its edits invalidate the caption pointer.
        const QString text = QString::fromStdString(caption->text);
        const double positionX = caption->positionX;
        const double positionY = caption->positionY;
        const double fontSize = caption->fontSize;
        const std::uint32_t textColor = caption->textColor;
        const std::uint32_t backgroundColor = caption->backgroundColor;
        const bool bold = caption->bold;
        const bool italic = caption->italic;
        const core::Frame start = caption->timeline.start;
        const core::Frame duration = caption->timeline.duration;
        if (!addTitleClip(text, {}, positionX, positionY, fontSize, textColor,
                          backgroundColor, bold, italic, start, duration)) {
            return;
        }
        const core::EditResult removed = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Convert text to title",
            .command = core::RemoveCaptionCommand{captionId},
        });
        if (removed.succeeded()) {
            setDirty(true);
            refreshEditor();
            updateTextPanel();
            updateCaptionOverlay(timeline_->playheadFrame());
        }
        statusBar()->showMessage(tr("Text converted to a title clip"), 3000);
    });
    const auto applySetMarker = [this](const core::Marker& marker, const char* label) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = label,
            .command = core::SetMarkerCommand{marker.id, marker.position, marker.name,
                                              marker.comment, marker.color},
        });
        if (result.succeeded()) {
            setDirty(true);
            refreshEditor();
        } else {
            statusBar()->showMessage(tr("Could not update marker: %1")
                                         .arg(QString::fromStdString(result.message)),
                                     4000);
        }
    };
    const auto editMarkerDialog = [this, applySetMarker](const core::MarkerId markerId) {
        const core::Marker* found = nullptr;
        for (const core::Marker& candidate : editSession_.sequence().markers()) {
            if (candidate.id == markerId) {
                found = &candidate;
                break;
            }
        }
        if (found == nullptr) {
            return;
        }
        core::Marker marker = *found;
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Edit Marker"));
        auto* layout = new QFormLayout(&dialog);
        auto* nameEdit = new QLineEdit(QString::fromStdString(marker.name), &dialog);
        auto* commentEdit = new QPlainTextEdit(QString::fromStdString(marker.comment),
                                               &dialog);
        commentEdit->setPlaceholderText(tr("Comment"));
        commentEdit->setMinimumHeight(80);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(tr("Name"), nameEdit);
        layout->addRow(tr("Comment"), commentEdit);
        layout->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted ||
            nameEdit->text().trimmed().isEmpty()) {
            return;
        }
        marker.name = nameEdit->text().trimmed().toStdString();
        marker.comment = commentEdit->toPlainText().toStdString();
        applySetMarker(marker, "Edit marker");
    };
    timeline_->setMarkerMoveHandler(
        [this, applySetMarker](const core::MarkerId markerId, const core::Frame frame) {
            for (const core::Marker& candidate : editSession_.sequence().markers()) {
                if (candidate.id != markerId) {
                    continue;
                }
                if (candidate.position != frame) {
                    core::Marker marker = candidate;
                    marker.position = frame;
                    applySetMarker(marker, "Move marker");
                }
                return;
            }
        });
    timeline_->setMarkerEditHandler(editMarkerDialog);
    timeline_->setMarkerContextHandler(
        [this, applySetMarker, editMarkerDialog](const core::MarkerId markerId,
                                                 const QPoint& globalPosition) {
            const core::Marker* found = nullptr;
            for (const core::Marker& candidate : editSession_.sequence().markers()) {
                if (candidate.id == markerId) {
                    found = &candidate;
                    break;
                }
            }
            if (found == nullptr) {
                return;
            }
            // Copy before exec(): the modal loop can refresh the sequence.
            const core::Marker marker = *found;
            QMenu menu(this);
            QAction* edit = menu.addAction(tr("Edit Marker..."));
            QAction* toPlayhead = menu.addAction(tr("Move to Playhead"));
            menu.addSeparator();
            const std::array<std::pair<QString, std::uint32_t>, 5> palette{{
                {tr("Green"), 0xFF2E9E4FU},
                {tr("Red"), 0xFFD34C4CU},
                {tr("Yellow"), 0xFFE8B93AU},
                {tr("Blue"), 0xFF4C86D3U},
                {tr("Purple"), 0xFF9B59D0U},
            }};
            std::array<QAction*, 5> colorActions{};
            for (std::size_t index = 0; index < palette.size(); ++index) {
                colorActions[index] = menu.addAction(palette[index].first);
                colorActions[index]->setCheckable(true);
                colorActions[index]->setChecked(marker.color == palette[index].second);
            }
            menu.addSeparator();
            QAction* remove = menu.addAction(tr("Remove Marker"));
            QAction* chosen = menu.exec(globalPosition);
            if (chosen == nullptr) {
                return;
            }
            if (chosen == edit) {
                editMarkerDialog(markerId);
                return;
            }
            if (chosen == toPlayhead) {
                core::Marker moved = marker;
                moved.position = timeline_->playheadFrame();
                applySetMarker(moved, "Move marker");
                return;
            }
            if (chosen == remove) {
                const core::EditResult result = editSession_.apply({
                    .baseRevision = editSession_.sequence().revision(),
                    .label = "Remove marker",
                    .command = core::RemoveMarkerCommand{markerId},
                });
                if (result.succeeded()) {
                    setDirty(true);
                    refreshEditor();
                }
                return;
            }
            for (std::size_t index = 0; index < palette.size(); ++index) {
                if (chosen == colorActions[index]) {
                    core::Marker recolored = marker;
                    recolored.color = palette[index].second;
                    applySetMarker(recolored, "Set marker color");
                    return;
                }
            }
        });
    timeline_->setPlayheadHandler([this](const core::Frame timelineFrame) {
        // A user seek during audio-clock playback (ruler click, arrow keys,
        // marker jump) must restart audio from the new position, or the stale
        // chunk keeps playing while the picture freezes.
        if (playbackRequested_ && playbackDirection_ > 0 && audioSink_ != nullptr &&
            !mediaPlaybackClip_) {
            const double framesPerSecond =
                editSession_.sequence().frameRate().framesPerSecond();
            const core::Frame audioFrame = playbackStartFrame_ +
                static_cast<core::Frame>(std::floor(
                    audioSink_->processedUSecs() * framesPerSecond / 1'000'000.0));
            if (std::abs(timelineFrame - audioFrame) >
                static_cast<core::Frame>(framesPerSecond * 0.3)) {
                stopPlaybackAudio();
                startPlaybackClock();
                static_cast<void>(requestPlaybackAudio(timelineFrame));
            }
        }
        updateTransportUi(timelineFrame);
        updateCaptionOverlay(timelineFrame);
        if (!playbackRequested_) {
            updateProgramFrame(timelineFrame);
            updateEffectsPanel();
            updateTextPanel();
            if (inspectedClip_) updateInspector(inspectedClip_);
        }
    });
    timeline_->setTransportHandler([this] { togglePlayback(); });
    timeline_->setTrackLockHandler([this](const core::TrackId trackId, const bool locked) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = locked ? "Lock track" : "Unlock track",
            .command = core::SetTrackLockedCommand{trackId, locked},
        });
        if (result.succeeded()) {
            setDirty(true);
        }
        refreshEditor();
    });
    timeline_->setTrackSyncLockHandler([this](const core::TrackId trackId,
                                               const bool syncLocked) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = syncLocked ? "Enable sync lock" : "Disable sync lock",
            .command = core::SetTrackSyncLockedCommand{trackId, syncLocked},
        });
        if (result.succeeded()) setDirty(true);
        refreshEditor();
    });
    timeline_->setTrackMuteHandler([this](const core::TrackId trackId, const bool muted) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = muted ? "Mute track" : "Unmute track",
            .command = core::SetTrackMutedCommand{trackId, muted},
        });
        if (result.succeeded()) {
            setDirty(true);
        }
        refreshEditor();
    });
    timeline_->setTrackEnabledHandler([this](const core::TrackId trackId, const bool enabled) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = enabled ? "Enable video track" : "Disable video track",
            .command = core::SetTrackEnabledCommand{trackId, enabled},
        });
        if (result.succeeded()) {
            setDirty(true);
        }
        refreshEditor();
    });
    timeline_->setTrackTargetHandler([this](const core::TrackId trackId) {
        const core::Track* track = editSession_.sequence().findTrack(trackId);
        if (track == nullptr) return;
        if (track->kind == core::TrackKind::Video) {
            videoTrack_ = trackId;
        } else {
            audioTrack_ = trackId;
        }
        timeline_->setTargetTracks(videoTrack_, audioTrack_);
        updateSourcePatchUi();
        statusBar()->showMessage(
            tr("Targeted track %1")
                .arg(trackPatchLabel(editSession_.sequence(), trackId, track->kind)),
            2200);
    });
    transportTimer_ = new QTimer(this);
    transportTimer_->setTimerType(Qt::PreciseTimer);
    connect(transportTimer_, &QTimer::timeout, this, [this] {
        updateAudioMeters();
        if (timeline_ == nullptr) {
            return;
        }
        core::Frame sequenceEnd = sequenceEndFrame();
        const core::Frame playbackStart =
            sequenceInFrame_ >= 0 && sequenceOutFrame_ > sequenceInFrame_
                ? sequenceInFrame_ : 0;
        if (sequenceOutFrame_ > playbackStart) sequenceEnd = std::min(sequenceEnd, sequenceOutFrame_);
        const bool reachedEnd = playbackDirection_ > 0 &&
                                timeline_->playheadFrame() + 1 >= sequenceEnd;
        const bool reachedStart = playbackDirection_ < 0 &&
                                  timeline_->playheadFrame() <= playbackStart;
        if ((reachedEnd || reachedStart) && loopPlayback_ && sequenceEnd > playbackStart) {
            const core::Frame restartFrame =
                playbackDirection_ > 0 ? playbackStart
                                       : std::max(playbackStart, sequenceEnd - 1);
            transportTimer_->stop();
            stopPlaybackAudio();
            if (mediaPlayer_ != nullptr) {
                mediaPlayer_->stop();
            }
            mediaPlaybackClip_ = {};
            cachePlaybackActive_ = false;
            timeline_->setPlayheadFrame(restartFrame);
            if (playbackDirection_ > 0 && startContinuousPlayback(restartFrame)) {
                startPlaybackClock();
            } else if (playbackDirection_ < 0 || !requestPlaybackAudio(restartFrame)) {
                startPlaybackClock();
            }
            return;
        }
        if (reachedEnd || reachedStart) {
            transportTimer_->stop();
            playbackRequested_ = false;
            stopPlaybackAudio();
            if (mediaPlayer_ != nullptr) {
                mediaPlayer_->stop();
            }
            mediaPlaybackClip_ = {};
            cachePlaybackActive_ = false;
            if (playPauseButton_ != nullptr) {
                playPauseButton_->setText(tr("Play"));
            }
            updateProgramFrame(timeline_->playheadFrame());
            statusBar()->showMessage(tr("Playback stopped"), 2000);
            return;
        }

        core::Frame nextFrame = timeline_->playheadFrame() + playbackDirection_;
        if (playbackDirection_ > 0 && cachePlaybackActive_ && mediaPlayer_ != nullptr) {
            const double cacheFramesPerSecond =
                editSession_.sequence().frameRate().framesPerSecond();
            nextFrame = previewCacheStart_ + static_cast<core::Frame>(std::floor(
                static_cast<double>(mediaPlayer_->position()) * cacheFramesPerSecond /
                1000.0));
            nextFrame = std::max(nextFrame, timeline_->playheadFrame());
            const core::Frame cacheEnd = previewCacheStart_ + previewCacheDuration_;
            if (nextFrame < cacheEnd && previewCacheValid()) {
                nextFrame = std::clamp<core::Frame>(nextFrame, playbackStart,
                                                    sequenceEnd - 1);
                timeline_->setPlayheadFrame(nextFrame);
                return;
            }
            mediaPlayer_->stop();
            cachePlaybackActive_ = false;
            nextFrame = std::min(nextFrame, cacheEnd);
            if (nextFrame < sequenceEnd && startContinuousPlayback(nextFrame)) {
                stopPlaybackAudio();
                timeline_->setPlayheadFrame(nextFrame);
                return;
            }
            if (nextFrame < sequenceEnd) {
                timeline_->setPlayheadFrame(nextFrame);
                // Keep the transport running on the bridge clock while the
                // chunk renders; the sink trims itself into sync on arrival.
                stopPlaybackAudio();
                startPlaybackClock();
                static_cast<void>(requestPlaybackAudio(nextFrame));
                return;
            }
        }
        if (playbackClockTimer_.isValid()) {
            const double clockFramesPerSecond =
                editSession_.sequence().frameRate().framesPerSecond();
            const core::Frame elapsedFrames = static_cast<core::Frame>(std::floor(
                static_cast<double>(playbackClockTimer_.elapsed()) * clockFramesPerSecond /
                1000.0));
            const core::Frame clockFrame =
                playbackClockFrame_ + playbackDirection_ * elapsedFrames;
            nextFrame = playbackDirection_ > 0
                            ? std::max(clockFrame, timeline_->playheadFrame())
                            : std::min(clockFrame, timeline_->playheadFrame());
        }
        if (playbackDirection_ > 0 && mediaPlayer_ != nullptr && mediaPlaybackClip_) {
            const core::Clip* playingClip = editSession_.sequence().findClip(mediaPlaybackClip_);
            if (playingClip != nullptr) {
                const double framesPerSecond = editSession_.sequence().frameRate().framesPerSecond();
                const core::Frame sourceFrame = static_cast<core::Frame>(std::floor(
                    static_cast<double>(mediaPlayer_->position()) * framesPerSecond / 1000.0));
                nextFrame = playingClip->timeline.start + static_cast<core::Frame>(std::floor(
                    static_cast<double>(sourceFrame - playingClip->sourceStart) /
                    playingClip->playbackRate));
                nextFrame = std::max(nextFrame, timeline_->playheadFrame());
                if (nextFrame >= playingClip->timeline.end()) {
                    const core::Frame followingFrame = playingClip->timeline.end();
                    mediaPlayer_->stop();
                    mediaPlaybackClip_ = {};
                    if (followingFrame < sequenceEnd && startContinuousPlayback(followingFrame)) {
                        stopPlaybackAudio();
                        timeline_->setPlayheadFrame(followingFrame);
                        return;
                    }
                    if (followingFrame < sequenceEnd) {
                        timeline_->setPlayheadFrame(followingFrame);
                        stopPlaybackAudio();
                        startPlaybackClock();
                        static_cast<void>(requestPlaybackAudio(followingFrame));
                        return;
                    }
                    nextFrame = followingFrame;
                }
            }
        } else if (playbackDirection_ > 0 && audioSink_ != nullptr &&
            audioSink_->state() == QtAudio::ActiveState) {
            const double framesPerSecond = editSession_.sequence().frameRate().framesPerSecond();
            const double processedSeconds = audioSink_->processedUSecs() / 1'000'000.0;
            nextFrame = playbackStartFrame_ + static_cast<core::Frame>(std::floor(
                                                  processedSeconds * framesPerSecond));
            // Fetch the next chunk while this one still has ~0.7 s left, so
            // the boundary is a seamless sink swap instead of a stall.
            const double remainingSeconds =
                static_cast<double>(audioChunkEnd_ - nextFrame) / framesPerSecond;
            if (audioWorker_ == nullptr && pendingAudioStart_ < 0 &&
                audioChunkEnd_ > 0 && remainingSeconds < 0.7) {
                static_cast<void>(requestPlaybackAudio(audioChunkEnd_, true));
            }
            if (nextFrame <= timeline_->playheadFrame()) {
                return;
            }
        } else if (audioSink_ != nullptr && audioSink_->state() == QtAudio::IdleState) {
            if (pendingAudioStart_ >= 0 && !pendingAudioPcm_.isEmpty()) {
                const QByteArray pcm = pendingAudioPcm_;
                const core::Frame start = pendingAudioStart_;
                const int rate = pendingAudioSampleRate_;
                const int channels = pendingAudioChannels_;
                pendingAudioPcm_.clear();
                pendingAudioStart_ = -1;
                startAudioSinkFromPcm(pcm, rate, channels, start);
                return;
            }
            if (audioWorker_ == nullptr) {
                stopPlaybackAudio();
                startPlaybackClock();
                static_cast<void>(requestPlaybackAudio(timeline_->playheadFrame()));
            }
            return;
        }
        nextFrame = std::clamp<core::Frame>(nextFrame, playbackStart, sequenceEnd - 1);
        timeline_->setPlayheadFrame(nextFrame);
        if (mediaPlaybackClip_) {
            if (const core::Clip* clip = editSession_.sequence().findClip(mediaPlaybackClip_)) {
                const double envelope = clipEnvelope(*clip, nextFrame);
                const core::MotionKeyframe motion = motionAt(
                    *clip, nextFrame - clip->timeline.start);
                if (programMonitor_ != nullptr) {
                    programMonitor_->setFrameOpacity(motion.opacity * envelope);
                    programMonitor_->setFrameTransform(
                        motion.positionX, motion.positionY, motion.scaleX, motion.scaleY,
                        motion.rotationDegrees, motion.anchorX, motion.anchorY);
                    applyClipEffects(programMonitor_, *clip, nextFrame);
                    // Keep title overlays in sync as the playhead crosses
                    // title boundaries; a non-overlayable title bails to the
                    // compositor on the next tick.
                    int videoTrackIndex = -1;
                    int playingTrackIndex = -1;
                    for (const core::Track& track : editSession_.sequence().tracks()) {
                        if (track.kind != core::TrackKind::Video) continue;
                        ++videoTrackIndex;
                        if (std::ranges::any_of(track.clips,
                                                [this](const core::Clip& candidate) {
                                                    return candidate.id ==
                                                           mediaPlaybackClip_;
                                                })) {
                            playingTrackIndex = videoTrackIndex;
                            break;
                        }
                    }
                    std::vector<render::MonitorTitleOverlay> overlays;
                    if (collectTitleOverlays(nextFrame, playingTrackIndex, overlays)) {
                        programMonitor_->setTitleOverlays(std::move(overlays));
                    } else {
                        mediaPlayer_->stop();
                        mediaPlaybackClip_ = {};
                        programMonitor_->setTitleOverlays({});
                    }
                }
                if (mediaAudioOutput_ != nullptr) {
                    const core::Clip* gainClip = clip;
                    for (const core::Track& track : editSession_.sequence().tracks()) {
                        if (!audioTrackAudible(editSession_.sequence(), track)) continue;
                        const auto candidate = std::ranges::find_if(
                            track.clips, [clip, nextFrame](const core::Clip& item) {
                                return item.timeline.contains(nextFrame) &&
                                       (!clip->linkId || item.linkId == clip->linkId);
                            });
                        if (candidate != track.clips.end()) gainClip = &*candidate;
                    }
                    mediaAudioOutput_->setVolume(static_cast<float>(std::clamp(
                        std::pow(10.0, gainAt(*gainClip,
                            nextFrame - gainClip->timeline.start) / 20.0) * envelope,
                        0.0, 1.0)));
                }
            }
        }
        if (playbackDirection_ > 0 && !mediaPlaybackClip_) {
            if (startContinuousPlayback(nextFrame)) {
                stopPlaybackAudio();
            } else {
                updateProgramFrame(nextFrame);
            }
        }
        if (programMonitor_ != nullptr) {
            const int divisor = playbackResolutionDivisor_ == 0
                                    ? 2
                                    : playbackResolutionDivisor_;
            const QString clock = mediaPlaybackClip_
                                      ? tr("media clock")
                                      : (audioSink_ != nullptr ? tr("audio clock")
                                                               : tr("timer clock"));
            // Report the real vertical resolution and, when the compositor is
            // in use, why the direct path was unavailable. Without this the
            // 1/2-resolution fallback looks like an unexplained quality drop.
            const bool directPath =
                previewPathReason_ == PreviewPathReason::DirectOverlay ||
                previewPathReason_ == PreviewPathReason::PreviewCache;
            programMonitor_->setDiagnosticsText(
                tr("f %1 | %2p | %3 | %4")
                    .arg(nextFrame)
                    .arg(mediaPlaybackClip_ ? 720 : 720 / divisor)
                    .arg(clock)
                    .arg(directPath ? previewPathReasonText(previewPathReason_)
                                    : tr("compositor: %1")
                                          .arg(previewPathReasonText(previewPathReason_))));
        }
    });
    QDockWidget* timelineDock = createDock(tr("Timeline"), timeline_, this);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);

    jobsList_ = new QListWidget;
    jobsList_->addItem(tr("No background jobs"));
    QDockWidget* jobsDock = createDock(tr("Jobs"), jobsList_, this);
    addDockWidget(Qt::BottomDockWidgetArea, jobsDock);
    resizeDocks({timelineDock, jobsDock}, {1150, 240}, Qt::Horizontal);
}

void MainWindow::createStatusBar() {
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::createProject() {
    if (!confirmDiscardChanges()) {
        return;
    }

    pausePlayback();
    editSession_ = core::EditSession({24, 1});
    if (transportTimer_ != nullptr) {
        transportTimer_->stop();
    }
    playbackRequested_ = false;
    stopPlaybackAudio();
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
    }
    mediaPlaybackClip_ = {};
    if (playPauseButton_ != nullptr) {
        playPauseButton_->setText(tr("Play"));
    }

    const auto videoTrack = editSession_.apply({
        .baseRevision = editSession_.sequence().revision(),
        .label = "Add video track",
        .command = core::AddTrackCommand{core::TrackKind::Video},
    });
    const auto audioTrack = editSession_.apply({
        .baseRevision = editSession_.sequence().revision(),
        .label = "Add audio track",
        .command = core::AddTrackCommand{core::TrackKind::Audio},
    });
    videoTrack_ = videoTrack.primaryTrack;
    audioTrack_ = audioTrack.primaryTrack;
    nextAssetId_ = 1;
    nextLinkId_ = 1;
    nextInsertFrame_ = 0;
    sequenceInFrame_ = -1;
    sequenceOutFrame_ = -1;
    currentSourceAsset_ = {};
    sourceInFrame_ = 0;
    sourceOutFrame_ = 0;
    sourceDurationFrames_ = 0;
    inspectedClip_ = {};
    clipClipboard_.clear();
    assets_.clear();
    projectPath_.clear();
    dirty_ = false;
    editSession_.clearHistory();
    if (sourceMonitor_ != nullptr) {
        sourceMonitor_->clearFrame();
    }
    if (programMonitor_ != nullptr) {
        programMonitor_->clearFrame();
    }
    if (timeline_ != nullptr) {
        timeline_->clearWaveforms();
        timeline_->setInOutRange(-1, -1);
    }

    rebuildProjectTree();

    refreshEditor();
    if (timeline_ != nullptr) {
        timeline_->setPlayheadFrame(0);
    }
    updateWindowTitle();
    statusBar()->showMessage(tr("Untitled project - 24 fps"), 3000);
}

void MainWindow::importMedia() {
    const QString filePath =
        QFileDialog::getOpenFileName(this, tr("Import Media"), {}, tr("Media files (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }
    importMediaFile(filePath);
}

void MainWindow::importMediaFile(const QString& filePath) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    if (mediaWorker_ != nullptr) {
        if (!pendingImports_.contains(filePath)) {
            pendingImports_.append(filePath);
        }
        return;
    }

    QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
    workerName += QStringLiteral(".exe");
#endif
    const QString workerPath = QCoreApplication::applicationDirPath() + '/' + workerName;
    if (!QFileInfo::exists(workerPath)) {
        statusBar()->showMessage(tr("Media worker was not found: %1").arg(workerPath), 6000);
        return;
    }

    mediaWorker_ = new QProcess(this);
    mediaWorker_->setProcessChannelMode(QProcess::SeparateChannels);
    QProcess* worker = mediaWorker_;
    connect(mediaWorker_, &QProcess::errorOccurred, this,
            [this, worker](const QProcess::ProcessError processError) {
                statusBar()->showMessage(tr("Media worker failed"), 6000);
                if (processError == QProcess::FailedToStart && mediaWorker_ == worker) {
                    worker->deleteLater();
                    mediaWorker_ = nullptr;
                }
            });
    connect(mediaWorker_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, filePath](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const QByteArray output = mediaWorker_->readAllStandardOutput();
                const QString errorOutput = QString::fromUtf8(mediaWorker_->readAllStandardError());
                mediaWorker_->deleteLater();
                mediaWorker_ = nullptr;

                const auto continuePendingImports = [this] {
                    if (!pendingImports_.isEmpty()) {
                        const QString next = pendingImports_.takeFirst();
                        importMediaFile(next);
                    }
                };
                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    statusBar()->showMessage(
                        tr("Could not inspect media: %1").arg(errorOutput.trimmed()), 8000);
                    continuePendingImports();
                    return;
                }

                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                    statusBar()->showMessage(tr("Media worker returned invalid metadata"), 6000);
                    continuePendingImports();
                    return;
                }
                handleMediaProbe(filePath, document.object());
                continuePendingImports();
            });

    statusBar()->showMessage(tr("Inspecting %1...").arg(QFileInfo(filePath).fileName()));
    mediaWorker_->start(workerPath, {QStringLiteral("probe"), filePath});
#else
    Q_UNUSED(filePath);
#endif
}

void MainWindow::handleMediaProbe(const QString& filePath, const QJsonObject& metadata) {
    const QJsonArray streams = metadata.value(QStringLiteral("streams")).toArray();
    bool hasVideo = false;
    bool hasAudio = false;
    for (const QJsonValue& value : streams) {
        const QString kind = value.toObject().value(QStringLiteral("kind")).toString();
        hasVideo = hasVideo || kind == QStringLiteral("video");
        hasAudio = hasAudio || kind == QStringLiteral("audio");
    }

    const qint64 durationMicroseconds = metadata.value(QStringLiteral("duration_us")).toInteger(0);
    const double durationSeconds =
        std::max(0.0, static_cast<double>(durationMicroseconds) / 1'000'000.0);
    const core::Frame durationFrames = std::max<core::Frame>(
        1, static_cast<core::Frame>(
               std::ceil(durationSeconds * editSession_.sequence().frameRate().framesPerSecond())));
    if (durationFrames > std::numeric_limits<core::Frame>::max() - nextInsertFrame_) {
        statusBar()->showMessage(tr("Media duration exceeds the timeline range"), 6000);
        return;
    }
    const core::AssetId assetId{nextAssetId_};
    const core::LinkId importedLink = hasVideo && hasAudio ? core::LinkId{nextLinkId_++}
                                                           : core::LinkId{};

    std::vector<core::EditCommand> commands;
    if (hasVideo) {
        commands.push_back(core::OverwriteClipCommand{
            .targetTrack = videoTrack_,
            .assetId = assetId,
            .timelineStart = nextInsertFrame_,
            .sourceStart = 0,
            .duration = durationFrames,
            .linkId = importedLink,
        });
    }
    if (hasAudio) {
        commands.push_back(core::OverwriteClipCommand{
            .targetTrack = audioTrack_,
            .assetId = assetId,
            .timelineStart = nextInsertFrame_,
            .sourceStart = 0,
            .duration = durationFrames,
            .linkId = importedLink,
        });
    }

    if (!commands.empty()) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Import media",
            .commands = std::move(commands),
        });
        if (!result.succeeded()) {
            if (importedLink) {
                --nextLinkId_;
            }
            statusBar()->showMessage(
                tr("Could not add media to the timeline: %1")
                    .arg(QString::fromStdString(result.message)),
                6000);
            return;
        }
        nextInsertFrame_ += durationFrames;
    }

    ++nextAssetId_;
    assets_.push_back({.id = assetId, .path = filePath, .metadata = metadata});
    setDirty(true);
    rebuildProjectTree();

    refreshEditor();
    statusBar()->showMessage(tr("Imported %1").arg(QFileInfo(filePath).fileName()), 5000);
    openAssetInSource(assetId);
    startAssetCacheJobs(assetId);
}

void MainWindow::openAssetInSource(const core::AssetId assetId) {
    const auto asset = std::ranges::find_if(assets_, [assetId](const ProjectAsset& candidate) {
        return candidate.id == assetId;
    });
    if (asset == assets_.end()) {
        return;
    }
    currentSourceAsset_ = assetId;
    const qint64 durationMicroseconds =
        asset->metadata.value(QStringLiteral("duration_us")).toInteger(0);
    const double fps = editSession_.sequence().frameRate().framesPerSecond();
    sourceDurationFrames_ = std::max<core::Frame>(
        1, static_cast<core::Frame>(std::ceil(durationMicroseconds * fps / 1'000'000.0)));
    sourceInFrame_ = 0;
    sourceOutFrame_ = sourceDurationFrames_;
    if (sourceSeekSlider_ != nullptr) {
        sourceSeekSlider_->setEnabled(true);
        sourceSeekSlider_->setRange(
            0, static_cast<int>(std::min<core::Frame>(sourceDurationFrames_ - 1,
                                                      std::numeric_limits<int>::max())));
        sourceSeekSlider_->setValue(0);
    }
    if (sourceRangeLabel_ != nullptr) {
        sourceRangeLabel_->setText(
            tr("%1 — In %2 / Out %3")
                .arg(QFileInfo(asset->path).fileName())
                .arg(sourceInFrame_)
                .arg(sourceOutFrame_));
    }
    requestPreviewFrame(asset->path, 0, true, false);
}

void MainWindow::insertSourceSelection(const bool overwrite) {
    if (!currentSourceAsset_ || timeline_ == nullptr) {
        statusBar()->showMessage(tr("Open an asset in the Source monitor first"), 3000);
        return;
    }
    const auto asset = std::ranges::find_if(assets_, [this](const ProjectAsset& candidate) {
        return candidate.id == currentSourceAsset_;
    });
    if (asset == assets_.end()) {
        return;
    }
    bool hasVideo = false;
    bool hasAudio = false;
    for (const QJsonValue& value : asset->metadata.value(QStringLiteral("streams")).toArray()) {
        const QString kind = value.toObject().value(QStringLiteral("kind")).toString();
        hasVideo = hasVideo || kind == QStringLiteral("video");
        hasAudio = hasAudio || kind == QStringLiteral("audio");
    }
    const core::Frame duration = sourceOutFrame_ - sourceInFrame_;
    const bool insertVideo = hasVideo && videoSourcePatched_ &&
                             static_cast<bool>(videoTrack_);
    const bool insertAudio = hasAudio && audioSourcePatched_ &&
                             static_cast<bool>(audioTrack_);
    if (duration <= 0 || (!insertVideo && !insertAudio)) {
        statusBar()->showMessage(tr("The marked source range is empty"), 3000);
        return;
    }
    const core::Frame destination = timeline_->playheadFrame();
    const core::LinkId link = insertVideo && insertAudio ? core::LinkId{nextLinkId_++}
                                                         : core::LinkId{};
    std::vector<core::EditCommand> commands;
    if (overwrite) {
        if (insertVideo) {
            commands.push_back(core::OverwriteClipCommand{
                videoTrack_, asset->id, destination, sourceInFrame_, duration, link});
        }
        if (insertAudio) {
            commands.push_back(core::OverwriteClipCommand{
                audioTrack_, asset->id, destination, sourceInFrame_, duration, link});
        }
    } else if (insertVideo) {
        commands.push_back(core::InsertClipCommand{
            videoTrack_, asset->id, destination, sourceInFrame_, duration, link});
        if (insertAudio) {
            commands.push_back(core::OverwriteClipCommand{
                audioTrack_, asset->id, destination, sourceInFrame_, duration, link});
        }
    } else {
        commands.push_back(core::InsertClipCommand{
            audioTrack_, asset->id, destination, sourceInFrame_, duration, link});
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = overwrite ? "Overwrite source" : "Insert source",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        if (link) {
            --nextLinkId_;
        }
        statusBar()->showMessage(
            tr("Could not edit source: %1").arg(QString::fromStdString(result.message)), 5000);
        return;
    }
    setDirty(true);
    refreshEditor();
    timeline_->setPlayheadFrame(destination + duration);
    timeline_->setSelectedClipIds(result.createdClips);
}

void MainWindow::exportReview(const QString& outputPathOverride, const bool previewRender) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    if (exportWorker_ != nullptr) {
        statusBar()->showMessage(tr("An export is already running"), 4000);
        return;
    }
    const core::Frame sequenceDuration = sequenceEndFrame();
    const bool exportMarkedRange = sequenceInFrame_ >= 0 &&
                                   sequenceOutFrame_ > sequenceInFrame_;
    const core::Frame exportStart = exportMarkedRange ? sequenceInFrame_ : 0;
    const core::Frame durationFrames = exportMarkedRange
        ? std::min(sequenceOutFrame_, sequenceDuration) - exportStart : sequenceDuration;
    if (durationFrames <= 0) {
        statusBar()->showMessage(tr("There is no sequence content to export"), 4000);
        return;
    }

    QString outputPath = outputPathOverride;
    if (outputPath.isEmpty() && !previewRender) {
        if (!showExportDialog(outputPath, durationFrames, exportMarkedRange)) {
            return;
        }
    }
    if (outputPath.isEmpty()) {
        outputPath = QFileDialog::getSaveFileName(
            this, tr("Export Review MP4"), {}, tr("MP4 video (*.mp4)"));
        if (outputPath.isEmpty()) {
            return;
        }
    }
    if (!outputPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
        outputPath += QStringLiteral(".mp4");
    }
    exportIsPreviewRender_ = previewRender;
    exportRevisionAtStart_ = editSession_.sequence().revision();

    QByteArray manifest;
    int exportVideoOrder = -1;
    int exportAudioOrder = -1;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        const int trackOrder = track.kind == core::TrackKind::Video
                                   ? ++exportVideoOrder
                                   : ++exportAudioOrder;
        const bool includeTrack =
            track.kind == core::TrackKind::Video
                ? track.enabled
                : audioTrackAudible(editSession_.sequence(), track);
        if (!includeTrack) {
            continue;
        }
        for (const core::Clip& clip : track.clips) {
            if (clip.timeline.end() <= exportStart ||
                clip.timeline.start >= exportStart + durationFrames) continue;
            const auto asset = std::ranges::find_if(assets_, [&clip](const ProjectAsset& candidate) {
                return candidate.id == clip.assetId;
            });
            if (asset != assets_.end() && isTitleAsset(asset->metadata) &&
                !ensureTitleImage(asset->path, asset->metadata)) {
                QMessageBox::warning(this, tr("Cannot Export"),
                                     tr("Could not regenerate a title image."));
                return;
            }
            if (asset == assets_.end() || !QFileInfo::exists(asset->path)) {
                QMessageBox::warning(this, tr("Cannot Export"),
                                     tr("Relink missing media before export."));
                return;
            }
            const QByteArray path = asset->path.toUtf8();
            if (path.contains('\n') || path.contains('\r') || path.contains('\t')) {
                QMessageBox::warning(this, tr("Cannot Export"),
                                     tr("A media path contains unsupported control characters."));
                return;
            }
            manifest += track.kind == core::TrackKind::Video ? 'V' : 'A';
            manifest += '\t';
            manifest += QByteArray::number(clip.timeline.start - exportStart);
            manifest += '\t';
            manifest += QByteArray::number(clip.timeline.duration);
            manifest += '\t';
            manifest += QByteArray::number(clip.sourceStart);
            manifest += '\t';
            manifest += QByteArray::number(clip.opacity, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.audioGainDb, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.playbackRate, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.fadeInFrames);
            manifest += '\t';
            manifest += QByteArray::number(clip.fadeOutFrames);
            manifest += '\t';
            manifest += QByteArray::number(clip.videoTransitionInFrames);
            manifest += '\t';
            manifest += QByteArray::number(clip.audioTransitionInFrames);
            manifest += '\t';
            manifest += QByteArray::number(clip.positionX, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.positionY, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.scaleX, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.scaleY, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.rotationDegrees, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.anchorX, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.anchorY, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.cropLeft, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.cropRight, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.cropTop, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.cropBottom, 'g', 17);
            manifest += '\t';
            if (clip.speedKeyframes.empty()) {
                manifest += '-';
            } else {
                bool firstSpeed = true;
                for (const core::SpeedKeyframe& keyframe : clip.speedKeyframes) {
                    if (!firstSpeed) manifest += ';';
                    firstSpeed = false;
                    manifest += QByteArray::number(keyframe.frameOffset);
                    manifest += ':';
                    manifest += QByteArray::number(keyframe.rate, 'g', 17);
                    manifest += ':';
                    manifest += QByteArray::number(static_cast<int>(keyframe.interpolation));
                }
            }
            manifest += '\t';
            manifest += QByteArray::number(static_cast<int>(clip.maskShape));
            manifest += '\t';
            manifest += QByteArray::number(clip.maskCenterX, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.maskCenterY, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.maskWidth, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.maskHeight, 'g', 17);
            manifest += '\t';
            manifest += QByteArray::number(clip.maskFeather, 'g', 17);
            manifest += '\t';
            manifest += clip.maskInverted ? '1' : '0';
            manifest += '\t';
            QByteArray effectSpec;
            for (const core::ClipEffect& effect : clip.effects) {
                if (!effectSpec.isEmpty()) {
                    effectSpec += '|';
                }
                effectSpec += QByteArray::number(static_cast<int>(effect.type));
                effectSpec += ',';
                effectSpec += effect.enabled ? '1' : '0';
                effectSpec += ',';
                effectSpec += QByteArray::number(effect.amount, 'g', 17);
                effectSpec += ',';
                if (effect.keyframes.empty()) {
                    effectSpec += '-';
                } else {
                    bool firstKeyframe = true;
                    for (const core::EffectKeyframe& keyframe : effect.keyframes) {
                        if (!firstKeyframe) {
                            effectSpec += ';';
                        }
                        firstKeyframe = false;
                        effectSpec += QByteArray::number(keyframe.frameOffset);
                        effectSpec += ':';
                        effectSpec += QByteArray::number(keyframe.value, 'g', 17);
                        effectSpec += ':';
                        effectSpec += QByteArray::number(
                            static_cast<int>(keyframe.interpolation));
                    }
                }
            }
            manifest += effectSpec.isEmpty() ? QByteArray("-") : effectSpec;
            manifest += '\t';
            QByteArray motionSpec;
            for (const core::MotionKeyframe& keyframe : clip.motionKeyframes) {
                if (!motionSpec.isEmpty()) motionSpec += ';';
                motionSpec += QByteArray::number(keyframe.frameOffset) + ',' +
                              QByteArray::number(keyframe.opacity, 'g', 17) + ',' +
                              QByteArray::number(keyframe.positionX, 'g', 17) + ',' +
                              QByteArray::number(keyframe.positionY, 'g', 17) + ',' +
                              QByteArray::number(keyframe.scaleX, 'g', 17) + ',' +
                              QByteArray::number(keyframe.scaleY, 'g', 17) + ',' +
                              QByteArray::number(keyframe.rotationDegrees, 'g', 17) + ',' +
                              QByteArray::number(keyframe.anchorX, 'g', 17) + ',' +
                              QByteArray::number(keyframe.anchorY, 'g', 17) + ',' +
                              QByteArray::number(static_cast<int>(keyframe.interpolation));
            }
            manifest += motionSpec.isEmpty() ? QByteArray("-") : motionSpec;
            manifest += '\t';
            QByteArray gainSpec;
            for (const core::GainKeyframe& keyframe : clip.gainKeyframes) {
                if (!gainSpec.isEmpty()) gainSpec += ';';
                gainSpec += QByteArray::number(keyframe.frameOffset) + ',' +
                            QByteArray::number(keyframe.gainDb, 'g', 17) + ',' +
                            QByteArray::number(static_cast<int>(keyframe.interpolation));
            }
            manifest += gainSpec.isEmpty() ? QByteArray("-") : gainSpec;
            manifest += '\t';
            manifest += QByteArray::number(trackOrder);
            manifest += '\t';
            manifest += path;
            manifest += '\n';
        }
    }

    for (const QString& previous : std::as_const(exportOverlayPaths_)) QFile::remove(previous);
    exportOverlayPaths_.clear();
    const QString overlayDirectory =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/export-overlays");
    QDir().mkpath(overlayDirectory);
    const auto colorFromRgba = [](const std::uint32_t rgba) {
        return QColor(static_cast<int>((rgba >> 24U) & 0xFFU),
                      static_cast<int>((rgba >> 16U) & 0xFFU),
                      static_cast<int>((rgba >> 8U) & 0xFFU),
                      static_cast<int>(rgba & 0xFFU));
    };
    for (const core::Caption& caption : editSession_.sequence().captions()) {
        if (caption.timeline.end() <= exportStart ||
            caption.timeline.start >= exportStart + durationFrames) continue;
        QImage overlay(1280, 720, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QPainter painter(&overlay);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont font = painter.font();
        font.setPixelSize(static_cast<int>(std::llround(caption.fontSize)));
        font.setBold(caption.bold);
        font.setItalic(caption.italic);
        painter.setFont(font);
        const QRect bounds = painter.fontMetrics().boundingRect(
            QRect(0, 0, 1024, 680), Qt::AlignCenter | Qt::TextWordWrap,
            QString::fromStdString(caption.text)).adjusted(-12, -8, 12, 8);
        QRect positioned = bounds;
        positioned.moveCenter(QPoint(static_cast<int>(caption.positionX * overlay.width()),
                                     static_cast<int>(caption.positionY * overlay.height())));
        positioned.moveLeft(std::clamp(positioned.left(), 0,
                                       std::max(0, overlay.width() - positioned.width())));
        positioned.moveTop(std::clamp(positioned.top(), 0,
                                      std::max(0, overlay.height() - positioned.height())));
        painter.fillRect(positioned, colorFromRgba(caption.backgroundColor));
        painter.setPen(colorFromRgba(caption.textColor));
        painter.drawText(positioned.adjusted(12, 8, -12, -8),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         QString::fromStdString(caption.text));
        painter.end();
        const QString overlayPath = overlayDirectory + '/' +
            QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
        if (!overlay.save(overlayPath, "PNG")) {
            QMessageBox::critical(this, tr("Cannot Export"),
                                  tr("Could not render a title overlay."));
            for (const QString& path : std::as_const(exportOverlayPaths_)) QFile::remove(path);
            exportOverlayPaths_.clear();
            return;
        }
        exportOverlayPaths_.push_back(overlayPath);
        manifest += "C\t";
        manifest += QByteArray::number(caption.timeline.start - exportStart);
        manifest += '\t';
        manifest += QByteArray::number(caption.timeline.duration);
        manifest += '\t';
        manifest += overlayPath.toUtf8();
        manifest += '\n';
    }

    QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
    workerName += QStringLiteral(".exe");
#endif
    const QString workerPath = QCoreApplication::applicationDirPath() + '/' + workerName;
    if (!QFileInfo::exists(workerPath)) {
        for (const QString& path : std::as_const(exportOverlayPaths_)) QFile::remove(path);
        exportOverlayPaths_.clear();
        QMessageBox::critical(this, tr("Cannot Export"), tr("Media worker was not found."));
        return;
    }

    pausePlayback();
    exportOutputPath_ = outputPath;
    exportStartFrame_ = exportStart;
    exportDurationFrames_ = durationFrames;
    exportCancelled_ = false;
    exportErrorBuffer_.clear();
    exportDiagnostics_.clear();
    exportWorker_ = new QProcess(this);
    QProcess* worker = exportWorker_;
    worker->setProcessChannelMode(QProcess::SeparateChannels);

    if (jobsList_ != nullptr) {
        if (jobsList_->count() == 1 &&
            jobsList_->item(0)->text() == tr("No background jobs")) {
            jobsList_->clear();
        }
        exportJobItem_ = new QListWidgetItem(tr("Exporting %1 - 0%")
                                                 .arg(QFileInfo(outputPath).fileName()),
                                             jobsList_);
    }
    exportProgress_ = new QProgressDialog(tr("Rendering H.264/AAC review file..."),
                                          tr("Cancel"), 0, 1000, this);
    exportProgress_->setWindowTitle(tr("Export Review MP4"));
    exportProgress_->setWindowModality(Qt::WindowModal);
    exportProgress_->setMinimumDuration(0);
    exportProgress_->setValue(0);
    connect(exportProgress_, &QProgressDialog::canceled, this, [this, worker] {
        if (exportWorker_ == worker) {
            exportCancelled_ = true;
            worker->kill();
            QFile::remove(exportOutputPath_ + QStringLiteral(".part.mp4"));
        }
    });
    connect(worker, &QProcess::started, this, [worker, manifest] {
        worker->write(manifest);
        worker->closeWriteChannel();
    });
    connect(worker, &QProcess::readyReadStandardError, this, [this, worker] {
        exportErrorBuffer_ += worker->readAllStandardError();
        qsizetype newline = 0;
        while ((newline = exportErrorBuffer_.indexOf('\n')) >= 0) {
            const QByteArray line = exportErrorBuffer_.left(newline).trimmed();
            exportErrorBuffer_.remove(0, newline + 1);
            if (line.startsWith("PROGRESS ")) {
                const QList<QByteArray> fields = line.split(' ');
                if (fields.size() == 3) {
                    bool completedOk = false;
                    bool totalOk = false;
                    const qint64 completed = fields[1].toLongLong(&completedOk);
                    const qint64 total = fields[2].toLongLong(&totalOk);
                    if (completedOk && totalOk && total > 0) {
                        const int value = static_cast<int>(
                            std::clamp<qint64>(completed * 1000 / total, 0, 1000));
                        if (exportProgress_ != nullptr) {
                            exportProgress_->setValue(value);
                        }
                        if (exportJobItem_ != nullptr) {
                            exportJobItem_->setText(
                                tr("Exporting %1 - %2%")
                                    .arg(QFileInfo(exportOutputPath_).fileName())
                                    .arg(value / 10));
                        }
                    }
                }
            } else if (!line.isEmpty()) {
                exportDiagnostics_ += line + '\n';
            }
        }
    });
    connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, worker](const int exitCode, const QProcess::ExitStatus exitStatus) {
                exportDiagnostics_ += exportErrorBuffer_;
                exportErrorBuffer_.clear();
                const bool succeeded = !exportCancelled_ && exitStatus == QProcess::NormalExit &&
                                       exitCode == 0 && QFileInfo::exists(exportOutputPath_);
                const bool wasCancelled = exportCancelled_;
                if (exportProgress_ != nullptr) {
                    exportProgress_->disconnect(this);
                    exportProgress_->setValue(succeeded ? 1000 : exportProgress_->value());
                    exportProgress_->close();
                    exportProgress_->deleteLater();
                    exportProgress_ = nullptr;
                }
                if (exportJobItem_ != nullptr) {
                    exportJobItem_->setText(
                        succeeded ? tr("Completed %1").arg(QFileInfo(exportOutputPath_).fileName())
                                  : wasCancelled
                                        ? tr("Cancelled %1")
                                              .arg(QFileInfo(exportOutputPath_).fileName())
                                        : tr("Failed %1")
                                              .arg(QFileInfo(exportOutputPath_).fileName()));
                    exportJobItem_ = nullptr;
                }
                if (succeeded) {
                    if (exportIsPreviewRender_) {
                        previewCachePath_ = exportOutputPath_;
                        previewCacheStart_ = exportStartFrame_;
                        previewCacheDuration_ = exportDurationFrames_;
                        previewCacheRevision_ = exportRevisionAtStart_;
                        if (timeline_ != nullptr) {
                            timeline_->setPreviewCacheState(
                                previewCacheStart_, previewCacheDuration_,
                                previewCacheRevision_ ==
                                    editSession_.sequence().revision());
                        }
                        statusBar()->showMessage(
                            tr("Preview render ready for In/Out range"), 6000);
                    } else {
                        writeCaptionSidecar(exportOutputPath_);
                        statusBar()->showMessage(tr("Exported %1").arg(exportOutputPath_),
                                                 8000);
                    }
                } else if (!wasCancelled) {
                    QMessageBox::critical(
                        this, tr("Export Failed"),
                        QString::fromUtf8(exportDiagnostics_).trimmed().right(2000));
                }
                QFile::remove(exportOutputPath_ + QStringLiteral(".part.mp4"));
                for (const QString& path : std::as_const(exportOverlayPaths_)) QFile::remove(path);
                exportOverlayPaths_.clear();
                worker->deleteLater();
                if (exportWorker_ == worker) {
                    exportWorker_ = nullptr;
                }
                exportOutputPath_.clear();
                exportDiagnostics_.clear();
            });
    connect(worker, &QProcess::errorOccurred, this,
            [this, worker](const QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    if (exportProgress_ != nullptr) {
                        exportProgress_->close();
                        exportProgress_->deleteLater();
                        exportProgress_ = nullptr;
                    }
                    if (exportJobItem_ != nullptr) {
                        exportJobItem_->setText(tr("Failed to start export worker"));
                        exportJobItem_ = nullptr;
                    }
                    QFile::remove(exportOutputPath_ + QStringLiteral(".part.mp4"));
                    for (const QString& path : std::as_const(exportOverlayPaths_)) QFile::remove(path);
                    exportOverlayPaths_.clear();
                    QMessageBox::critical(this, tr("Export Failed"),
                                          tr("Could not start the media worker."));
                    worker->deleteLater();
                    if (exportWorker_ == worker) {
                        exportWorker_ = nullptr;
                    }
                    exportOutputPath_.clear();
                }
            });

    const core::FrameRate frameRate = editSession_.sequence().frameRate();
    const int exportWidth = previewRender ? 1280 : exportWidth_;
    const int exportHeight = previewRender ? 720 : exportHeight_;
    const std::int64_t exportBitrate = previewRender ? 5'000'000 : exportBitrate_;
    worker->start(workerPath,
                  {QStringLiteral("export"), outputPath,
                   QString::number(frameRate.numerator), QString::number(frameRate.denominator),
                   QString::number(exportWidth), QString::number(exportHeight),
                   QString::number(durationFrames), QString::number(exportBitrate)});
#endif
}

bool MainWindow::showExportDialog(QString& outputPath, const core::Frame durationFrames,
                                  const bool markedRange) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Settings"));
    auto* form = new QFormLayout(&dialog);

    auto* presetCombo = new QComboBox(&dialog);
    presetCombo->addItem(tr("Match Source"), QVariant::fromValue(QSize(0, 0)));
    presetCombo->addItem(tr("H.264 1080p"), QVariant::fromValue(QSize(1920, 1080)));
    presetCombo->addItem(tr("H.264 720p"), QVariant::fromValue(QSize(1280, 720)));
    presetCombo->addItem(tr("H.264 4K"), QVariant::fromValue(QSize(3840, 2160)));
    presetCombo->addItem(tr("Social 1080x1920"), QVariant::fromValue(QSize(1080, 1920)));
    presetCombo->addItem(tr("Low bitrate preview"), QVariant::fromValue(QSize(960, 540)));
    form->addRow(tr("Preset"), presetCombo);

    auto* widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(16, 7680);
    widthSpin->setValue(exportWidth_);
    auto* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(16, 4320);
    heightSpin->setValue(exportHeight_);
    form->addRow(tr("Width"), widthSpin);
    form->addRow(tr("Height"), heightSpin);

    auto* bitrateSpin = new QSpinBox(&dialog);
    bitrateSpin->setRange(200, 200'000);
    bitrateSpin->setSuffix(tr(" kbps"));
    bitrateSpin->setValue(static_cast<int>(exportBitrate_ / 1000));
    form->addRow(tr("Video Bitrate"), bitrateSpin);

    auto* videoCodecLabel = new QLabel(QStringLiteral("H.264 (AVC)"), &dialog);
    auto* audioCodecLabel = new QLabel(QStringLiteral("AAC LC, 48 kHz stereo"), &dialog);
    form->addRow(tr("Video Codec"), videoCodecLabel);
    form->addRow(tr("Audio Codec"), audioCodecLabel);

    auto* rangeCombo = new QComboBox(&dialog);
    rangeCombo->addItem(tr("Entire Sequence"), 0);
    rangeCombo->addItem(tr("In/Out Range"), 1);
    rangeCombo->setCurrentIndex(markedRange ? 1 : 0);
    rangeCombo->setEnabled(markedRange);
    form->addRow(tr("Range"), rangeCombo);

    const double fps = editSession_.sequence().frameRate().framesPerSecond();
    auto* sizeLabel = new QLabel(&dialog);
    const auto updateEstimate = [&] {
        const double seconds = static_cast<double>(durationFrames) / std::max(1.0, fps);
        const double megabytes =
            (bitrateSpin->value() * 1000.0 + 192'000.0) * seconds / 8.0 / 1'000'000.0;
        sizeLabel->setText(tr("~%1 MB  (%2 s)")
                               .arg(megabytes, 0, 'f', 1)
                               .arg(seconds, 0, 'f', 1));
    };
    updateEstimate();
    connect(bitrateSpin, &QSpinBox::valueChanged, &dialog, [updateEstimate](int) {
        updateEstimate();
    });
    form->addRow(tr("Estimated Size"), sizeLabel);

    connect(presetCombo, &QComboBox::currentIndexChanged, &dialog,
            [presetCombo, widthSpin, heightSpin, bitrateSpin](const int) {
                const QSize size = presetCombo->currentData().value<QSize>();
                if (size.width() > 0) {
                    widthSpin->setValue(size.width());
                    heightSpin->setValue(size.height());
                    const long long pixels =
                        static_cast<long long>(size.width()) * size.height();
                    bitrateSpin->setValue(static_cast<int>(
                        std::clamp<long long>(pixels / 200, 800, 80'000)));
                }
            });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    if ((widthSpin->value() % 2) != 0 || (heightSpin->value() % 2) != 0) {
        QMessageBox::warning(this, tr("Export Settings"),
                             tr("Width and height must be even numbers for H.264."));
        return false;
    }
    exportWidth_ = widthSpin->value();
    exportHeight_ = heightSpin->value();
    exportBitrate_ = static_cast<std::int64_t>(bitrateSpin->value()) * 1000;

    outputPath = QFileDialog::getSaveFileName(this, tr("Export Review MP4"), {},
                                              tr("MP4 video (*.mp4)"));
    return !outputPath.isEmpty();
#else
    Q_UNUSED(outputPath);
    Q_UNUSED(durationFrames);
    Q_UNUSED(markedRange);
    return false;
#endif
}

void MainWindow::loadWaveformCache(const ProjectAsset& asset) {
    if (timeline_ == nullptr) {
        return;
    }
    const QString path = asset.metadata.value(QStringLiteral("waveform_cache")).toString();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto peaks = parseWaveform(file.readAll());
    if (!peaks.has_value()) {
        return;
    }
    const qint64 durationMicroseconds =
        asset.metadata.value(QStringLiteral("duration_us")).toInteger(0);
    const core::Frame sourceDuration = std::max<core::Frame>(
        1, static_cast<core::Frame>(std::ceil(
               static_cast<double>(durationMicroseconds) / 1'000'000.0 *
               editSession_.sequence().frameRate().framesPerSecond())));
    timeline_->setWaveform(asset.id, sourceDuration, *peaks);
}

void MainWindow::pruneAssetCache() {
    const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/assets");
    QDir cacheDir(cacheRoot);
    if (!cacheDir.exists()) {
        return;
    }
    const int quotaMegabytes = qEnvironmentVariableIntValue("VIDEX_CACHE_QUOTA_MB");
    const qint64 quotaBytes = quotaMegabytes > 0
                                  ? static_cast<qint64>(quotaMegabytes) * 1024 * 1024
                                  : 4LL * 1024 * 1024 * 1024;
    const QFileInfoList entries =
        cacheDir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
    qint64 totalBytes = 0;
    for (const QFileInfo& entry : entries) {
        totalBytes += entry.size();
    }
    if (totalBytes <= quotaBytes) {
        return;
    }
    QSet<QString> referenced;
    for (const ProjectAsset& asset : assets_) {
        for (const QString& key :
             {QStringLiteral("proxy_cache"), QStringLiteral("waveform_cache"),
              QStringLiteral("thumbnail_cache")}) {
            const QString path = asset.metadata.value(key).toString();
            if (!path.isEmpty()) {
                referenced.insert(QFileInfo(path).absoluteFilePath());
            }
        }
    }
    for (const QFileInfo& entry : entries) {
        if (totalBytes <= quotaBytes) {
            break;
        }
        if (referenced.contains(entry.absoluteFilePath())) {
            continue;
        }
        if (QFile::remove(entry.absoluteFilePath())) {
            totalBytes -= entry.size();
        }
    }
    if (totalBytes > quotaBytes) {
        statusBar()->showMessage(
            tr("Media cache exceeds its quota; caches used by this project were kept"), 4000);
    }
}

void MainWindow::exportOtio() {
    const core::Sequence& sequence = editSession_.sequence();
    bool hasClips = false;
    for (const core::Track& track : sequence.tracks()) {
        hasClips = hasClips || !track.clips.empty();
    }
    if (!hasClips) {
        statusBar()->showMessage(tr("Add clips to the timeline before exporting"), 4000);
        return;
    }
    const QString outputPath = QFileDialog::getSaveFileName(
        this, tr("Export OpenTimelineIO"), {}, tr("OpenTimelineIO (*.otio)"));
    if (outputPath.isEmpty()) {
        return;
    }

    const double rate = sequence.frameRate().framesPerSecond();
    const auto rationalTime = [rate](const core::Frame value) {
        return QJsonObject{
            {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("RationalTime.1")},
            {QStringLiteral("rate"), rate},
            {QStringLiteral("value"), static_cast<double>(value)},
        };
    };
    const auto timeRange = [&rationalTime](const core::Frame start, const core::Frame duration) {
        return QJsonObject{
            {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("TimeRange.1")},
            {QStringLiteral("start_time"), rationalTime(start)},
            {QStringLiteral("duration"), rationalTime(duration)},
        };
    };

    QStringList compatibilityNotes;
    QJsonArray otioTracks;
    int videoIndex = 0;
    int audioIndex = 0;
    for (const core::Track& track : sequence.tracks()) {
        const bool video = track.kind == core::TrackKind::Video;
        QJsonArray children;
        core::Frame cursor = 0;
        std::vector<core::Clip> ordered = track.clips;
        std::ranges::sort(ordered, {}, [](const core::Clip& clip) {
            return clip.timeline.start;
        });
        for (const core::Clip& clip : ordered) {
            if (clip.timeline.start > cursor) {
                children.append(QJsonObject{
                    {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Gap.1")},
                    {QStringLiteral("name"), QString{}},
                    {QStringLiteral("source_range"),
                     timeRange(0, clip.timeline.start - cursor)},
                });
            }
            const auto asset = std::ranges::find_if(
                assets_, [&clip](const ProjectAsset& candidate) {
                    return candidate.id == clip.assetId;
                });
            QJsonObject mediaReference{
                {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("ExternalReference.1")},
                {QStringLiteral("target_url"),
                 asset == assets_.end()
                     ? QString{}
                     : QUrl::fromLocalFile(asset->path).toString()},
            };
            QJsonObject otioClip{
                {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Clip.1")},
                {QStringLiteral("name"),
                 asset == assets_.end() ? tr("Clip %1").arg(clip.id.value)
                                        : QFileInfo(asset->path).completeBaseName()},
                {QStringLiteral("source_range"),
                 timeRange(clip.sourceStart, clip.timeline.duration)},
                {QStringLiteral("media_reference"), mediaReference},
            };
            if (clip.playbackRate != 1.0) {
                otioClip.insert(QStringLiteral("effects"),
                                QJsonArray{QJsonObject{
                                    {QStringLiteral("OTIO_SCHEMA"),
                                     QStringLiteral("LinearTimeWarp.1")},
                                    {QStringLiteral("name"), QStringLiteral("speed")},
                                    {QStringLiteral("effect_name"),
                                     QStringLiteral("LinearTimeWarp")},
                                    {QStringLiteral("time_scalar"), clip.playbackRate},
                                }});
            }
            if (!clip.effects.empty() || !clip.motionKeyframes.empty() ||
                !clip.speedKeyframes.empty() || clip.maskShape != core::MaskShape::None) {
                const QString note = tr("Clip %1: effects, motion/speed keyframes, and masks "
                                        "are not represented in OTIO")
                                         .arg(clip.id.value);
                if (!compatibilityNotes.contains(note)) compatibilityNotes.append(note);
            }
            children.append(otioClip);
            cursor = clip.timeline.end();
        }
        otioTracks.append(QJsonObject{
            {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Track.1")},
            {QStringLiteral("kind"), video ? QStringLiteral("Video")
                                           : QStringLiteral("Audio")},
            {QStringLiteral("name"), video ? QStringLiteral("V%1").arg(++videoIndex)
                                           : QStringLiteral("A%1").arg(++audioIndex)},
            {QStringLiteral("children"), children},
        });
    }

    QJsonArray otioMarkers;
    for (const core::Marker& marker : sequence.markers()) {
        otioMarkers.append(QJsonObject{
            {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Marker.2")},
            {QStringLiteral("name"), QString::fromStdString(marker.name)},
            {QStringLiteral("color"), QStringLiteral("GREEN")},
            {QStringLiteral("marked_range"), timeRange(marker.position, 1)},
        });
    }
    if (!sequence.captions().empty()) {
        compatibilityNotes.append(
            tr("Captions are exported only as the review MP4's SRT sidecar, not in OTIO"));
    }

    const QJsonObject timeline{
        {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
        {QStringLiteral("name"),
         projectPath_.isEmpty() ? tr("Videx Sequence")
                                : QFileInfo(projectPath_).completeBaseName()},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("videx"),
                      QJsonObject{{QStringLiteral("revision"),
                                   QString::number(sequence.revision())}}}}},
        {QStringLiteral("tracks"),
         QJsonObject{
             {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
             {QStringLiteral("name"), QStringLiteral("tracks")},
             {QStringLiteral("markers"), otioMarkers},
             {QStringLiteral("children"), otioTracks},
         }},
    };

    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Export OpenTimelineIO"), file.errorString());
        return;
    }
    const QByteArray serialized = QJsonDocument(timeline).toJson(QJsonDocument::Indented);
    if (file.write(serialized) != serialized.size() || !file.commit()) {
        QMessageBox::critical(this, tr("Export OpenTimelineIO"), file.errorString());
        return;
    }
    if (!compatibilityNotes.isEmpty()) {
        QMessageBox::information(
            this, tr("OpenTimelineIO Compatibility"),
            tr("The timeline was exported with these limitations:\n\n%1")
                .arg(compatibilityNotes.join(QStringLiteral("\n"))));
    }
    statusBar()->showMessage(tr("Exported %1").arg(outputPath), 6000);
}

void MainWindow::startAssetCacheJobs(const core::AssetId assetId) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    auto asset = std::ranges::find_if(assets_, [assetId](const ProjectAsset& candidate) {
        return candidate.id == assetId;
    });
    if (asset == assets_.end() || !QFileInfo::exists(asset->path)) {
        return;
    }
    // Title assets are app-rendered stills; they need no proxies, thumbnails,
    // or waveforms.
    if (asset->metadata.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("title")) {
        return;
    }
    if (jobsList_ != nullptr && jobsList_->count() == 1 &&
        jobsList_->item(0)->text() == tr("No background jobs")) {
        jobsList_->clear();
    }
    bool hasVideo = false;
    bool hasAudio = false;
    for (const QJsonValue& stream : asset->metadata.value(QStringLiteral("streams")).toArray()) {
        const QString kind = stream.toObject().value(QStringLiteral("kind")).toString();
        hasVideo = hasVideo || kind == QStringLiteral("video");
        hasAudio = hasAudio || kind == QStringLiteral("audio");
    }
    const qint64 durationMicroseconds =
        asset->metadata.value(QStringLiteral("duration_us")).toInteger(0);
    const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/assets");
    QDir().mkpath(cacheRoot);
    pruneAssetCache();
    const QString cacheKey = cacheKeyForPath(asset->path);
    const QString waveformPath = cacheRoot + '/' + cacheKey + QStringLiteral(".vxw");
    const QString thumbnailPath = cacheRoot + '/' + cacheKey + QStringLiteral(".png");
    const QString proxyPath = cacheRoot + '/' + cacheKey + QStringLiteral(".proxy.mp4");
    asset->metadata.insert(QStringLiteral("cache_key"), cacheKey);
    asset->metadata.insert(QStringLiteral("waveform_cache"), waveformPath);
    asset->metadata.insert(QStringLiteral("thumbnail_cache"), thumbnailPath);
    if (QFileInfo::exists(proxyPath)) {
        asset->metadata.insert(QStringLiteral("proxy_cache"), proxyPath);
    } else if (hasVideo) {
        // Sources above 1080p are too heavy for software compositing (titles
        // and multi-layer sections play through the compositor, not the media
        // player). Build the 540p proxy automatically, Premiere-style; the
        // manifest and player already prefer it once it exists.
        int sourceWidth = 0;
        int sourceHeight = 0;
        for (const QJsonValue& stream :
             asset->metadata.value(QStringLiteral("streams")).toArray()) {
            const QJsonObject object = stream.toObject();
            if (object.value(QStringLiteral("kind")).toString() ==
                QStringLiteral("video")) {
                sourceWidth = std::max(sourceWidth,
                                       object.value(QStringLiteral("width")).toInt());
                sourceHeight = std::max(
                    sourceHeight, object.value(QStringLiteral("height")).toInt());
            }
        }
        if (sourceWidth > 1920 || sourceHeight > 1080) {
            generateProxy(assetId);
        }
    }

    QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
    workerName += QStringLiteral(".exe");
#endif
    const QString workerPath = QCoreApplication::applicationDirPath() + '/' + workerName;
    if (!QFileInfo::exists(workerPath)) {
        return;
    }

    if (hasAudio && QFileInfo::exists(waveformPath)) {
        loadWaveformCache(*asset);
    } else if (hasAudio && durationMicroseconds > 0) {
        const QString jobKey = QStringLiteral("waveform:%1").arg(assetId.value);
        if (!activeCacheJobs_.contains(jobKey)) {
            activeCacheJobs_.insert(jobKey);
            auto* worker = new QProcess(this);
            auto* jobItem = jobsList_ == nullptr
                                ? nullptr
                                : new QListWidgetItem(
                                      tr("Generating waveform: %1")
                                          .arg(QFileInfo(asset->path).fileName()),
                                      jobsList_);
            connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, worker, assetId, waveformPath, jobKey,
                     jobItem](const int exitCode, const QProcess::ExitStatus status) {
                        const QByteArray output = worker->readAllStandardOutput();
                        const bool valid = status == QProcess::NormalExit && exitCode == 0 &&
                                           parseWaveform(output).has_value();
                        bool saved = false;
                        if (valid) {
                            QSaveFile file(waveformPath);
                            saved = file.open(QIODevice::WriteOnly) && file.write(output) == output.size() &&
                                    file.commit();
                        }
                        if (jobItem != nullptr) {
                            jobItem->setText(saved ? tr("Waveform ready")
                                                  : tr("Waveform generation failed"));
                        }
                        activeCacheJobs_.remove(jobKey);
                        worker->deleteLater();
                        if (saved) {
                            const auto current = std::ranges::find_if(
                                assets_, [assetId](const ProjectAsset& candidate) {
                                    return candidate.id == assetId;
                                });
                            if (current != assets_.end()) {
                                loadWaveformCache(*current);
                            }
                        }
                    });
            worker->setProcessChannelMode(QProcess::SeparateChannels);
            worker->start(workerPath,
                          {QStringLiteral("waveform"), asset->path,
                           QString::number(durationMicroseconds), QStringLiteral("4096")});
        }
    }

    if (hasVideo && !QFileInfo::exists(thumbnailPath)) {
        const QString jobKey = QStringLiteral("thumbnail:%1").arg(assetId.value);
        if (!activeCacheJobs_.contains(jobKey)) {
            activeCacheJobs_.insert(jobKey);
            auto* worker = new QProcess(this);
            auto* jobItem = jobsList_ == nullptr
                                ? nullptr
                                : new QListWidgetItem(
                                      tr("Generating thumbnail: %1")
                                          .arg(QFileInfo(asset->path).fileName()),
                                      jobsList_);
            connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, worker, thumbnailPath, jobKey,
                     jobItem](const int exitCode, const QProcess::ExitStatus status) {
                        const auto image = parseVideoFrame(worker->readAllStandardOutput());
                        bool saved = false;
                        if (status == QProcess::NormalExit && exitCode == 0 && image.has_value()) {
                            QSaveFile file(thumbnailPath);
                            saved = file.open(QIODevice::WriteOnly) && image->save(&file, "PNG") &&
                                    file.commit();
                        }
                        if (jobItem != nullptr) {
                            jobItem->setText(saved ? tr("Thumbnail ready")
                                                  : tr("Thumbnail generation failed"));
                        }
                        activeCacheJobs_.remove(jobKey);
                        worker->deleteLater();
                        if (saved) {
                            rebuildProjectTree();
                        }
                    });
            worker->setProcessChannelMode(QProcess::SeparateChannels);
            worker->start(workerPath,
                          {QStringLiteral("frame"), asset->path, QStringLiteral("0")});
        }
    }
#else
    Q_UNUSED(assetId);
#endif
}

void MainWindow::generateProxy(const core::AssetId assetId) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    auto asset = std::ranges::find_if(assets_, [assetId](const ProjectAsset& candidate) {
        return candidate.id == assetId;
    });
    if (asset == assets_.end() || !QFileInfo::exists(asset->path)) {
        return;
    }
    const QString jobKey = QStringLiteral("proxy:%1").arg(assetId.value);
    if (activeCacheJobs_.contains(jobKey)) {
        statusBar()->showMessage(tr("Proxy generation is already running"), 3000);
        return;
    }
    const qint64 durationMicroseconds =
        asset->metadata.value(QStringLiteral("duration_us")).toInteger(0);
    const core::Frame durationFrames = std::max<core::Frame>(
        1, static_cast<core::Frame>(std::ceil(
               durationMicroseconds / 1'000'000.0 *
               editSession_.sequence().frameRate().framesPerSecond())));
    const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/assets");
    QDir().mkpath(cacheRoot);
    const QString proxyPath = cacheRoot + '/' + cacheKeyForPath(asset->path) +
                              QStringLiteral(".proxy.mp4");

    bool hasVideo = false;
    bool hasAudio = false;
    for (const QJsonValue& stream : asset->metadata.value(QStringLiteral("streams")).toArray()) {
        const QString kind = stream.toObject().value(QStringLiteral("kind")).toString();
        hasVideo = hasVideo || kind == QStringLiteral("video");
        hasAudio = hasAudio || kind == QStringLiteral("audio");
    }
    if (!hasVideo) {
        statusBar()->showMessage(tr("The selected asset has no video stream"), 4000);
        return;
    }

    QByteArray manifest;
    auto appendClip = [&](const core::TrackKind kind) {
        core::Clip proxyClip;
        proxyClip.timeline = {.start = 0, .duration = durationFrames};
        manifest += timelineManifestLine(kind, proxyClip, asset->path);
    };
    appendClip(core::TrackKind::Video);
    if (hasAudio) {
        appendClip(core::TrackKind::Audio);
    }

    QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
    workerName += QStringLiteral(".exe");
#endif
    const QString workerPath = QCoreApplication::applicationDirPath() + '/' + workerName;
    if (!QFileInfo::exists(workerPath)) {
        return;
    }
    activeCacheJobs_.insert(jobKey);
    auto* worker = new QProcess(this);
    auto* jobItem = jobsList_ == nullptr
                        ? nullptr
                        : new QListWidgetItem(
                              tr("Generating proxy: %1").arg(QFileInfo(asset->path).fileName()),
                              jobsList_);
    QByteArray progressBuffer;
    connect(worker, &QProcess::started, this, [worker, manifest] {
        worker->write(manifest);
        worker->closeWriteChannel();
    });
    connect(worker, &QProcess::readyReadStandardError, this,
            [worker, jobItem, progressBuffer]() mutable {
                progressBuffer += worker->readAllStandardError();
                qsizetype newline = 0;
                while ((newline = progressBuffer.indexOf('\n')) >= 0) {
                    const QByteArray line = progressBuffer.left(newline).trimmed();
                    progressBuffer.remove(0, newline + 1);
                    if (jobItem != nullptr && line.startsWith("PROGRESS ")) {
                        const QList<QByteArray> fields = line.split(' ');
                        if (fields.size() == 3) {
                            const qint64 completed = fields[1].toLongLong();
                            const qint64 total = fields[2].toLongLong();
                            if (total > 0) {
                                jobItem->setText(QObject::tr("Generating proxy - %1%")
                                                     .arg(completed * 100 / total));
                            }
                        }
                    }
                }
            });
    connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, worker, assetId, proxyPath, jobKey,
             jobItem](const int exitCode, const QProcess::ExitStatus status) {
                const bool succeeded = status == QProcess::NormalExit && exitCode == 0 &&
                                       QFileInfo::exists(proxyPath);
                if (succeeded) {
                    const auto current = std::ranges::find_if(
                        assets_, [assetId](const ProjectAsset& candidate) {
                            return candidate.id == assetId;
                        });
                    if (current != assets_.end()) {
                        current->metadata.insert(QStringLiteral("proxy_cache"), proxyPath);
                    }
                    // The next preview frame must pick up the proxy path.
                    manifestCacheRevision_ = ~0ULL;
                    pruneAssetCache();
                } else {
                    QFile::remove(proxyPath + QStringLiteral(".part.mp4"));
                }
                if (jobItem != nullptr) {
                    jobItem->setText(succeeded ? tr("Proxy ready") : tr("Proxy generation failed"));
                }
                activeCacheJobs_.remove(jobKey);
                worker->deleteLater();
                rebuildProjectTree();
            });
    const core::FrameRate frameRate = editSession_.sequence().frameRate();
    worker->start(workerPath,
                  {QStringLiteral("export"), proxyPath, QString::number(frameRate.numerator),
                   QString::number(frameRate.denominator), QStringLiteral("960"),
                   QStringLiteral("540"), QString::number(durationFrames)});
#else
    Q_UNUSED(assetId);
#endif
}

void MainWindow::requestPreviewFrame(const QString& filePath,
                                     const std::int64_t timestampMicroseconds,
                                     const bool updateSource, const bool updateProgram,
                                     const QString& blendPath,
                                     const std::int64_t blendTimestampMicroseconds,
                                     const double blendAmount) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    if (previewWorker_ != nullptr) {
        pendingPreviewPath_ = filePath;
        pendingPreviewTimestamp_ = timestampMicroseconds;
        pendingPreviewSource_ = updateSource;
        pendingPreviewProgram_ = updateProgram;
        pendingPreviewBlendPath_ = blendPath;
        pendingPreviewBlendTimestamp_ = blendTimestampMicroseconds;
        pendingPreviewBlendAmount_ = blendAmount;
        hasPendingPreview_ = true;
        return;
    }

    QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
    workerName += QStringLiteral(".exe");
#endif
    const QString workerPath = QCoreApplication::applicationDirPath() + '/' + workerName;
    if (!QFileInfo::exists(workerPath)) {
        statusBar()->showMessage(tr("Media worker was not found: %1").arg(workerPath), 6000);
        return;
    }

    previewWorker_ = new QProcess(this);
    QProcess* worker = previewWorker_;
    worker->setProcessChannelMode(QProcess::SeparateChannels);
    connect(worker, &QProcess::errorOccurred, this,
            [this, worker](const QProcess::ProcessError processError) {
                if (processError == QProcess::FailedToStart && previewWorker_ == worker) {
                    statusBar()->showMessage(tr("Could not start the preview worker"), 6000);
                    worker->deleteLater();
                    previewWorker_ = nullptr;
                    continuePendingPreview();
                }
            });
    connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, worker, filePath, updateSource,
             updateProgram](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const QByteArray output = worker->readAllStandardOutput();
                const QString errorOutput = QString::fromUtf8(worker->readAllStandardError());
                if (previewWorker_ == worker) {
                    previewWorker_ = nullptr;
                }
                worker->deleteLater();

                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    statusBar()->showMessage(
                        tr("Could not decode preview: %1").arg(errorOutput.trimmed()), 8000);
                    continuePendingPreview();
                    return;
                }
                const auto frame = parseVideoFrame(output);
                if (!frame.has_value()) {
                    statusBar()->showMessage(tr("Preview worker returned an invalid frame"), 6000);
                    continuePendingPreview();
                    return;
                }
                if (updateSource && sourceMonitor_ != nullptr) {
                    sourceMonitor_->setFrameOpacity(1.0);
                    sourceMonitor_->setFrame(*frame);
                }
                if (updateProgram && programMonitor_ != nullptr) {
                    programMonitor_->setFrame(*frame);
                }
                if (updateSource) {
                    statusBar()->showMessage(
                        tr("Showing %1").arg(QFileInfo(filePath).fileName()), 3000);
                }
                continuePendingPreview();
            });
    if (blendPath.isEmpty()) {
        worker->start(workerPath,
                      {QStringLiteral("frame"), filePath,
                       QString::number(timestampMicroseconds)});
    } else {
        worker->start(workerPath,
                      {QStringLiteral("blend-frame"), filePath,
                       QString::number(timestampMicroseconds), blendPath,
                       QString::number(blendTimestampMicroseconds),
                       QString::number(blendAmount, 'g', 17)});
    }
#else
    Q_UNUSED(filePath);
    Q_UNUSED(timestampMicroseconds);
    Q_UNUSED(updateSource);
    Q_UNUSED(updateProgram);
    Q_UNUSED(blendPath);
    Q_UNUSED(blendTimestampMicroseconds);
    Q_UNUSED(blendAmount);
#endif
}

void MainWindow::continuePendingPreview() {
    if (!hasPendingPreview_ || previewWorker_ != nullptr) {
        return;
    }
    const QString path = pendingPreviewPath_;
    const std::int64_t timestamp = pendingPreviewTimestamp_;
    const bool updateSource = pendingPreviewSource_;
    const bool updateProgram = pendingPreviewProgram_;
    const QString blendPath = pendingPreviewBlendPath_;
    const std::int64_t blendTimestamp = pendingPreviewBlendTimestamp_;
    const double blendAmount = pendingPreviewBlendAmount_;
    hasPendingPreview_ = false;
    pendingPreviewPath_.clear();
    pendingPreviewBlendPath_.clear();
    requestPreviewFrame(path, timestamp, updateSource, updateProgram, blendPath,
                        blendTimestamp, blendAmount);
}

core::Frame MainWindow::sequenceEndFrame() const {
    core::Frame sequenceEnd = 0;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        for (const core::Clip& clip : track.clips) {
            sequenceEnd = std::max(sequenceEnd, clip.timeline.end());
        }
    }
    for (const core::Caption& caption : editSession_.sequence().captions()) {
        sequenceEnd = std::max(sequenceEnd, caption.timeline.end());
    }
    return sequenceEnd;
}

void MainWindow::updateTransportUi(const core::Frame frame) {
    const core::Frame sequenceEnd = sequenceEndFrame();
    if (seekSlider_ != nullptr) {
        const int maximum = static_cast<int>(std::min<core::Frame>(
            std::max<core::Frame>(0, sequenceEnd - 1), std::numeric_limits<int>::max()));
        updatingSeekSlider_ = true;
        seekSlider_->setRange(0, maximum);
        seekSlider_->setValue(static_cast<int>(
            std::min<core::Frame>(std::max<core::Frame>(0, frame), maximum)));
        updatingSeekSlider_ = false;
    }

    if (timecodeLabel_ != nullptr) {
        timecodeLabel_->setText(
            timecodeString(editSession_.sequence().frameRate(), frame));
    }
}

void MainWindow::togglePlayback() {
    if (transportTimer_ == nullptr || timeline_ == nullptr) {
        return;
    }
    if (playbackRequested_) {
        pausePlayback();
        return;
    }

    startPlayback(1);
}

void MainWindow::pausePlayback() {
    if (!playbackRequested_) {
        return;
    }
    playbackRequested_ = false;
    if (transportTimer_ != nullptr) {
        transportTimer_->stop();
    }
    stopPlaybackAudio();
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
    }
    mediaPlaybackClip_ = {};
    cachePlaybackActive_ = false;
    if (playPauseButton_ != nullptr) {
        playPauseButton_->setText(tr("Play"));
    }
    if (timeline_ != nullptr) {
        updateProgramFrame(timeline_->playheadFrame());
    }
    updateMonitorEditTarget();
    statusBar()->showMessage(tr("Playback paused"), 2000);
}

void MainWindow::startPlayback(const int direction) {
    if (transportTimer_ == nullptr || timeline_ == nullptr) {
        return;
    }

    core::Frame sequenceEnd = sequenceEndFrame();
    const core::Frame playbackStart =
        sequenceInFrame_ >= 0 && sequenceOutFrame_ > sequenceInFrame_
            ? sequenceInFrame_ : 0;
    if (sequenceOutFrame_ > playbackStart)
        sequenceEnd = std::min(sequenceEnd, sequenceOutFrame_);
    if (sequenceEnd <= 0) {
        statusBar()->showMessage(tr("Import media before starting playback"), 4000);
        return;
    }
    if (direction > 0 && timeline_->playheadFrame() >= sequenceEnd - 1) {
        timeline_->setPlayheadFrame(playbackStart);
    } else if (direction > 0 && timeline_->playheadFrame() < playbackStart) {
        timeline_->setPlayheadFrame(playbackStart);
    } else if (direction < 0 && timeline_->playheadFrame() <= playbackStart) {
        timeline_->setPlayheadFrame(sequenceEnd - 1);
    }

    if (playbackRequested_) {
        transportTimer_->stop();
        stopPlaybackAudio();
        if (mediaPlayer_ != nullptr) {
            mediaPlayer_->stop();
        }
        mediaPlaybackClip_ = {};
        cachePlaybackActive_ = false;
    }
    playbackDirection_ = direction < 0 ? -1 : 1;
    playbackRequested_ = true;
    updateMonitorEditTarget();
    if (playPauseButton_ != nullptr) {
        playPauseButton_->setText(tr("Pause"));
    }
    updateProgramFrame(timeline_->playheadFrame());
    if (playbackDirection_ > 0 && startContinuousPlayback(timeline_->playheadFrame())) {
        startPlaybackClock();
    } else if (playbackDirection_ < 0 || !requestPlaybackAudio(timeline_->playheadFrame())) {
        startPlaybackClock();
    } else {
        statusBar()->showMessage(tr("Preparing audio..."));
    }
}

void MainWindow::ensureMediaPlayer() {
    if (mediaPlayer_ != nullptr) {
        return;
    }
    mediaPlayer_ = new QMediaPlayer(this);
    mediaAudioOutput_ = new QAudioOutput(this);
    mediaVideoSink_ = new QVideoSink(this);
    mediaPlayer_->setAudioOutput(mediaAudioOutput_);
    mediaPlayer_->setVideoSink(mediaVideoSink_);
    connect(mediaVideoSink_, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame& frame) {
                const QImage image = frame.toImage();
                if (!image.isNull() && programMonitor_ != nullptr && playbackRequested_) {
                    programMonitor_->setFrame(image);
                }
            });
    connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) {
                if (!message.isEmpty()) {
                    statusBar()->showMessage(tr("Playback decoder: %1").arg(message), 5000);
                }
                // A decode failure would otherwise leave the transport ticking
                // against a frozen player position with the button on "Pause".
                if (playbackRequested_) {
                    pausePlayback();
                }
            });
}

bool MainWindow::collectTitleOverlays(const core::Frame frame, const int baseTrackIndex,
                                      std::vector<render::MonitorTitleOverlay>& out) {
    out.clear();
    int trackIndex = -1;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (track.kind != core::TrackKind::Video) continue;
        ++trackIndex;
        if (!track.enabled) continue;
        for (const core::Clip& clip : track.clips) {
            if (!clip.timeline.contains(frame)) continue;
            const auto asset = std::ranges::find_if(
                assets_, [&clip](const ProjectAsset& candidate) {
                    return candidate.id == clip.assetId;
                });
            if (asset == assets_.end() || !isTitleAsset(asset->metadata)) continue;
            // Overlays draw in front of the base clip and support any static
            // transform (position/scale/rotation, e.g. from monitor drags);
            // keyframed motion, effects, masks, crops, and transitions still
            // need the compositor. Each bail records why so the monitor can
            // explain the resolution drop instead of silently halving.
            if (trackIndex < baseTrackIndex) {
                setPreviewPathReason(PreviewPathReason::TitleBehindBase);
                return false;
            }
            // Effects and masks are per-pixel work, but a title raster is a
            // still: when the parameters do not vary over time the result can
            // be baked once and reused, so only keyframed effects genuinely
            // need the compositor. Masks have no keyframes in the data model,
            // so they are always bakeable.
            if (std::ranges::any_of(clip.effects, [](const core::ClipEffect& effect) {
                    return effect.enabled && !effect.keyframes.empty();
                })) {
                setPreviewPathReason(PreviewPathReason::TitleEffects);
                return false;
            }
            // A transition only blends when an immediately adjacent clip
            // precedes this one on the same track; the compositor leaves
            // incomingMix at 1.0 otherwise (timeline_export.cpp renderLayer),
            // so a lone title with a transition renders identically here at
            // full resolution. Only a real two-source blend needs the
            // compositor.
            if (clip.videoTransitionInFrames > 0) {
                const bool hasAdjacentOutgoing = std::ranges::any_of(
                    track.clips, [&clip](const core::Clip& other) {
                        return other.id != clip.id &&
                               other.timeline.end() == clip.timeline.start;
                    });
                if (hasAdjacentOutgoing) {
                    setPreviewPathReason(PreviewPathReason::TitleTransition);
                    return false;
                }
            }
            const QFileInfo info(asset->path);
            if (!info.exists() && !ensureTitleImage(asset->path, asset->metadata)) {
                setPreviewPathReason(PreviewPathReason::TitleRasterUnavailable);
                return false;
            }
            auto& cached = titleOverlayCache_[asset->path];
            const qint64 modified = QFileInfo(asset->path)
                                        .lastModified().toMSecsSinceEpoch();
            if (cached.second.isNull() || cached.first != modified) {
                cached.second = readTitleImage(asset->path);
                cached.first = modified;
            }
            if (cached.second.isNull()) {
                setPreviewPathReason(PreviewPathReason::TitleRasterUnavailable);
                return false;
            }
            // Bake time-invariant effects/masks into a per-title cached raster.
            QImage overlayImage = cached.second;
            const bool hasMask = clip.maskShape != core::MaskShape::None;
            const bool hasEffects = std::ranges::any_of(
                clip.effects,
                [](const core::ClipEffect& effect) { return effect.enabled; });
            if (hasMask || hasEffects) {
                render::MonitorEffects effects;
                for (const core::ClipEffect& effect : clip.effects) {
                    if (!effect.enabled) continue;
                    switch (effect.type) {
                    case core::EffectType::Brightness: effects.brightness += effect.amount; break;
                    case core::EffectType::Contrast: effects.contrast += effect.amount; break;
                    case core::EffectType::Saturation: effects.saturation += effect.amount; break;
                    case core::EffectType::Blur:
                        effects.blur = std::max(effects.blur, effect.amount);
                        break;
                    case core::EffectType::Vignette:
                        effects.vignette = std::max(effects.vignette, effect.amount);
                        break;
                    }
                }
                const render::MonitorMask mask{
                    .shape = static_cast<int>(clip.maskShape),
                    .centerX = clip.maskCenterX,
                    .centerY = clip.maskCenterY,
                    .width = clip.maskWidth,
                    .height = clip.maskHeight,
                    .feather = clip.maskFeather,
                    .inverted = clip.maskInverted};
                const QString signature =
                    QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13")
                        .arg(modified)
                        .arg(effects.brightness).arg(effects.contrast)
                        .arg(effects.saturation).arg(effects.blur).arg(effects.vignette)
                        .arg(mask.shape).arg(mask.centerX).arg(mask.centerY)
                        .arg(mask.width).arg(mask.height).arg(mask.feather)
                        .arg(mask.inverted ? 1 : 0);
                auto& baked = titleBakedCache_[asset->path];
                if (baked.first != signature || baked.second.isNull()) {
                    baked.first = signature;
                    baked.second = render::applyStillAdjustments(cached.second, mask, effects);
                }
                if (baked.second.isNull()) {
                    setPreviewPathReason(PreviewPathReason::TitleRasterUnavailable);
                    return false;
                }
                overlayImage = baked.second;
            }
            // Motion keyframes are only a time-varying form of the static
            // transform the overlay already applies, and motionAt() falls back
            // to the static fields when there are no keyframes. Evaluating it
            // per frame therefore costs nothing and keeps animated titles on
            // the native-resolution path instead of dropping to the compositor.
            const core::MotionKeyframe motion =
                motionAt(clip, frame - clip.timeline.start);
            out.push_back({.image = overlayImage,
                           .opacity = std::clamp(
                               motion.opacity * clipEnvelope(clip, frame), 0.0, 1.0),
                           .positionX = motion.positionX,
                           .positionY = motion.positionY,
                           .scaleX = motion.scaleX,
                           .scaleY = motion.scaleY,
                           .rotationDegrees = motion.rotationDegrees,
                           .anchorX = motion.anchorX,
                           .anchorY = motion.anchorY,
                           .cropLeft = clip.cropLeft,
                           .cropRight = clip.cropRight,
                           .cropTop = clip.cropTop,
                           .cropBottom = clip.cropBottom});
        }
    }
    return true;
}

bool MainWindow::startContinuousPlayback(const core::Frame timelineFrame) {
    if (previewCacheValid() && timelineFrame >= previewCacheStart_ &&
        timelineFrame < previewCacheStart_ + previewCacheDuration_) {
        ensureMediaPlayer();
        mediaPlayer_->stop();
        mediaPlayer_->setSource(QUrl::fromLocalFile(previewCachePath_));
        mediaAudioOutput_->setMuted(false);
        mediaAudioOutput_->setVolume(1.0F);
        mediaPlayer_->setPlaybackRate(1.0);
        if (programMonitor_ != nullptr) {
            programMonitor_->setFrameOpacity(1.0);
            programMonitor_->setFrameTransform(0.0, 0.0, 1.0, 1.0, 0.0, 0.5, 0.5);
            programMonitor_->setFrameCrop(0.0, 0.0, 0.0, 0.0);
            programMonitor_->setFrameMask(0, 0.5, 0.5, 1.0, 1.0, 0.0, false);
            programMonitor_->setVideoEffects(0.0, 0.0, 0.0, 0.0, 0.0);
        }
        const double framesPerSecondCache =
            editSession_.sequence().frameRate().framesPerSecond();
        mediaPlayer_->setPosition(static_cast<qint64>(std::llround(
            static_cast<double>(timelineFrame - previewCacheStart_) * 1000.0 /
            framesPerSecondCache)));
        mediaPlaybackClip_ = {};
        cachePlaybackActive_ = true;
        setPreviewPathReason(PreviewPathReason::PreviewCache);
        // The preview cache has all layers baked in.
        programMonitor_->setTitleOverlays({});
        mediaPlayer_->play();
        return true;
    }
    cachePlaybackActive_ = false;
    const core::Clip* playbackClip = nullptr;
    const core::Track* playbackTrack = nullptr;
    const core::Clip* audioPlaybackClip = nullptr;
    std::size_t activeVideoClips = 0;
    bool audioEnabled = false;
    bool sawTitle = false;
    int trackIndex = -1;
    int baseTrackIndex = -1;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (track.kind != core::TrackKind::Video) continue;
        ++trackIndex;
        if (!track.enabled) continue;
        for (const core::Clip& clip : track.clips) {
            if (!clip.timeline.contains(timelineFrame)) continue;
            const auto clipAsset = std::ranges::find_if(
                assets_, [&clip](const ProjectAsset& candidate) {
                    return candidate.id == clip.assetId;
                });
            // Title stills do not break continuous playback: they ride on top
            // of the media player as monitor overlays.
            if (clipAsset != assets_.end() && isTitleAsset(clipAsset->metadata)) {
                sawTitle = true;
                continue;
            }
            ++activeVideoClips;
            playbackClip = &clip;
            playbackTrack = &track;
            baseTrackIndex = trackIndex;
        }
    }
    if (activeVideoClips != 1 || playbackClip == nullptr) {
        setPreviewPathReason(activeVideoClips > 1 ? PreviewPathReason::MultipleVideoClips
                                                 : PreviewPathReason::NoVideoClip);
        return false;
    }
    std::vector<render::MonitorTitleOverlay> titleOverlays;
    // collectTitleOverlays records the specific title reason on failure.
    if (sawTitle && !collectTitleOverlays(timelineFrame, baseTrackIndex, titleOverlays)) {
        return false;
    }
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (!audioTrackAudible(editSession_.sequence(), track)) continue;
        for (const core::Clip& clip : track.clips) {
            if (clip.timeline.contains(timelineFrame) &&
                (!playbackClip->linkId || clip.linkId == playbackClip->linkId)) {
                audioEnabled = true;
                audioPlaybackClip = &clip;
            }
        }
    }
    const core::Frame playbackLocal = timelineFrame - playbackClip->timeline.start;
    // As with titles, a transition with no adjacent outgoing clip on the same
    // track is a no-op in the compositor, so it must not cost resolution here.
    if (playbackClip->videoTransitionInFrames > 0 && playbackLocal >= 0 &&
        playbackLocal < playbackClip->videoTransitionInFrames &&
        playbackTrack != nullptr &&
        std::ranges::any_of(playbackTrack->clips,
                            [playbackClip](const core::Clip& other) {
                                return other.id != playbackClip->id &&
                                       other.timeline.end() ==
                                           playbackClip->timeline.start;
                            })) {
        setPreviewPathReason(PreviewPathReason::BaseTransition);
        return false;
    }
    if (!playbackClip->speedKeyframes.empty()) {
        setPreviewPathReason(PreviewPathReason::BaseSpeedKeyframes);
        return false;
    }
    const auto asset = std::ranges::find_if(assets_, [playbackClip](const ProjectAsset& candidate) {
        return candidate.id == playbackClip->assetId;
    });
    if (asset == assets_.end() || !QFileInfo::exists(asset->path)) {
        setPreviewPathReason(PreviewPathReason::BaseMediaMissing);
        return false;
    }
    const QString proxyPath = asset->metadata.value(QStringLiteral("proxy_cache")).toString();
    const QString playbackPath = QFileInfo::exists(proxyPath) ? proxyPath : asset->path;

    ensureMediaPlayer();

    mediaPlayer_->stop();
    mediaPlayer_->setSource(QUrl::fromLocalFile(playbackPath));
    mediaAudioOutput_->setMuted(!audioEnabled);
    const core::Clip& gainClip = audioPlaybackClip == nullptr ? *playbackClip : *audioPlaybackClip;
    mediaAudioOutput_->setVolume(static_cast<float>(
        std::clamp(std::pow(10.0, gainAt(gainClip,
            timelineFrame - gainClip.timeline.start) / 20.0), 0.0, 1.0)));
    mediaPlayer_->setPlaybackRate(playbackClip->playbackRate);
    if (programMonitor_ != nullptr) {
        const core::MotionKeyframe motion = motionAt(*playbackClip, playbackLocal);
        programMonitor_->setFrameOpacity(motion.opacity *
                                         clipEnvelope(*playbackClip, timelineFrame));
        programMonitor_->setFrameTransform(
            motion.positionX, motion.positionY, motion.scaleX, motion.scaleY,
            motion.rotationDegrees, motion.anchorX, motion.anchorY);
        programMonitor_->setFrameCrop(playbackClip->cropLeft, playbackClip->cropRight,
                                      playbackClip->cropTop, playbackClip->cropBottom);
        programMonitor_->setFrameMask(
            static_cast<int>(playbackClip->maskShape), playbackClip->maskCenterX,
            playbackClip->maskCenterY, playbackClip->maskWidth, playbackClip->maskHeight,
            playbackClip->maskFeather, playbackClip->maskInverted);
        applyClipEffects(programMonitor_, *playbackClip, timelineFrame);
        programMonitor_->setTitleOverlays(std::move(titleOverlays));
    }
    const double framesPerSecond = editSession_.sequence().frameRate().framesPerSecond();
    const core::Frame sourceFrame = playbackClip->sourceStart +
        sourceOffsetAt(*playbackClip, timelineFrame - playbackClip->timeline.start);
    mediaPlayer_->setPosition(static_cast<qint64>(
        std::llround(static_cast<double>(sourceFrame) * 1000.0 / framesPerSecond)));
    mediaPlaybackClip_ = playbackClip->id;
    setPreviewPathReason(PreviewPathReason::DirectOverlay);
    mediaPlayer_->play();
    return true;
}

void MainWindow::startPlaybackClock() {
    if (!playbackRequested_ || transportTimer_ == nullptr) {
        return;
    }
    const double framesPerSecond = editSession_.sequence().frameRate().framesPerSecond();
    const int intervalMilliseconds =
        std::max(1, static_cast<int>(std::lround(1000.0 / framesPerSecond)));
    playbackClockFrame_ = timeline_ != nullptr ? timeline_->playheadFrame() : 0;
    playbackClockTimer_.start();
    transportTimer_->start(intervalMilliseconds);
    if (playPauseButton_ != nullptr) {
        playPauseButton_->setText(tr("Pause"));
    }
    statusBar()->showMessage(tr("Playing - Space to pause"));
}

void MainWindow::startAudioSinkFromPcm(const QByteArray& pcm, const int sampleRate,
                                       const int channelCount,
                                       const core::Frame chunkStart) {
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channelCount);
    format.setSampleFormat(QAudioFormat::Float);
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull() || !outputDevice.isFormatSupported(format)) {
        startPlaybackClock();
        return;
    }
    // The playhead may have run ahead on the bridge clock while this chunk
    // rendered; trim the stale leading samples so audio stays in sync.
    QByteArray sinkData = pcm;
    core::Frame effectiveStart = chunkStart;
    const core::Frame playhead =
        timeline_ == nullptr ? chunkStart : timeline_->playheadFrame();
    if (playhead > chunkStart) {
        const double framesPerSecond =
            editSession_.sequence().frameRate().framesPerSecond();
        const qint64 samplesToSkip = static_cast<qint64>(
            std::llround((playhead - chunkStart) / framesPerSecond * sampleRate));
        const qint64 bytesToSkip =
            samplesToSkip * channelCount * static_cast<qint64>(sizeof(float));
        if (bytesToSkip < sinkData.size()) {
            sinkData = sinkData.mid(static_cast<qsizetype>(bytesToSkip));
            effectiveStart = playhead;
        }
    }
    if (audioSink_ != nullptr) {
        audioSink_->stop();
        audioSink_->deleteLater();
        audioSink_ = nullptr;
    }
    if (audioBuffer_ != nullptr) {
        audioBuffer_->close();
        audioBuffer_->deleteLater();
        audioBuffer_ = nullptr;
    }
    playbackStartFrame_ = effectiveStart;
    // Content end of the untrimmed chunk drives the prefetch target.
    const double contentFramesPerSecond =
        editSession_.sequence().frameRate().framesPerSecond();
    const qint64 totalSamples =
        static_cast<qint64>(pcm.size()) / (channelCount * static_cast<qint64>(sizeof(float)));
    audioChunkEnd_ = chunkStart + static_cast<core::Frame>(std::llround(
        static_cast<double>(totalSamples) / sampleRate * contentFramesPerSecond));
    audioBuffer_ = new QBuffer(this);
    audioBuffer_->setData(sinkData);
    audioBuffer_->open(QIODevice::ReadOnly);
    audioSink_ = new QAudioSink(outputDevice, format, this);
    audioSink_->start(audioBuffer_);
}

bool MainWindow::requestPlaybackAudio(const core::Frame timelineFrame,
                                      const bool prefetch) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    const core::Clip* audibleClip = nullptr;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (!audioTrackAudible(editSession_.sequence(), track)) {
            continue;
        }
        for (const core::Clip& clip : track.clips) {
            if (clip.timeline.contains(timelineFrame)) {
                audibleClip = &clip;
                break;
            }
        }
        if (audibleClip != nullptr) {
            break;
        }
    }
    if (audibleClip == nullptr) {
        return false;
    }

    QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
    workerName += QStringLiteral(".exe");
#endif
    const QString workerPath = QCoreApplication::applicationDirPath() + '/' + workerName;
    if (!QFileInfo::exists(workerPath)) {
        return false;
    }

    if (!prefetch) {
        stopPlaybackAudio();
    } else if (audioWorker_ != nullptr) {
        return false;
    }
    const double framesPerSecond = editSession_.sequence().frameRate().framesPerSecond();
    const core::Frame playbackEnd =
        sequenceOutFrame_ > sequenceInFrame_ ? sequenceOutFrame_ : sequenceEndFrame();
    const core::Frame remainingFrames = playbackEnd - timelineFrame;
    if (remainingFrames <= 0) {
        return false;
    }
    const core::Frame previewFrames = std::max<core::Frame>(1, std::min<core::Frame>(
        remainingFrames, static_cast<core::Frame>(std::ceil(framesPerSecond * 2.0))));
    QByteArray manifest;
    int audioOrder = -1;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (track.kind != core::TrackKind::Audio) continue;
        ++audioOrder;
        if (!audioTrackAudible(editSession_.sequence(), track)) continue;
        for (const core::Clip& clip : track.clips) {
            const auto clipAsset = std::ranges::find_if(
                assets_, [&clip](const ProjectAsset& candidate) {
                    return candidate.id == clip.assetId;
                });
            if (clipAsset == assets_.end()) continue;
            const QString proxyPath = clipAsset->metadata
                .value(QStringLiteral("proxy_cache")).toString();
            const QString path = QFileInfo::exists(proxyPath) ? proxyPath : clipAsset->path;
            if (QFileInfo::exists(path)) {
                manifest += timelineManifestLine(track.kind, clip, path, audioOrder);
            }
        }
    }
    if (manifest.isEmpty()) return false;
    if (!prefetch) {
        playbackStartFrame_ = timelineFrame;
    }

    audioWorker_ = new QProcess(this);
    QProcess* worker = audioWorker_;
    worker->setProcessChannelMode(QProcess::SeparateChannels);
    connect(worker, &QProcess::started, this, [worker, manifest] {
        worker->write(manifest);
        worker->closeWriteChannel();
    });
    connect(worker, &QProcess::errorOccurred, this,
            [this, worker](const QProcess::ProcessError processError) {
                if (processError == QProcess::FailedToStart && audioWorker_ == worker) {
                    worker->deleteLater();
                    audioWorker_ = nullptr;
                    startPlaybackClock();
                }
            });
    connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, worker, timelineFrame, prefetch, previewFrames](
                const int exitCode, const QProcess::ExitStatus exitStatus) {
                const QByteArray output = worker->readAllStandardOutput();
                const QString errorOutput = QString::fromUtf8(worker->readAllStandardError());
                if (audioWorker_ == worker) {
                    audioWorker_ = nullptr;
                }
                worker->deleteLater();
                if (!playbackRequested_) {
                    return;
                }
                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    if (!prefetch) {
                        statusBar()->showMessage(
                            tr("Audio preview unavailable: %1").arg(errorOutput.trimmed()),
                            5000);
                        startPlaybackClock();
                    }
                    return;
                }
                const auto audio = parseAudioBuffer(output);
                if (!audio.has_value()) {
                    if (!prefetch) {
                        statusBar()->showMessage(
                            tr("Audio worker returned invalid samples"), 5000);
                        startPlaybackClock();
                    }
                    return;
                }
                if (prefetch) {
                    // Hold the chunk until the playing one drains.
                    pendingAudioPcm_ = audio->pcm;
                    pendingAudioStart_ = timelineFrame;
                    pendingAudioSampleRate_ = static_cast<int>(audio->sampleRate);
                    pendingAudioChannels_ = static_cast<int>(audio->channelCount);
                    return;
                }
                if (timeline_ != nullptr &&
                    timeline_->playheadFrame() >= timelineFrame + previewFrames) {
                    // The bridge clock outran this whole chunk; fetch a
                    // current one instead of playing stale audio.
                    if (!requestPlaybackAudio(timeline_->playheadFrame())) {
                        startPlaybackClock();
                    }
                    return;
                }
                startAudioSinkFromPcm(audio->pcm, static_cast<int>(audio->sampleRate),
                                      static_cast<int>(audio->channelCount),
                                      timelineFrame);
            });
    const core::FrameRate frameRate = editSession_.sequence().frameRate();
    worker->start(workerPath,
                  {QStringLiteral("timeline-audio"), QString::number(timelineFrame),
                   QString::number(frameRate.numerator), QString::number(frameRate.denominator),
                   QStringLiteral("1280"), QStringLiteral("720"),
                   QString::number(std::max<core::Frame>(1, sequenceEndFrame())),
                   QString::number(previewFrames)});
    return true;
#else
    Q_UNUSED(timelineFrame);
    return false;
#endif
}

void MainWindow::stopPlaybackAudio() {
    pendingAudioPcm_.clear();
    pendingAudioStart_ = -1;
    audioChunkEnd_ = -1;
    if (audioWorker_ != nullptr) {
        disconnect(audioWorker_, nullptr, this, nullptr);
        audioWorker_->kill();
        audioWorker_->deleteLater();
        audioWorker_ = nullptr;
    }
    if (audioSink_ != nullptr) {
        audioSink_->stop();
        audioSink_->deleteLater();
        audioSink_ = nullptr;
    }
    if (audioBuffer_ != nullptr) {
        audioBuffer_->close();
        audioBuffer_->deleteLater();
        audioBuffer_ = nullptr;
    }
}

QByteArray MainWindow::buildTimelineVideoManifest() const {
    const std::uint64_t revision = editSession_.sequence().revision();
    if (!liveDragClip_ && manifestCacheRevision_ == revision) {
        return manifestCache_;
    }
    QByteArray manifest;
    int videoOrder = -1;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (track.kind != core::TrackKind::Video) continue;
        ++videoOrder;
        if (!track.enabled) continue;
        for (const core::Clip& clip : track.clips) {
            const auto asset = std::ranges::find_if(
                assets_, [&clip](const ProjectAsset& candidate) {
                    return candidate.id == clip.assetId;
                });
            if (asset == assets_.end()) continue;
            if (isTitleAsset(asset->metadata) &&
                !ensureTitleImage(asset->path, asset->metadata)) {
                continue;
            }
            const QString proxyPath =
                asset->metadata.value(QStringLiteral("proxy_cache")).toString();
            const QString path = QFileInfo::exists(proxyPath) ? proxyPath : asset->path;
            if (!QFileInfo::exists(path)) continue;
            if (liveDragClip_ && clip.id == liveDragClip_ &&
                clip.motionKeyframes.empty()) {
                // A monitor drag in progress: render this layer at the
                // transient transform so it follows the pointer live.
                core::Clip dragged = clip;
                dragged.positionX = liveDragTransform_.positionX;
                dragged.positionY = liveDragTransform_.positionY;
                dragged.scaleX = liveDragTransform_.scaleX;
                dragged.scaleY = liveDragTransform_.scaleY;
                dragged.rotationDegrees = liveDragTransform_.rotationDegrees;
                dragged.anchorX = liveDragTransform_.anchorX;
                dragged.anchorY = liveDragTransform_.anchorY;
                manifest += timelineManifestLine(track.kind, dragged, path, videoOrder);
                continue;
            }
            manifest += timelineManifestLine(track.kind, clip, path, videoOrder);
        }
    }
    if (!liveDragClip_) {
        manifestCache_ = manifest;
        manifestCacheRevision_ = revision;
    } else {
        // Drag manifests carry transient values; never serve them from cache.
        manifestCacheRevision_ = ~0ULL;
    }
    return manifest;
}

void MainWindow::requestTimelineFrame(const core::Frame timelineFrame) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    if (frameServerBusy_) {
        pendingTimelineFrame_ = timelineFrame;
        return;
    }
    QByteArray manifest = buildTimelineVideoManifest();
    if (manifest.isEmpty()) {
        programMonitor_->clearFrame();
        return;
    }
    if (frameServer_ == nullptr || frameServer_->state() == QProcess::NotRunning) {
        if (frameServer_ != nullptr) {
            frameServer_->deleteLater();
            frameServer_ = nullptr;
        }
        QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
        workerName += QStringLiteral(".exe");
#endif
        const QString workerPath =
            QCoreApplication::applicationDirPath() + '/' + workerName;
        if (!QFileInfo::exists(workerPath)) return;
        frameServer_ = new QProcess(this);
        QProcess* server = frameServer_;
        server->setProcessChannelMode(QProcess::SeparateChannels);
        connect(server, &QProcess::readyReadStandardOutput, this, [this, server] {
            if (frameServer_ != server) return;
            frameServerBuffer_ += server->readAllStandardOutput();
            handleFrameServerResponse();
        });
        connect(server, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this, [this, server](int, QProcess::ExitStatus) {
                    if (frameServer_ != server) return;
                    frameServer_ = nullptr;
                    server->deleteLater();
                    frameServerBusy_ = false;
                    frameServerBuffer_.clear();
                });
        connect(server, &QProcess::errorOccurred, this,
                [this, server](const QProcess::ProcessError processError) {
                    if (processError == QProcess::FailedToStart &&
                        frameServer_ == server) {
                        frameServer_ = nullptr;
                        server->deleteLater();
                        frameServerBusy_ = false;
                        statusBar()->showMessage(
                            tr("Could not start the timeline preview worker"), 4000);
                    }
                });
        server->start(workerPath, {QStringLiteral("timeline-frame-serve")});
    }
    if (!manifest.endsWith('\n')) {
        manifest += '\n';
    }
    const int manifestLines = static_cast<int>(manifest.count('\n'));
    const core::Frame requestedFrame = std::max<core::Frame>(0, timelineFrame);
    const core::FrameRate frameRate = editSession_.sequence().frameRate();
    int divisor = 1;
    if (playbackRequested_) {
        divisor = playbackResolutionDivisor_ == 0 ? 2 : playbackResolutionDivisor_;
    }
    QByteArray request = "REQ ";
    request += QByteArray::number(static_cast<qlonglong>(requestedFrame)) + ' ';
    request += QByteArray::number(frameRate.numerator) + ' ';
    request += QByteArray::number(frameRate.denominator) + ' ';
    request += QByteArray::number(1280 / divisor) + ' ';
    request += QByteArray::number(720 / divisor) + ' ';
    request += QByteArray::number(
        static_cast<qlonglong>(std::max<core::Frame>(1, sequenceEndFrame()))) + ' ';
    request += QByteArray::number(manifestLines) + '\n';
    request += manifest;
    frameServerFrame_ = requestedFrame;
    frameServerRevision_ = editSession_.sequence().revision();
    frameServerBusy_ = true;
    frameServerDiscard_ = false;
    frameServerBuffer_.clear();
    pendingTimelineFrame_ = -1;
    frameServer_->write(request);
#else
    Q_UNUSED(timelineFrame);
#endif
}

void MainWindow::handleFrameServerResponse() {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    const auto readU32 = [this](const int offset) {
        return static_cast<quint32>(static_cast<uchar>(frameServerBuffer_[offset])) |
               (static_cast<quint32>(static_cast<uchar>(frameServerBuffer_[offset + 1])) << 8U) |
               (static_cast<quint32>(static_cast<uchar>(frameServerBuffer_[offset + 2])) << 16U) |
               (static_cast<quint32>(static_cast<uchar>(frameServerBuffer_[offset + 3])) << 24U);
    };
    if (!frameServerBusy_ || frameServerBuffer_.size() < 4) {
        return;
    }
    QByteArray payload;
    QString errorText;
    if (frameServerBuffer_.startsWith("VXF1")) {
        if (frameServerBuffer_.size() < 28) {
            return;
        }
        const quint64 rgbaSize =
            static_cast<quint64>(readU32(20)) |
            (static_cast<quint64>(readU32(24)) << 32U);
        const qint64 total = 28 + static_cast<qint64>(rgbaSize);
        if (rgbaSize > 512ULL * 1024ULL * 1024ULL) {
            frameServerBuffer_.clear();
            frameServerBusy_ = false;
            return;
        }
        if (frameServerBuffer_.size() < total) {
            return;
        }
        payload = frameServerBuffer_.left(static_cast<int>(total));
        frameServerBuffer_.remove(0, static_cast<int>(total));
    } else if (frameServerBuffer_.startsWith("VXE1")) {
        if (frameServerBuffer_.size() < 8) {
            return;
        }
        const quint32 length = readU32(4);
        const qint64 total = 8 + static_cast<qint64>(length);
        if (frameServerBuffer_.size() < total) {
            return;
        }
        errorText = QString::fromUtf8(frameServerBuffer_.mid(8, static_cast<int>(length)));
        frameServerBuffer_.remove(0, static_cast<int>(total));
    } else {
        // Unknown data on the pipe: resynchronize by dropping it.
        frameServerBuffer_.clear();
        frameServerBusy_ = false;
        return;
    }
    frameServerBusy_ = false;
    if (!frameServerDiscard_) {
        if (!payload.isEmpty()) {
            if (const auto image = parseVideoFrame(payload); image.has_value()) {
                programMonitor_->setFrameOpacity(1.0);
                programMonitor_->setFrameTransform(0.0, 0.0, 1.0, 1.0, 0.0, 0.5, 0.5);
                programMonitor_->setFrameCrop(0.0, 0.0, 0.0, 0.0);
                programMonitor_->setFrameMask(0, 0.5, 0.5, 1.0, 1.0, 0.0, false);
                programMonitor_->setVideoEffects(0.0, 0.0, 0.0, 0.0, 0.0);
                programMonitor_->setFrame(*image);
                // Only a frame rendered from the current model may re-baseline
                // the edit overlay; stale frames keep the post-commit delta so
                // the image does not jump back.
                if (editSession_.sequence().revision() == frameServerRevision_) {
                    monitorAwaitingRender_ = false;
                }
                updateMonitorEditTarget();
            }
        } else if (!errorText.isEmpty()) {
            statusBar()->showMessage(tr("Timeline preview: %1").arg(errorText), 3500);
        }
    }
    const core::Frame pending = pendingTimelineFrame_;
    pendingTimelineFrame_ = -1;
    if (pending >= 0 && (pending != frameServerFrame_ ||
                         editSession_.sequence().revision() != frameServerRevision_)) {
        requestTimelineFrame(pending);
    }
#endif
}

void MainWindow::requestTrimTwoUp(const core::Frame outgoingFrame,
                                  const core::Frame incomingFrame) {
    trimTwoUpOutgoing_ = outgoingFrame;
    trimTwoUpIncoming_ = incomingFrame;
    trimTwoUpActive_ = true;
    // A running render chain picks up the latest pair when it finishes.
    if (trimTwoUpWorker_ == nullptr && trimTwoUpPhase_ == 0) {
        startTrimTwoUpRender();
    }
}

void MainWindow::startTrimTwoUpRender() {
    if (!trimTwoUpActive_ || trimTwoUpWorker_ != nullptr) {
        return;
    }
    trimTwoUpRenderedOutgoing_ = trimTwoUpOutgoing_;
    trimTwoUpRenderedIncoming_ = trimTwoUpIncoming_;
    trimTwoUpOutgoingImage_ = {};
    startTrimTwoUpPhase(trimTwoUpRenderedOutgoing_, 1);
}

void MainWindow::startTrimTwoUpPhase(const core::Frame frame, const int phase) {
#if defined(VIDEX_HAS_MEDIA_WORKER)
    QString workerName = QStringLiteral("videx-media-worker");
#if defined(Q_OS_WIN)
    workerName += QStringLiteral(".exe");
#endif
    const QString workerPath = QCoreApplication::applicationDirPath() + '/' + workerName;
    if (!QFileInfo::exists(workerPath)) {
        trimTwoUpPhase_ = 0;
        return;
    }
    const QByteArray manifest = buildTimelineVideoManifest();
    if (manifest.isEmpty()) {
        trimTwoUpPhase_ = 0;
        return;
    }
    trimTwoUpPhase_ = phase;
    trimTwoUpWorker_ = new QProcess(this);
    QProcess* worker = trimTwoUpWorker_;
    worker->setProcessChannelMode(QProcess::SeparateChannels);
    connect(worker, &QProcess::started, this, [worker, manifest] {
        worker->write(manifest);
        worker->closeWriteChannel();
    });
    // finished() never fires on FailedToStart; reset the phase machine so the
    // two-up preview can retry instead of wedging for the session.
    connect(worker, &QProcess::errorOccurred, this,
            [this, worker](const QProcess::ProcessError processError) {
                if (processError == QProcess::FailedToStart &&
                    trimTwoUpWorker_ == worker) {
                    trimTwoUpWorker_ = nullptr;
                    trimTwoUpPhase_ = 0;
                    worker->deleteLater();
                }
            });
    connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, worker, phase](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const QByteArray output = worker->readAllStandardOutput();
                if (trimTwoUpWorker_ == worker) {
                    trimTwoUpWorker_ = nullptr;
                }
                worker->deleteLater();
                if (!trimTwoUpActive_) {
                    trimTwoUpPhase_ = 0;
                    return;
                }
                std::optional<QImage> image;
                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    image = parseVideoFrame(output);
                }
                if (!image.has_value()) {
                    trimTwoUpPhase_ = 0;
                    return;
                }
                if (phase == 1) {
                    trimTwoUpOutgoingImage_ = *image;
                    startTrimTwoUpPhase(trimTwoUpRenderedIncoming_, 2);
                    return;
                }
                if (!trimTwoUpOutgoingImage_.isNull() && programMonitor_ != nullptr) {
                    const QImage& left = trimTwoUpOutgoingImage_;
                    const QImage& right = *image;
                    QImage composite(left.width() + right.width() + 4,
                                     std::max(left.height(), right.height()),
                                     QImage::Format_ARGB32);
                    composite.fill(QColor(12, 13, 16));
                    QPainter painter(&composite);
                    painter.drawImage(0, (composite.height() - left.height()) / 2, left);
                    painter.drawImage(left.width() + 4,
                                      (composite.height() - right.height()) / 2, right);
                    painter.setPen(QColor(240, 245, 255));
                    painter.drawText(QRectF(6.0, 4.0, 90.0, 18.0), Qt::AlignLeft,
                                     tr("OUT"));
                    painter.drawText(QRectF(left.width() + 10.0, 4.0, 90.0, 18.0),
                                     Qt::AlignLeft, tr("IN"));
                    painter.end();
                    programMonitor_->setFrameOpacity(1.0);
                    programMonitor_->setFrameTransform(0.0, 0.0, 1.0, 1.0, 0.0, 0.5, 0.5);
                    programMonitor_->setFrameCrop(0.0, 0.0, 0.0, 0.0);
                    programMonitor_->setFrameMask(0, 0.5, 0.5, 1.0, 1.0, 0.0, false);
                    programMonitor_->setVideoEffects(0.0, 0.0, 0.0, 0.0, 0.0);
                    programMonitor_->setFrame(composite);
                }
                trimTwoUpPhase_ = 0;
                if (trimTwoUpOutgoing_ != trimTwoUpRenderedOutgoing_ ||
                    trimTwoUpIncoming_ != trimTwoUpRenderedIncoming_) {
                    startTrimTwoUpRender();
                }
            });
    const core::FrameRate frameRate = editSession_.sequence().frameRate();
    worker->start(workerPath,
                  {QStringLiteral("timeline-frame"),
                   QString::number(std::max<core::Frame>(0, frame)),
                   QString::number(frameRate.numerator),
                   QString::number(frameRate.denominator), QString::number(640),
                   QString::number(360),
                   QString::number(std::max<core::Frame>(1, sequenceEndFrame()))});
#else
    Q_UNUSED(frame);
    Q_UNUSED(phase);
#endif
}

void MainWindow::updateProgramFrame(const core::Frame timelineFrame) {
    if (programMonitor_ != nullptr) {
        // Composited frames have any titles baked in already.
        programMonitor_->setTitleOverlays({});
    }
    bool hasVisibleClip = false;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (track.kind != core::TrackKind::Video || !track.enabled) {
            continue;
        }
        for (const core::Clip& clip : track.clips) {
            if (clip.timeline.contains(timelineFrame)) {
                hasVisibleClip = true;
                break;
            }
        }
        if (hasVisibleClip) break;
    }

    if (!hasVisibleClip) {
        pendingTimelineFrame_ = -1;
        // An in-flight server response must not repaint the cleared monitor.
        frameServerDiscard_ = true;
        if (programMonitor_ != nullptr) {
            programMonitor_->setFrameOpacity(1.0);
            programMonitor_->clearFrame();
        }
        return;
    }

    requestTimelineFrame(timelineFrame);
}

bool MainWindow::addTitleClip(const QString& text, const QString& fontFamily,
                              const double positionX, const double positionY,
                              const double fontSize, const std::uint32_t textColor,
                              const std::uint32_t backgroundColor, const bool bold,
                              const bool italic, const core::Frame start,
                              const core::Frame duration) {
    if (timeline_ == nullptr || duration <= 0) {
        return false;
    }
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/titles");
    QDir().mkpath(directory);
    const QString path = directory + '/' +
                         QUuid::createUuid().toString(QUuid::WithoutBraces) +
                         QStringLiteral(".vxtitle");
    const TitleStyle style{.text = text,
                           .fontFamily = fontFamily,
                           .positionX = positionX,
                           .positionY = positionY,
                           .fontSize = fontSize,
                           .textColor = textColor,
                           .backgroundColor = backgroundColor,
                           .bold = bold,
                           .italic = italic};
    if (!writeTitleImage(path, style)) {
        statusBar()->showMessage(tr("Could not render the title image"), 4000);
        return false;
    }
    // Stack onto the front-most video track with free space; make one if none.
    core::TrackId target;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (track.kind != core::TrackKind::Video || track.locked) {
            continue;
        }
        const bool blocked = std::ranges::any_of(
            track.clips, [start, duration](const core::Clip& clip) {
                return clip.timeline.start < start + duration &&
                       clip.timeline.end() > start;
            });
        if (!blocked) {
            target = track.id;
        }
    }
    std::vector<core::EditCommand> commands;
    if (!target) {
        // A null overwrite target binds to this new track inside the same
        // transaction: create+place is one undo step and stays atomic.
        commands.push_back(core::AddTrackCommand{core::TrackKind::Video});
    }
    const core::AssetId assetId{nextAssetId_++};
    assets_.push_back(ProjectAsset{
        .id = assetId,
        .path = path,
        .metadata = titleAssetMetadata(style),
    });
    commands.push_back(core::OverwriteClipCommand{target, assetId, start, 0, duration});
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = "Add title",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        assets_.pop_back();
        --nextAssetId_;
        QFile::remove(path);
        statusBar()->showMessage(
            tr("Could not add the title: %1").arg(QString::fromStdString(result.message)),
            4000);
        return false;
    }
    setDirty(true);
    rebuildProjectTree();
    refreshEditor();
    if (result.primaryClip) {
        timeline_->setSelectedClipIds({result.primaryClip});
    }
    updateTextPanel();
    updateProgramFrame(timeline_->playheadFrame());
    return true;
}

void MainWindow::createTitleClipAtPlayhead() {
    if (timeline_ == nullptr) {
        return;
    }
    const core::Frame duration = std::max<core::Frame>(
        1, static_cast<core::Frame>(std::llround(
               editSession_.sequence().frameRate().framesPerSecond() * 4.0)));
    addTitleClip(tr("New Title"), {}, 0.5, 0.5, 64.0, 0xFFFFFFFFU, 0x00000000U, true,
                 false, timeline_->playheadFrame(), duration);
}

core::CaptionId MainWindow::captionAtPlayhead() const {
    if (timeline_ == nullptr) {
        return {};
    }
    const core::Frame frame = timeline_->playheadFrame();
    for (const core::Caption& caption : editSession_.sequence().captions()) {
        if (caption.timeline.contains(frame)) {
            return caption.id;
        }
    }
    return {};
}

void MainWindow::createTextClipAtPlayhead() {
    if (timeline_ == nullptr) {
        return;
    }
    const core::Frame duration = std::max<core::Frame>(
        1, static_cast<core::Frame>(std::llround(
               editSession_.sequence().frameRate().framesPerSecond() * 3.0)));
    const core::EditResult result = editSession_.apply({
        .baseRevision = editSession_.sequence().revision(),
        .label = "Add text",
        .command = core::AddCaptionCommand{{timeline_->playheadFrame(), duration},
                                            tr("New Text").toStdString()},
    });
    if (result.succeeded()) {
        setDirty(true);
        refreshEditor();
        updateTextPanel();
        updateCaptionOverlay(timeline_->playheadFrame());
        if (textEditField_ != nullptr) {
            textEditField_->setFocus(Qt::OtherFocusReason);
            textEditField_->selectAll();
        }
    }
}

void MainWindow::updateTextPanel() {
    if (textStatusLabel_ == nullptr || textEditField_ == nullptr) {
        return;
    }
    // Title clips take priority: when the inspected clip references a title
    // asset, the panel edits that title instead of the subtitle lane.
    const core::ClipId previousTitle = editingTitleClip_;
    editingTitleClip_ = {};
    if (const core::Clip* selectedClip = editSession_.sequence().findClip(inspectedClip_);
        selectedClip != nullptr) {
        const auto asset = std::ranges::find_if(
            assets_, [selectedClip](const ProjectAsset& candidate) {
                return candidate.id == selectedClip->assetId;
            });
        if (asset != assets_.end() && isTitleAsset(asset->metadata)) {
            editingTitleClip_ = inspectedClip_;
            editingCaption_ = {};
            for (QWidget* widget : std::initializer_list<QWidget*>{
                     textEditField_, textFontCombo_, textSizeSpin_, textPosXSpin_,
                     textPosYSpin_, textColorButton_, textBackgroundButton_,
                     textBoldCheck_, textItalicCheck_}) {
                if (widget != nullptr) {
                    widget->setEnabled(true);
                }
            }
            textStatusLabel_->setText(tr("Editing title clip on a video track"));
            const bool typingHere =
                textEditField_->hasFocus() && previousTitle == inspectedClip_;
            const TitleStyle style = titleStyleFromMetadata(asset->metadata);
            updatingTextPanel_ = true;
            if (!typingHere) {
                textEditField_->setPlainText(style.text);
            }
            textSizeSpin_->setValue(style.fontSize);
            textPosXSpin_->setValue(style.positionX * 100.0);
            textPosYSpin_->setValue(style.positionY * 100.0);
            textBoldCheck_->setChecked(style.bold);
            textItalicCheck_->setChecked(style.italic);
            textColorValue_ = style.textColor;
            textBackgroundValue_ = style.backgroundColor;
            if (textFontCombo_ != nullptr && !style.fontFamily.isEmpty()) {
                const QSignalBlocker blocker(textFontCombo_);
                textFontCombo_->setCurrentFont(QFont(style.fontFamily));
            }
            updatingTextPanel_ = false;
            return;
        }
    }
    const core::CaptionId captionId = captionAtPlayhead();
    const core::CaptionId previousEditing = editingCaption_;
    editingCaption_ = captionId;
    const core::Caption* caption = nullptr;
    for (const core::Caption& candidate : editSession_.sequence().captions()) {
        if (candidate.id == captionId) {
            caption = &candidate;
            break;
        }
    }
    const bool enabled = caption != nullptr;
    for (QWidget* widget : std::initializer_list<QWidget*>{
             textEditField_, textFontCombo_, textSizeSpin_, textPosXSpin_, textPosYSpin_,
             textColorButton_, textBackgroundButton_, textBoldCheck_, textItalicCheck_}) {
        if (widget != nullptr) {
            widget->setEnabled(enabled);
        }
    }
    if (!enabled) {
        textStatusLabel_->setText(tr("No text at the playhead. Use New Text or press T."));
        return;
    }
    textStatusLabel_->setText(tr("Editing text at %1-%2")
                                  .arg(caption->timeline.start)
                                  .arg(caption->timeline.end()));
    // Never clobber the field while the user is typing into this caption; a
    // refresh mid-edit would reset the cursor and drop pending keystrokes.
    const bool typingHere =
        textEditField_->hasFocus() && previousEditing == captionId;
    updatingTextPanel_ = true;
    if (!typingHere) {
        textEditField_->setPlainText(QString::fromStdString(caption->text));
    }
    textSizeSpin_->setValue(caption->fontSize);
    textPosXSpin_->setValue(caption->positionX * 100.0);
    textPosYSpin_->setValue(caption->positionY * 100.0);
    textBoldCheck_->setChecked(caption->bold);
    textItalicCheck_->setChecked(caption->italic);
    textColorValue_ = caption->textColor;
    textBackgroundValue_ = caption->backgroundColor;
    updatingTextPanel_ = false;
}

void MainWindow::applyTextPanel() {
    if (updatingTextPanel_) {
        return;
    }
    if (editingTitleClip_) {
        const core::Clip* clip = editSession_.sequence().findClip(editingTitleClip_);
        if (clip == nullptr) {
            editingTitleClip_ = {};
            return;
        }
        const auto asset = std::ranges::find_if(
            assets_, [clip](const ProjectAsset& candidate) {
                return candidate.id == clip->assetId;
            });
        if (asset == assets_.end()) {
            return;
        }
        const QString text = textEditField_->toPlainText();
        if (text.trimmed().isEmpty()) {
            return;
        }
        const TitleStyle style{
            .text = text,
            .fontFamily = textFontCombo_ != nullptr
                              ? textFontCombo_->currentFont().family()
                              : QString{},
            .positionX = std::clamp(textPosXSpin_->value() / 100.0, 0.0, 1.0),
            .positionY = std::clamp(textPosYSpin_->value() / 100.0, 0.0, 1.0),
            .fontSize = std::clamp(textSizeSpin_->value(), 8.0, 300.0),
            .textColor = textColorValue_,
            .backgroundColor = textBackgroundValue_,
            .bold = textBoldCheck_->isChecked(),
            .italic = textItalicCheck_->isChecked()};
        if (!writeTitleImage(asset->path, style)) {
            statusBar()->showMessage(tr("Could not render the title image"), 4000);
            return;
        }
        writeTitleStyleToMetadata(style, asset->metadata);
        setDirty(true);
        updateProgramFrame(timeline_->playheadFrame());
        return;
    }
    if (!editingCaption_) {
        return;
    }
    const core::Caption* caption = nullptr;
    for (const core::Caption& candidate : editSession_.sequence().captions()) {
        if (candidate.id == editingCaption_) {
            caption = &candidate;
            break;
        }
    }
    if (caption == nullptr) {
        return;
    }
    const QString text = textEditField_->toPlainText();
    if (text.trimmed().isEmpty()) {
        // Keep the previous content instead of forcing placeholder text; the
        // user is mid-edit and the empty state must not become an edit.
        return;
    }
    const core::EditResult result = editSession_.apply({
        .baseRevision = editSession_.sequence().revision(),
        .label = "Edit text",
        .command = core::SetCaptionCommand{
            editingCaption_, caption->timeline, text.toStdString(),
            std::clamp(textPosXSpin_->value() / 100.0, 0.0, 1.0),
            std::clamp(textPosYSpin_->value() / 100.0, 0.0, 1.0),
            std::clamp(textSizeSpin_->value(), 8.0, 300.0), textColorValue_,
            textBackgroundValue_, textBoldCheck_->isChecked(),
            textItalicCheck_->isChecked()},
    });
    if (result.succeeded()) {
        setDirty(true);
        if (timeline_ != nullptr) {
            timeline_->setSequence(&editSession_.sequence());
        }
        updateCaptionOverlay(timeline_->playheadFrame());
    }
}

void MainWindow::updateCaptionOverlay(const core::Frame frame) {
    if (programMonitor_ == nullptr) {
        return;
    }
    QStringList lines;
    const core::Caption* styledCaption = nullptr;
    for (const core::Caption& caption : editSession_.sequence().captions()) {
        if (caption.timeline.contains(frame)) {
            if (styledCaption == nullptr) styledCaption = &caption;
            lines.push_back(QString::fromStdString(caption.text));
        }
    }
    if (styledCaption != nullptr) {
        programMonitor_->setOverlayStyle(
            styledCaption->positionX, styledCaption->positionY, styledCaption->fontSize,
            styledCaption->textColor, styledCaption->backgroundColor,
            styledCaption->bold, styledCaption->italic);
    }
    programMonitor_->setOverlayText(lines.join(QLatin1Char('\n')));
}

void MainWindow::writeCaptionSidecar(const QString& mp4Path) {
    if (editSession_.sequence().captions().empty()) {
        return;
    }
    const QFileInfo mp4Info(mp4Path);
    const QString srtPath = mp4Info.dir().filePath(mp4Info.completeBaseName() +
                                                   QStringLiteral(".srt"));
    QSaveFile file(srtPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    const double framesPerSecond = editSession_.sequence().frameRate().framesPerSecond();
    auto timestamp = [framesPerSecond](const core::Frame frame) {
        const qint64 milliseconds = static_cast<qint64>(std::llround(
            static_cast<double>(frame) * 1000.0 / framesPerSecond));
        const qint64 hours = milliseconds / 3'600'000;
        const qint64 minutes = (milliseconds / 60'000) % 60;
        const qint64 seconds = (milliseconds / 1000) % 60;
        const qint64 millis = milliseconds % 1000;
        return QStringLiteral("%1:%2:%3,%4")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'))
            .arg(millis, 3, 10, QLatin1Char('0'));
    };
    int index = 1;
    for (const core::Caption& caption : editSession_.sequence().captions()) {
        const core::Frame exportEnd = exportStartFrame_ + exportDurationFrames_;
        if (caption.timeline.end() <= exportStartFrame_ ||
            caption.timeline.start >= exportEnd) continue;
        const core::Frame captionStart =
            std::max(caption.timeline.start, exportStartFrame_) - exportStartFrame_;
        const core::Frame captionEnd =
            std::min(caption.timeline.end(), exportEnd) - exportStartFrame_;
        QString text = QString::fromStdString(caption.text);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        static const QRegularExpression blankLines(QStringLiteral("\n{2,}"));
        text.replace(blankLines, QStringLiteral("\n"));
        while (text.startsWith(QLatin1Char('\n'))) text.removeFirst();
        while (text.endsWith(QLatin1Char('\n'))) text.removeLast();
        if (text.isEmpty()) continue;
        const QString entry = QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                                  .arg(index++)
                                  .arg(timestamp(captionStart))
                                  .arg(timestamp(captionEnd))
                                  .arg(text);
        file.write(entry.toUtf8());
    }
    static_cast<void>(file.commit());
}

void MainWindow::openProject() {
    if (!confirmDiscardChanges()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), {}, tr("Videx projects (*.videx);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QString loadPath = path;
    const QString autosavePath = path + QStringLiteral(".autosave");
    const QFileInfo autosaveInfo(autosavePath);
    if (autosaveInfo.exists() &&
        autosaveInfo.lastModified() > QFileInfo(path).lastModified() &&
        QMessageBox::question(this, tr("Recover Autosave"),
                              tr("A newer autosave exists. Recover it?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) ==
            QMessageBox::Yes) {
        loadPath = autosavePath;
    }

    QString error;
    auto project = loadProjectFile(loadPath, error);
    if (!project.has_value()) {
        QMessageBox::critical(this, tr("Could Not Open Project"), error);
        return;
    }

    pausePlayback();
    playbackRequested_ = false;
    if (transportTimer_ != nullptr) {
        transportTimer_->stop();
    }
    stopPlaybackAudio();
    if (mediaPlayer_ != nullptr) {
        mediaPlayer_->stop();
    }
    mediaPlaybackClip_ = {};
    if (playPauseButton_ != nullptr) {
        playPauseButton_->setText(tr("Play"));
    }

    editSession_ = core::EditSession(std::move(project->sequence));
    assets_ = std::move(project->assets);
    projectPath_ = path;
    dirty_ = loadPath != path;
    videoTrack_ = {};
    audioTrack_ = {};
    nextAssetId_ = 1;
    nextLinkId_ = 1;
    nextInsertFrame_ = 0;
    sequenceInFrame_ = -1;
    sequenceOutFrame_ = -1;
    currentSourceAsset_ = {};
    sourceInFrame_ = 0;
    sourceOutFrame_ = 0;
    sourceDurationFrames_ = 0;
    inspectedClip_ = {};
    clipClipboard_.clear();
    if (sourceMonitor_ != nullptr) {
        sourceMonitor_->clearFrame();
    }
    if (programMonitor_ != nullptr) {
        programMonitor_->clearFrame();
    }

    for (const ProjectAsset& asset : assets_) {
        if (asset.id.value < std::numeric_limits<std::uint64_t>::max()) {
            nextAssetId_ = std::max(nextAssetId_, asset.id.value + 1U);
        }
    }
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (!videoTrack_ && track.kind == core::TrackKind::Video) {
            videoTrack_ = track.id;
        }
        if (!audioTrack_ && track.kind == core::TrackKind::Audio) {
            audioTrack_ = track.id;
        }
        for (const core::Clip& clip : track.clips) {
            nextInsertFrame_ = std::max(nextInsertFrame_, clip.timeline.end());
            if (clip.linkId && clip.linkId.value < std::numeric_limits<std::uint64_t>::max()) {
                nextLinkId_ = std::max(nextLinkId_, clip.linkId.value + 1U);
            }
        }
    }

    {
        // Legacy projects placed A/V pairs without linkIds; link exact matches
        // (same asset, range, and source start) so moves keep them in sync.
        // Runs before clearHistory, so it never pollutes the undo stack.
        std::vector<const core::Clip*> unlinkedVideo;
        std::vector<const core::Clip*> unlinkedAudio;
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                if (clip.linkId) continue;
                (track.kind == core::TrackKind::Video ? unlinkedVideo : unlinkedAudio)
                    .push_back(&clip);
            }
        }
        std::vector<core::EditCommand> linkCommands;
        QSet<quint64> consumedAudio;
        for (const core::Clip* video : unlinkedVideo) {
            for (const core::Clip* audio : unlinkedAudio) {
                if (consumedAudio.contains(audio->id.value) ||
                    video->assetId != audio->assetId ||
                    video->timeline != audio->timeline ||
                    video->sourceStart != audio->sourceStart) {
                    continue;
                }
                const core::LinkId link{nextLinkId_++};
                linkCommands.push_back(core::SetClipLinkCommand{video->id, link});
                linkCommands.push_back(core::SetClipLinkCommand{audio->id, link});
                consumedAudio.insert(audio->id.value);
                break;
            }
        }
        if (!linkCommands.empty()) {
            const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
                .baseRevision = editSession_.sequence().revision(),
                .label = "Relink audio/video pairs",
                .commands = std::move(linkCommands),
            });
            if (result.succeeded()) {
                dirty_ = true;
            }
        }
    }

    {
        // Titles: drop orphaned title assets (left by undone "Add title"
        // actions in the saved session) and regenerate any missing rasters
        // from their metadata so preview and export keep working.
        std::erase_if(assets_, [this](const ProjectAsset& asset) {
            if (!isTitleAsset(asset.metadata)) {
                return false;
            }
            const bool referenced = std::ranges::any_of(
                editSession_.sequence().tracks(), [&asset](const core::Track& track) {
                    return std::ranges::any_of(
                        track.clips, [&asset](const core::Clip& clip) {
                            return clip.assetId == asset.id;
                        });
                });
            return !referenced;
        });
        int regenerated = 0;
        for (const ProjectAsset& asset : assets_) {
            if (isTitleAsset(asset.metadata) && !QFileInfo::exists(asset.path) &&
                ensureTitleImage(asset.path, asset.metadata)) {
                ++regenerated;
            }
        }
        if (regenerated > 0) {
            statusBar()->showMessage(
                tr("Regenerated %1 missing title image(s)").arg(regenerated), 4000);
        }
    }

    if (!videoTrack_) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Add default video track",
            .command = core::AddTrackCommand{core::TrackKind::Video},
        });
        videoTrack_ = result.primaryTrack;
    }
    if (!audioTrack_) {
        const core::EditResult result = editSession_.apply({
            .baseRevision = editSession_.sequence().revision(),
            .label = "Add default audio track",
            .command = core::AddTrackCommand{core::TrackKind::Audio},
        });
        audioTrack_ = result.primaryTrack;
    }
    editSession_.clearHistory();

    rebuildProjectTree();
    refreshEditor();
    if (timeline_ != nullptr) {
        timeline_->clearWaveforms();
        timeline_->setInOutRange(-1, -1);
    }
    for (const ProjectAsset& asset : assets_) {
        startAssetCacheJobs(asset.id);
    }
    updateWindowTitle();
    statusBar()->showMessage(loadPath == path
                                 ? tr("Opened %1").arg(QFileInfo(path).fileName())
                                 : tr("Recovered autosave for %1").arg(QFileInfo(path).fileName()),
                             5000);
}

bool MainWindow::saveProject() {
    if (projectPath_.isEmpty()) {
        return saveProjectAs();
    }

    QString error;
    const ProjectData project{.sequence = editSession_.sequence(), .assets = assets_};
    if (!saveProjectFile(projectPath_, project, error)) {
        QMessageBox::critical(this, tr("Could Not Save Project"), error);
        return false;
    }

    setDirty(false);
    QFile::remove(projectPath_ + QStringLiteral(".autosave"));
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(projectPath_).fileName()), 5000);
    return true;
}

void MainWindow::autosaveProject() {
    if (!dirty_ || projectPath_.isEmpty()) {
        return;
    }
    QString error;
    const ProjectData project{.sequence = editSession_.sequence(), .assets = assets_};
    const QString autosavePath = projectPath_ + QStringLiteral(".autosave");
    if (saveProjectFile(autosavePath, project, error)) {
        statusBar()->showMessage(tr("Autosaved recovery copy"), 2000);
    } else {
        statusBar()->showMessage(tr("Autosave failed: %1").arg(error), 5000);
    }
}

bool MainWindow::saveProjectAs() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), projectPath_, tr("Videx projects (*.videx)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".videx"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".videx");
    }

    const QString previousPath = projectPath_;
    projectPath_ = path;
    if (saveProject()) {
        return true;
    }
    projectPath_ = previousPath;
    updateWindowTitle();
    return false;
}

bool MainWindow::confirmDiscardChanges() {
    if (!dirty_) {
        return true;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, tr("Unsaved Changes"), tr("Save changes to the current project?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Save) {
        return saveProject();
    }
    return choice == QMessageBox::Discard;
}

void MainWindow::setDirty(const bool dirty) {
    dirty_ = dirty;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    const QString name = projectPath_.isEmpty() ? tr("Untitled")
                                                 : QFileInfo(projectPath_).fileName();
    setWindowTitle(QStringLiteral("%1%2 - Videx")
                       .arg(name, dirty_ ? QStringLiteral(" *") : QString{}));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardChanges()) {
        // Re-dock a fullscreen program monitor so it is destroyed with the
        // window and the saved workspace layout still contains it.
        if (monitorFullscreen_) {
            toggleMonitorFullscreen();
        }
        saveWorkspaceState();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::saveWorkspaceState() {
    QSettings settings(QStringLiteral("Videx"), QStringLiteral("Videx"));
    settings.setValue(QStringLiteral("workspace/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("workspace/state"), saveState());
}

void MainWindow::restoreWorkspaceState() {
    QSettings settings(QStringLiteral("Videx"), QStringLiteral("Videx"));
    const QByteArray geometry =
        settings.value(QStringLiteral("workspace/geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("workspace/state")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    if (!state.isEmpty()) {
        restoreState(state);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        QMainWindow::dropEvent(event);
        return;
    }
    int imported = 0;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString filePath = url.toLocalFile();
        if (!QFileInfo(filePath).isFile()) {
            continue;
        }
        importMediaFile(filePath);
        ++imported;
    }
    if (imported > 0) {
        event->acceptProposedAction();
        statusBar()->showMessage(tr("Importing %n dropped file(s)...", nullptr, imported),
                                 4000);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == programMonitor_ && event->type() == QEvent::Close &&
        monitorFullscreen_) {
        // Alt+F4 on the fullscreen monitor must return it to its tab instead
        // of closing (and stranding) the widget.
        toggleMonitorFullscreen();
        event->ignore();
        return true;
    }
    if (effectsBrowserList_ != nullptr && watched == effectsBrowserList_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                assetDragStartPosition_ = mouse->pos();
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->buttons() & Qt::LeftButton) != 0 &&
                (mouse->pos() - assetDragStartPosition_).manhattanLength() >=
                    QApplication::startDragDistance()) {
                QListWidgetItem* item =
                    effectsBrowserList_->itemAt(assetDragStartPosition_);
                if (item != nullptr && !item->data(Qt::UserRole).isNull()) {
                    auto* mime = new QMimeData;
                    mime->setData(QStringLiteral("application/x-videx-effect"),
                                  QByteArray::number(item->data(Qt::UserRole).toInt()));
                    auto* drag = new QDrag(effectsBrowserList_);
                    drag->setMimeData(mime);
                    drag->exec(Qt::CopyAction);
                    return true;
                }
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }
    if (projectTree_ != nullptr && watched == projectTree_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                assetDragStartPosition_ = mouse->pos();
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->buttons() & Qt::LeftButton) != 0 &&
                (mouse->pos() - assetDragStartPosition_).manhattanLength() >=
                    QApplication::startDragDistance()) {
                QTreeWidgetItem* item = projectTree_->itemAt(assetDragStartPosition_);
                if (item != nullptr && !item->data(0, Qt::UserRole).isNull()) {
                    const std::uint64_t assetId =
                        item->data(0, Qt::UserRole).toULongLong();
                    if (assetId != 0) {
                        auto* mime = new QMimeData;
                        mime->setData(
                            QStringLiteral("application/x-videx-asset"),
                            QByteArray::number(static_cast<qulonglong>(assetId)));
                        auto* drag = new QDrag(projectTree_);
                        drag->setMimeData(mime);
                        drag->exec(Qt::CopyAction);
                        return true;
                    }
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::rebuildProjectTree() {
    if (projectTree_ == nullptr) {
        return;
    }

    projectTree_->clear();
    const QString search = projectSearch_ == nullptr ? QString{} :
        projectSearch_->text().trimmed();
    QMap<QString, QTreeWidgetItem*> bins;

    for (const ProjectAsset& asset : assets_) {
        const QJsonArray streams = asset.metadata.value(QStringLiteral("streams")).toArray();
        bool hasVideo = false;
        bool hasAudio = false;
        for (const QJsonValue& value : streams) {
            const QString kind = value.toObject().value(QStringLiteral("kind")).toString();
            hasVideo = hasVideo || kind == QStringLiteral("video");
            hasAudio = hasAudio || kind == QStringLiteral("audio");
        }

        QString type = hasVideo && hasAudio ? tr("Video + Audio")
                              : hasVideo           ? tr("Video")
                              : hasAudio           ? tr("Audio")
                                                   : tr("Media");
        const QString proxyPath =
            asset.metadata.value(QStringLiteral("proxy_cache")).toString();
        if (QFileInfo::exists(proxyPath)) {
            type += tr(" (Proxy)");
        }
        const double durationSeconds = std::max(
            0.0, static_cast<double>(
                     asset.metadata.value(QStringLiteral("duration_us")).toInteger(0)) /
                     1'000'000.0);
        const QString displayName = asset.metadata.value(QStringLiteral("display_name"))
            .toString(QFileInfo(asset.path).fileName());
        const QString binName = asset.metadata.value(QStringLiteral("bin"))
            .toString(QStringLiteral("Media"));
        if (!search.isEmpty() && !displayName.contains(search, Qt::CaseInsensitive) &&
            !asset.path.contains(search, Qt::CaseInsensitive) &&
            !type.contains(search, Qt::CaseInsensitive) &&
            !binName.contains(search, Qt::CaseInsensitive)) continue;
        QTreeWidgetItem* mediaBin = bins.value(binName, nullptr);
        if (mediaBin == nullptr) {
            mediaBin = new QTreeWidgetItem({binName, tr("Bin"), {}});
            projectTree_->addTopLevelItem(mediaBin);
            bins.insert(binName, mediaBin);
        }
        auto* item = new QTreeWidgetItem({displayName, type,
                                          QStringLiteral("%1 s").arg(durationSeconds, 0, 'f', 2)});
        const QString thumbnailPath =
            asset.metadata.value(QStringLiteral("thumbnail_cache")).toString();
        if (QFileInfo::exists(thumbnailPath)) {
            item->setIcon(0, QIcon(thumbnailPath));
        }
        if (!QFileInfo::exists(asset.path)) {
            item->setText(1, tr("Missing"));
            item->setForeground(0, QColor(230, 110, 100));
            item->setForeground(1, QColor(230, 110, 100));
        }
        item->setToolTip(0, asset.path);
        item->setData(0, Qt::UserRole,
                      QVariant::fromValue<qulonglong>(static_cast<qulonglong>(asset.id.value)));
        mediaBin->addChild(item);
    }
    for (QTreeWidgetItem* bin : bins) bin->setExpanded(true);
    projectTree_->sortItems(0, Qt::AscendingOrder);
}

QRectF MainWindow::clipMonitorContentRect(const core::Clip& clip) const {
    const auto asset = std::ranges::find_if(assets_, [&clip](const ProjectAsset& candidate) {
        return candidate.id == clip.assetId;
    });
    if (asset != assets_.end() && isTitleAsset(asset->metadata)) {
        return titleTextRectNormalized(titleStyleFromMetadata(asset->metadata))
            .intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    }
    return {0.0, 0.0, 1.0, 1.0};
}

void MainWindow::updateSourcePatchUi() {
    if (videoPatchButton_ == nullptr || audioPatchButton_ == nullptr) {
        return;
    }
    const core::Sequence& sequence = editSession_.sequence();
    const QString videoLabel =
        trackPatchLabel(sequence, videoTrack_, core::TrackKind::Video);
    const QString audioLabel =
        trackPatchLabel(sequence, audioTrack_, core::TrackKind::Audio);
    videoPatchButton_->setText(videoLabel);
    audioPatchButton_->setText(audioLabel);
    videoPatchButton_->setToolTip(
        tr("Patch source video to track %1").arg(videoLabel));
    audioPatchButton_->setToolTip(
        tr("Patch source audio to track %1").arg(audioLabel));
}

void MainWindow::refreshEditor() {
    // Assets may change without a sequence revision bump (proxy arrival,
    // regenerated titles); refresh always invalidates the manifest cache.
    manifestCacheRevision_ = ~0ULL;
    if (timeline_ != nullptr) {
        // Deleted or never-assigned targets fall back to the first track of
        // their kind so Insert/Overwrite never lands on a stale TrackId.
        const core::Sequence& sequence = editSession_.sequence();
        if (videoTrack_ && sequence.findTrack(videoTrack_) == nullptr) {
            videoTrack_ = {};
        }
        if (audioTrack_ && sequence.findTrack(audioTrack_) == nullptr) {
            audioTrack_ = {};
        }
        for (const core::Track& track : sequence.tracks()) {
            if (!videoTrack_ && track.kind == core::TrackKind::Video) {
                videoTrack_ = track.id;
            }
            if (!audioTrack_ && track.kind == core::TrackKind::Audio) {
                audioTrack_ = track.id;
            }
        }
        updateSourcePatchUi();
        timeline_->setSequence(&editSession_.sequence());
        timeline_->setTargetTracks(videoTrack_, audioTrack_);
        timeline_->setPreviewCacheState(previewCacheStart_, previewCacheDuration_,
                                        previewCacheValid());
        std::unordered_map<std::uint64_t, QString> labels;
        labels.reserve(assets_.size());
        for (const ProjectAsset& asset : assets_) {
            labels.emplace(asset.id.value,
                           isTitleAsset(asset.metadata)
                               ? titleStyleFromMetadata(asset.metadata)
                                     .text.simplified()
                               : QFileInfo(asset.path).completeBaseName());
        }
        timeline_->setAssetLabels(std::move(labels));
        std::unordered_map<std::uint64_t, QImage> thumbnails;
        for (const ProjectAsset& asset : assets_) {
            const QString thumbnailPath =
                asset.metadata.value(QStringLiteral("thumbnail_cache")).toString();
            if (thumbnailPath.isEmpty()) {
                continue;
            }
            const QFileInfo info(thumbnailPath);
            if (!info.exists()) {
                continue;
            }
            // Reload only when the cached file changes (path + mtime key).
            const QString cacheKey =
                thumbnailPath + '|' +
                QString::number(info.lastModified().toMSecsSinceEpoch());
            TimelineThumbnail& entry = timelineThumbnails_[asset.id.value];
            if (entry.key != cacheKey) {
                const QImage image(thumbnailPath);
                entry.key = cacheKey;
                entry.image = image.isNull()
                                  ? QImage{}
                                  : image.scaledToHeight(
                                        92, Qt::SmoothTransformation);
            }
            if (!entry.image.isNull()) {
                thumbnails.emplace(asset.id.value, entry.image);
            }
        }
        timeline_->setAssetThumbnails(std::move(thumbnails));
    }

    if (undoAction_ != nullptr) {
        undoAction_->setEnabled(editSession_.canUndo());
        undoAction_->setText(editSession_.canUndo() ? tr("Undo %1").arg(QString::fromStdString(
                                                          editSession_.undoLabel()))
                                                    : tr("Undo"));
    }
    if (redoAction_ != nullptr) {
        redoAction_->setEnabled(editSession_.canRedo());
        redoAction_->setText(editSession_.canRedo() ? tr("Redo %1").arg(QString::fromStdString(
                                                          editSession_.redoLabel()))
                                                    : tr("Redo"));
    }
    if (timeline_ != nullptr) {
        updateTransportUi(timeline_->playheadFrame());
    }
    if (inspectedClip_) {
        updateInspector(inspectedClip_);
    }
    updateHistoryPanel();
}

void MainWindow::updateInspector(const core::ClipId clipId) {
    inspectedClip_ = clipId;
    const core::Clip* clip = editSession_.sequence().findClip(clipId);
    const bool enabled = clip != nullptr;
    if (speedInterpolationCombo_ != nullptr) speedInterpolationCombo_->setEnabled(enabled);
    if (motionInterpolationCombo_ != nullptr) motionInterpolationCombo_->setEnabled(enabled);
    if (gainInterpolationCombo_ != nullptr) gainInterpolationCombo_->setEnabled(enabled);
    if (maskShapeCombo_ != nullptr) maskShapeCombo_->setEnabled(enabled);
    if (maskInvertedCheck_ != nullptr) maskInvertedCheck_->setEnabled(enabled);
    for (QDoubleSpinBox* spin : {opacitySpin_, gainSpin_, rateSpin_, fadeInSpin_, fadeOutSpin_,
                                 positionXSpin_, positionYSpin_, scaleXSpin_, scaleYSpin_,
                                 rotationSpin_, anchorXSpin_, anchorYSpin_, cropLeftSpin_,
                                 cropRightSpin_, cropTopSpin_, cropBottomSpin_, maskCenterXSpin_,
                                 maskCenterYSpin_, maskWidthSpin_, maskHeightSpin_,
                                 maskFeatherSpin_}) {
        if (spin != nullptr) {
            spin->setEnabled(enabled);
        }
    }
    if (!enabled) {
        inspectedClip_ = {};
        updateEffectsPanel();
        updateMonitorEditTarget();
        return;
    }
    const core::Frame localFrame = timeline_ == nullptr ? 0 : std::clamp<core::Frame>(
        timeline_->playheadFrame() - clip->timeline.start, 0, clip->timeline.duration - 1);
    const core::MotionKeyframe motion = motionAt(*clip, localFrame);
    opacitySpin_->setValue(motion.opacity * 100.0);
    gainSpin_->setValue(gainAt(*clip, localFrame));
    rateSpin_->setValue(clip->playbackRate);
    fadeInSpin_->setMaximum(static_cast<double>(clip->timeline.duration));
    fadeOutSpin_->setMaximum(static_cast<double>(clip->timeline.duration));
    fadeInSpin_->setValue(static_cast<double>(clip->fadeInFrames));
    fadeOutSpin_->setValue(static_cast<double>(clip->fadeOutFrames));
    positionXSpin_->setValue(motion.positionX);
    positionYSpin_->setValue(motion.positionY);
    scaleXSpin_->setValue(motion.scaleX * 100.0);
    scaleYSpin_->setValue(motion.scaleY * 100.0);
    rotationSpin_->setValue(motion.rotationDegrees);
    anchorXSpin_->setValue(motion.anchorX * 100.0);
    anchorYSpin_->setValue(motion.anchorY * 100.0);
    if (motionInterpolationCombo_ != nullptr) {
        const auto exact = std::ranges::find(clip->motionKeyframes, localFrame,
                                              &core::MotionKeyframe::frameOffset);
        if (exact != clip->motionKeyframes.end()) {
            const QSignalBlocker blocker(motionInterpolationCombo_);
            motionInterpolationCombo_->setCurrentIndex(
                motionInterpolationCombo_->findData(static_cast<int>(exact->interpolation)));
        }
    }
    if (gainInterpolationCombo_ != nullptr) {
        const auto exact = std::ranges::find(clip->gainKeyframes, localFrame,
                                             &core::GainKeyframe::frameOffset);
        if (exact != clip->gainKeyframes.end()) {
            const QSignalBlocker blocker(gainInterpolationCombo_);
            gainInterpolationCombo_->setCurrentIndex(
                gainInterpolationCombo_->findData(static_cast<int>(exact->interpolation)));
        }
    }
    cropLeftSpin_->setValue(clip->cropLeft * 100.0);
    cropRightSpin_->setValue(clip->cropRight * 100.0);
    cropTopSpin_->setValue(clip->cropTop * 100.0);
    cropBottomSpin_->setValue(clip->cropBottom * 100.0);
    {
        const QSignalBlocker shapeBlocker(maskShapeCombo_);
        maskShapeCombo_->setCurrentIndex(maskShapeCombo_->findData(
            static_cast<int>(clip->maskShape)));
    }
    maskCenterXSpin_->setValue(clip->maskCenterX * 100.0);
    maskCenterYSpin_->setValue(clip->maskCenterY * 100.0);
    maskWidthSpin_->setValue(clip->maskWidth * 100.0);
    maskHeightSpin_->setValue(clip->maskHeight * 100.0);
    maskFeatherSpin_->setValue(clip->maskFeather * 100.0);
    {
        const QSignalBlocker invertedBlocker(maskInvertedCheck_);
        maskInvertedCheck_->setChecked(clip->maskInverted);
    }
    updateEffectsPanel();
    updateTextPanel();
    updateMonitorEditTarget();
}

void MainWindow::updateMonitorEditTarget() {
    if (programMonitor_ == nullptr) {
        return;
    }
    const core::Clip* clip =
        inspectedClip_ ? editSession_.sequence().findClip(inspectedClip_) : nullptr;
    bool videoClip = false;
    if (clip != nullptr) {
        for (const core::Track& track : editSession_.sequence().tracks()) {
            if (std::ranges::any_of(track.clips, [this](const core::Clip& candidate) {
                    return candidate.id == inspectedClip_;
                })) {
                videoClip = track.kind == core::TrackKind::Video;
                break;
            }
        }
    }
    if (clip == nullptr || !videoClip || playbackRequested_) {
        programMonitor_->setEditTarget(false, {}, {});
        return;
    }
    const core::Frame localFrame = timeline_ == nullptr ? 0 : std::clamp<core::Frame>(
        timeline_->playheadFrame() - clip->timeline.start, 0, clip->timeline.duration - 1);
    const core::MotionKeyframe motion = motionAt(*clip, localFrame);
    const render::MonitorTransform committed{motion.positionX, motion.positionY,
                                             motion.scaleX, motion.scaleY,
                                             motion.rotationDegrees, motion.anchorX,
                                             motion.anchorY};
    const render::MonitorCrop committedCrop{clip->cropLeft, clip->cropRight,
                                            clip->cropTop, clip->cropBottom};
    if (monitorAwaitingRender_) {
        // A commit is waiting for its re-rendered frame; keep the displayed
        // frame's baseline so the preview does not snap back meanwhile.
        programMonitor_->setEditTargetKeepBaked(committed, committedCrop);
    } else {
        programMonitor_->setEditTarget(true, committed, committedCrop);
    }
    const QRectF contentRect = clipMonitorContentRect(*clip);
    programMonitor_->setEditTargetContentRect(contentRect.left(), contentRect.top(),
                                              contentRect.width(),
                                              contentRect.height());
    // With several video layers under the playhead, dragging one layer must
    // not warp the whole composed frame: keep the image static and move the
    // outline only.
    int videoLayers = 0;
    const core::Frame playhead =
        timeline_ == nullptr ? 0 : timeline_->playheadFrame();
    for (const core::Track& track : editSession_.sequence().tracks()) {
        if (track.kind != core::TrackKind::Video || !track.enabled) {
            continue;
        }
        for (const core::Clip& candidate : track.clips) {
            if (candidate.timeline.contains(playhead)) {
                ++videoLayers;
                break;
            }
        }
    }
    monitorLayeredTarget_ = videoLayers > 1;
    programMonitor_->setLayeredTarget(monitorLayeredTarget_);
}

bool MainWindow::previewCacheValid() const {
    return !previewCachePath_.isEmpty() && previewCacheDuration_ > 0 &&
           previewCacheRevision_ == editSession_.sequence().revision() &&
           QFileInfo::exists(previewCachePath_);
}

void MainWindow::toggleMonitorFullscreen() {
    if (programMonitor_ == nullptr || monitorTabs_ == nullptr) {
        return;
    }
    if (monitorFullscreen_) {
        programMonitor_->removeEventFilter(this);
        programMonitor_->setFullscreenMode(false);
        programMonitor_->setParent(nullptr);
        monitorTabs_->insertTab(0, programMonitor_, tr("Program"));
        monitorTabs_->setCurrentWidget(programMonitor_);
        programMonitor_->show();
        monitorFullscreen_ = false;
        statusBar()->showMessage(tr("Exited fullscreen preview"), 2000);
    } else {
        const int index = monitorTabs_->indexOf(programMonitor_);
        if (index >= 0) {
            monitorTabs_->removeTab(index);
        }
        programMonitor_->setParent(nullptr);
        // Fullscreen on the screen the cursor is on (multi-monitor).
        if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
            screen != nullptr) {
            programMonitor_->setGeometry(screen->geometry());
        }
        programMonitor_->setFullscreenMode(true);
        // Intercept Alt+F4 on the fullscreen top-level: without this the
        // monitor widget is closed and stranded outside the tab bar.
        programMonitor_->installEventFilter(this);
        programMonitor_->showFullScreen();
        if (programMonitor_->viewportWidget() != nullptr) {
            programMonitor_->viewportWidget()->setFocus(Qt::OtherFocusReason);
        }
        monitorFullscreen_ = true;
        statusBar()->showMessage(
            tr("Fullscreen preview - Esc to exit, Space/J/K/L for transport"), 4000);
    }
}

void MainWindow::applyInspectorProperties() {
    const core::Clip* selected = editSession_.sequence().findClip(inspectedClip_);
    if (selected == nullptr || opacitySpin_ == nullptr || gainSpin_ == nullptr ||
        rateSpin_ == nullptr || fadeInSpin_ == nullptr || fadeOutSpin_ == nullptr ||
        positionXSpin_ == nullptr || positionYSpin_ == nullptr || scaleXSpin_ == nullptr ||
        scaleYSpin_ == nullptr || rotationSpin_ == nullptr || anchorXSpin_ == nullptr ||
        anchorYSpin_ == nullptr || cropLeftSpin_ == nullptr || cropRightSpin_ == nullptr ||
        cropTopSpin_ == nullptr || cropBottomSpin_ == nullptr || maskShapeCombo_ == nullptr ||
        maskCenterXSpin_ == nullptr || maskCenterYSpin_ == nullptr ||
        maskWidthSpin_ == nullptr || maskHeightSpin_ == nullptr ||
        maskFeatherSpin_ == nullptr || maskInvertedCheck_ == nullptr ||
        motionInterpolationCombo_ == nullptr) {
        return;
    }
    const double opacity = opacitySpin_->value() / 100.0;
    const double gain = gainSpin_->value();
    const double rate = rateSpin_->value();
    const core::Frame fadeIn = static_cast<core::Frame>(std::llround(fadeInSpin_->value()));
    const core::Frame fadeOut = static_cast<core::Frame>(std::llround(fadeOutSpin_->value()));
    const double positionX = positionXSpin_->value();
    const double positionY = positionYSpin_->value();
    const double scaleX = scaleXSpin_->value() / 100.0;
    const double scaleY = scaleYSpin_->value() / 100.0;
    const double rotation = rotationSpin_->value();
    const double anchorX = anchorXSpin_->value() / 100.0;
    const double anchorY = anchorYSpin_->value() / 100.0;
    const double cropLeft = cropLeftSpin_->value() / 100.0;
    const double cropRight = cropRightSpin_->value() / 100.0;
    const double cropTop = cropTopSpin_->value() / 100.0;
    const double cropBottom = cropBottomSpin_->value() / 100.0;
    const auto maskShape = static_cast<core::MaskShape>(maskShapeCombo_->currentData().toInt());
    const double maskCenterX = maskCenterXSpin_->value() / 100.0;
    const double maskCenterY = maskCenterYSpin_->value() / 100.0;
    const double maskWidth = maskWidthSpin_->value() / 100.0;
    const double maskHeight = maskHeightSpin_->value() / 100.0;
    const double maskFeather = maskFeatherSpin_->value() / 100.0;
    const bool maskInverted = maskInvertedCheck_->isChecked();
    const core::Frame selectedLocal = timeline_ == nullptr ? 0 : std::clamp<core::Frame>(
        timeline_->playheadFrame() - selected->timeline.start, 0,
        selected->timeline.duration - 1);
    const core::MotionKeyframe selectedMotion = motionAt(*selected, selectedLocal);
    const double selectedGain = gainAt(*selected, selectedLocal);
    const bool playheadInsideSelected = timeline_ != nullptr &&
                                        selected->timeline.contains(timeline_->playheadFrame());
    const bool motionChanged = selectedMotion.opacity != opacity ||
                               selectedMotion.positionX != positionX ||
                               selectedMotion.positionY != positionY ||
                               selectedMotion.scaleX != scaleX ||
                               selectedMotion.scaleY != scaleY ||
                               selectedMotion.rotationDegrees != rotation ||
                               selectedMotion.anchorX != anchorX ||
                               selectedMotion.anchorY != anchorY;
    if (!playheadInsideSelected &&
        ((!selected->gainKeyframes.empty() && selectedGain != gain) ||
         (!selected->motionKeyframes.empty() && motionChanged))) {
        statusBar()->showMessage(tr("Move the playhead inside the clip to edit automation"),
                                 3000);
        updateInspector(selected->id);
        return;
    }
    if (selectedMotion.opacity == opacity && selectedGain == gain &&
        selected->playbackRate == rate && selected->fadeInFrames == fadeIn &&
        selected->fadeOutFrames == fadeOut && selectedMotion.positionX == positionX &&
        selectedMotion.positionY == positionY && selectedMotion.scaleX == scaleX &&
        selectedMotion.scaleY == scaleY && selectedMotion.rotationDegrees == rotation &&
        selectedMotion.anchorX == anchorX && selectedMotion.anchorY == anchorY &&
        selected->cropLeft == cropLeft && selected->cropRight == cropRight &&
        selected->cropTop == cropTop && selected->cropBottom == cropBottom &&
        selected->maskShape == maskShape && selected->maskCenterX == maskCenterX &&
        selected->maskCenterY == maskCenterY && selected->maskWidth == maskWidth &&
        selected->maskHeight == maskHeight && selected->maskFeather == maskFeather &&
        selected->maskInverted == maskInverted) {
        return;
    }
    std::vector<core::EditCommand> commands;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        for (const core::Clip& clip : track.clips) {
            if (clip.id == selected->id || (selected->linkId && clip.linkId == selected->linkId)) {
                const bool animatedMotion = track.kind == core::TrackKind::Video &&
                                            !clip.motionKeyframes.empty();
                const bool animatedGain = track.kind == core::TrackKind::Audio &&
                                          !clip.gainKeyframes.empty();
                commands.push_back(core::SetClipPropertiesCommand{
                    .clipId = clip.id,
                    .opacity = animatedMotion ? clip.opacity : opacity,
                    .audioGainDb = animatedGain ? clip.audioGainDb : gain,
                    .playbackRate = rate,
                });
                if (animatedGain && timeline_ != nullptr &&
                    clip.timeline.contains(timeline_->playheadFrame())) {
                    const core::Frame local = timeline_->playheadFrame() -
                                              clip.timeline.start;
                    commands.push_back(core::SetGainKeyframeCommand{
                        clip.id, local, gain,
                        static_cast<core::KeyframeInterpolation>(
                            gainInterpolationCombo_->currentData().toInt())});
                }
                commands.push_back(core::SetClipFadesCommand{clip.id, fadeIn, fadeOut});
                if (track.kind == core::TrackKind::Video) {
                    if (animatedMotion && timeline_ != nullptr &&
                        clip.timeline.contains(timeline_->playheadFrame())) {
                        const core::Frame local = timeline_->playheadFrame() -
                                                  clip.timeline.start;
                        commands.push_back(core::SetMotionKeyframeCommand{
                            clip.id, local, opacity, positionX, positionY, scaleX, scaleY,
                            rotation, anchorX, anchorY,
                            static_cast<core::KeyframeInterpolation>(
                                motionInterpolationCombo_->currentData().toInt())});
                    } else if (!animatedMotion) {
                        commands.push_back(core::SetClipTransformCommand{
                            clip.id, positionX, positionY, scaleX, scaleY, rotation, anchorX,
                            anchorY});
                    }
                    commands.push_back(core::SetClipCropCommand{
                        clip.id, cropLeft, cropRight, cropTop, cropBottom});
                    commands.push_back(core::SetClipMaskCommand{
                        clip.id, maskShape, maskCenterX, maskCenterY, maskWidth, maskHeight,
                        maskFeather, maskInverted});
                }
            }
        }
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = "Change clip properties",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not change properties: %1").arg(QString::fromStdString(result.message)),
            5000);
        return;
    }
    monitorAwaitingRender_ = true;
    setDirty(true);
    refreshEditor();
    updateProgramFrame(timeline_->playheadFrame());
}

void MainWindow::updateEffectsPanel() {
    if (effectsList_ == nullptr || effectAmountSpin_ == nullptr ||
        effectEnabledCheck_ == nullptr || effectInterpolationCombo_ == nullptr) {
        return;
    }
    const qulonglong previousId = effectsList_->currentItem() == nullptr
                                      ? 0
                                      : effectsList_->currentItem()
                                            ->data(Qt::UserRole)
                                            .toULongLong();
    const QSignalBlocker listBlocker(effectsList_);
    effectsList_->clear();
    const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
    if (clip == nullptr) {
        effectAmountSpin_->setEnabled(false);
        effectEnabledCheck_->setEnabled(false);
        effectInterpolationCombo_->setEnabled(false);
        if (keyframeLanesWidget_ != nullptr) {
            static_cast<KeyframeLaneWidget*>(keyframeLanesWidget_)
                ->setLanes({}, 0, 1, 0);
        }
        return;
    }
    if (keyframeLanesWidget_ != nullptr) {
        std::vector<KeyframeLaneEntry> lanes;
        if (!clip->motionKeyframes.empty()) {
            KeyframeLaneEntry lane{.name = tr("Motion"), .laneType = 0};
            for (const core::MotionKeyframe& key : clip->motionKeyframes) {
                lane.offsets.push_back(key.frameOffset);
                lane.interpolations.push_back(static_cast<int>(key.interpolation));
            }
            lanes.push_back(std::move(lane));
        }
        if (!clip->gainKeyframes.empty()) {
            KeyframeLaneEntry lane{.name = tr("Gain"), .laneType = 1};
            for (const core::GainKeyframe& key : clip->gainKeyframes) {
                lane.offsets.push_back(key.frameOffset);
                lane.interpolations.push_back(static_cast<int>(key.interpolation));
            }
            lanes.push_back(std::move(lane));
        }
        if (!clip->speedKeyframes.empty()) {
            KeyframeLaneEntry lane{.name = tr("Speed"), .laneType = 2};
            for (const core::SpeedKeyframe& key : clip->speedKeyframes) {
                lane.offsets.push_back(key.frameOffset);
                lane.interpolations.push_back(static_cast<int>(key.interpolation));
            }
            lanes.push_back(std::move(lane));
        }
        for (const core::ClipEffect& effect : clip->effects) {
            if (effect.keyframes.empty()) {
                continue;
            }
            KeyframeLaneEntry lane{.name = effectName(effect.type),
                                   .laneType = 3,
                                   .effectId = effect.id.value};
            for (const core::EffectKeyframe& key : effect.keyframes) {
                lane.offsets.push_back(key.frameOffset);
                lane.interpolations.push_back(static_cast<int>(key.interpolation));
            }
            lanes.push_back(std::move(lane));
        }
        static_cast<KeyframeLaneWidget*>(keyframeLanesWidget_)
            ->setLanes(std::move(lanes), clip->timeline.start, clip->timeline.duration,
                       timeline_ == nullptr ? 0 : timeline_->playheadFrame());
    }
    QListWidgetItem* selectedItem = nullptr;
    for (const core::ClipEffect& effect : clip->effects) {
        auto* item = new QListWidgetItem(
            tr("%1  %2  [%3 keyframes]")
                .arg(effect.enabled ? QStringLiteral("✓") : QStringLiteral("○"),
                     effectName(effect.type))
                .arg(effect.keyframes.size()),
            effectsList_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(effect.id.value));
        if (effect.id.value == previousId) {
            selectedItem = item;
        }
    }
    if (selectedItem == nullptr && effectsList_->count() > 0) {
        selectedItem = effectsList_->item(0);
    }
    effectsList_->setCurrentItem(selectedItem);
    const bool enabled = selectedItem != nullptr;
    effectAmountSpin_->setEnabled(enabled);
    effectEnabledCheck_->setEnabled(enabled);
    effectInterpolationCombo_->setEnabled(enabled);
    if (!enabled) {
        return;
    }
    const core::EffectId effectId{selectedItem->data(Qt::UserRole).toULongLong()};
    const auto effect = std::ranges::find(clip->effects, effectId, &core::ClipEffect::id);
    if (effect == clip->effects.end()) {
        return;
    }
    const QSignalBlocker enabledBlocker(effectEnabledCheck_);
    const QSignalBlocker amountBlocker(effectAmountSpin_);
    effectEnabledCheck_->setChecked(effect->enabled);
    switch (effect->type) {
    case core::EffectType::Brightness:
    case core::EffectType::Vignette:
        effectAmountSpin_->setRange(effect->type == core::EffectType::Brightness ? -1.0 : 0.0,
                                    1.0);
        break;
    case core::EffectType::Contrast:
    case core::EffectType::Saturation:
        effectAmountSpin_->setRange(-1.0, 3.0);
        break;
    case core::EffectType::Blur:
        effectAmountSpin_->setRange(0.0, 50.0);
        break;
    }
    const core::Frame localFrame = timeline_ == nullptr
                                       ? 0
                                       : timeline_->playheadFrame() - clip->timeline.start;
    effectAmountSpin_->setValue(effectValueAt(*effect, localFrame));
    const auto keyframe = std::ranges::find(effect->keyframes, localFrame,
                                             &core::EffectKeyframe::frameOffset);
    if (keyframe != effect->keyframes.end()) {
        const QSignalBlocker interpolationBlocker(effectInterpolationCombo_);
        effectInterpolationCombo_->setCurrentIndex(effectInterpolationCombo_->findData(
            static_cast<int>(keyframe->interpolation)));
    }
}

void MainWindow::updateHistoryPanel() {
    if (historyList_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(historyList_);
    historyList_->clear();
    const std::vector<std::string> undo = editSession_.undoLabels();
    auto* initial = new QListWidgetItem(tr("Initial state"), historyList_);
    initial->setData(Qt::UserRole, -static_cast<int>(undo.size()));
    for (std::size_t index = 0; index < undo.size(); ++index) {
        auto* item = new QListWidgetItem(QString::fromStdString(undo[index]), historyList_);
        item->setData(Qt::UserRole,
                      -static_cast<int>(undo.size() - index - 1U));
    }
    auto* current = new QListWidgetItem(tr("Current"), historyList_);
    current->setData(Qt::UserRole, 0);
    current->setSelected(true);
    const std::vector<std::string> redo = editSession_.redoLabels();
    for (std::size_t index = 0; index < redo.size(); ++index) {
        auto* item = new QListWidgetItem(tr("Redo: %1").arg(
                                             QString::fromStdString(redo[index])),
                                         historyList_);
        item->setData(Qt::UserRole, static_cast<int>(index + 1U));
    }
}

void MainWindow::updateAudioMeters() {
    int leftLevel = 0;
    int rightLevel = 0;
    if (audioSink_ != nullptr && audioBuffer_ != nullptr &&
        audioSink_->state() == QtAudio::ActiveState) {
        const QByteArray& pcm = audioBuffer_->data();
        constexpr std::int64_t bytesPerFrame = 2 * static_cast<std::int64_t>(sizeof(float));
        const std::int64_t centerFrame = audioSink_->processedUSecs() * 48'000 / 1'000'000;
        const std::int64_t firstByte = std::clamp<std::int64_t>(
            centerFrame * bytesPerFrame, 0, std::max<std::int64_t>(0, pcm.size() - bytesPerFrame));
        const std::int64_t lastByte = std::min<std::int64_t>(pcm.size(),
                                                             firstByte + 2048 * bytesPerFrame);
        float leftPeak = 0.0F;
        float rightPeak = 0.0F;
        for (std::int64_t offset = firstByte; offset + bytesPerFrame <= lastByte;
             offset += bytesPerFrame) {
            float left = 0.0F;
            float right = 0.0F;
            std::memcpy(&left, pcm.constData() + offset, sizeof(float));
            std::memcpy(&right, pcm.constData() + offset + sizeof(float), sizeof(float));
            leftPeak = std::max(leftPeak, std::abs(left));
            rightPeak = std::max(rightPeak, std::abs(right));
        }
        leftLevel = static_cast<int>(std::clamp(leftPeak * 100.0F, 0.0F, 100.0F));
        rightLevel = static_cast<int>(std::clamp(rightPeak * 100.0F, 0.0F, 100.0F));
    }
    if (leftAudioMeter_ != nullptr) leftAudioMeter_->setValue(leftLevel);
    if (rightAudioMeter_ != nullptr) rightAudioMeter_->setValue(rightLevel);
}

void MainWindow::applySelectedEffect() {
    const core::Clip* clip = editSession_.sequence().findClip(inspectedClip_);
    QListWidgetItem* item = effectsList_ == nullptr ? nullptr : effectsList_->currentItem();
    if (clip == nullptr || item == nullptr) {
        return;
    }
    const core::EffectId effectId{item->data(Qt::UserRole).toULongLong()};
    const core::EditResult result = editSession_.apply({
        .baseRevision = editSession_.sequence().revision(),
        .label = "Change effect",
        .command = core::SetEffectCommand{inspectedClip_, effectId,
                                          effectEnabledCheck_->isChecked(),
                                          effectAmountSpin_->value()},
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not change effect: %1").arg(QString::fromStdString(result.message)), 4000);
        return;
    }
    setDirty(true);
    refreshEditor();
    updateProgramFrame(timeline_->playheadFrame());
}

void MainWindow::slipSelected(const core::Frame sourceDelta) {
    const core::Clip* selected = editSession_.sequence().findClip(inspectedClip_);
    if (selected == nullptr) {
        statusBar()->showMessage(tr("Select a clip to slip"), 2500);
        return;
    }
    std::vector<core::EditCommand> commands;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        for (const core::Clip& clip : track.clips) {
            if (clip.id == selected->id || (selected->linkId && clip.linkId == selected->linkId)) {
                commands.push_back(core::SlipClipCommand{clip.id, sourceDelta});
            }
        }
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = "Slip linked clips",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not slip clip: %1").arg(QString::fromStdString(result.message)), 4000);
        return;
    }
    setDirty(true);
    refreshEditor();
    updateProgramFrame(timeline_->playheadFrame());
}

void MainWindow::rollSelected(const core::Frame cutDelta) {
    const core::Clip* selected = editSession_.sequence().findClip(inspectedClip_);
    if (selected == nullptr) {
        statusBar()->showMessage(tr("Select the clip on the left side of a cut"), 2500);
        return;
    }
    std::vector<core::EditCommand> commands;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        for (std::size_t index = 0; index + 1U < track.clips.size(); ++index) {
            const core::Clip& left = track.clips[index];
            const core::Clip& right = track.clips[index + 1U];
            const bool matches = left.id == selected->id ||
                                 (selected->linkId && left.linkId == selected->linkId);
            if (matches && left.timeline.end() == right.timeline.start) {
                commands.push_back(
                    core::RollEditCommand{left.id, right.id, right.timeline.start + cutDelta});
            }
        }
    }
    if (commands.empty()) {
        statusBar()->showMessage(tr("The selected clip has no adjacent cut on its right"), 3000);
        return;
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = "Roll linked cut",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not roll cut: %1").arg(QString::fromStdString(result.message)), 4000);
        return;
    }
    setDirty(true);
    refreshEditor();
    updateProgramFrame(timeline_->playheadFrame());
}

void MainWindow::copySelectedClips() {
    if (timeline_ == nullptr || timeline_->selectedClipIds().empty()) {
        statusBar()->showMessage(tr("Select one or more clips to copy"), 2500);
        return;
    }
    std::vector<core::ClipId> ids = timeline_->selectedClipIds();
    for (const core::ClipId selectedId : timeline_->selectedClipIds()) {
        const core::Clip* selected = editSession_.sequence().findClip(selectedId);
        if (selected == nullptr || !selected->linkId) {
            continue;
        }
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                if (clip.linkId == selected->linkId &&
                    std::ranges::find(ids, clip.id) == ids.end()) {
                    ids.push_back(clip.id);
                }
            }
        }
    }
    core::Frame anchor = std::numeric_limits<core::Frame>::max();
    for (const core::ClipId id : ids) {
        if (const core::Clip* clip = editSession_.sequence().findClip(id)) {
            anchor = std::min(anchor, clip->timeline.start);
        }
    }
    clipClipboard_.clear();
    for (const core::Track& track : editSession_.sequence().tracks()) {
        for (const core::Clip& clip : track.clips) {
            if (std::ranges::find(ids, clip.id) != ids.end()) {
                clipClipboard_.push_back({.sourceTrack = track.id,
                                          .kind = track.kind,
                                          .clip = clip,
                                          .offset = clip.timeline.start - anchor});
            }
        }
    }
    statusBar()->showMessage(tr("Copied %1 clip(s)").arg(clipClipboard_.size()), 2500);
}

void MainWindow::pasteClipsAt(const core::Frame frame) {
    if (clipClipboard_.empty()) {
        statusBar()->showMessage(tr("The clip clipboard is empty"), 2500);
        return;
    }
    std::vector<core::ClipPlacement> placements;
    placements.reserve(clipClipboard_.size());
    for (const ClipboardClip& item : clipClipboard_) {
        core::TrackId target = item.sourceTrack;
        const core::Track* originalTrack = editSession_.sequence().findTrack(target);
        if (originalTrack == nullptr || originalTrack->kind != item.kind || originalTrack->locked) {
            target = {};
            for (const core::Track& track : editSession_.sequence().tracks()) {
                if (track.kind == item.kind && !track.locked) {
                    target = track.id;
                    break;
                }
            }
        }
        if (!target) {
            statusBar()->showMessage(tr("No compatible unlocked target track for paste"), 4000);
            return;
        }
        placements.push_back({.targetTrack = target,
                              .kind = item.kind,
                              .clip = item.clip,
                              .timelineStart = frame + item.offset});
    }
    const core::EditResult result = editSession_.apply({
        .baseRevision = editSession_.sequence().revision(),
        .label = "Paste clips",
        .command = core::PasteClipsCommand{std::move(placements)},
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not paste clips: %1").arg(QString::fromStdString(result.message)), 5000);
        return;
    }
    setDirty(true);
    refreshEditor();
    timeline_->setSelectedClipIds(result.createdClips);
}

void MainWindow::deleteSelectedClips(const bool ripple) {
    if (timeline_ == nullptr || timeline_->selectedClipIds().empty()) {
        return;
    }
    std::vector<core::ClipId> ids = timeline_->selectedClipIds();
    for (const core::ClipId selectedId : timeline_->selectedClipIds()) {
        const core::Clip* selected = editSession_.sequence().findClip(selectedId);
        if (selected == nullptr || !selected->linkId) {
            continue;
        }
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                if (clip.linkId == selected->linkId &&
                    std::ranges::find(ids, clip.id) == ids.end()) {
                    ids.push_back(clip.id);
                }
            }
        }
    }
    std::vector<core::EditCommand> commands;
    if (ripple) {
        std::vector<core::FrameRange> ranges;
        for (const core::ClipId id : ids) {
            if (const core::Clip* clip = editSession_.sequence().findClip(id);
                clip != nullptr && std::ranges::find(ranges, clip->timeline) == ranges.end()) {
                ranges.push_back(clip->timeline);
            }
        }
        std::ranges::sort(ranges, {}, &core::FrameRange::start);
        std::vector<core::FrameRange> merged;
        for (const core::FrameRange range : ranges) {
            if (!merged.empty() && range.start <= merged.back().end()) {
                const core::Frame mergedEnd = std::max(merged.back().end(), range.end());
                merged.back().duration = mergedEnd - merged.back().start;
            } else {
                merged.push_back(range);
            }
        }
        for (auto it = merged.rbegin(); it != merged.rend(); ++it) {
            commands.push_back(core::ExtractRangeCommand{*it});
        }
    } else {
        for (const core::ClipId id : ids) {
            commands.push_back(core::LiftClipCommand{id});
        }
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = ripple ? "Ripple delete clips" : "Delete clips",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not delete clips: %1").arg(QString::fromStdString(result.message)), 5000);
        return;
    }
    setDirty(true);
    refreshEditor();
    timeline_->setSelectedClipIds({});
}

void MainWindow::editSequenceRange(const bool ripple) {
    if (timeline_ == nullptr || sequenceInFrame_ < 0 ||
        sequenceOutFrame_ <= sequenceInFrame_) {
        statusBar()->showMessage(tr("Mark a valid Sequence In and Out first"), 3000);
        return;
    }
    const core::FrameRange range{sequenceInFrame_, sequenceOutFrame_ - sequenceInFrame_};
    const core::EditResult result = editSession_.apply({
        .baseRevision = editSession_.sequence().revision(),
        .label = ripple ? "Extract sequence range" : "Lift sequence range",
        .command = ripple ? core::EditCommand{core::ExtractRangeCommand{range}}
                          : core::EditCommand{core::LiftRangeCommand{range}},
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not edit sequence range: %1")
                .arg(QString::fromStdString(result.message)), 5000);
        return;
    }
    if (ripple) {
        sequenceOutFrame_ = sequenceInFrame_;
        sequenceInFrame_ = -1;
        sequenceOutFrame_ = -1;
        timeline_->setInOutRange(-1, -1);
        timeline_->setPlayheadFrame(range.start);
    }
    setDirty(true);
    refreshEditor();
    updateProgramFrame(timeline_->playheadFrame());
}

void MainWindow::splitSelectedClips(const core::Frame frame) {
    if (timeline_ == nullptr) {
        return;
    }
    std::vector<core::EditCommand> commands;
    std::vector<core::ClipId> ids = timeline_->selectedClipIds();
    for (const core::ClipId selectedId : timeline_->selectedClipIds()) {
        if (const core::Clip* selected = editSession_.sequence().findClip(selectedId);
            selected != nullptr && selected->linkId) {
            for (const core::Track& track : editSession_.sequence().tracks()) {
                for (const core::Clip& clip : track.clips) {
                    if (clip.linkId == selected->linkId &&
                        std::ranges::find(ids, clip.id) == ids.end()) {
                        ids.push_back(clip.id);
                    }
                }
            }
        }
    }
    std::unordered_map<std::uint64_t, core::LinkId> rightLinks;
    for (const core::ClipId id : ids) {
        if (const core::Clip* clip = editSession_.sequence().findClip(id);
            clip != nullptr && frame > clip->timeline.start && frame < clip->timeline.end()) {
            core::LinkId rightLink;
            if (clip->linkId) {
                auto [entry, inserted] = rightLinks.try_emplace(clip->linkId.value);
                if (inserted) {
                    entry->second = core::LinkId{nextLinkId_++};
                }
                rightLink = entry->second;
            }
            commands.push_back(core::SplitClipCommand{id, frame, rightLink});
        }
    }
    if (commands.empty()) {
        statusBar()->showMessage(tr("The playhead is not inside the selected clips"), 3000);
        return;
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = "Split selected clips",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not split clips: %1").arg(QString::fromStdString(result.message)), 5000);
        return;
    }
    setDirty(true);
    refreshEditor();
    timeline_->setSelectedClipIds(result.createdClips);
}

void MainWindow::setSelectedFades(const core::Frame fadeIn, const core::Frame fadeOut) {
    if (timeline_ == nullptr || timeline_->selectedClipIds().empty()) {
        return;
    }
    std::vector<core::EditCommand> commands;
    std::vector<core::ClipId> ids = timeline_->selectedClipIds();
    for (const core::ClipId selectedId : timeline_->selectedClipIds()) {
        if (const core::Clip* selected = editSession_.sequence().findClip(selectedId);
            selected != nullptr && selected->linkId) {
            for (const core::Track& track : editSession_.sequence().tracks()) {
                for (const core::Clip& clip : track.clips) {
                    if (clip.linkId == selected->linkId &&
                        std::ranges::find(ids, clip.id) == ids.end()) {
                        ids.push_back(clip.id);
                    }
                }
            }
        }
    }
    for (const core::ClipId id : ids) {
        if (const core::Clip* clip = editSession_.sequence().findClip(id)) {
            commands.push_back(core::SetClipFadesCommand{
                id,
                fadeIn < 0 ? clip->fadeInFrames : std::min(fadeIn, clip->timeline.duration),
                fadeOut < 0 ? clip->fadeOutFrames : std::min(fadeOut, clip->timeline.duration),
            });
        }
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = "Change clip fades",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not change fades: %1").arg(QString::fromStdString(result.message)), 5000);
        return;
    }
    setDirty(true);
    refreshEditor();
    updateProgramFrame(timeline_->playheadFrame());
}

void MainWindow::setSelectedTransitions(const core::Frame videoFrames,
                                        const core::Frame audioFrames) {
    if (timeline_ == nullptr || timeline_->selectedClipIds().empty()) return;
    std::vector<core::ClipId> ids = timeline_->selectedClipIds();
    for (const core::ClipId selectedId : timeline_->selectedClipIds()) {
        const core::Clip* selected = editSession_.sequence().findClip(selectedId);
        if (selected == nullptr || !selected->linkId) continue;
        for (const core::Track& track : editSession_.sequence().tracks()) {
            for (const core::Clip& clip : track.clips) {
                if (clip.linkId == selected->linkId &&
                    std::ranges::find(ids, clip.id) == ids.end()) {
                    ids.push_back(clip.id);
                }
            }
        }
    }
    std::vector<core::EditCommand> commands;
    for (const core::Track& track : editSession_.sequence().tracks()) {
        for (const core::Clip& clip : track.clips) {
            if (std::ranges::find(ids, clip.id) == ids.end()) continue;
            const core::Frame video = track.kind == core::TrackKind::Video
                ? (videoFrames < 0 ? clip.videoTransitionInFrames
                                   : std::min(videoFrames, clip.timeline.duration))
                : 0;
            const core::Frame audio = track.kind == core::TrackKind::Audio
                ? (audioFrames < 0 ? clip.audioTransitionInFrames
                                   : std::min(audioFrames, clip.timeline.duration))
                : 0;
            commands.push_back(core::SetClipTransitionsCommand{clip.id, video, audio});
        }
    }
    const core::EditResult result = editSession_.apply(core::TransactionEnvelope{
        .baseRevision = editSession_.sequence().revision(),
        .label = "Change clip transitions",
        .commands = std::move(commands),
    });
    if (!result.succeeded()) {
        statusBar()->showMessage(
            tr("Could not change transition: %1")
                .arg(QString::fromStdString(result.message)), 5000);
        return;
    }
    setDirty(true);
    refreshEditor();
    updateProgramFrame(timeline_->playheadFrame());
}

} // namespace videx::ui
