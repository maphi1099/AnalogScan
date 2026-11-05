#include "ui/panels.hpp"

#include "steps/other_steps.hpp"
#include "steps/step_ids.hpp"

#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace autoneg::ui {

namespace id = steps::id;

namespace {

constexpr int ticks = 1000;

double paramOr(const model::EditList& l, std::string_view step, const std::string& name, double fallback)
{
    if (const model::EditEntry* e = l.get(model::StepId(std::string(step)))) {
        if (const auto v = e->params.get<double>(name)) {
            return *v;
        }
    }
    return fallback;
}

} // namespace

// --- ToolPanel ---------------------------------------------------------------------------------

model::EditEntry ToolPanel::withParam(const std::string& step, const std::string& name,
                                      model::ParamValue v) const
{
    const model::EditList cur = ui_.currentList();
    model::EditEntry e;
    if (const model::EditEntry* old = cur.get(model::StepId(step))) {
        e = *old;
    } else {
        e.step = model::StepId(step);
    }
    e.params = e.params.with(name, std::move(v));
    e.origin = model::Origin::Manual;
    e.enabled = true;
    e.copiedFrom.reset();
    return e;
}

void ToolPanel::commit(model::EditEntry e)
{
    ui_.apply(std::make_unique<application::SetEntryCommand>(std::move(e)));
}

void ToolPanel::previewWith(model::EditEntry e)
{
    ui_.preview(ui_.currentList().with(std::move(e)));
}

// --- ParamSlider -------------------------------------------------------------------------------

ParamSlider::ParamSlider(std::string step, std::string param, const QString& label, const QString& tip,
                         const pipeline::StepRegistry& registry, QWidget* parent)
    : QWidget(parent)
    , step_(std::move(step))
    , param_(std::move(param))
{
    if (const pipeline::IStep* s = registry.find(model::StepId(step_))) {
        for (const pipeline::ParamSpec& p : s->descriptor().params) {
            if (p.name == param_) {
                min_ = p.min;
                max_ = p.max;
                if (const auto* n = std::get_if<double>(&p.neutral)) {
                    neutral_ = *n;
                }
            }
        }
    }
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    auto* name = new QLabel(label, this);
    name->setMinimumWidth(110);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, ticks);
    slider_->setObjectName(QString::fromStdString(step_ + "." + param_));
    spin_ = new QDoubleSpinBox(this);
    spin_->setRange(min_, max_);
    spin_->setDecimals(std::abs(max_ - min_) > 50.0 ? 0 : 2);
    spin_->setSingleStep((max_ - min_) / 100.0);
    row->addWidget(name);
    row->addWidget(slider_, 1);
    row->addWidget(spin_);
    setToolTip(tip);
    slider_->setToolTip(tip);
    spin_->setToolTip(tip);
    slider_->installEventFilter(this);
    setValue(neutral_);

    connect(slider_, &QSlider::valueChanged, this, [this](int t) {
        const QSignalBlocker block(spin_);
        spin_->setValue(fromTicks(t));
        if (slider_->isSliderDown()) {
            Q_EMIT moving(fromTicks(t));
        }
    });
    connect(slider_, &QSlider::sliderReleased, this,
            [this] { Q_EMIT committed(fromTicks(slider_->value())); });
    // Tastatur und Mausrad am Regler: jede Änderung ist abgeschlossen
    connect(slider_, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove) {
            QMetaObject::invokeMethod(
                this, [this] { Q_EMIT committed(fromTicks(slider_->value())); }, Qt::QueuedConnection);
        }
    });
    connect(spin_, &QDoubleSpinBox::editingFinished, this, [this] {
        const QSignalBlocker block(slider_);
        slider_->setValue(toTicks(spin_->value()));
        Q_EMIT committed(spin_->value());
    });
}

double ParamSlider::fromTicks(int t) const
{
    return min_ + (max_ - min_) * t / ticks;
}

int ParamSlider::toTicks(double v) const
{
    return static_cast<int>(std::lround((v - min_) / (max_ - min_) * ticks));
}

void ParamSlider::setValue(double v)
{
    const QSignalBlocker a(slider_);
    const QSignalBlocker b(spin_);
    slider_->setValue(toTicks(v));
    spin_->setValue(v);
}

