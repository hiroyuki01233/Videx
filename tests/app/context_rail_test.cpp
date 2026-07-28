#include "context_rail.hpp"
#include "effect_card.hpp"
#include "property_row.hpp"
#include "property_section.hpp"
#include "selection_model.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSettings>
#include <QToolButton>

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    videx::ui::SelectionModel selection;
    int changeCount = 0;
    QObject::connect(&selection, &videx::ui::SelectionModel::selectionChanged,
                     [&changeCount] { ++changeCount; });
    const videx::ui::SelectionState videoState{
        .kind = videx::ui::SelectionKind::VideoClip,
        .clipIds = {videx::core::ClipId{4}},
        .primaryClip = videx::core::ClipId{4},
        .track = videx::core::TrackId{2},
        .title = QStringLiteral("Interview A"),
        .subtitle = QStringLiteral("V2 · 120 frames"),
        .linked = true,
    };
    selection.setState(videoState);
    selection.setState(videoState);
    expect(changeCount == 1, "selection model suppresses duplicate notifications");

    videx::ui::ContextRail rail;
    rail.bindSelectionModel(&selection);
    auto* kind = rail.findChild<QLabel*>(QStringLiteral("selectionKind"));
    auto* title = rail.findChild<QLabel*>(QStringLiteral("selectionTitle"));
    expect(kind != nullptr && kind->text() == QStringLiteral("VIDEO"),
           "context rail reflects selection kind");
    expect(title != nullptr && title->text() == QStringLiteral("Interview A"),
           "context rail reflects selection title");

    const QString sectionKey = QStringLiteral("context-rail-test");
    QSettings(QStringLiteral("Videx"), QStringLiteral("Videx"))
        .remove(QStringLiteral("contextRail/sections/") + sectionKey);
    auto* section =
        new videx::ui::PropertySection(QStringLiteral("Transform"), sectionKey);
    rail.addSection(section);
    section->setExpanded(false);
    expect(!section->isExpanded(), "property section collapses");
    section->setExpanded(true);
    expect(section->isExpanded(), "property section expands");

    videx::ui::MixedDoubleSpinBox mixedValue;
    mixedValue.setValue(42.0);
    mixedValue.setMixed(true);
    expect(mixedValue.isMixed() && mixedValue.text() == QStringLiteral("—"),
           "mixed numeric properties show an explicit em dash");
    mixedValue.setMixed(false);
    expect(!mixedValue.text().contains(QStringLiteral("—")),
           "mixed numeric properties recover their concrete value");

    videx::ui::EffectCard card;
    int changes = 0;
    bool removed = false;
    bool keyAdded = false;
    card.setHandlers(
        [&changes](bool, double) { ++changes; }, [&removed] { removed = true; },
        [&keyAdded](const bool add) { keyAdded = add; }, [] {}, [] {},
        [](videx::core::KeyframeInterpolation) {});
    card.setState({.id = videx::core::EffectId{9},
                   .type = videx::core::EffectType::Blur,
                   .name = QStringLiteral("Blur"),
                   .parameterName = QStringLiteral("Radius"),
                   .suffix = QStringLiteral(" px"),
                   .minimum = 0.0,
                   .maximum = 50.0,
                   .amount = 12.0,
                   .keyAtPlayhead = false,
                   .animated = true});
    auto* amount = card.findChild<QDoubleSpinBox*>();
    expect(amount != nullptr && amount->value() == 12.0 && amount->suffix() == " px",
           "effect card exposes effect-specific value and unit");
    amount->setValue(18.0);
    QMetaObject::invokeMethod(amount, "editingFinished");
    expect(changes == 1, "effect card commits edited values");
    for (QToolButton* button : card.findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("◆")) button->click();
        if (button->text() == QStringLiteral("×")) button->click();
    }
    expect(keyAdded, "effect card key button requests a keyframe");
    expect(removed, "effect card remove button requests removal");

    selection.clear();
    expect(changeCount == 2, "selection clear notifies observers");
    QSettings(QStringLiteral("Videx"), QStringLiteral("Videx"))
        .remove(QStringLiteral("contextRail/sections/") + sectionKey);

    if (failures == 0) {
        std::cout << "Context rail tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
