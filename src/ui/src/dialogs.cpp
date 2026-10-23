#include "autoneg/ui/dialogs.hpp"

#include "autoneg/ui/presets.hpp"
#include "autoneg/ui/translations.hpp"
#include "autoneg/ui/ui_controller.hpp"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

namespace autoneg::ui {

namespace {

QString qs(const std::filesystem::path& p)
{
    return QString::fromStdU16String(p.u16string());
}

std::filesystem::path fromQs(const QString& s)
{
    return std::filesystem::path(s.toStdU16String());
}

} // namespace

// --- ExportDialog ------------------------------------------------------------------------------

ExportDialog::ExportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Exportieren"));
    setObjectName(QStringLiteral("exportDialog"));
    auto* form = new QFormLayout;
    format_ = new QComboBox(this);
    format_->setObjectName(QStringLiteral("format"));
    format_->addItem(QStringLiteral("TIFF"), static_cast<int>(ports::FileFormat::Tiff));
    format_->addItem(QStringLiteral("JPEG"), static_cast<int>(ports::FileFormat::Jpeg));
    format_->addItem(QStringLiteral("PNG"), static_cast<int>(ports::FileFormat::Png));
    format_->addItem(QStringLiteral("JPEG XL"), static_cast<int>(ports::FileFormat::JpegXl));
    depth_ = new QComboBox(this);
    depth_->setObjectName(QStringLiteral("bitDepth"));
    depth_->addItem(tr("8 bit"), 8);
    depth_->addItem(tr("16 bit"), 16);
    quality_ = new QSpinBox(this);
    quality_->setObjectName(QStringLiteral("quality"));
    quality_->setRange(1, 100);
    quality_->setValue(90);
    profile_ = new QComboBox(this);
    profile_->setObjectName(QStringLiteral("profile"));
    profile_->addItem(QStringLiteral("sRGB"), static_cast<int>(ports::ProfileId::Srgb));
    profile_->addItem(QStringLiteral("Adobe RGB"), static_cast<int>(ports::ProfileId::AdobeRgb));
    profile_->addItem(QStringLiteral("ProPhoto RGB"), static_cast<int>(ports::ProfileId::ProPhoto));
    longEdge_ = new QSpinBox(this);
    longEdge_->setObjectName(QStringLiteral("longEdge"));
    longEdge_->setRange(0, 20000);
    longEdge_->setSpecialValueText(tr("Originalgröße"));
    longEdge_->setSuffix(tr(" px"));
    gray_ = new QCheckBox(tr("als Graustufen (S/W)"), this);
    gray_->setObjectName(QStringLiteral("grayscale"));
    folder_ = new QLineEdit(this);
    folder_->setObjectName(QStringLiteral("folder"));
    auto* browse = new QPushButton(tr("…"), this);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);
    pattern_ = new QLineEdit(QStringLiteral("{roll}_{frame}_{name}"), this);
    pattern_->setObjectName(QStringLiteral("pattern"));
    pattern_->setToolTip(tr("Platzhalter: {roll}, {frame}, {name}, {film}"));
    presetCombo_ = new QComboBox(this);
    presetCombo_->setObjectName(QStringLiteral("presetCombo"));
    presetCombo_->setToolTip(tr("Gespeicherte Exportvorlage laden"));
    auto* savePreset = new QPushButton(tr("Als Vorlage speichern …"), this);
    savePreset->setObjectName(QStringLiteral("savePreset"));
    savePreset->setToolTip(tr("Aktuelle Exporteinstellungen als Vorlage speichern"));
    auto* presetRow = new QHBoxLayout;
    presetRow->addWidget(presetCombo_, 1);
    presetRow->addWidget(savePreset);
    form->addRow(tr("Vorlage"), presetRow);
    form->addRow(tr("Format"), format_);
    form->addRow(tr("Bittiefe"), depth_);
    form->addRow(tr("Qualität"), quality_);
    form->addRow(tr("Farbraum"), profile_);
    form->addRow(tr("Lange Kante"), longEdge_);
    form->addRow(QString(), gray_);
    form->addRow(tr("Zielordner"), folderRow);
    form->addRow(tr("Dateiname"), pattern_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Exportieren"));
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Zielordner"), folder_->text());
        if (!d.isEmpty()) {
            folder_->setText(d);
        }
    });
    connect(format_, &QComboBox::currentIndexChanged, this, [this] { updateEnabled(); });
    connect(presetCombo_, &QComboBox::activated, this, [this](int i) {
        if (presets_ == nullptr || i <= 0) {
            return;
        }
        const auto j = presets_->load({ports::PresetKind::Export, presetCombo_->itemText(i).toStdString()});
        if (j) {
            application::ExportSettings s = exportFromJson(*j);
            if (s.folder.empty()) {
                s.folder = settings().folder; // Vorlage ohne Ordner: Ziel bleibt
            }
            setSettings(s);
        }
    });
    connect(savePreset, &QPushButton::clicked, this, [this] {
        if (presets_ == nullptr) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Vorlage speichern"), tr("Name der Vorlage"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            if (auto r = presets_->save(ports::PresetKind::Export, name.trimmed().toStdString(),
                                        exportToJson(settings()));
                !r) {
                QMessageBox::warning(this, tr("Vorlage speichern"),
                                     QString::fromStdString(r.error().message));
            }
            refreshPresets();
        }
    });
    updateEnabled();
}