double ParamSlider::value() const
{
    return spin_->value();
}

bool ParamSlider::eventFilter(QObject* o, QEvent* e)
{
    if (o == slider_ && e->type() == QEvent::MouseButtonDblClick) {
        setValue(neutral_);
        Q_EMIT reset();
        return true;
    }
    return QWidget::eventFilter(o, e);
}

// --- GradationPanel ----------------------------------------------------------------------------

double GradationPanel::gradeOf(int button)
{
    // 00 = -1, 0 = 0, ½ = 0,5 … 5 = 5; "ohne Filter" wie Stufe 2 (U-UI-05)
    return button == 0 ? -1.0 : button == 12 ? 2.0 : (button - 1) * 0.5;
}

QString GradationPanel::labelOf(int button)
{
    if (button == 0) {
        return QStringLiteral("00");
    }
    if (button == 12) {
        return tr("ohne Filter");
    }
    const double g = gradeOf(button);
    const int whole = static_cast<int>(std::floor(g));
    const bool half = g - whole > 0.25;
    if (whole == 0 && half) {
        return QStringLiteral("½");
    }
    return QString::number(whole) + (half ? QStringLiteral("½") : QString());
}

GradationPanel::GradationPanel(UiController& ui, QWidget* parent)
    : ToolPanel(ui, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* box = new QGroupBox(tr("Gradation (Multigrade)"), this);
    auto* grid = new QGridLayout(box);
    grades_ = new QButtonGroup(this);
    grades_->setExclusive(true);
    for (int i = 0; i <= 12; ++i) {
        auto* b = new QPushButton(labelOf(i), box);
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("grade_%1").arg(i));
        b->setToolTip(i == 12 ? tr("Ohne Filter (entspricht Stufe 2)")
                              : tr("Filterstufe %1").arg(labelOf(i)));
        grades_->addButton(b, i);
        if (i < 12) {
            grid->addWidget(b, i / 6, i % 6);
        } else {
            grid->addWidget(b, 2, 0, 1, 6);
        }
    }
    fine_ = new QDoubleSpinBox(box);
    fine_->setRange(-1.0, 5.0);
    fine_->setSingleStep(0.1);
    fine_->setDecimals(1);
    fine_->setValue(2.0);
    fine_->setToolTip(tr("Stufe in Zehntelschritten (LF-KON-11)"));
    grid->addWidget(new QLabel(tr("Fein"), box), 3, 0);
    grid->addWidget(fine_, 3, 1, 1, 2);
    layout->addWidget(box);

    auto* split = new QGroupBox(tr("Split-Grade"), this);
    auto* sl = new QGridLayout(split);
    soft_ = new QSlider(Qt::Horizontal, split);
    hard_ = new QSlider(Qt::Horizontal, split);
    for (QSlider* s : {soft_, hard_}) {
        s->setRange(0, 100);
    }
    soft_->setToolTip(tr("Anteil der Belichtung mit Stufe 00 (weich)"));
    hard_->setToolTip(tr("Anteil der Belichtung mit Stufe 5 (hart)"));
    soft_->setObjectName(QStringLiteral("splitSoft"));
    hard_->setObjectName(QStringLiteral("splitHard"));
    sl->addWidget(new QLabel(tr("00 (weich)"), split), 0, 0);
    sl->addWidget(soft_, 0, 1);
    sl->addWidget(new QLabel(tr("5 (hart)"), split), 1, 0);
    sl->addWidget(hard_, 1, 1);
    layout->addWidget(split);
    layout->addStretch(1);

    connect(grades_, &QButtonGroup::idClicked, this, [this](int i) {
        const QSignalBlocker block(fine_);
        fine_->setValue(gradeOf(i));
        model::EditEntry e = withParam(std::string(id::gradation), "grade", gradeOf(i));
        e.params = e.params.with("splitSoft", 0.0).with("splitHard", 0.0);
        commit(std::move(e));
    });
    connect(fine_, &QDoubleSpinBox::editingFinished, this, [this] {
        model::EditEntry e = withParam(std::string(id::gradation), "grade", fine_->value());
        e.params = e.params.with("splitSoft", 0.0).with("splitHard", 0.0);
        commit(std::move(e));
    });
    for (QSlider* s : {soft_, hard_}) {
        connect(s, &QSlider::sliderReleased, this,
                [this] { setSplit(soft_->value() / 100.0, hard_->value() / 100.0); });
    }
}

