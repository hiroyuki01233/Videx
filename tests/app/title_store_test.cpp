#include "title_store.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <cstdint>
#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::uint32_t littleEndian32(const QByteArray& bytes, const int offset) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1]))
            << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2]))
            << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3]))
            << 24U);
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    using namespace videx::ui;

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return fail("could not create a temporary directory");
    }
    const QString path = temporaryDirectory.filePath(QStringLiteral("title.vxtitle"));

    const TitleStyle style{.text = QStringLiteral("T10 タイトル"),
                           .fontFamily = {},
                           .positionX = 0.25,
                           .positionY = 0.75,
                           .fontSize = 48.0,
                           .textColor = 0xFF2040FFU,
                           .backgroundColor = 0x102030C0U,
                           .bold = true,
                           .italic = true};
    if (!writeTitleImage(path, style)) {
        return fail("could not write the title image");
    }

    QFile reader(path);
    if (!reader.open(QIODevice::ReadOnly)) {
        return fail("could not reopen the title image");
    }
    const QByteArray header = reader.read(12);
    reader.close();
    if (header.size() != 12 || !header.startsWith("VXTI") ||
        littleEndian32(header, 4) != 1920U || littleEndian32(header, 8) != 1080U) {
        return fail("title image header is not VXTI 1920x1080");
    }

    QJsonObject metadata = titleAssetMetadata(style);
    if (!isTitleAsset(metadata)) {
        return fail("title metadata must identify as a title asset");
    }
    const TitleStyle loaded = titleStyleFromMetadata(metadata);
    if (loaded.text != style.text || loaded.fontFamily != style.fontFamily ||
        loaded.positionX != style.positionX || loaded.positionY != style.positionY ||
        loaded.fontSize != style.fontSize || loaded.textColor != style.textColor ||
        loaded.backgroundColor != style.backgroundColor || loaded.bold != style.bold ||
        loaded.italic != style.italic) {
        return fail("title style did not round-trip through metadata");
    }

    if (!QFile::remove(path)) {
        return fail("could not delete the title image fixture");
    }
    if (!ensureTitleImage(path, metadata) || !QFile::exists(path)) {
        return fail("a missing title image must be regenerated from metadata");
    }
    if (!ensureTitleImage(path, metadata)) {
        return fail("ensure must succeed when the image already exists");
    }
    QJsonObject nonTitle{{QStringLiteral("kind"), QStringLiteral("media")}};
    if (!ensureTitleImage(temporaryDirectory.filePath(QStringLiteral("none.vxtitle")),
                          nonTitle)) {
        return fail("ensure must be a no-op success for non-title assets");
    }
    return 0;
}
