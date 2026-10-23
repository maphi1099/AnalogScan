#pragma once

#include "autoneg/application/commands.hpp"
#include "autoneg/application/services.hpp"
#include "autoneg/layout/contact_sheet.hpp"
#include "autoneg/pipeline/step_registry.hpp"
#include "autoneg/ports/repositories.hpp"

#include <QtWidgets/QDialog>

#include <set>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QRadioButton;
class QSpinBox;

namespace autoneg::ui {

/// Exportdialog (LF-EXP-01 … -10): Format, Bittiefe, Qualität, Farbraum, Größe, Graustufen,
/// Zielordner und Dateinamensschema.
class ExportDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(QWidget* parent = nullptr);
    void setSettings(const application::ExportSettings& s);
    [[nodiscard]] application::ExportSettings settings() const;
    /// Exportvorlagen laden und speichern (LF-EXP-10).
    void setPresets(ports::IPresetRepository* repo);

private:
    void updateEnabled();
    void refreshPresets();
    ports::IPresetRepository* presets_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QComboBox* format_ = nullptr;
    QComboBox* depth_ = nullptr;
    QSpinBox* quality_ = nullptr;
    QComboBox* profile_ = nullptr;
    QSpinBox* longEdge_ = nullptr;
    QCheckBox* gray_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QLineEdit* pattern_ = nullptr;
};

/// Kontaktabzug (LF-KTA-01 … -10): Format, Anordnung, Darstellung, Beschriftung, Papier.
class ContactSheetDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ContactSheetDialog(QWidget* parent = nullptr);
    void setSpec(const layout::ContactSheetSpec& s, const std::string& format);
    [[nodiscard]] layout::ContactSheetSpec spec() const;
    [[nodiscard]] std::string format() const;
    /// Layoutvorlagen laden und speichern (LF-KTA-10).
    void setPresets(ports::IPresetRepository* repo);

Q_SIGNALS:
    void exportPdfRequested();
    void printRequested();

private:
    void refreshPresets();
    ports::IPresetRepository* presets_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QComboBox* format_ = nullptr;
    QSpinBox* perStrip_ = nullptr;
    QCheckBox* rebate_ = nullptr;
    QComboBox* content_ = nullptr;
    QComboBox* background_ = nullptr;
    QComboBox* scale_ = nullptr;
    QComboBox* paper_ = nullptr;
    QCheckBox* landscape_ = nullptr;
    QDoubleSpinBox* binding_ = nullptr;
    QCheckBox* punch_ = nullptr;
    QLineEdit* rollNo_ = nullptr;
    QPlainTextEdit* notes_ = nullptr;
    layout::SheetHeader header_;
};

/// Einfügen auf andere Bilder (LF-BAT-02 … -04): Auswahl der Einträge, Werte oder neu erkennen.
class PasteDialog final : public QDialog
{
    Q_OBJECT

public:
    PasteDialog(const model::EditList& source, std::set<model::StepId> preselected,
                const pipeline::StepRegistry& registry, QWidget* parent = nullptr);
    [[nodiscard]] std::set<model::StepId> selection() const;
    [[nodiscard]] application::PasteMode mode() const;
    [[nodiscard]] QListWidget* list() const { return list_; }

private:
    QListWidget* list_ = nullptr;
    QRadioButton* redetect_ = nullptr;
};

/// Einstellungen (LF-DAT-04, LF-UI-03, NF-PERF-12): Sprache, Threadzahl, Speicherbudget.
class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    struct Values
    {
        std::string language = "de"; ///< "de" oder "en"
        int threads = 0;             ///< 0 = alle Kerne
        int memoryMb = 4096;         ///< Budget für Zwischenbilder
    };

    explicit SettingsDialog(QWidget* parent = nullptr);
    void setValues(const Values& v);
    [[nodiscard]] Values values() const;

private:
    QComboBox* language_ = nullptr;
    QSpinBox* threads_ = nullptr;
    QSpinBox* memory_ = nullptr;
};

} // namespace autoneg::ui