QAbstractButton* GradationPanel::button(int i) const
{
    return grades_->button(i);
}

void GradationPanel::setSplit(double soft, double hard)
{
    {
        const QSignalBlocker a(soft_);
        const QSignalBlocker b(hard_);
        soft_->setValue(static_cast<int>(std::lround(soft * 100.0)));
        hard_->setValue(static_cast<int>(std::lround(hard * 100.0)));
    }
    model::EditEntry e = withParam(std::string(id::gradation), "splitSoft", soft);
    e.params = e.params.with("splitHard", hard);
    commit(std::move(e));
}

void GradationPanel::bindList(const model::EditList& l)
{
    const double g = paramOr(l, id::gradation, "grade", 2.0);
    const QSignalBlocker a(fine_);
    fine_->setValue(g);
    grades_->setExclusive(false);
    for (QAbstractButton* b : grades_->buttons()) {
        b->setChecked(false);
    }
    grades_->setExclusive(true);
    for (int i = 0; i < 12; ++i) {
        if (std::abs(gradeOf(i) - g) < 1e-9 && l.get(model::StepId(std::string(id::gradation))) != nullptr) {
            grades_->button(i)->setChecked(true);
        }
    }
    const QSignalBlocker b(soft_);
    const QSignalBlocker c(hard_);
    soft_->setValue(static_cast<int>(std::lround(paramOr(l, id::gradation, "splitSoft", 0.0) * 100.0)));
    hard_->setValue(static_cast<int>(std::lround(paramOr(l, id::gradation, "splitHard", 0.0) * 100.0)));
}

// --- ConversionPanel ---------------------------------------------------------------------------