void ExportDialog::setPresets(ports::IPresetRepository* repo)
{
    presets_ = repo;
    refreshPresets();
}

void ExportDialog::refreshPresets()
{
    presetCombo_->clear();
    presetCombo_->addItem(tr("– keine –"));
    if (presets_ != nullptr) {
        for (const ports::PresetInfo& p : presets_->list(ports::PresetKind::Export)) {
            presetCombo_->addItem(QString::fromStdString(p.id.name));
        }
    }
}

void ExportDialog::updateEnabled()
{
    const auto f = static_cast<ports::FileFormat>(format_->currentData().toInt());
    quality_->setEnabled(f == ports::FileFormat::Jpeg || f == ports::FileFormat::JpegXl);
    if (f == ports::FileFormat::Jpeg) {
        depth_->setCurrentIndex(0); // JPEG nur 8 bit
    }
    depth_->setEnabled(f != ports::FileFormat::Jpeg);
}

void ExportDialog::setSettings(const application::ExportSettings& s)
{
    format_->setCurrentIndex(std::max(0, format_->findData(static_cast<int>(s.write.format))));
    depth_->setCurrentIndex(std::max(0, depth_->findData(s.write.bitDepth)));
    quality_->setValue(s.write.quality);
    profile_->setCurrentIndex(std::max(0, profile_->findData(static_cast<int>(s.profile))));
    longEdge_->setValue(s.longEdge);
    gray_->setChecked(s.grayscale);
    folder_->setText(qs(s.folder));
    pattern_->setText(QString::fromStdString(s.pattern));
    updateEnabled();
}

application::ExportSettings ExportDialog::settings() const
{
    application::ExportSettings s;
    s.write.format = static_cast<ports::FileFormat>(format_->currentData().toInt());
    s.write.bitDepth = depth_->currentData().toInt();
    s.write.quality = quality_->value();
    s.profile = static_cast<ports::ProfileId>(profile_->currentData().toInt());
    s.longEdge = longEdge_->value();
    s.grayscale = gray_->isChecked();
    s.folder = fromQs(folder_->text());
    s.pattern = pattern_->text().toStdString();
    return s;
}

// --- ContactSheetDialog ------------------------------------------------------------------------

