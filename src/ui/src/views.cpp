#include "autoneg/ui/views.hpp"

#include "autoneg/ui/qt_session_bridge.hpp"

#include <QtCore/QItemSelectionModel>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>

#include <algorithm>

namespace autoneg::ui {

// --- HistogramWidget ---------------------------------------------------------------------------

HistogramWidget::HistogramWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(80);
    setToolTip(tr("Histogramm R, G, B und Luminanz; Ecken: Beschnitt der Tiefen bzw. Lichter"));
}

void HistogramWidget::setHistogram(const image::Histogram& h)
{
    h_ = h;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent* /*e*/)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    std::uint32_t peak = 1;
    for (const auto& b : h_.bins) {
        for (const std::uint32_t v : b) {
            peak = std::max(peak, v);
        }
    }
    const std::array<QColor, 4> colors{QColor(255, 60, 60, 150), QColor(60, 255, 60, 150),
                                       QColor(80, 120, 255, 150), QColor(220, 220, 220, 120)};
    p.setRenderHint(QPainter::Antialiasing);
    for (std::size_t c = 0; c < 4; ++c) {
        const auto& b = h_.bins[c];
        if (b.empty()) {
            continue;
        }
        QPainterPath path;
        path.moveTo(0, height());
        for (std::size_t i = 0; i < b.size(); ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(b.size() - 1) * width();
            const double y = height() - static_cast<double>(b[i]) / peak * (height() - 4);
            path.lineTo(x, y);
        }
        path.lineTo(width(), height());
        p.fillPath(path, colors[c]);
    }
    // Beschnittanzeige: Dreiecke in den Ecken, wenn ein Kanal mehr als 0,1 % verliert
    const auto clipped = [](const base::Vec3& v) { return v.x > 0.001 || v.y > 0.001 || v.z > 0.001; };
    p.setPen(Qt::NoPen);
    if (clipped(h_.clipLow)) {
        p.setBrush(QColor(80, 120, 255));
        p.drawPolygon(QPolygonF({QPointF(0, 0), QPointF(12, 0), QPointF(0, 12)}));
    }
    if (clipped(h_.clipHigh)) {
        p.setBrush(QColor(255, 60, 60));
        p.drawPolygon(QPolygonF({QPointF(width(), 0), QPointF(width() - 12, 0), QPointF(width(), 12)}));
    }
}

// --- FilmstripModel ----------------------------------------------------------------------------

FilmstripModel::FilmstripModel(QObject* parent)
    : QAbstractListModel(parent)
{}

void FilmstripModel::setFrames(std::vector<model::Frame> frames)
{
    beginResetModel();
    all_.clear();
    for (model::Frame& f : frames) {
        all_.push_back({std::move(f), {}, false});
    }
    rebuildVisible();
    endResetModel();
}

void FilmstripModel::rebuildVisible()
{
    visible_.clear();
    for (std::size_t i = 0; i < all_.size(); ++i) {
        if (!filter_ || all_[i].frame.rating == *filter_) {
            visible_.push_back(static_cast<int>(i));
        }
    }
}

void FilmstripModel::setFilter(std::optional<model::Rating> r)
{
    beginResetModel();
    filter_ = r;
    rebuildVisible();
    endResetModel();
}

int FilmstripModel::rowOf(const model::FrameId& f) const
{
    for (std::size_t r = 0; r < visible_.size(); ++r) {
        if (all_[static_cast<std::size_t>(visible_[r])].frame.id == f) {
            return static_cast<int>(r);
        }
    }
    return -1;
}

const FilmstripModel::Item* FilmstripModel::itemAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(visible_.size())) {
        return nullptr;
    }
    return &all_[static_cast<std::size_t>(visible_[static_cast<std::size_t>(row)])];
}

void FilmstripModel::setThumbnail(const model::FrameId& f, const QImage& img)
{
    for (Item& it : all_) {
        if (it.frame.id == f) {
            it.thumbnail = img;
            it.failed = img.isNull();
        }
    }
    if (const int r = rowOf(f); r >= 0) {
        Q_EMIT dataChanged(index(r), index(r), {Qt::DecorationRole});
    }
}

void FilmstripModel::setFailed(const model::FrameId& f)
{
    for (Item& it : all_) {
        if (it.frame.id == f) {
            it.failed = true;
        }
    }
    if (const int r = rowOf(f); r >= 0) {
        Q_EMIT dataChanged(index(r), index(r));
    }
}

void FilmstripModel::setRating(const model::FrameId& f, model::Rating rating)
{
    for (Item& it : all_) {
        if (it.frame.id == f) {
            it.frame.rating = rating;
        }
    }
    if (filter_) {
        setFilter(filter_);
    } else if (const int r = rowOf(f); r >= 0) {
        Q_EMIT dataChanged(index(r), index(r));
    }
}

int FilmstripModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

int FilmstripModel::thumbnailsLoaded() const
{
    return static_cast<int>(std::ranges::count_if(all_, [](const Item& i) { return !i.thumbnail.isNull(); }));
}

QVariant FilmstripModel::data(const QModelIndex& index, int role) const
{
    const Item* it = itemAt(index.row());
    if (it == nullptr) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole: {
        QString text = QString::number(it->frame.frameNo);
        if (it->frame.rating == model::Rating::Keep) {
            text += QStringLiteral(" ✓");
        } else if (it->frame.rating == model::Rating::Reject) {
            text += QStringLiteral(" ✗");
        }
        return text;
    }
    case Qt::DecorationRole:
        if (!it->thumbnail.isNull()) {
            return QPixmap::fromImage(it->thumbnail);
        }
        return {};
    case Qt::ToolTipRole:
        return it->failed ? tr("%1: Datei kann nicht gelesen werden")
                                .arg(QString::fromStdString(it->frame.path.filename().string()))
                          : QString::fromStdString(it->frame.path.filename().string());
    case Qt::ForegroundRole:
        return it->failed ? QColor(Qt::red) : QVariant();
    case FrameIdRole:
        return QVariant::fromValue(it->frame.id);
    case RatingRole:
        return static_cast<int>(it->frame.rating);
    default:
        return {};
    }
}

// --- FilmstripView -----------------------------------------------------------------------------

FilmstripView::FilmstripView(QWidget* parent)
    : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setMovement(QListView::Static);
    setIconSize(QSize(160, 120));
    setGridSize(QSize(176, 150)); // feste Zellen, auch bevor Miniaturen da sind
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setMinimumHeight(170);
    setObjectName(QStringLiteral("filmstrip"));
}

void FilmstripView::setStripModel(FilmstripModel* m)
{
    model_ = m;
    setModel(m);
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& cur) {
        if (const FilmstripModel::Item* it = model_->itemAt(cur.row())) {
            Q_EMIT currentFrameChanged(it->frame.id);
        }
    });
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { Q_EMIT selectionChangedFrames(); });
}

std::vector<model::FrameId> FilmstripView::selectedFrames() const
{
    std::vector<model::FrameId> out;
    if (model_ == nullptr) {
        return out;
    }
    for (const QModelIndex& i : selectionModel()->selectedIndexes()) {
        if (const FilmstripModel::Item* it = model_->itemAt(i.row())) {
            out.push_back(it->frame.id);
        }
    }
    return out;
}

void FilmstripView::selectFrame(const model::FrameId& f)
{
    if (model_ == nullptr) {
        return;
    }
    const int r = model_->rowOf(f);
    if (r >= 0) {
        selectionModel()->setCurrentIndex(model_->index(r), QItemSelectionModel::ClearAndSelect);
    }
}

} // namespace autoneg::ui
