#include "property_row.hpp"

#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>

namespace videx::ui {

MixedDoubleSpinBox::MixedDoubleSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {}

void MixedDoubleSpinBox::setMixed(const bool mixed) {
    if (mixed_ == mixed) return;
    mixed_ = mixed;
    setProperty("mixedValue", mixed);
    lineEdit()->setText(textFromValue(value()));
    update();
}

bool MixedDoubleSpinBox::isMixed() const noexcept {
    return mixed_;
}

QString MixedDoubleSpinBox::textFromValue(const double value) const {
    return mixed_ ? QStringLiteral("—") : QDoubleSpinBox::textFromValue(value);
}

void MixedDoubleSpinBox::focusInEvent(QFocusEvent* event) {
    setMixed(false);
    QDoubleSpinBox::focusInEvent(event);
    selectAll();
}

void MixedDoubleSpinBox::stepBy(const int steps) {
    setMixed(false);
    QDoubleSpinBox::stepBy(steps);
}

PropertyRow::PropertyRow(const QString& label, QWidget* editor, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("propertyRow"));
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(6);
    label_ = new QLabel(label, this);
    label_->setObjectName(QStringLiteral("propertyRowLabel"));
    label_->setMinimumWidth(76);
    layout_->addWidget(label_);
    if (editor != nullptr) {
        layout_->addWidget(editor, 1);
    }
}

void PropertyRow::addTrailingWidget(QWidget* widget) {
    if (widget != nullptr) {
        layout_->addWidget(widget);
    }
}

void PropertyRow::setLabel(const QString& label) {
    label_->setText(label);
}

void PropertyRow::setMixed(const bool mixed) {
    setProperty("mixed", mixed);
    style()->unpolish(this);
    style()->polish(this);
}

} // namespace videx::ui
