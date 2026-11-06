#include "ui/edit_list_model.hpp"

#include "steps/step_ids.hpp"
#include "ui/translations.hpp"

#include <QtGui/QColor>

#include <algorithm>
#include <cmath>

namespace autoneg::ui {

namespace key = steps::key;

namespace {

QString number(double v, int decimals, bool sign)
{
    QString s = QString::number(v, 'f', decimals).replace('.', ',');
    return sign && v >= 0.0 ? QStringLiteral("+") + s : s;
}

QString percent(double v, bool sign)
{
    const int p = static_cast<int>(std::lround(v * 100.0));
    return (sign && p >= 0 ? QStringLiteral("+") : QString()) + QString::number(p) + QStringLiteral(" %");
}

QString filmName(const std::string& type)
{
    if (type == "bw") {
        return EditListModel::tr("S/W-Negativ");
    }
    if (type == "slide") {
        return EditListModel::tr("Dia");
    }
    return EditListModel::tr("Farbnegativ");
}

// Ein Parameter als "Bezeichnung Wert"; leer, wenn er in der Liste nichts aussagt
QString describe(const std::string& name, const model::ParamValue& value)
{
    const auto* d = std::get_if<double>(&value);
    const auto* i = std::get_if<int>(&value);
    const auto* b = std::get_if<bool>(&value);
    const auto* str = std::get_if<std::string>(&value);
    const auto labelled = [](const QString& label, const QString& v) {
        return label + QStringLiteral(" ") + v;
    };
    if (name == key::on && b != nullptr) {
        return *b ? EditListModel::tr("an") : EditListModel::tr("aus");
    }
    if (name == key::type && str != nullptr) {
        return filmName(*str);
    }
    if (name == key::flipH && b != nullptr) {
        return *b ? EditListModel::tr("horizontal gespiegelt") : QString();
    }
    if (name == key::flipV && b != nullptr) {
        return *b ? EditListModel::tr("vertikal gespiegelt") : QString();
    }
    if (name == key::quarterTurns && i != nullptr) {
        return *i != 0
                   ? labelled(EditListModel::tr("Drehung"), QString::number(*i * 90) + QStringLiteral("°"))
                   : QString();
    }
    if (const auto* r = std::get_if<model::NormRect>(&value); r != nullptr && name == key::rect) {
        return labelled(EditListModel::tr("Fläche"), percent(r->w * r->h, false));
    }
    if (const auto* spots = std::get_if<model::SpotList>(&value)) {
        return EditListModel::tr("%n Stelle(n)", nullptr, static_cast<int>(spots->size()));
    }
    if (str != nullptr && name == key::aspect) {
        return str->empty() ? QString() : labelled(EditListModel::tr("Format"), QString::fromStdString(*str));
    }
    if (i != nullptr && name == key::level) {
        return labelled(EditListModel::tr("Stufe"), QString::number(*i));
    }
    if (d == nullptr) {
        return {}; // interne Werte (Basis, Messfläche, Kurven, Herkunft)
    }
    if (name == key::angle) {
        return labelled(EditListModel::tr("Winkel"), number(*d, 2, false) + QStringLiteral("°"));
    }
    if (name == key::ev) {
        return number(*d, 2, true) + QStringLiteral(" EV");
    }
    if (name == key::temperature) {
        return labelled(EditListModel::tr("Temperatur"), number(*d, 0, true));
    }
    if (name == key::tint) {
        return labelled(EditListModel::tr("Tönung"), number(*d, 0, true));
    }
    if (name == key::grade) {
        return labelled(EditListModel::tr("Gradation"), number(*d, 1, false));
    }
    if (name == key::radius) {
        return labelled(EditListModel::tr("Radius"), number(*d, 1, false) + QStringLiteral(" px"));
    }
    if (name == key::pivot) {
        return labelled(EditListModel::tr("Mitte"), number(*d, 2, false));
    }
    const std::pair<std::string_view, QString> shares[] = {
        {key::saturation, EditListModel::tr("Sättigung")}, {key::vibrance, EditListModel::tr("Dynamik")},
        {key::amount, EditListModel::tr("Stärke")},        {key::strength, EditListModel::tr("Stärke")},
        {key::shadows, EditListModel::tr("Schatten")},       {key::highlights, EditListModel::tr("Lichter")},
        {key::splitSoft, EditListModel::tr("weich")},      {key::splitHard, EditListModel::tr("hart")},
        {key::black, EditListModel::tr("Schwarz")},        {key::white, EditListModel::tr("Weiß")},
        {key::inset, EditListModel::tr("Rand")},           {key::clip, EditListModel::tr("Beschnitt")},
        {key::threshold, EditListModel::tr("Schwelle")},
    };
    const bool sign = name == key::saturation || name == key::vibrance || name == key::amount;
    for (const auto& [key, label] : shares) {
        if (name == key) {
            return labelled(label, percent(*d, sign));
        }
    }
    return labelled(QString::fromStdString(name), number(*d, 2, true));
}

QString describeParams(const model::EditEntry& e)
{
    QStringList parts;
    for (const auto& [name, value] : e.params.entries()) {
        if (QString text = describe(name, value); !text.isEmpty()) {
            parts << text;
        }
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

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
            return describeParams(*e);
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

void EditListModel::removeEntries(const std::vector<int>& rows)
{
    // ein Befehl für alle Zeilen: ein Schritt im Verlauf
    class RemoveSteps final : public application::ICommand
    {
    public:
        explicit RemoveSteps(std::vector<model::StepId> steps)
            : steps_(std::move(steps))
        {}
        [[nodiscard]] std::string label() const override { return "Entfernen"; }
        [[nodiscard]] model::EditList apply(const model::EditList& l,
                                            const application::CommandContext& /*c*/) const override
        {
            model::EditList out = l;
            for (const model::StepId& s : steps_) {
                out = out.without(s);
            }
            return out;
        }

    private:
        std::vector<model::StepId> steps_;
    };
    std::vector<model::StepId> steps;
    for (const int r : rows) {
        if (const model::EditEntry* e = entryAt(r)) {
            steps.push_back(e->step);
        }
    }
    if (!steps.empty()) {
        ui_.apply(std::make_unique<RemoveSteps>(std::move(steps)));
    }
}

void EditListModel::toggleEntry(int row)
{
    if (const model::EditEntry* e = entryAt(row)) {
        ui_.apply(std::make_unique<application::ToggleEntryCommand>(e->step, !e->enabled));
    }
}

} // namespace autoneg::ui