ContactSheetDialog::ContactSheetDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Kontaktabzug"));
    setObjectName(QStringLiteral("contactSheetDialog"));
    auto* form = new QFormLayout;
    format_ = new QComboBox(this);
    format_->setObjectName(QStringLiteral("filmFormat"));
    for (const layout::FilmFormat& f : layout::FilmFormat::catalog()) {
        format_->addItem(QString::fromStdString(f.name));
    }
    perStrip_ = new QSpinBox(this);
    perStrip_->setObjectName(QStringLiteral("perStrip"));
    perStrip_->setRange(1, 20);
    perStrip_->setValue(layout::FilmFormat::catalog()[0].perStrip);
    rebate_ = new QCheckBox(tr("mit Filmrand"), this);
    rebate_->setObjectName(QStringLiteral("rebate"));
    rebate_->setChecked(true);
    content_ = new QComboBox(this);
    content_->setObjectName(QStringLiteral("content"));
    content_->addItem(tr("wie bearbeitet"));
    content_->addItem(tr("einheitlich"));
    background_ = new QComboBox(this);
    background_->setObjectName(QStringLiteral("background"));
    background_->addItem(tr("schwarz"));
    background_->addItem(tr("weiß"));
    scale_ = new QComboBox(this);
    scale_->setObjectName(QStringLiteral("scale"));
    scale_->addItem(tr("einpassen"));
    scale_->addItem(tr("1:1"));
    paper_ = new QComboBox(this);
    paper_->setObjectName(QStringLiteral("paper"));
    paper_->addItem(QStringLiteral("A4"));
    paper_->addItem(QStringLiteral("A3"));
    paper_->addItem(QStringLiteral("Letter"));
    landscape_ = new QCheckBox(tr("quer"), this);
    landscape_->setObjectName(QStringLiteral("landscape"));
    binding_ = new QDoubleSpinBox(this);
    binding_->setObjectName(QStringLiteral("binding"));
    binding_->setRange(0.0, 50.0);
    binding_->setValue(25.0);
    binding_->setSuffix(QStringLiteral(" mm"));
    punch_ = new QCheckBox(tr("Lochmarkierung"), this);
    punch_->setObjectName(QStringLiteral("punchMarks"));
    rollNo_ = new QLineEdit(this);
    rollNo_->setObjectName(QStringLiteral("rollNo"));
    notes_ = new QPlainTextEdit(this);
    notes_->setObjectName(QStringLiteral("notes"));
    notes_->setMaximumHeight(60);
    presetCombo_ = new QComboBox(this);
    presetCombo_->setObjectName(QStringLiteral("presetCombo"));
    presetCombo_->setToolTip(tr("Gespeicherte Layoutvorlage laden"));
    form->addRow(tr("Vorlage"), presetCombo_);
    form->addRow(tr("Filmformat"), format_);
    form->addRow(tr("Bilder je Streifen"), perStrip_);
    form->addRow(QString(), rebate_);
    form->addRow(tr("Bildinhalt"), content_);
    form->addRow(tr("Hintergrund"), background_);
    form->addRow(tr("Maßstab"), scale_);
    form->addRow(tr("Papier"), paper_);
    form->addRow(QString(), landscape_);
    form->addRow(tr("Heftrand"), binding_);
    form->addRow(QString(), punch_);
    form->addRow(tr("Rollennummer"), rollNo_);
    form->addRow(tr("Notiz"), notes_);
    auto* pdf = new QPushButton(tr("Als PDF …"), this);
    pdf->setObjectName(QStringLiteral("exportPdf"));
    auto* print = new QPushButton(tr("Drucken …"), this);
    print->setObjectName(QStringLiteral("print"));
    auto* save = new QPushButton(tr("Als Vorlage speichern"), this);
    save->setObjectName(QStringLiteral("saveTemplate"));
    auto* close = new QPushButton(tr("Schließen"), this);
    auto* row = new QHBoxLayout;
    row->addWidget(save);
    row->addStretch(1);
    row->addWidget(pdf);
    row->addWidget(print);
    row->addWidget(close);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(row);
    connect(pdf, &QPushButton::clicked, this, &ContactSheetDialog::exportPdfRequested);
    connect(print, &QPushButton::clicked, this, &ContactSheetDialog::printRequested);
    connect(save, &QPushButton::clicked, this, [this] {
        if (presets_ == nullptr) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Vorlage speichern"), tr("Name der Vorlage"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            if (auto r = presets_->save(ports::PresetKind::ContactSheet, name.trimmed().toStdString(),
                                        sheetToJson({spec(), format()}));
                !r) {
                QMessageBox::warning(this, tr("Vorlage speichern"),
                                     QString::fromStdString(r.error().message));
            }
            refreshPresets();
        }
    });
    connect(presetCombo_, &QComboBox::activated, this, [this](int i) {
        if (presets_ == nullptr || i <= 0) {
            return;
        }
        const auto j =
            presets_->load({ports::PresetKind::ContactSheet, presetCombo_->itemText(i).toStdString()});
        if (j) {
            SheetPreset p = sheetFromJson(*j);
            p.spec.header = spec().header; // Rollendaten bleiben
            setSpec(p.spec, p.format);
        }
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(format_, &QComboBox::currentIndexChanged, this, [this](int i) {
        const auto c = layout::FilmFormat::catalog();
        if (i >= 0 && i < static_cast<int>(c.size())) {
            perStrip_->setValue(c[static_cast<std::size_t>(i)].perStrip);
        }
    });
}

void ContactSheetDialog::setSpec(const layout::ContactSheetSpec& s, const std::string& format)
{
    const int fi = format_->findText(QString::fromStdString(format));
    format_->setCurrentIndex(std::max(0, fi));
    if (s.framesPerStrip > 0) {
        perStrip_->setValue(s.framesPerStrip);
    }
    rebate_->setChecked(s.showRebate);
    content_->setCurrentIndex(s.content == layout::SheetContent::Uniform ? 1 : 0);
    background_->setCurrentIndex(s.background == layout::SheetBackground::White ? 1 : 0);
    scale_->setCurrentIndex(s.scale == layout::SheetScale::OneToOne ? 1 : 0);
    paper_->setCurrentIndex(static_cast<int>(s.paper));
    landscape_->setChecked(s.landscape);
    binding_->setValue(s.bindingMm);
    punch_->setChecked(s.punchMarks);
    header_ = s.header;
    rollNo_->setText(QString::fromStdString(s.header.rollNo));
    notes_->setPlainText(QString::fromStdString(s.header.notes));
}

layout::ContactSheetSpec ContactSheetDialog::spec() const
{
    layout::ContactSheetSpec s;
    s.framesPerStrip = perStrip_->value();
    s.showRebate = rebate_->isChecked();
    s.content =
        content_->currentIndex() == 1 ? layout::SheetContent::Uniform : layout::SheetContent::AsEdited;
    s.background =
        background_->currentIndex() == 1 ? layout::SheetBackground::White : layout::SheetBackground::Black;
    s.scale = scale_->currentIndex() == 1 ? layout::SheetScale::OneToOne : layout::SheetScale::Fit;
    s.paper = static_cast<layout::PaperSize>(paper_->currentIndex());
    s.landscape = landscape_->isChecked();
    s.bindingMm = binding_->value();
    s.punchMarks = punch_->isChecked();
    s.header = header_;
    s.header.rollNo = rollNo_->text().toStdString();
    s.header.notes = notes_->toPlainText().toStdString();
    return s;
}

std::string ContactSheetDialog::format() const
{
    return format_->currentText().toStdString();
}

void ContactSheetDialog::setPresets(ports::IPresetRepository* repo)
{
    presets_ = repo;
    refreshPresets();
}

void ContactSheetDialog::refreshPresets()
{
    presetCombo_->clear();
    presetCombo_->addItem(tr("– keine –"));
    if (presets_ != nullptr) {
        for (const ports::PresetInfo& p : presets_->list(ports::PresetKind::ContactSheet)) {
            presetCombo_->addItem(QString::fromStdString(p.id.name));
        }
    }
}

// --- PasteDialog -------------------------------------------------------------------------------

PasteDialog::PasteDialog(const model::EditList& source, std::set<model::StepId> preselected,
                         const pipeline::StepRegistry& registry, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Einfügen"));
    setObjectName(QStringLiteral("pasteDialog"));
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("pasteSelection"));
    for (const model::EditEntry& e : source.entries()) {
        auto* item = new QListWidgetItem(translated(stepName(registry, e.step)), list_);
        item->setData(Qt::UserRole, QString::fromStdString(e.step.value()));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(preselected.contains(e.step) ? Qt::Checked : Qt::Unchecked);
    }
    auto* values = new QRadioButton(tr("Werte übernehmen"), this);
    values->setChecked(true);
    redetect_ = new QRadioButton(tr("Bildfeld und Basis neu erkennen"), this);
    redetect_->setObjectName(QStringLiteral("redetect"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(list_);
    layout->addWidget(values);
    layout->addWidget(redetect_);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

std::set<model::StepId> PasteDialog::selection() const
{
    std::set<model::StepId> out;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->checkState() == Qt::Checked) {
            out.insert(model::StepId(list_->item(i)->data(Qt::UserRole).toString().toStdString()));
        }
    }
    return out;
}

application::PasteMode PasteDialog::mode() const
{
    return redetect_->isChecked() ? application::PasteMode::Redetect : application::PasteMode::Values;
}

// --- SettingsDialog ----------------------------------------------------------------------------

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Einstellungen"));
    setObjectName(QStringLiteral("settingsDialog"));
    auto* form = new QFormLayout;
    language_ = new QComboBox(this);
    language_->setObjectName(QStringLiteral("language"));
    language_->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language_->setToolTip(tr("Sprache der Oberfläche; wirkt nach einem Neustart"));
    threads_ = new QSpinBox(this);
    threads_->setObjectName(QStringLiteral("threads"));
    threads_->setRange(0, 256);
    threads_->setSpecialValueText(tr("alle Kerne"));
    threads_->setToolTip(tr("Anzahl der Rechen-Threads (NF-PERF-12)"));
    memory_ = new QSpinBox(this);
    memory_->setObjectName(QStringLiteral("memoryMb"));
    memory_->setRange(512, 262144);
    memory_->setSingleStep(512);
    memory_->setSuffix(QStringLiteral(" MB"));
    memory_->setToolTip(tr("Arbeitsspeicher für Zwischenbilder bei Export und Stapelverarbeitung"));
    form->addRow(tr("Sprache"), language_);
    form->addRow(tr("Threads"), threads_);
    form->addRow(tr("Speicherbudget"), memory_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::setValues(const Values& v)
{
    language_->setCurrentIndex(std::max(0, language_->findData(QString::fromStdString(v.language))));
    threads_->setValue(v.threads);
    memory_->setValue(v.memoryMb);
}

SettingsDialog::Values SettingsDialog::values() const
{
    return {language_->currentData().toString().toStdString(), threads_->value(), memory_->value()};
}

} // namespace autoneg::ui
