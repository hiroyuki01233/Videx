#pragma once

#include <QFrame>
#include <QString>

class QBoxLayout;
class QToolButton;
class QVBoxLayout;

namespace videx::ui {

class PropertySection final : public QFrame {
    Q_OBJECT

  public:
    explicit PropertySection(const QString& title, const QString& settingsKey,
                             QWidget* parent = nullptr);

    [[nodiscard]] QVBoxLayout* contentLayout() const noexcept;
    [[nodiscard]] bool isExpanded() const noexcept;
    void setExpanded(bool expanded);
    void setSummary(const QString& summary);
    void setSectionEnabled(bool enabled);

  signals:
    void expansionChanged(bool expanded);

  private:
    void updateHeader();

    QString title_;
    QString settingsKey_;
    QString summary_;
    QToolButton* headerButton_ = nullptr;
    QWidget* content_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    bool expanded_ = true;
};

} // namespace videx::ui
