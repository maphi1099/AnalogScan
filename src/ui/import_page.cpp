#include "ui/import_page.hpp"

#include "ui/qt_session_bridge.hpp"
#include "ui/views.hpp"

#include <QtCore/QItemSelectionModel>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QVBoxLayout>

namespace autoneg::ui {

namespace {

constexpr int idOf(model::FilmType f)
{
    return static_cast<int>(f);
}

} // namespace

ImportPage::ImportPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("importPage"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    title_ = new QLabel(this);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    big.setBold(true);
    title_->setFont(big);
    layout->addWidget(title_);
    info_ = new QLabel(this);
    info_->setWordWrap(true);
    layout->addWidget(info_);

    auto* row = new QHBoxLayout;
    auto* group = new QGroupBox(tr("Was für ein Film ist das?"), this);
    auto* groupLayout = new QVBoxLayout(group);
    films_ = new QButtonGroup(this);
    const auto add = [&](model::FilmType f, const QString& name, const QString& text, const QString& hint) {
        auto* b = new QRadioButton(text, group);
        b->setObjectName(name);
        b->setToolTip(hint);
        films_->addButton(b, idOf(f));
        groupLayout->addWidget(b);
        auto* h = new QLabel(hint, group);
        h->setWordWrap(true);
        h->setContentsMargins(24, 0, 0, 6);
        h->setEnabled(false);
        groupLayout->addWidget(h);
    };
    add(model::FilmType::ColorNegative, QStringLiteral("filmColor"), tr("Farbnegativ (C-41)"),
        tr("Orangefarbene Negative; die Filmbasis wird herausgerechnet und das Bild umgekehrt."));
    add(model::FilmType::BwNegative, QStringLiteral("filmBw"), tr("S/W-Negativ"),
        tr("Schwarzweißnegative; Gradation nach Ilford-Multigrade-Filterstufen."));
    add(model::FilmType::Slide, QStringLiteral("filmSlide"), tr("Dia (E-6)"),
        tr("Diapositive; keine Umkehrung, nur Farbe und Tonwerte."));
    detect_ = new QCheckBox(tr("Automatisch konvertieren"), group);
    detect_->setObjectName(QStringLiteral("detectNow"));
    detect_->setToolTip(
        tr("Nach dem Import Bildfeld, Schräglage, Filmbasis und Tonwerte aller Bilder erkennen.") +
        QLatin1Char(' ') + tr("Ohne Haken bleiben Negative unverändert, bis die Filmbasis gemessen ist."));
    groupLayout->addSpacing(8);
    groupLayout->addWidget(detect_);
    groupLayout->addStretch(1);
    row->addWidget(group, 0);

    preview_ = new QListView(this);
    preview_->setObjectName(QStringLiteral("importPreview"));
    preview_->setViewMode(QListView::IconMode);
    preview_->setWrapping(true);
    preview_->setResizeMode(QListView::Adjust);
    preview_->setMovement(QListView::Static);
    preview_->setIconSize(QSize(160, 120));
    preview_->setGridSize(QSize(176, 150));
    preview_->setUniformItemSizes(true);
    preview_->setSelectionMode(QAbstractItemView::MultiSelection); // Klick schaltet um
    auto* delegate = new ThumbnailDelegate(preview_);
    delegate->setCheckable(true);
    preview_->setItemDelegate(delegate);
    preview_->setToolTip(tr("Die Scans so, wie sie aufgenommen wurden. Klick wählt ab bzw. wieder an."));
    auto* right = new QVBoxLayout;
    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("importCount"));
    auto* all = new QPushButton(tr("Alle"), this);
    all->setObjectName(QStringLiteral("importAll"));
    auto* none = new QPushButton(tr("Keine"), this);
    none->setObjectName(QStringLiteral("importNone"));
    auto* selRow = new QHBoxLayout;
    selRow->addWidget(count_, 1);
    selRow->addWidget(all);
    selRow->addWidget(none);
    right->addLayout(selRow);
    right->addWidget(preview_, 1);
    row->addLayout(right, 1);
    connect(all, &QPushButton::clicked, preview_, &QListView::selectAll);
    connect(none, &QPushButton::clicked, preview_, &QListView::clearSelection);
    layout->addLayout(row, 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Abbrechen"), this);
    cancel->setObjectName(QStringLiteral("importCancel"));
    import_ = new QPushButton(tr("Importieren"), this);
    import_->setObjectName(QStringLiteral("importButton"));
    import_->setDefault(true);
    import_->setEnabled(false);
    import_->setToolTip(tr("Zuerst den Filmtyp wählen"));
    buttons->addWidget(cancel);
    buttons->addWidget(import_);
    layout->addLayout(buttons);

    connect(films_, &QButtonGroup::idClicked, this, [this](int) { updateState(); });
    connect(import_, &QPushButton::clicked, this, [this] {
        if (const auto f = film()) {
            Q_EMIT accepted(*f, detect_->isChecked());
        }
    });
    connect(cancel, &QPushButton::clicked, this, &ImportPage::cancelled);
}

void ImportPage::setRoll(const QString& folder, const QString& name, int frames)
{
    title_->setText(tr("Rolle importieren: %1").arg(name));
    info_->setText(tr("%n Scan(s) in %1, zunächst unverändert gezeigt.", nullptr, frames).arg(folder) +
                   QLatin1Char(' ') + tr("Den Filmtyp kann man später auf dem Leuchttisch ändern."));
    films_->setExclusive(false);
    for (QAbstractButton* b : films_->buttons()) {
        b->setChecked(false);
    }
    films_->setExclusive(true);
    detect_->setChecked(true);
    preview_->selectAll();
    updateState();
}

void ImportPage::setPreviewModel(QAbstractItemModel* m)
{
    preview_->setModel(m);
    connect(preview_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { updateState(); });
}

void ImportPage::updateState()
{
    const int total = preview_->model() != nullptr ? preview_->model()->rowCount() : 0;
    const auto chosen = static_cast<int>(
        preview_->selectionModel() != nullptr ? preview_->selectionModel()->selectedIndexes().size() : 0);
    count_->setText(tr("%1 von %2 Bildern ausgewählt").arg(chosen).arg(total));
    const bool ok = film().has_value() && chosen > 0;
    import_->setEnabled(ok);
    import_->setToolTip(!film()       ? tr("Zuerst den Filmtyp wählen")
                        : chosen == 0 ? tr("Kein Bild ausgewählt")
                                      : QString());
}

std::vector<model::FrameId> ImportPage::excludedFrames() const
{
    std::vector<model::FrameId> out;
    const QAbstractItemModel* m = preview_->model();
    if (m == nullptr) {
        return out;
    }
    for (int r = 0; r < m->rowCount(); ++r) {
        const QModelIndex i = m->index(r, 0);
        if (!preview_->selectionModel()->isSelected(i)) {
            out.push_back(i.data(FilmstripModel::FrameIdRole).value<model::FrameId>());
        }
    }
    return out;
}

std::optional<model::FilmType> ImportPage::film() const
{
    const int id = films_->checkedId();
    if (id < 0) {
        return std::nullopt;
    }
    return static_cast<model::FilmType>(id);
}

bool ImportPage::detectNow() const
{
    return detect_->isChecked();
}

void ImportPage::choose(model::FilmType f)
{
    if (QAbstractButton* b = films_->button(idOf(f))) {
        b->click();
    }
}

} // namespace autoneg::ui
