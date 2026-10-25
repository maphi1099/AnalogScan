#pragma once

#include "autoneg/image/histogram.hpp"
#include "autoneg/model/roll.hpp"

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtWidgets/QListView>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QWidget>

#include <vector>

namespace autoneg::ui {

/// Histogramm R, G, B und Luminanz mit Beschnittanzeige (LF-EDT-13, LF-EDT-14).
class HistogramWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget* parent = nullptr);
    void setHistogram(const image::Histogram& h);
    [[nodiscard]] const image::Histogram& histogram() const { return h_; }
    [[nodiscard]] QSize sizeHint() const override { return {256, 110}; }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    image::Histogram h_;
};

/// Bilder der Rolle für den Filmstreifen: Nummer, Miniatur, Bewertung (LF-IMP-07, LF-BAT-10).
class FilmstripModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        FrameIdRole = Qt::UserRole + 1,
        RatingRole
    };
    struct Item
    {
        model::Frame frame;
        QImage thumbnail;
        bool failed = false; ///< Datei defekt oder nicht lesbar (NF-REL-02)
    };

    explicit FilmstripModel(QObject* parent = nullptr);
    void setFrames(std::vector<model::Frame> frames);
    void setThumbnail(const model::FrameId& f, const QImage& img);
    void setFailed(const model::FrameId& f);
    void setRating(const model::FrameId& f, model::Rating r);
    /// Filter nach Bewertung (LF-BAT-10); leer = alle.
    void setFilter(std::optional<model::Rating> r);

    [[nodiscard]] int rowOf(const model::FrameId& f) const;
    [[nodiscard]] const Item* itemAt(int row) const;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] int thumbnailsLoaded() const;

private:
    void rebuildVisible();
    std::vector<Item> all_;
    std::vector<int> visible_; ///< Indizes in all_
    std::optional<model::Rating> filter_;
};

/// Filmstreifen: waagerechte Liste der Miniaturen mit Mehrfachauswahl.
/// Zeichnet eine Miniatur eingepasst in die feste Zelle der Ansicht (Seitenverhältnis bleibt,
/// mittig, Rand frei), darunter Bildnummer und Bewertung. Für Filmstreifen und Übersicht.
class ThumbnailDelegate final : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    /// Skalierte Miniaturen je Bild und Zielgröße (Miniaturen ändern sich selten)
    mutable QHash<QString, QPixmap> cache_;
};

class FilmstripView final : public QListView
{
    Q_OBJECT

public:
    explicit FilmstripView(QWidget* parent = nullptr);
    void setStripModel(FilmstripModel* m);
    [[nodiscard]] FilmstripModel* stripModel() const { return model_; }
    [[nodiscard]] std::vector<model::FrameId> selectedFrames() const;
    void selectFrame(const model::FrameId& f);

Q_SIGNALS:
    void currentFrameChanged(const autoneg::model::FrameId& f);
    void selectionChangedFrames();

private:
    FilmstripModel* model_ = nullptr;
};

/// Ordneransicht der Rolle (Filmansicht): alle Bilder im aktuellen Stand als Raster, gleiche
/// Auswahl wie der Filmstreifen. Doppelklick öffnet das Bild in der Bildansicht.
class OverviewView final : public QListView
{
    Q_OBJECT

public:
    explicit OverviewView(QWidget* parent = nullptr);
    /// Modell und Auswahl des Filmstreifens übernehmen.
    void share(FilmstripView* strip);

Q_SIGNALS:
    void openFrame(const autoneg::model::FrameId& f);

private:
    FilmstripModel* model_ = nullptr;
};

} // namespace autoneg::ui
