#include "title_store.hpp"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace videx::ui {

bool isTitleAsset(const QJsonObject& metadata) {
    return metadata.value(QStringLiteral("kind")).toString() == QStringLiteral("title");
}

TitleStyle titleStyleFromMetadata(const QJsonObject& metadata) {
    TitleStyle style;
    style.text = metadata.value(QStringLiteral("title_text")).toString();
    style.fontFamily = metadata.value(QStringLiteral("title_font")).toString();
    style.positionX = metadata.value(QStringLiteral("title_pos_x")).toDouble(0.5);
    style.positionY = metadata.value(QStringLiteral("title_pos_y")).toDouble(0.5);
    style.fontSize = metadata.value(QStringLiteral("title_size")).toDouble(64.0);
    style.textColor = static_cast<std::uint32_t>(
        metadata.value(QStringLiteral("title_color")).toString().toULongLong());
    style.backgroundColor = static_cast<std::uint32_t>(
        metadata.value(QStringLiteral("title_background")).toString().toULongLong());
    style.bold = metadata.value(QStringLiteral("title_bold")).toBool(false);
    style.italic = metadata.value(QStringLiteral("title_italic")).toBool(false);
    return style;
}

void writeTitleStyleToMetadata(const TitleStyle& style, QJsonObject& metadata) {
    metadata.insert(QStringLiteral("title_text"), style.text);
    metadata.insert(QStringLiteral("title_font"), style.fontFamily);
    metadata.insert(QStringLiteral("title_size"), style.fontSize);
    metadata.insert(QStringLiteral("title_pos_x"), style.positionX);
    metadata.insert(QStringLiteral("title_pos_y"), style.positionY);
    metadata.insert(QStringLiteral("title_color"), QString::number(style.textColor));
    metadata.insert(QStringLiteral("title_background"),
                    QString::number(style.backgroundColor));
    metadata.insert(QStringLiteral("title_bold"), style.bold);
    metadata.insert(QStringLiteral("title_italic"), style.italic);
}

QJsonObject titleAssetMetadata(const TitleStyle& style) {
    QJsonObject metadata{
        {QStringLiteral("kind"), QStringLiteral("title")},
        {QStringLiteral("duration_us"), 0},
        {QStringLiteral("streams"),
         QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("video")}}}},
    };
    writeTitleStyleToMetadata(style, metadata);
    return metadata;
}

bool writeTitleImage(const QString& path, const TitleStyle& style) {
    // Rasterize at 1080p; the media reader scales the still to any output
    // size, and fontSize keeps its 720p-relative meaning via uiScale.
    constexpr int canvasWidth = 1920;
    constexpr int canvasHeight = 1080;
    constexpr double uiScale = canvasHeight / 720.0;
    QImage canvas(canvasWidth, canvasHeight, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font;
    if (!style.fontFamily.isEmpty()) {
        font.setFamily(style.fontFamily);
    }
    font.setPixelSize(
        std::max(8, static_cast<int>(std::lround(style.fontSize * uiScale))));
    font.setBold(style.bold);
    font.setItalic(style.italic);
    painter.setFont(font);
    const auto colorFromRgba = [](const std::uint32_t rgba) {
        return QColor(static_cast<int>((rgba >> 24U) & 0xFFU),
                      static_cast<int>((rgba >> 16U) & 0xFFU),
                      static_cast<int>((rgba >> 8U) & 0xFFU),
                      static_cast<int>(rgba & 0xFFU));
    };
    const double padX = 12.0 * uiScale;
    const double padY = 8.0 * uiScale;
    const QRectF bounds = painter.boundingRect(
        QRectF(0.0, 0.0, canvasWidth - padX * 4.0, canvasHeight - padY * 4.0),
        Qt::AlignCenter | Qt::TextWordWrap, style.text);
    QRectF positioned = bounds.adjusted(-padX, -padY, padX, padY);
    positioned.moveCenter(
        QPointF(style.positionX * canvasWidth, style.positionY * canvasHeight));
    const QColor background = colorFromRgba(style.backgroundColor);
    if (background.alpha() > 0) {
        painter.fillRect(positioned, background);
    }
    painter.setPen(colorFromRgba(style.textColor));
    painter.drawText(positioned.adjusted(padX, padY, -padX, -padY),
                     Qt::AlignCenter | Qt::TextWordWrap, style.text);
    painter.end();
    // The media worker's FFmpeg build has no image decoders, so titles are
    // stored as a raw RGBA still: "VXTI" magic + u32 width/height + rows.
    const QImage converted = canvas.convertToFormat(QImage::Format_RGBA8888);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write("VXTI", 4);
    const quint32 width = static_cast<quint32>(converted.width());
    const quint32 height = static_cast<quint32>(converted.height());
    file.write(reinterpret_cast<const char*>(&width), 4);
    file.write(reinterpret_cast<const char*>(&height), 4);
    for (int y = 0; y < converted.height(); ++y) {
        file.write(reinterpret_cast<const char*>(converted.constScanLine(y)),
                   static_cast<qint64>(converted.width()) * 4);
    }
    return file.commit();
}

bool ensureTitleImage(const QString& path, const QJsonObject& metadata) {
    if (!isTitleAsset(metadata)) {
        return true;
    }
    if (QFileInfo::exists(path)) {
        return true;
    }
    return writeTitleImage(path, titleStyleFromMetadata(metadata));
}

QImage readTitleImage(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    char magic[4] = {};
    quint32 width = 0;
    quint32 height = 0;
    if (file.read(magic, 4) != 4 || std::memcmp(magic, "VXTI", 4) != 0 ||
        file.read(reinterpret_cast<char*>(&width), 4) != 4 ||
        file.read(reinterpret_cast<char*>(&height), 4) != 4 || width == 0 ||
        height == 0 || width > 8192U || height > 8192U) {
        return {};
    }
    QImage image(static_cast<int>(width), static_cast<int>(height),
                 QImage::Format_RGBA8888);
    const qint64 rowBytes = static_cast<qint64>(width) * 4;
    for (quint32 row = 0; row < height; ++row) {
        if (file.read(reinterpret_cast<char*>(image.scanLine(static_cast<int>(row))),
                      rowBytes) != rowBytes) {
            return {};
        }
    }
    return image;
}

} // namespace videx::ui
