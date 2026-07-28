#include "selection_model.hpp"

#include <QCoreApplication>

#include <utility>

namespace videx::ui {

SelectionModel::SelectionModel(QObject* parent) : QObject(parent) {}

const SelectionState& SelectionModel::state() const noexcept {
    return state_;
}

void SelectionModel::setState(SelectionState state) {
    if (state_ == state) {
        return;
    }
    state_ = std::move(state);
    emit selectionChanged();
}

void SelectionModel::clear() {
    setState({});
}

QString selectionKindLabel(const SelectionKind kind) {
    const auto label = [](const char* text) {
        return QCoreApplication::translate("SelectionModel", text);
    };
    switch (kind) {
    case SelectionKind::VideoClip:
        return label("VIDEO");
    case SelectionKind::AudioClip:
        return label("AUDIO");
    case SelectionKind::TitleClip:
        return label("TITLE");
    case SelectionKind::Caption:
        return label("CAPTION");
    case SelectionKind::Track:
        return label("TRACK");
    case SelectionKind::Marker:
        return label("MARKER");
    case SelectionKind::Multiple:
        return label("MULTIPLE");
    case SelectionKind::None:
        return label("NO SELECTION");
    }
    return {};
}

} // namespace videx::ui
