#pragma once

#include "autoneg/ports/compute_backend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace autoneg::backend::cpu {

/// SIMD-Befehlssatz, den Highway zur Laufzeit wählen kann.
struct SimdTarget
{
    std::int64_t bits = 0; ///< Highway-Zielkennung
    std::string name;      ///< z. B. "AVX2", "EMU128" (ohne SIMD)

    friend bool operator==(const SimdTarget&, const SimdTarget&) = default;
};

/// Referenz-Rechenbackend auf der CPU (AE-07, OE-01: Google Highway + oneTBB).
///
/// Datenparallel über Zeilenblöcke zu 64 Zeilen (blockRows) mit oneTBB in der Arena des
/// Aufrufers, je Kern SIMD über Highway mit Laufzeitauswahl des Befehlssatzes. Ergebnisse sind
/// unabhängig von der Threadzahl bitidentisch: jede Zeile wird unabhängig von der Aufteilung
/// gerechnet, Summen (Histogramm) sind ganzzahlig. Abbruch wird zwischen Zeilenblöcken geprüft.
/// Definition der Ergebnisse: tests/reference (skalare Referenz).
/// Threadsicher.
class CpuBackend final : public ports::IComputeBackend
{
public:
    struct Options
    {
        /// Höchstzahl der Threads; 0 = Arena des Aufrufers (Standard, alle Kerne).
        int threads = 0;
    };

    CpuBackend();
    explicit CpuBackend(Options o);
    ~CpuBackend() override;
    CpuBackend(const CpuBackend&) = delete;
    CpuBackend& operator=(const CpuBackend&) = delete;
    CpuBackend(CpuBackend&&) = delete;
    CpuBackend& operator=(CpuBackend&&) = delete;

    static constexpr int blockRows = 64;

    [[nodiscard]] std::string id() const override { return "cpu"; }
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] int concurrency() const override;

    using In = const image::ImageBuffer&;
    using Ctx = const ports::KernelContext&;
    using Result = base::Expected<image::ImageBuffer>;

    Result upload(In img) override { return img; }
    Result download(In img) override { return img; }
    Result runPixel(Ctx c, const image::PixelProgram& p, In in) override;
    Result warp(Ctx c, In in, const image::WarpPlan& w) override;
    Result resample(Ctx c, In in, base::Size s) override;
    Result multiply(Ctx c, In in, In gain) override;
    Result gaussian(Ctx c, In in, float sigma) override;
    Result localTone(Ctx c, In in, float s, float h, float r) override;
    Result unsharp(Ctx c, In in, float a, float r, float t) override;
    Result denoise(Ctx c, In in, float strength) override;
    Result inpaint(Ctx c, In in, const model::SpotList& spots) override;
    base::Expected<image::Histogram> histogram(Ctx c, In in, int bins) override;
    base::Expected<image::ImageBuffer> colorTransform(Ctx c, In in,
                                                      const ports::IColorTransform& xf) override;

    /// Befehlssätze, die auf diesem Rechner nutzbar und mitkompiliert sind (U-BCK-02).
    [[nodiscard]] static std::vector<SimdTarget> targets();
    /// Gerade gewählter Befehlssatz.
    [[nodiscard]] static SimdTarget activeTarget();
    /// Nur für Tests: Auswahl auf einen Befehlssatz beschränken (prozessweit) bzw. zurücksetzen.
    static void forceTargetForTest(const SimdTarget& t);
    static void resetTargetForTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace autoneg::backend::cpu
