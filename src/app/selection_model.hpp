#pragma once

#include <videx/core/timeline.hpp>

#include <QObject>
#include <QString>

#include <vector>

namespace videx::ui {

enum class SelectionKind {
    None,
    VideoClip,
    AudioClip,
    TitleClip,
    Caption,
    Track,
    Marker,
    Multiple,
};

struct SelectionState final {
    SelectionKind kind = SelectionKind::None;
    std::vector<core::ClipId> clipIds;
    core::ClipId primaryClip;
    core::TrackId track;
    QString title;
    QString subtitle;
    bool linked = false;

    auto operator<=>(const SelectionState&) const = default;
};

class SelectionModel final : public QObject {
    Q_OBJECT

  public:
    explicit SelectionModel(QObject* parent = nullptr);

    [[nodiscard]] const SelectionState& state() const noexcept;
    void setState(SelectionState state);
    void clear();

  signals:
    void selectionChanged();

  private:
    SelectionState state_;
};

[[nodiscard]] QString selectionKindLabel(SelectionKind kind);

} // namespace videx::ui
