#pragma once

#include "autoneg/model/film.hpp"

#include <QtWidgets/QWidget>

#include <optional>

class QAbstractItemModel;
class QButtonGroup;
class QCheckBox;
class QLabel;
class QListView;
class QPushButton;

namespace autoneg::ui {

/// Import-Bildschirm einer neuen Rolle (K-M10-08): zeigt die Scans unverändert und fragt nach
/// dem Filmtyp (Farbnegativ, S/W-Negativ, Dia). Erst mit „Importieren“ geht es in die
/// Bildansicht; der Filmtyp bleibt später änderbar (Übersicht, Reiter Konvertierung).
class ImportPage final : public QWidget
{
    Q_OBJECT

public:
    explicit ImportPage(QWidget* parent = nullptr);
    /// Rolle anzeigen; die Auswahl des Filmtyps beginnt leer.
    void setRoll(const QString& folder, const QString& name, int frames);
    /// Miniaturen der Scans (Modell des Filmstreifens).
    void setPreviewModel(QAbstractItemModel* m);
    [[nodiscard]] std::optional<model::FilmType> film() const;
    [[nodiscard]] bool detectNow() const;
    /// Filmtyp wählen (wie ein Klick auf die Schaltfläche; für Tests).
    void choose(model::FilmType f);
    [[nodiscard]] QPushButton* importButton() const { return import_; }

Q_SIGNALS:
    void accepted(autoneg::model::FilmType film, bool detect);
    void cancelled();

private:
    QLabel* title_ = nullptr;
    QLabel* info_ = nullptr;
    QButtonGroup* films_ = nullptr;
    QCheckBox* detect_ = nullptr;
    QListView* preview_ = nullptr;
    QPushButton* import_ = nullptr;
};

} // namespace autoneg::ui
