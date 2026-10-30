#pragma once

#include "model/film.hpp"
#include "model/ids.hpp"

#include <QtWidgets/QWidget>

#include <optional>
#include <vector>

class QAbstractItemModel;
class QButtonGroup;
class QCheckBox;
class QLabel;
class QListView;
class QPushButton;

namespace autoneg::ui {

/// Import-Bildschirm einer neuen Rolle (K-M10-08): zeigt die Scans unverändert und fragt nach
/// dem Filmtyp (Farbnegativ, S/W-Negativ, Dia). Einzelne Scans lassen sich abwählen (Haken
/// oben rechts, anfangs alle gewählt). Erst mit „Importieren“ geht es weiter; der Filmtyp
/// bleibt später änderbar (Leuchttisch, Reiter Konvertierung).
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
    /// Abgewählte Scans (gehören nicht zur Rolle).
    [[nodiscard]] std::vector<model::FrameId> excludedFrames() const;
    [[nodiscard]] QListView* preview() const { return preview_; }
    /// Filmtyp wählen (wie ein Klick auf die Schaltfläche; für Tests).
    void choose(model::FilmType f);
    [[nodiscard]] QPushButton* importButton() const { return import_; }

Q_SIGNALS:
    void accepted(autoneg::model::FilmType film, bool detect);
    void cancelled();

private:
    void updateState();
    QLabel* title_ = nullptr;
    QLabel* count_ = nullptr;
    QLabel* info_ = nullptr;
    QButtonGroup* films_ = nullptr;
    QCheckBox* detect_ = nullptr;
    QListView* preview_ = nullptr;
    QPushButton* import_ = nullptr;
};

} // namespace autoneg::ui
