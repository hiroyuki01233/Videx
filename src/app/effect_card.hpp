#pragma once

#include <videx/core/timeline.hpp>

#include <QFrame>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QToolButton;

namespace videx::ui {

struct EffectCardState final {
    core::EffectId id;
    core::EffectType type = core::EffectType::Brightness;
    QString name;
    QString parameterName;
    QString suffix;
    bool enabled = true;
    double minimum = 0.0;
    double maximum = 1.0;
    double amount = 0.0;
    bool keyAtPlayhead = false;
    bool animated = false;
    core::KeyframeInterpolation interpolation = core::KeyframeInterpolation::Linear;
};

struct EffectParameterDescriptor final {
    QString name;
    QString suffix;
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.05;
    double neutral = 0.0;
};

[[nodiscard]] EffectParameterDescriptor effectParameterDescriptor(core::EffectType type);

class EffectCard final : public QFrame {
    Q_OBJECT

  public:
    using ChangeHandler = std::function<void(bool enabled, double amount)>;
    using ActionHandler = std::function<void()>;
    using KeyHandler = std::function<void(bool add)>;
    using InterpolationHandler =
        std::function<void(core::KeyframeInterpolation interpolation)>;

    explicit EffectCard(QWidget* parent = nullptr);

    void setState(const EffectCardState& state);
    [[nodiscard]] core::EffectId effectId() const noexcept;
    void setHandlers(ChangeHandler change, ActionHandler remove, KeyHandler key,
                     ActionHandler previousKey, ActionHandler nextKey,
                     InterpolationHandler interpolation);

  private:
    void notifyChanged();

    core::EffectId id_;
    QLabel* titleLabel_ = nullptr;
    QLabel* parameterLabel_ = nullptr;
    QCheckBox* enabledCheck_ = nullptr;
    QDoubleSpinBox* amountSpin_ = nullptr;
    QToolButton* resetButton_ = nullptr;
    QToolButton* removeButton_ = nullptr;
    QToolButton* keyButton_ = nullptr;
    QToolButton* previousKeyButton_ = nullptr;
    QToolButton* nextKeyButton_ = nullptr;
    QComboBox* interpolationCombo_ = nullptr;
    bool updating_ = false;
    ChangeHandler changeHandler_;
    ActionHandler removeHandler_;
    KeyHandler keyHandler_;
    ActionHandler previousKeyHandler_;
    ActionHandler nextKeyHandler_;
    InterpolationHandler interpolationHandler_;
};

} // namespace videx::ui
