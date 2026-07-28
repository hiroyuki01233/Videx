#pragma once

#include "selection_model.hpp"

#include <QWidget>

class QCheckBox;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace videx::ui {

class PropertySection;

class ContextRail final : public QWidget {
    Q_OBJECT

  public:
    explicit ContextRail(QWidget* parent = nullptr);

    void bindSelectionModel(SelectionModel* model);
    void addSection(PropertySection* section);
    void setPinnedWidget(QWidget* widget);
    void setEmptyMessage(const QString& message);
    [[nodiscard]] QVBoxLayout* sectionsLayout() const noexcept;

  signals:
    void enabledChanged(bool enabled);

  private:
    void updateSelection();

    SelectionModel* selectionModel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
    QLabel* kindLabel_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QCheckBox* enabledCheck_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* sectionsHost_ = nullptr;
    QVBoxLayout* sectionsLayout_ = nullptr;
    QVBoxLayout* pinnedLayout_ = nullptr;
    SelectionKind currentKind_ = SelectionKind::None;
};

} // namespace videx::ui
