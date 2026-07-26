#include <videx/render/qt_monitor_widget.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointF>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "monitor test failed: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    using videx::render::MonitorCrop;
    using videx::render::MonitorTransform;
    using videx::render::QtMonitorWidget;

    QtMonitorWidget monitor(QStringLiteral("Test Monitor"));
    monitor.resize(700, 460);
    monitor.show();
    QCoreApplication::processEvents();

    QImage frame(1280, 720, QImage::Format_ARGB32);
    frame.fill(QColor(90, 120, 150));
    monitor.setFrame(frame);
    monitor.setEditTarget(true, MonitorTransform{}, MonitorCrop{});
    QCoreApplication::processEvents();

    QWidget* viewport = monitor.viewportWidget();
    expect(viewport != nullptr, "viewport must exist");
    if (viewport == nullptr) {
        return 1;
    }

    std::vector<MonitorTransform> transientStates;
    std::vector<MonitorTransform> committedStates;
    monitor.setTransformEditHandler(
        [&](const MonitorTransform& state, const bool committed) {
            if (committed) {
                committedStates.push_back(state);
            } else {
                transientStates.push_back(state);
            }
        });

    const auto sendMouse = [viewport](const QEvent::Type type, const QPointF& position,
                                      const Qt::MouseButton button,
                                      const Qt::MouseButtons buttons,
                                      const Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPointF global(viewport->mapToGlobal(position.toPoint()));
        QMouseEvent event(type, position, position, global, button, buttons, modifiers);
        QApplication::sendEvent(viewport, &event);
    };

    const QPointF center(viewport->width() * 0.5 + 70.0, viewport->height() * 0.5 + 40.0);

    // Hover over the clip body must show the move cursor before any drag starts.
    sendMouse(QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton);
    expect(viewport->cursor().shape() == Qt::SizeAllCursor,
           "hovering the clip body should show the SizeAll cursor");

    // Position drag: transient states while moving, exactly one commit on release,
    // and the committed position matches the transient end value (no jump back).
    sendMouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, center + QPointF(60.0, 30.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, center + QPointF(90.0, 45.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, center + QPointF(90.0, 45.0), Qt::LeftButton,
              Qt::NoButton);
    expect(!transientStates.empty(), "position drag should stream transient states");
    expect(committedStates.size() == 1U, "one drag must produce exactly one commit");
    if (!committedStates.empty()) {
        expect(committedStates.back().positionX > 0.0,
               "dragging right should increase position X");
        expect(committedStates.back().positionY > 0.0,
               "dragging down should increase position Y");
        if (!transientStates.empty()) {
            expect(std::abs(committedStates.back().positionX -
                            transientStates.back().positionX) < 1e-9,
                   "committed value must equal the last transient value");
        }
    }

    // Escape must cancel the drag without committing and restore the start value.
    const MonitorTransform committedBase =
        committedStates.empty() ? MonitorTransform{} : committedStates.back();
    monitor.setEditTarget(true, committedBase, MonitorCrop{});
    transientStates.clear();
    committedStates.clear();
    sendMouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, center + QPointF(-70.0, 0.0), Qt::NoButton, Qt::LeftButton);
    {
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(viewport, &escape);
    }
    sendMouse(QEvent::MouseButtonRelease, center + QPointF(-70.0, 0.0), Qt::LeftButton,
              Qt::NoButton);
    expect(committedStates.empty(), "escape must cancel the drag without a commit");
    expect(!transientStates.empty() &&
               std::abs(transientStates.back().positionX - committedBase.positionX) < 1e-9,
           "escape must restore the drag-start value");

    // Shift+drag constrains the position to one axis.
    transientStates.clear();
    committedStates.clear();
    sendMouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, center + QPointF(80.0, 18.0), Qt::NoButton, Qt::LeftButton,
              Qt::ShiftModifier);
    sendMouse(QEvent::MouseButtonRelease, center + QPointF(80.0, 18.0), Qt::LeftButton,
              Qt::NoButton, Qt::ShiftModifier);
    expect(committedStates.size() == 1U, "shift drag should commit once");
    if (!committedStates.empty()) {
        expect(std::abs(committedStates.back().positionY - committedBase.positionY) < 1e-9,
               "shift drag with a dominant X delta must not change Y");
    }

    // Zoom API: 100% then fit must change the reported state.
    monitor.setZoomFactor(1.0);
    expect(!monitor.zoomIsFit() && std::abs(monitor.zoomFactor() - 1.0) < 1e-9,
           "setZoomFactor(1.0) should report 100% zoom");
    monitor.setZoomFit();
    expect(monitor.zoomIsFit(), "setZoomFit should report fit mode");

    // Crop mode: dragging the left edge inward must report a positive left crop
    // and commit exactly once.
    monitor.setEditTarget(true, MonitorTransform{}, MonitorCrop{});
    monitor.setCropEditMode(true);
    std::vector<MonitorCrop> committedCrops;
    monitor.setCropEditHandler([&](const MonitorCrop& crop, const bool committed) {
        if (committed) {
            committedCrops.push_back(crop);
        }
    });
    QCoreApplication::processEvents();
    // The left crop handle sits at the mid-left of the displayed frame. With fit
    // zoom the frame spans the viewport width (16:9 in a wider viewport leaves
    // side bars, so locate the frame rect from the widget geometry).
    const double frameAspect = 1280.0 / 720.0;
    double displayWidth = viewport->width();
    double displayHeight = displayWidth / frameAspect;
    if (displayHeight > viewport->height()) {
        displayHeight = viewport->height();
        displayWidth = displayHeight * frameAspect;
    }
    const double frameLeft = (viewport->width() - displayWidth) * 0.5;
    const QPointF leftHandle(frameLeft, viewport->height() * 0.5);
    sendMouse(QEvent::MouseButtonPress, leftHandle, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, leftHandle + QPointF(displayWidth * 0.25, 0.0),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, leftHandle + QPointF(displayWidth * 0.25, 0.0),
              Qt::LeftButton, Qt::NoButton);
    expect(committedCrops.size() == 1U, "crop drag should commit once");
    if (!committedCrops.empty()) {
        expect(committedCrops.back().left > 0.15 && committedCrops.back().left < 0.35,
               "left crop should be near the dragged quarter of the frame");
    }

    // Clicking the frame without an edit target must request a selection so the
    // host can pick the clip under the playhead.
    monitor.setCropEditMode(false);
    monitor.setEditTarget(false, MonitorTransform{}, MonitorCrop{});
    int selectRequests = 0;
    int outsideClicks = 0;
    double selectX = -1.0;
    double selectY = -1.0;
    monitor.setSelectRequestHandler([&](const bool insideFrame, const double x,
                                        const double y) {
        if (insideFrame) {
            ++selectRequests;
            selectX = x;
            selectY = y;
        } else {
            ++outsideClicks;
        }
    });
    sendMouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    expect(selectRequests == 1, "clicking the frame without a target must request selection");
    const double expectedSelectX = (center.x() - frameLeft) / displayWidth;
    const double expectedSelectY =
        (center.y() - (viewport->height() - displayHeight) * 0.5) / displayHeight;
    expect(std::abs(selectX - expectedSelectX) < 0.02 &&
               std::abs(selectY - expectedSelectY) < 0.02,
           "a frame click must report its normalized canvas position");
    // The 16:9 frame letterboxes inside the 700x460 viewport, so a click near
    // the top edge lands outside the picture and must report an outside click.
    const QPointF letterbox(viewport->width() * 0.5, 4.0);
    sendMouse(QEvent::MouseButtonPress, letterbox, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, letterbox, Qt::LeftButton, Qt::NoButton);
    expect(outsideClicks == 1, "clicking the canvas outside the picture must report outside");

    // A confined content rect (a title's text box) must limit the grab zone:
    // clicks inside the frame but outside the box fall through to selection,
    // clicks inside the box grab the target instead of re-selecting.
    monitor.setEditTarget(true, MonitorTransform{}, MonitorCrop{});
    monitor.setEditTargetContentRect(0.4, 0.4, 0.2, 0.2);
    QCoreApplication::processEvents();
    const auto canvasToViewPoint = [&](const double normalizedX,
                                       const double normalizedY) {
        return QPointF(frameLeft + displayWidth * normalizedX,
                       (viewport->height() - displayHeight) * 0.5 +
                           displayHeight * normalizedY);
    };
    const int selectionsBeforeConfined = selectRequests;
    const QPointF outsideBox = canvasToViewPoint(0.25, 0.5);
    sendMouse(QEvent::MouseButtonPress, outsideBox, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, outsideBox, Qt::LeftButton, Qt::NoButton);
    expect(selectRequests == selectionsBeforeConfined + 1,
           "clicking beside a confined target must fall through to selection");
    const QPointF insideBox = canvasToViewPoint(0.45, 0.5);
    sendMouse(QEvent::MouseButtonPress, insideBox, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, insideBox, Qt::LeftButton, Qt::NoButton);
    expect(selectRequests == selectionsBeforeConfined + 1,
           "clicking inside a confined target must grab it, not re-select");
    monitor.setEditTarget(false, MonitorTransform{}, MonitorCrop{});
    monitor.setEditTargetContentRect(0.0, 0.0, 1.0, 1.0);
    // Later overlay assertions expect the pre-confinement counter value.
    selectRequests = selectionsBeforeConfined;

    // Dragging the caption overlay streams transient positions and commits once
    // on release with the moved position.
    monitor.setOverlayText(QStringLiteral("Title"));
    monitor.setOverlayStyle(0.5, 0.8, 24.0, 0xFFFFFFFFU, 0x99000000U, false, false);
    QCoreApplication::processEvents();
    struct TextEvent final {
        double x = 0.0;
        double y = 0.0;
        bool committed = false;
    };
    std::vector<TextEvent> textEvents;
    monitor.setTextDragHandler([&](const double x, const double y, const bool committed) {
        textEvents.push_back({x, y, committed});
    });
    const QPointF labelCenter(monitor.width() * 0.5 - viewport->x(),
                              monitor.height() * 0.8 - viewport->y());
    sendMouse(QEvent::MouseButtonPress, labelCenter, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, labelCenter + QPointF(40.0, -30.0), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, labelCenter + QPointF(40.0, -30.0),
              Qt::LeftButton, Qt::NoButton);
    expect(!textEvents.empty() && textEvents.back().committed,
           "text drag must end with a committed position");
    if (!textEvents.empty() && textEvents.back().committed) {
        expect(textEvents.back().x > 0.5 && textEvents.back().y < 0.8,
               "text drag right/up must move the normalized position accordingly");
    }
    expect(selectRequests == 1, "text drag must not trigger clip selection");

    // A plain click on the overlay (no movement) must not commit an edit.
    const std::size_t eventsBeforeClick = textEvents.size();
    const QPointF movedLabelCenter(monitor.width() * textEvents.back().x - viewport->x(),
                                   monitor.height() * textEvents.back().y - viewport->y());
    sendMouse(QEvent::MouseButtonPress, movedLabelCenter, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, movedLabelCenter, Qt::LeftButton, Qt::NoButton);
    expect(textEvents.size() == eventsBeforeClick,
           "clicking text without moving must not commit a move");

    return failures == 0 ? 0 : 1;
}
