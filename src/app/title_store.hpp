#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace videx::ui {

// Style of a title clip. Titles are ordinary video clips whose asset is an
// app-rendered raster (.vxtitle); the style persists in the asset metadata,
// which is sufficient to regenerate the raster at any time.
struct TitleStyle final {
    QString text;
    QString fontFamily;
    double positionX = 0.5;
    double positionY = 0.5;
    double fontSize = 64.0;
    std::uint32_t textColor = 0xFFFFFFFFU;
    std::uint32_t backgroundColor = 0;
    bool bold = false;
    bool italic = false;
};

[[nodiscard]] bool isTitleAsset(const QJsonObject& metadata);
[[nodiscard]] TitleStyle titleStyleFromMetadata(const QJsonObject& metadata);
// Writes the title_* style keys into the metadata (other keys untouched).
void writeTitleStyleToMetadata(const TitleStyle& style, QJsonObject& metadata);
// Full metadata for a fresh title asset (kind, style, and stream shape).
[[nodiscard]] QJsonObject titleAssetMetadata(const TitleStyle& style);
// Rasterizes the style to the .vxtitle still at the given path.
[[nodiscard]] bool writeTitleImage(const QString& path, const TitleStyle& style);
// Regenerates a missing raster from metadata; no-op success when the file
// exists or the metadata does not describe a title.
[[nodiscard]] bool ensureTitleImage(const QString& path, const QJsonObject& metadata);

// Reads a .vxtitle raster back into a QImage (RGBA8888); null on failure.
[[nodiscard]] QImage readTitleImage(const QString& path);

} // namespace videx::ui
