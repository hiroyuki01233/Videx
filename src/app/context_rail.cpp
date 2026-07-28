#include "context_rail.hpp"

#include "property_section.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace videx::ui {

ContextRail::ContextRail(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("contextRail"));
    setMinimumWidth(300);
    setMaximumWidth(520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("contextRailHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    headerLayout->setSpacing(4);

    auto* identityRow = new QHBoxLayout;
    identityRow->setSpacing(8);
    kindLabel_ = new QLabel(tr("NO SELECTION"), header);
    kindLabel_->setObjectName(QStringLiteral("selectionKind"));
    titleLabel_ = new QLabel(tr("Nothing selected"), header);
    titleLabel_->setObjectName(QStringLiteral("selectionTitle"));
    titleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    enabledCheck_ = new QCheckBox(tr("Enabled"), header);
    enabledCheck_->setObjectName(QStringLiteral("selectionEnabled"));
    enabledCheck_->setVisible(false);
    identityRow->addWidget(kindLabel_);
    identityRow->addWidget(titleLabel_, 1);
    identityRow->addWidget(enabledCheck_);
    headerLayout->addLayout(identityRow);

    subtitleLabel_ = new QLabel(header);
    subtitleLabel_->setObjectName(QStringLiteral("selectionSubtitle"));
    subtitleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerLayout->addWidget(subtitleLabel_);
    root->addWidget(header);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setObjectName(QStringLiteral("contextRailSeparator"));
    root->addWidget(separator);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("contextRailScroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    sectionsHost_ = new QWidget(scroll_);
    sectionsLayout_ = new QVBoxLayout(sectionsHost_);
    sectionsLayout_->setContentsMargins(0, 0, 0, 0);
    sectionsLayout_->setSpacing(1);
    emptyLabel_ = new QLabel(tr("Select a clip to edit its properties."), sectionsHost_);
    emptyLabel_->setObjectName(QStringLiteral("contextRailEmpty"));
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setMinimumHeight(160);
    sectionsLayout_->addWidget(emptyLabel_);
    sectionsLayout_->addStretch(1);
    scroll_->setWidget(sectionsHost_);
    root->addWidget(scroll_, 1);

    auto* pinned = new QWidget(this);
    pinned->setObjectName(QStringLiteral("contextRailPinned"));
    pinnedLayout_ = new QVBoxLayout(pinned);
    pinnedLayout_->setContentsMargins(0, 0, 0, 0);
    pinnedLayout_->setSpacing(0);
    root->addWidget(pinned);

    connect(enabledCheck_, &QCheckBox::toggled, this, &ContextRail::enabledChanged);
}

void ContextRail::bindSelectionModel(SelectionModel* model) {
    if (selectionModel_ != nullptr) {
        disconnect(selectionModel_, nullptr, this, nullptr);
    }
    selectionModel_ = model;
    if (selectionModel_ != nullptr) {
        connect(selectionModel_, &SelectionModel::selectionChanged, this,
                &ContextRail::updateSelection);
    }
    updateSelection();
}

void ContextRail::addSection(PropertySection* section) {
    const int stretchIndex = sectionsLayout_->count() - 1;
    sectionsLayout_->insertWidget(std::max(0, stretchIndex), section);
}

void ContextRail::setPinnedWidget(QWidget* widget) {
    while (QLayoutItem* item = pinnedLayout_->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->setParent(nullptr);
        }
        delete item;
    }
    if (widget != nullptr) {
        pinnedLayout_->addWidget(widget);
    }
}

void ContextRail::setEmptyMessage(const QString& message) {
    emptyLabel_->setText(message);
}

QVBoxLayout* ContextRail::sectionsLayout() const noexcept {
    return sectionsLayout_;
}

void ContextRail::updateSelection() {
    QSettings settings(QStringLiteral("Videx"), QStringLiteral("Videx"));
    if (scroll_ != nullptr && currentKind_ != SelectionKind::None) {
        settings.setValue(
            QStringLiteral("contextRail/scroll/%1").arg(static_cast<int>(currentKind_)),
            scroll_->verticalScrollBar()->value());
    }
    const SelectionState state =
        selectionModel_ == nullptr ? SelectionState{} : selectionModel_->state();
    const bool hasSelection = state.kind != SelectionKind::None;
    kindLabel_->setText(selectionKindLabel(state.kind));
    titleLabel_->setText(state.title.isEmpty()
                             ? (hasSelection ? tr("Selected item") : tr("Nothing selected"))
                             : state.title);
    subtitleLabel_->setText(state.subtitle);
    subtitleLabel_->setVisible(!state.subtitle.isEmpty());
    emptyLabel_->setVisible(!hasSelection);
    // Clip enable/disable is not part of the current core model. Keep the
    // control reserved in the header, but do not expose a decorative toggle
    // that cannot commit an edit.
    enabledCheck_->setVisible(false);
    sectionsHost_->setProperty("hasSelection", hasSelection);
    sectionsHost_->style()->unpolish(sectionsHost_);
    sectionsHost_->style()->polish(sectionsHost_);
    currentKind_ = state.kind;
    if (scroll_ != nullptr) {
        const int position =
            settings
                .value(QStringLiteral("contextRail/scroll/%1")
                           .arg(static_cast<int>(currentKind_)),
                       0)
                .toInt();
        QTimer::singleShot(0, this, [this, position] {
            if (scroll_ != nullptr) {
                scroll_->verticalScrollBar()->setValue(position);
            }
        });
    }
}

} // namespace videx::ui
