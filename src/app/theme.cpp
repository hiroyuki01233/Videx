#include "theme.hpp"

#include <QFile>

namespace videx::ui {

QPalette darkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#181a1f")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e7e9ee")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#111318")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#20232a")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#272b33")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#f4f5f7")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e7e9ee")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#292d35")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e7e9ee")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#4c8dff")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#7f8794")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText,
                     QColor(QStringLiteral("#747b86")));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#747b86")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(QStringLiteral("#747b86")));
    return palette;
}

QString applicationStyleSheet() {
    QFile file(QStringLiteral(":/theme/videx-dark.qss"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace videx::ui
