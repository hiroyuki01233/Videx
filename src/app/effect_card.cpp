#include "effect_card.hpp"
#include "property_row.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace videx::ui {

EffectParameterDescriptor effectParameterDescriptor(const core::EffectType type) {
    switch (type) {
    case core::EffectType::Brightness:
        return {.name = QCoreApplication::translate("EffectCard", "Exposure"),
                .minimum = -1.0,
                .maximum = 1.0};
    case core::EffectType::Contrast:
        return {.name = QCoreApplication::translate("EffectCard", "Contrast"),
                .minimum = -1.0,
                .maximum = 3.0};
    case core::EffectType::Saturation:
        return {.name = QCoreApplication::translate("EffectCard", "Saturation"),
                .minimum = -1.0,
                .maximum = 3.0};
    case core::EffectType::Blur:
        return {.name = QCoreApplication::translate("EffectCard", "Radius"),
                .suffix = QCoreApplication::translate("EffectCard", " px"),
                .minimum = 0.0,
                .maximum = 50.0};
    case core::EffectType::Vignette:
        return {.name = QCoreApplication::translate("EffectCard", "Amount"),
                .minimum = 0.0,
                .maximum = 1.0};
    }
    return {};
}

EffectCard::EffectCard(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("effectCard"));
    setFrameShape(QFrame::StyledPanel);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 7, 8, 8);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    header->setSpacing(5);
    auto* dragHandle = new QLabel(QStringLiteral("≡"), this);
    dragHandle->setObjectName(QStringLiteral("effectDragHandle"));
    dragHandle->setToolTip(tr("Drag to change render order"));
    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("effectCardTitle"));
    enabledCheck_ = new QCheckBox(tr("On"), this);
    enabledCheck_->setToolTip(tr("Enable or bypass this effect"));
    resetButton_ = new QToolButton(this);
    resetButton_->setText(QStringLiteral("↺"));
    resetButton_->setToolTip(tr("Reset this effect"));
    removeButton_ = new QToolButton(this);
    removeButton_->setText(QStringLiteral("×"));
    removeButton_->setToolTip(tr("Remove this effect"));
    header->addWidget(dragHandle);
    header->addWidget(titleLabel_, 1);
    header->addWidget(enabledCheck_);
    header->addWidget(resetButton_);
    header->addWidget(removeButton_);
    root->addLayout(header);

    amountSpin_ = new QDoubleSpinBox(this);
    amountSpin_->setDecimals(3);
    amountSpin_->setSingleStep(0.05);
    amountSpin_->setKeyboardTracking(false);
    keyButton_ = new QToolButton(this);
    keyButton_->setCheckable(true);
    keyButton_->setText(QStringLiteral("◆"));
    keyButton_->setToolTip(tr("Add or remove a keyframe at the playhead"));
    auto* parameterRow = new PropertyRow(QString{}, amountSpin_, this);
    parameterRow->addTrailingWidget(keyButton_);
    root->addWidget(parameterRow);
    parameterLabel_ = parameterRow->findChild<QLabel*>(
        QStringLiteral("propertyRowLabel"));

    auto* animationRow = new QHBoxLayout;
    animationRow->setSpacing(5);
    previousKeyButton_ = new QToolButton(this);
    previousKeyButton_->setText(QStringLiteral("◀"));
    previousKeyButton_->setToolTip(tr("Previous keyframe"));
    nextKeyButton_ = new QToolButton(this);
    nextKeyButton_->setText(QStringLiteral("▶"));
    nextKeyButton_->setToolTip(tr("Next keyframe"));
    interpolationCombo_ = new QComboBox(this);
    interpolationCombo_->addItem(tr("No key at playhead"), QVariant{});
    interpolationCombo_->addItem(
        tr("Linear"), static_cast<int>(core::KeyframeInterpolation::Linear));
    interpolationCombo_->addItem(
        tr("Hold"), static_cast<int>(core::KeyframeInterpolation::Hold));
    interpolationCombo_->addItem(
        tr("Ease"), static_cast<int>(core::KeyframeInterpolation::Ease));
    interpolationCombo_->addItem(
        tr("Ease In"), static_cast<int>(core::KeyframeInterpolation::EaseIn));
    interpolationCombo_->addItem(
        tr("Ease Out"), static_cast<int>(core::KeyframeInterpolation::EaseOut));
    interpolationCombo_->addItem(
        tr("Ease In-Out"), static_cast<int>(core::KeyframeInterpolation::EaseInOut));
    animationRow->addWidget(previousKeyButton_);
    animationRow->addWidget(nextKeyButton_);
    animationRow->addWidget(interpolationCombo_, 1);
    root->addLayout(animationRow);

    connect(enabledCheck_, &QCheckBox::toggled, this, [this] { notifyChanged(); });
    connect(amountSpin_, &QDoubleSpinBox::editingFinished, this,
            [this] { notifyChanged(); });
    connect(resetButton_, &QToolButton::clicked, this, [this] {
        amountSpin_->setValue(0.0);
        notifyChanged();
    });
    connect(removeButton_, &QToolButton::clicked, this, [this] {
        if (removeHandler_) removeHandler_();
    });
    connect(keyButton_, &QToolButton::clicked, this, [this](const bool checked) {
        if (!updating_ && keyHandler_) keyHandler_(checked);
    });
    connect(previousKeyButton_, &QToolButton::clicked, this, [this] {
        if (previousKeyHandler_) previousKeyHandler_();
    });
    connect(nextKeyButton_, &QToolButton::clicked, this, [this] {
        if (nextKeyHandler_) nextKeyHandler_();
    });
    connect(interpolationCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_ || interpolationCombo_->currentIndex() == 0 ||
            !interpolationHandler_) {
            return;
        }
        interpolationHandler_(static_cast<core::KeyframeInterpolation>(
            interpolationCombo_->currentData().toInt()));
    });
}

void EffectCard::setState(const EffectCardState& state) {
    updating_ = true;
    id_ = state.id;
    titleLabel_->setText(state.name);
    parameterLabel_->setText(state.parameterName);
    enabledCheck_->setChecked(state.enabled);
    amountSpin_->setRange(state.minimum, state.maximum);
    amountSpin_->setSuffix(state.suffix);
    amountSpin_->setValue(state.amount);
    keyButton_->setChecked(state.keyAtPlayhead);
    keyButton_->setProperty("keyAtPlayhead", state.keyAtPlayhead);
    previousKeyButton_->setEnabled(state.animated);
    nextKeyButton_->setEnabled(state.animated);
    interpolationCombo_->setEnabled(state.keyAtPlayhead);
    interpolationCombo_->setCurrentIndex(
        state.keyAtPlayhead
            ? interpolationCombo_->findData(static_cast<int>(state.interpolation))
            : 0);
    updating_ = false;
}

core::EffectId EffectCard::effectId() const noexcept {
    return id_;
}

void EffectCard::setHandlers(ChangeHandler change, ActionHandler remove, KeyHandler key,
                             ActionHandler previousKey, ActionHandler nextKey,
                             InterpolationHandler interpolation) {
    changeHandler_ = std::move(change);
    removeHandler_ = std::move(remove);
    keyHandler_ = std::move(key);
    previousKeyHandler_ = std::move(previousKey);
    nextKeyHandler_ = std::move(nextKey);
    interpolationHandler_ = std::move(interpolation);
}

void EffectCard::notifyChanged() {
    if (!updating_ && changeHandler_) {
        changeHandler_(enabledCheck_->isChecked(), amountSpin_->value());
    }
}

} // namespace videx::ui
