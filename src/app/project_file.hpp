#pragma once

#include <videx/core/timeline.hpp>

#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace videx::ui {

struct ProjectAsset final {
    core::AssetId id;
    QString path;
    QJsonObject metadata;
};

struct ProjectData final {
    core::Sequence sequence;
    std::vector<ProjectAsset> assets;
};

[[nodiscard]] bool saveProjectFile(
    const QString& path, const ProjectData& project, QString& error);
[[nodiscard]] std::optional<ProjectData> loadProjectFile(const QString& path, QString& error);

} // namespace videx::ui

