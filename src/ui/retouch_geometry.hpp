#pragma once

#include "model/values.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace autoneg::ui {

/// Geometrie der Retusche-Formen für Liste und Auswahl (K-M10-23), ohne Bilddaten.
///
/// Freie Flächen werden auf ein grobes Raster gezeichnet (512 Zellen über die Bildbreite):
/// zusammenhängende Flächen sind ein Listeneintrag, getrennte mehrere; ein Radierer, der eine
/// Fläche teilt, ergibt zwei. Jede Fläche merkt sich einen Punkt in ihrem Inneren (center);
/// gezeichnet wird nur das zusammenhängende Stück um diesen Punkt (wie im CpuBackend).
/// aspect = Bildhöhe / Bildbreite.
class RetouchGeometry
{
public:
    static constexpr int gridWidth = 512;

    explicit RetouchGeometry(double aspect);

    [[nodiscard]] int gridHeight() const { return gh_; }
    /// Rastermaske einer Form (1 = innen), Zeilen nacheinander.
    [[nodiscard]] std::vector<std::uint8_t> raster(const model::RetouchShape& s) const;
    /// Pinselstrich hinzufügen (Fläche malen oder radieren) und die Flächen neu gruppieren.
    /// newName liefert Namen für neue Flächen.
    [[nodiscard]] model::RetouchShapes addStroke(model::RetouchShapes shapes, const model::RetouchStroke& stroke,
                                                 const std::function<std::string()>& newName) const;
    /// Flächen nach Zusammenhang neu bilden (Kreise bleiben unverändert).
    [[nodiscard]] model::RetouchShapes regroup(const model::RetouchShapes& shapes,
                                               const std::function<std::string()>& newName) const;
    /// Oberste Form an der Stelle p (Kreis: im Radius, Fläche: auf der Fläche).
    [[nodiscard]] std::optional<std::size_t> hit(const model::RetouchShapes& shapes, model::NormPoint p) const;

private:
    void stamp(std::vector<std::uint8_t>& m, double cx, double cy, double r, std::uint8_t v) const;
    /// Zusammenhangskomponente (4er-Nachbarschaft) um (x, y) in m mit Wert 1 → Wert mark.
    void fill(std::vector<std::uint8_t>& m, int x, int y, std::uint8_t mark) const;

    double aspect_;
    int gh_;
};

} // namespace autoneg::ui