ConversionPanel::ConversionPanel(UiController& ui, QWidget* parent)
    : ToolPanel(ui, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto_ = new QCheckBox(tr("Autokonvertierung"), this);
    auto_->setObjectName(QStringLiteral("autoConvert"));
    auto_->setToolTip(tr("Bildfeld, Filmbasis und Tonwerte vollautomatisch bestimmen"));
    QFont bold = auto_->font();
    bold.setBold(true);
    auto_->setFont(bold);
    layout->addWidget(auto_);
    filmType_ = new QComboBox(this);
    filmType_->setObjectName(QStringLiteral("filmType"));
    filmType_->addItem(tr("– Filmtyp wählen –"), QStringLiteral("raw"));
    filmType_->addItem(tr("Farbnegativ"), QStringLiteral("color"));
    filmType_->addItem(tr("S/W-Negativ"), QStringLiteral("bw"));
    filmType_->addItem(tr("Dia"), QStringLiteral("slide"));
    filmType_->setToolTip(tr("Filmtyp (LF-KON-01)"));
    layout->addWidget(new QLabel(tr("Filmtyp"), this));
    layout->addWidget(filmType_);
    hint_ = new QLabel(tr("Zu Beginn zeigt AutoNeg den Scan unverändert.") + QLatin1Char(' ') +
                           tr("Filmtyp wählen, um die markierten Bilder zu konvertieren."),
                       this);
    hint_->setObjectName(QStringLiteral("filmHint"));
    hint_->setWordWrap(true);
    layout->addWidget(hint_);
    // Einzelne Schritte; bei Autokonvertierung ausgeblendet
    manual_ = new QWidget(this);
    auto* m = new QVBoxLayout(manual_);
    m->setContentsMargins(0, 0, 0, 0);
    const auto button = [this](const QString& text, const QString& name, const QString& tip) {
        auto* b = new QPushButton(text, manual_);
        b->setObjectName(name);
        b->setToolTip(tip);
        return b;
    };
    // Schalter: an = der Schritt ist eingetragen (hellblau); nochmal klicken nimmt ihn zurück
    autoFrame_ = button(tr("Auto-Bildfeld"), QStringLiteral("autoFrame"),
                        tr("Nur das Bildfeld (Zuschnitt und Feindrehung) automatisch erkennen") +
                            QLatin1Char(' ') + tr("Der Rahmen lässt sich danach ziehen."));
    QPushButton* frameOptions = button(tr("Optionen …"), QStringLiteral("frameOptions"),
                                       tr("Bevorzugtes Bildformat und Lage als Hinweis für die Erkennung"));
    autoBase_ =
        button(tr("Auto-Basis"), QStringLiteral("autoBase"), tr("Nur die Filmbasis im Filmrand messen"));
    pickBase_ =
        button(tr("Basis messen"), QStringLiteral("pickBase"), tr("Messrechteck in den Filmrand ziehen"));
    convert_ = button(tr("Konvertieren"), QStringLiteral("convert"),
                      tr("Negativ mit der gemessenen Filmbasis umkehren (Tonwerte automatisch)"));
    for (QPushButton* b : {autoFrame_, autoBase_, pickBase_, convert_}) {
        b->setCheckable(true);
    }
    auto* frameRow = new QHBoxLayout;
    frameRow->addWidget(autoFrame_, 1);
    frameRow->addWidget(frameOptions);
    m->addLayout(frameRow);
    quality_ = new QLabel(manual_);
    quality_->setObjectName(QStringLiteral("frameQuality"));
    quality_->setWordWrap(true);
    QFont small = quality_->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    quality_->setFont(small);
    m->addWidget(quality_);
    QPushButton* autoBase = autoBase_;
    QPushButton* pickBase = pickBase_;
    auto* baseRow = new QHBoxLayout;
    baseRow->addWidget(autoBase);
    baseRow->addWidget(pickBase);
    m->addLayout(baseRow);
    // direkt unter der Basis: wo sie gemessen wurde
    showArea_ = new QCheckBox(tr("Messbereich anzeigen"), manual_);
    showArea_->setObjectName(QStringLiteral("showBaseArea"));
    showArea_->setToolTip(tr("Zeigt das ganze Bild mit dem Bereich, in dem die Filmbasis gemessen wurde"));
    m->addWidget(showArea_);
    auto* levelRow = new QHBoxLayout;
    auto* levelLabel = new QLabel(tr("Tonwerte"), manual_);
    levels_ = new QComboBox(manual_);
    levels_->setObjectName(QStringLiteral("levelsSource"));
    levels_->setToolTip(
        tr("Tonwertgrenzen automatisch aus dem Bild oder aus einer gespeicherten Filmvorgabe"));
    QPushButton* saveLevels =
        button(tr("Speichern …"), QStringLiteral("saveLevels"),
               tr("Tonwerte des Bilds als Filmvorgabe speichern, z. B. unter dem Filmnamen"));
    levelRow->addWidget(levelLabel);
    levelRow->addWidget(levels_, 1);
    levelRow->addWidget(saveLevels);
    m->addLayout(levelRow);
    m->addWidget(convert_);
    layout->addWidget(manual_);
    layout->addStretch(1);
    setLevelPresets({});
    connect(autoFrame_, &QPushButton::clicked, this, &ConversionPanel::autoFrameToggled);
    connect(autoBase_, &QPushButton::clicked, this, &ConversionPanel::autoBaseToggled);
    connect(pickBase_, &QPushButton::clicked, this, &ConversionPanel::pickBaseToggled);
    connect(convert_, &QPushButton::clicked, this, &ConversionPanel::convertToggled);
    connect(frameOptions, &QPushButton::clicked, this, &ConversionPanel::frameOptionsRequested);
    connect(saveLevels, &QPushButton::clicked, this, &ConversionPanel::saveLevelsRequested);
    connect(levels_, &QComboBox::activated, this, [this](int i) {
        const QString name = levels_->itemData(i).toString();
        if (name.isEmpty()) {
            Q_EMIT autoLevelsRequested();
        } else {
            Q_EMIT levelPresetChosen(name);
        }
    });
    connect(auto_, &QCheckBox::toggled, this, [this](bool on) {
        manual_->setVisible(!on);
        Q_EMIT autoConvertToggled(on);
    });
    filmButtons_ = {autoFrame_, auto_};
    baseControls_.push_back(convert_);
    baseControls_.insert(baseControls_.end(), {autoBase, pickBase, showArea_});
    connect(showArea_, &QCheckBox::toggled, this, &ConversionPanel::showBaseAreaToggled);
    // Die Wahl gilt für alle markierten Bilder; der MainWindow trägt sie ein
    connect(filmType_, &QComboBox::activated, this,
            [this](int i) { Q_EMIT filmTypeChosen(filmType_->itemData(i).toString()); });
    setFilm(model::FilmType::Unconverted);
}

