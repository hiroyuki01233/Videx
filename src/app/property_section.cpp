#include "property_section.hpp"

#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace videx::ui {

PropertySection::PropertySection(const QString& title, const QString& settingsKey,
                                 QWidget* parent)
    : QFrame(parent), title_(title), settingsKey_(settingsKey) {
    setObjectName(QStringLiteral("propertySection"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    headerButton_ = new QToolButton(this);
    headerButton_->setObjectName(QStringLiteral("propertySectionHeader"));
    headerButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    headerButton_->setCheckable(true);
    headerButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(headerButton_);

    content_ = new QWidget(this);
    content_->setObjectName(QStringLiteral("propertySectionContent"));
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(10, 8, 10, 10);
    contentLayout_->setSpacing(6);
    layout->addWidget(content_);

    const bool stored = QSettings(QStringLiteral("Videx"), QStringLiteral("Videx"))
                            .value(QStringLiteral("contextRail/sections/") + settingsKey_, true)
                            .toBool();
    connect(headerButton_, &QToolButton::toggled, this, &PropertySection::setExpanded);
    setExpanded(stored);
}

QVBoxLayout* PropertySection::contentLayout() const noexcept {
    return contentLayout_;
}

bool PropertySection::isExpanded() const noexcept {
    return expanded_;
}

void PropertySection::setExpanded(const bool expanded) {
    expanded_ = expanded;
    headerButton_->setChecked(expanded_);
    content_->setVisible(expanded_);
    updateHeader();
    if (!settingsKey_.isEmpty()) {
        QSettings(QStringLiteral("Videx"), QStringLiteral("Videx"))
            .setValue(QStringLiteral("contextRail/sections/") + settingsKey_, expanded_);
    }
    emit expansionChanged(expanded_);
}

void PropertySection::setSummary(const QString& summary) {
    summary_ = summary;
    updateHeader();
}

void PropertySection::setSectionEnabled(const bool enabled) {
    content_->setEnabled(enabled);
    headerButton_->setProperty("sectionEnabled", enabled);
    headerButton_->style()->unpolish(headerButton_);
    headerButton_->style()->polish(headerButton_);
}

void PropertySection::updateHeader() {
    headerButton_->setArrowType(expanded_ ? Qt::DownArrow : Qt::RightArrow);
    headerButton_->setText(summary_.isEmpty() || expanded_
                               ? title_
                               : QStringLiteral("%1  ·  %2").arg(title_, summary_));
}

} // namespace videx::ui
