#pragma once

// Rechen-Backend aus den skalaren Referenzkernels. Langsam, einkernig, aber exakt nach
// Definition; dient als Attrappe für Pipeline-Tests und als Vergleich für das CpuBackend.
// Zählt die Aufrufe je Kernel (U-PIP-11) und kann nach n Aufrufen abbrechen (U-PIP-15).

#include "autoneg/ports/compute_backend.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace autoneg::reference {

class ReferenceBackend final : public ports::IComputeBackend
{
public:
    using In = const image::ImageBuffer&;
    using Ctx = const ports::KernelContext&;
    using Result = base::Expected<image::ImageBuffer>;

    [[nodiscard]] std::string id() const override { return "reference"; }
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] int concurrency() const override { return 1; }

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
    base::Expected<image::ImageBuffer> quantize(Ctx c, In in, int bits) override;
    base::Expected<image::ImageBuffer> colorTransform(Ctx c, In in,
                                                      const ports::IColorTransform& xf) override;

    /// Anzahl der Aufrufe des Kernels name ("runPixel", "warp", …).
    [[nodiscard]] int calls(const std::string& name) const;
    [[nodiscard]] int totalCalls() const;
    void resetCalls();
    /// Wird vor jedem Kernelaufruf mit der laufenden Nummer (ab 1) aufgerufen.
    void setHook(std::function<void(int)> hook) { hook_ = std::move(hook); }

private:
    bool begin(Ctx c, const std::string& name);

    mutable std::mutex mtx_;
    std::map<std::string, int> calls_;
    int total_ = 0;
    std::function<void(int)> hook_;
};

} // namespace autoneg::reference