void ConversionPanel::setAutoMode(bool on)
{
    const QSignalBlocker block(auto_);
    auto_->setChecked(on);
    manual_->setVisible(!on);
}

void ConversionPanel::setBusy(const QString& button, bool on)
{
    auto* b = manual_->findChild<QPushButton*>(button);
    if (b == nullptr || busy(button) == on) {
        return;
    }
    if (on) {
        busy_ << button;
    } else {
        busy_.removeAll(button);
        b->setIcon({});
    }
    b->setEnabled(!on);
    if (spin_ == nullptr) {
        spin_ = new QTimer(this);
        spin_->setInterval(80);
        connect(spin_, &QTimer::timeout, this, [this] {
            angle_ = (angle_ + 30) % 360;
            QPixmap icon(32, 32);
            icon.fill(Qt::transparent);
            QPainter p(&icon);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(palette().color(QPalette::Highlight), 4, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(4, 4, 24, 24), -angle_ * 16, 270 * 16);
            p.end();
            for (const QString& name : busy_) {
                if (auto* busyButton = manual_->findChild<QPushButton*>(name)) {
                    busyButton->setIcon(QIcon(icon));
                }
            }
        });
    }
    if (busy_.isEmpty()) {
        spin_->stop();
    } else {
        spin_->start();
    }
}

bool ConversionPanel::autoMode() const
{
    return auto_->isChecked();
}

void ConversionPanel::setLevelPresets(const QStringList& names)
{
    levels_->clear();
    levels_->addItem(tr("Automatisch"), QString());
    for (const QString& n : names) {
        levels_->addItem(n, n);
    }
}

void ConversionPanel::setShowArea(bool on)
{
    const QSignalBlocker block(showArea_);
    showArea_->setChecked(on);
}

void ConversionPanel::setQuality(const QString& text)
{
#ifdef NDEBUG
    // Die Bewertung ist noch nicht verlässlich genug für Anwender (Version 1.1)
    quality_->hide();
    return;
#endif
    quality_->setText(text);
    quality_->setVisible(!text.isEmpty());
}

void ConversionPanel::bindList(const model::EditList& l)
{
    // Schalterzustand aus der Liste: was eingetragen ist, ist an
    const auto get = [&l](std::string_view step) { return l.get(model::StepId(std::string(step))); };
    const model::EditEntry* crop = get(steps::id::crop);
    const model::EditEntry* base = get(steps::id::filmbase);
    const model::EditEntry* conv = get(steps::id::convert);
    const bool manualBase =
        base != nullptr && base->params.get<std::string>("source").value_or("") == "manual";
    autoFrame_->setChecked(crop != nullptr && crop->origin == model::Origin::Automatic);
    autoBase_->setChecked(base != nullptr && !manualBase);
    pickBase_->setChecked(!autoBase_->isChecked() && (manualBase || pickActive_));
    convert_->setChecked(conv != nullptr && conv->enabled && conv->params.get<bool>("on").value_or(false));
    // Der Filmtyp hängt auch vom Rollen-Standard ab und kommt über setFilm
}

void ConversionPanel::setFilm(model::FilmType film)
{
    const QSignalBlocker block(filmType_);
    filmType_->setCurrentIndex(
        std::max(0, filmType_->findData(QString::fromLatin1(steps::FilmModeStep::toString(film)))));
    const bool chosen = film != model::FilmType::Unconverted;
    const bool negative = film == model::FilmType::ColorNegative || film == model::FilmType::BwNegative;
    hint_->setVisible(!chosen);
    for (QWidget* b : filmButtons_) {
        b->setEnabled(chosen && !busy(b->objectName()));
    }
    levels_->setEnabled(negative);
    for (QWidget* w : baseControls_) {
        w->setEnabled(negative && !busy(w->objectName()));
    }
}

// --- AdjustPanel -------------------------------------------------------------------------------

