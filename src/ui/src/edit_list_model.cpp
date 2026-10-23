#include "autoneg/ui/edit_list_model.hpp"

#include "autoneg/ui/translations.hpp"

#include <QtGui/QColor>

#include <algorithm>

namespace autoneg::ui {

EditListModel::EditListModel(UiController& ui, QObject* parent)
    : QAbstractTableModel(parent)
    , ui_(ui)
{}

void EditListModel::setList(const model::EditList& l)
{
    beginResetModel();
    rows_.clear();
    for (const model::EditEntry& e : l.entries()) {
        const pipeline::IStep* s = ui_.registry().find(e.step);
        if (s != nullptr && !s->descriptor().visible) {
            continue; // interne Stufen erscheinen nicht
        }
        rows_.push_back(e);
    }
    // Pipeline-Reihenfolge: Rang aus der Registry, bei Gleichstand Kennung
    const auto rankOf = [this](const model::EditEntry& e) {
        const pipeline::IStep* s = ui_.registry().find(e.step);
        return s != nullptr ? s->descriptor().rank : e.rank;
    };
    std::ranges::stable_sort(rows_, [&](const model::EditEntry& a, const model::EditEntry& b) {
        const int ra = rankOf(a);
        const int rb = rankOf(b);
        return ra != rb ? ra < rb : a.step < b.step;
    });
    endResetModel();
}

const model::EditEntry* EditListModel::entryAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(row)];
}

int EditListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int EditListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant EditListModel::data(const QModelIndex& index, int role) const
{
    const model::EditEntry* e = entryAt(index.row());
    if (e == nullptr) {
        return {};
    }
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Name:
            return translated(stepName(ui_.registry(), e->step));
        case Value:
            return QString::fromStdString(describeParams(*e));
        case Origin:
            return translated(originName(e->origin));
        default:
            return {};
        }
    }
    if (role == Qt::CheckStateRole && index.column() == Active) {
        return e->enabled ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::ForegroundRole && !e->enabled) {
        return QColor(Qt::gray);
    }
    if (role == Qt::ToolTipRole && index.column() == Origin && e->copiedFrom) {
        return tr("kopiert von Bild %1").arg(QString::fromStdString(e->copiedFrom->toHex().substr(0, 8)));
    }
    return {};
}

QVariant EditListModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case Name:
        return tr("Änderung");
    case Value:
        return tr("Wert");
    case Origin:
        return tr("Herkunft");
    case Active:
        return tr("Aktiv");
    default:
        return {};
    }
}

Qt::ItemFlags EditListModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == Active) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

bool EditListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::CheckStateRole || index.column() != Active || entryAt(index.row()) == nullptr) {
        return false;
    }
    const bool on = value.toInt() == Qt::Checked;
    if (on == entryAt(index.row())->enabled) {
        return false;
    }
    toggleEntry(index.row());
    return true;
}

void EditListModel::removeEntry(int row)
{
    if (const model::EditEntry* e = entryAt(row)) {
        ui_.apply(std::make_unique<application::RemoveEntryCommand>(e->step));
    }
}

void EditListModel::toggleEntry(int row)
{
    if (const model::EditEntry* e = entryAt(row)) {
        ui_.apply(std::make_unique<application::ToggleEntryCommand>(e->step, !e->enabled));
    }
}

} // namespace autoneg::ui
