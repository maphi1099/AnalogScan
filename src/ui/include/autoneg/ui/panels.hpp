#pragma once

#include "autoneg/ui/ui_controller.hpp"

#include <QtWidgets/QWidget>

#include <array>
#include <map>
#include <string>
#include <vector>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace autoneg::ui {

/// Gemeinsame Basis der Werkzeugfelder (Architektur 9.14): kennt den UiController und zeigt
/// den aktuellen Stand der Liste. bindList() ruft der MainWindow nach jeder Änderung auf.
class ToolPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPanel(UiController& ui, QWidget* parent = nullptr)
        : QWidget(parent)
        , ui_(ui)
    {}
    virtual void bindList(const model::EditList& l) = 0;
    /// Filmtyp des aktuellen Bilds: Bedienelemente, die für ihn nicht gelten, werden gesperrt
    /// (K-M10-08). Vor der Wahl eines Filmtyps (Unconverted) gibt es nur den Scan.
    virtual void setFilm(model::FilmType /*film*/) {}

protected:
    /// Setzt einen Parameter eines Schritts; die übrigen Parameter des Eintrags bleiben.
    [[nodiscard]] model::EditEntry withParam(const std::string& step, const std::string& name,
                                             model::ParamValue v) const;
    void commit(model::EditEntry e);
    void previewWith(model::EditEntry e);

    UiController& ui_;
};

/// Ein Regler für einen Double-Parameter eines Schritts, Bereich aus dem Parameterschema
/// der StepRegistry. Ziehen zeigt die Vorschau, Loslassen erzeugt den Befehl, Doppelklick
/// setzt auf den Neutralwert zurück (LF-EDT-16).
class ParamSlider final : public QWidget
{
    Q_OBJECT

public:
    ParamSlider(std::string step, std::string param, const QString& label, const QString& tip,
                const pipeline::StepRegistry& registry, QWidget* parent = nullptr);

    void setValue(double v);
    [[nodiscard]] double value() const;
    [[nodiscard]] double neutral() const { return neutral_; }
    [[nodiscard]] const std::string& step() const { return step_; }
    [[nodiscard]] const std::string& param() const { return param_; }
    [[nodiscard]] QSlider* slider() const { return slider_; }

Q_SIGNALS:
    void moving(double v);
    void committed(double v);
    void reset();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    [[nodiscard]] double fromTicks(int t) const;
    [[nodiscard]] int toTicks(double v) const;

    std::string step_;
    std::string param_;
    double min_ = -1.0;
    double max_ = 1.0;
    double neutral_ = 0.0;
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* spin_ = nullptr;
};

/// Gradation nach Ilford-Multigrade-Filterstufen (LF-KON-09 … -12): zwölf Stufen 00 … 5 in
/// halben Schritten, „ohne Filter“ (entspricht Stufe 2), Feinwert in Zehnteln, Split-Grade.
class GradationPanel final : public ToolPanel
{
    Q_OBJECT

public:
    explicit GradationPanel(UiController& ui, QWidget* parent = nullptr);
    void bindList(const model::EditList& l) override;

    /// Stufenwert der Schaltfläche i (0 … 11), "ohne Filter" ist Index 12.
    [[nodiscard]] static double gradeOf(int button);
    [[nodiscard]] static QString labelOf(int button);
    [[nodiscard]] QAbstractButton* button(int i) const;
    void setSplit(double soft, double hard);

private:
    QButtonGroup* grades_ = nullptr;
    QDoubleSpinBox* fine_ = nullptr;
    QSlider* soft_ = nullptr;
    QSlider* hard_ = nullptr;
};

/// Konvertierung (LF-KON-01, LF-BAS-01 … -04, LF-GEO-01): Filmtyp und Automatiken.
class ConversionPanel final : public ToolPanel
{
    Q_OBJECT

public:
    explicit ConversionPanel(UiController& ui, QWidget* parent = nullptr);
    void bindList(const model::EditList& l) override;
    void setFilm(model::FilmType film) override;
    [[nodiscard]] QComboBox* filmType() const { return filmType_; }

Q_SIGNALS:
    /// Wahl im Feld Filmtyp: "color", "bw", "slide" oder "raw" (Filmtyp entfernen).
    void filmTypeChosen(const QString& type);
    void autoFrameRequested();
    void autoBaseRequested();
    void autoLevelsRequested();
    void pickBaseRequested();
    void showBaseAreaToggled(bool on);

private:
    QComboBox* filmType_ = nullptr;
    QCheckBox* showArea_ = nullptr;
    QLabel* hint_ = nullptr;
    std::vector<QPushButton*> filmButtons_; ///< Auto-Bildfeld, Auto-Tonwerte: nur mit Filmtyp
    std::vector<QWidget*> baseControls_;    ///< Filmbasis: nur bei Negativen
};

/// Regler der Bildbearbeitung (LF-EDT-04 … -11): Belichtung, Weißabgleich, Kontrast, Lichter/
/// Tiefen, Sättigung, Schärfen, Rauschminderung. Jeder Regler hat einen Tooltip (LF-UI-08).
class AdjustPanel final : public ToolPanel
{
    Q_OBJECT

public:
    explicit AdjustPanel(UiController& ui, QWidget* parent = nullptr);
    void bindList(const model::EditList& l) override;
    void setFilm(model::FilmType film) override;
    [[nodiscard]] const std::vector<ParamSlider*>& sliders() const { return sliders_; }
    [[nodiscard]] ParamSlider* slider(const std::string& step, const std::string& param) const;

Q_SIGNALS:
    void whitePickerRequested();

private:
    std::vector<ParamSlider*> sliders_;
    QLabel* hint_ = nullptr;
    QPushButton* picker_ = nullptr;
};

} // namespace autoneg::ui