AdjustPanel::AdjustPanel(UiController& ui, QWidget* parent)
    : ToolPanel(ui, parent)
{
    auto* layout = new QVBoxLayout(this);
    struct Def
    {
        std::string_view step;
        const char* param;
        QString label;
        QString tip;
    };
    const std::vector<Def> defs{
        {id::exposure, "ev", tr("Belichtung"), tr("Belichtung in Blendenstufen (EV)")},
        {id::whitebalance, "temperature", tr("Farbtemperatur"), tr("Weißabgleich: warm/kalt")},
        {id::whitebalance, "tint", tr("Tönung"), tr("Weißabgleich: grün/magenta")},
        {id::contrast, "amount", tr("Kontrast"), tr("Kontrast um den Mittelton")},
        {id::shadowshighlights, "shadows", tr("Tiefen"), tr("Tiefen aufhellen")},
        {id::shadowshighlights, "highlights", tr("Lichter"), tr("Lichter abdunkeln")},
        {id::saturation, "saturation", tr("Sättigung"), tr("Farbsättigung")},
        {id::saturation, "vibrance", tr("Dynamik"), tr("Sättigung schwacher Farben")},
        {id::sharpen, "amount", tr("Schärfen"), tr("Unscharf maskieren: Stärke")},
        {id::denoise, "strength", tr("Rauschminderung"), tr("Stärke der Rauschminderung")},
    };
    for (const Def& d : defs) {
        auto* s = new ParamSlider(std::string(d.step), d.param, d.label, d.tip, ui_.registry(), this);
        sliders_.push_back(s);
        layout->addWidget(s);
        const std::string step(d.step);
        const std::string param(d.param);
        connect(s, &ParamSlider::moving, this,
                [this, step, param](double v) { previewWith(withParam(step, param, v)); });
        connect(s, &ParamSlider::committed, this,
                [this, step, param](double v) { commit(withParam(step, param, v)); });
        connect(s, &ParamSlider::reset, this, [this, s, step, param] {
            // Regler zurück: bleibt ein Eintrag nur aus Neutralwerten, entfällt er ganz
            model::EditEntry e = withParam(step, param, s->neutral());
            bool allNeutral = true;
            if (const pipeline::IStep* st = ui_.registry().find(e.step)) {
                for (const pipeline::ParamSpec& spec : st->descriptor().params) {
                    if (const auto it =
                            std::ranges::find(e.params.entries(), spec.name, &model::ParamSet::Entry::first);
                        it != e.params.entries().end() && it->second != spec.neutral) {
                        allNeutral = false;
                    }
                }
            }
            if (allNeutral) {
                ui_.apply(std::make_unique<application::RemoveEntryCommand>(e.step));
            } else {
                commit(std::move(e));
            }
        });
    }
    picker_ = new QPushButton(tr("Weißabgleich-Pipette"), this);
    picker_->setObjectName(QStringLiteral("whitePicker"));
    picker_->setToolTip(tr("Neutrale Stelle im Bild anklicken"));
    layout->addWidget(picker_);
    hint_ = new QLabel(tr("Erst den Filmtyp wählen (Reiter Konvertierung)."), this);
    hint_->setObjectName(QStringLiteral("adjustHint"));
    hint_->setWordWrap(true);
    layout->insertWidget(0, hint_);
    layout->addStretch(1);
    connect(picker_, &QPushButton::clicked, this, &AdjustPanel::whitePickerRequested);
    setFilm(model::FilmType::Unconverted);
}

void AdjustPanel::setFilm(model::FilmType film)
{
    // Ein Regler gilt, wenn sein Schritt für den Filmtyp aktiv ist (Tabelle 1 der Architektur)
    const auto applies = [this, film](const std::string& step) {
        const pipeline::IStep* st = ui_.registry().find(model::StepId(step));
        return st != nullptr && st->descriptor().films.contains(film);
    };
    for (ParamSlider* s : sliders_) {
        s->setEnabled(applies(s->step()));
    }
    picker_->setEnabled(applies(std::string(id::whitebalance)));
    hint_->setVisible(film == model::FilmType::Unconverted);
}

ParamSlider* AdjustPanel::slider(const std::string& step, const std::string& param) const
{
    for (ParamSlider* s : sliders_) {
        if (s->step() == step && s->param() == param) {
            return s;
        }
    }
    return nullptr;
}

void AdjustPanel::bindList(const model::EditList& l)
{
    for (ParamSlider* s : sliders_) {
        s->setValue(paramOr(l, s->step(), s->param(), s->neutral()));
    }
}

} // namespace autoneg::ui
