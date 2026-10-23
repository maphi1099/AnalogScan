#pragma once

#include "autoneg/model/edit_list.hpp"
#include "autoneg/ui/ui_controller.hpp"

#include <QtCore/QAbstractTableModel>

#include <vector>

namespace autoneg::ui {

/// Tabelle der Bearbeitungsliste (Architektur 9.14, LF-LST-01, -03 … -06).
/// Zeilen in Pipeline-Reihenfolge (nur sichtbare Schritte); Spalten Name, Wert, Herkunft,
/// Aktiv (ankreuzbar). Entfernen und Umschalten erzeugen Befehle über den UiController.
class EditListModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        Name = 0,
        Value,
        Origin,
        Active,
        ColumnCount
    };

    explicit EditListModel(UiController& ui, QObject* parent = nullptr);

    void setList(const model::EditList& l);
    [[nodiscard]] const model::EditEntry* entryAt(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation o,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    /// Entfernt den Eintrag der Zeile mit einem Klick (LF-LST-03).
    void removeEntry(int row);
    /// Schaltet den Eintrag aus bzw. ein, Werte bleiben (LF-LST-06).
    void toggleEntry(int row);

private:
    UiController& ui_;
    std::vector<model::EditEntry> rows_;
};

} // namespace autoneg::ui
