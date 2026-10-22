#pragma once

#include "autoneg/base/geometry.hpp"
#include "autoneg/model/values.hpp"
#include "autoneg/pipeline/pixel_program_builder.hpp"
#include "autoneg/pipeline/step.hpp"
#include "autoneg/steps/multigrade.hpp"

#include <utility>

namespace autoneg::steps {

/// Konvertierungsmodell (Architektur 5.2/5.4, LF-KON-02…04):
///  - Negativ: T' = T / Basis (filmbase) → D = −log10 T' (density) → n = (D − dMin)/(dMax − dMin),
///    v = (10^(R·(n − 1)) − 10^−R)/(1 − 10^−R) mit R = 2 (densityrange). dMin/dMax je Kanal
///    gleichen zugleich die Farbbalance aus. v ist ein lineares Positiv (0 = Schwarz, 1 = Weiß).
///  - Dia: v = (T − dMin)/(dMax − dMin), ohne Basis und ohne Inversion.
/// Danach wirken alle Stufen auf v.
inline constexpr double densityOutputRange = 2.0;

/// Gemeinsame Grundlage: Beschreibung je Instanz.
class DescribedPointwise : public pipeline::PointwiseStep
{
public:
    explicit DescribedPointwise(pipeline::StepDescriptor d)
        : d_(std::move(d))
    {}
    [[nodiscard]] const pipeline::StepDescriptor& descriptor() const noexcept override { return d_; }

private:
    pipeline::StepDescriptor d_;
};

/// S/W-Kanalmischung (LF-KON-03): alle Kanäle = gewichtete Summe (Gewichte auf Summe 1
/// normiert). Bei S/W immer aktiv, Standard: Luminanzgewichte Rec. 2020.
class BwMixStep final : public DescribedPointwise
{
public:
    BwMixStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& /*p*/) const override { return false; }
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Basiskorrektur (LF-BAS-01): Transmission je Kanal durch die Basis teilen.
class FilmBaseStep final : public DescribedPointwise
{
public:
    FilmBaseStep();
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Dichte D = −log10 T (intern, ohne Parameter, Abgriffpunkt A2).
class DensityStep final : public DescribedPointwise
{
public:
    DensityStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& /*p*/) const override { return false; }
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Tonwertgrenzen und Inversion (LF-KON-02, -04, -05). Immer aktiv.
class DensityRangeStep final : public DescribedPointwise
{
public:
    DensityRangeStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& /*p*/) const override { return false; }
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Belichtung in Blendenstufen (LF-EDT-04): v · 2^ev.
class ExposureStep final : public DescribedPointwise
{
public:
    ExposureStep();
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Weißabgleich (LF-EDT-06): Verstärkung R = 2^(t/100), G = 2^(−u/100), B = 2^(−t/100) mit
/// t = Farbtemperatur, u = Tönung (je 100 Einheiten eine Blendenstufe).
class WhiteBalanceStep final : public DescribedPointwise
{
public:
    WhiteBalanceStep();
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
    /// Parameter (Temperatur, Tönung), die einen Graupunkt mit dem Wert rgb neutralisieren
    /// (Pipette, U-STP-15).
    [[nodiscard]] static std::pair<double, double> fromNeutral(const base::Vec3& rgb);
};

/// Gradation nach Ilford Multigrade (LF-KON-09…12): Tabelle der Papierkennlinie.
class GradationStep final : public DescribedPointwise
{
public:
    GradationStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& /*p*/) const override { return false; }
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
    [[nodiscard]] const MultigradeModel& model() const noexcept { return model_; }
    /// Kennlinie zu den Parametern (für Tests und Anzeige).
    [[nodiscard]] PaperCurve curve(const model::ParamSet& p) const;

private:
    MultigradeModel model_;
};

/// Gradation Farbe (LF-KON-13): Papierkennlinie in drei Stufen (1 weich, 2 normal, 3 hart)
/// für alle Kanäle; 0 = aus.
class ColorGradeStep final : public DescribedPointwise
{
public:
    ColorGradeStep();
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;

private:
    MultigradeModel model_;
};

/// Kontrast um einen Drehpunkt (LF-EDT-05): v' = pivot·(v/pivot)^(1 + amount).
class ContrastStep final : public DescribedPointwise
{
public:
    ContrastStep();
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Tonwerte und Kurven (LF-EDT-08): Schwarz-/Weißpunkt, Kurve gesamt und je Kanal
/// (monotone kubische Interpolation, keine Überschwinger).
class ToneCurveStep final : public DescribedPointwise
{
public:
    ToneCurveStep();
    [[nodiscard]] bool isNeutral(const model::ParamSet& p) const override;
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Sättigung und Dynamik (LF-EDT-09), siehe image::Saturate.
class SaturationStep final : public DescribedPointwise
{
public:
    SaturationStep();
    void emit(pipeline::PixelProgramBuilder& b, const model::ParamSet& p,
              const pipeline::StepContext& ctx) const override;
};

/// Monotone kubische Interpolation (Fritsch–Carlson) durch die Stützpunkte; außerhalb konstant.
/// Weniger als zwei Punkte: Identität.
[[nodiscard]] double evaluateCurve(const model::Curve& c, double x);
/// Kurve als Tabelle über [0, 1].
[[nodiscard]] image::Lut1D curveLut(const model::Curve& c, int channel, int n = 4096);

} // namespace autoneg::steps
