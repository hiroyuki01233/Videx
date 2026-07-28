#pragma once

#include <QDoubleSpinBox>
#include <QWidget>

class QFocusEvent;
class QHBoxLayout;
class QLabel;
class QString;

namespace videx::ui {

class MixedDoubleSpinBox final : public QDoubleSpinBox {
  public:
    explicit MixedDoubleSpinBox(QWidget* parent = nullptr);

    void setMixed(bool mixed);
    [[nodiscard]] bool isMixed() const noexcept;

  protected:
    QString textFromValue(double value) const override;
    void focusInEvent(QFocusEvent* event) override;
    void stepBy(int steps) override;

  private:
    bool mixed_ = false;
};

class PropertyRow final : public QWidget {
  public:
    explicit PropertyRow(const QString& label, QWidget* editor, QWidget* parent = nullptr);

    void addTrailingWidget(QWidget* widget);
    void setLabel(const QString& label);
    void setMixed(bool mixed);

  private:
    QLabel* label_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
};

} // namespace videx::ui
