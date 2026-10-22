#pragma once

// Gemeinsame Typen des Benchmarks. Bilder sind RGB-float, Kanäle verschachtelt (RGBRGB…),
// wie autoneg::image::ImageBuffer.

#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace bench {

struct AlignedFree
{
    void operator()(float* p) const noexcept { ::operator delete[](p, std::align_val_t{64}); }
};

struct Image
{
    int w = 0;
    int h = 0;
    std::unique_ptr<float[], AlignedFree> data;

    Image() = default;
    Image(int width, int height)
        : w(width)
        , h(height)
        , data(new(std::align_val_t{64}) float[size()])
    {}

    [[nodiscard]] std::size_t size() const noexcept { return static_cast<std::size_t>(w) * h * 3; }
    [[nodiscard]] float* row(int y) noexcept { return data.get() + static_cast<std::size_t>(y) * w * 3; }
    [[nodiscard]] const float* row(int y) const noexcept
    {
        return data.get() + static_cast<std::size_t>(y) * w * 3;
    }
};

/// Basiskorrektur bis Belichtung (Durchlauf 4 der Pipeline, ohne Gradation):
/// v = max(in * invBase, 1e-6); d = -log10(v); out = (d - dMin) * scale + exposure
struct PointwiseParams
{
    std::array<float, 3> invBase{1.0F / 0.71F, 1.0F / 0.43F, 1.0F / 0.28F};
    std::array<float, 3> dMin{0.05F, 0.08F, 0.11F};
    std::array<float, 3> scale{0.55F, 0.6F, 0.5F};
    float exposure = 0.05F;
};

/// Gradation: Tabelle mit 4096 Stützstellen über [0, 1], linear interpoliert, Eingabe begrenzt.
struct Lut
{
    std::vector<float> values;
};

/// Unscharfmaskierung: blur = Gauß(sigma); diff = in - blur;
/// out = in + amount * diff, falls |diff| > threshold, sonst in. Rand: Randpixel wiederholen.
struct UnsharpParams
{
    float amount = 0.6F;
    float sigma = 1.2F;
    float threshold = 0.002F;
    static constexpr int radius = 4;
    [[nodiscard]] std::array<float, 2 * radius + 1> weights() const;
};

/// Zeilenblockgröße der Parallelisierung (wie CpuBackend::blockRows, Architektur 9.10).
inline constexpr int blockRows = 64;

// Skalare Referenz und Highway-Variante, jeweils parallel über Zeilenblöcke mit oneTBB.
void scalarPointwise(const Image& in, Image& out, const PointwiseParams& p);
void scalarLut(const Image& in, Image& out, const Lut& lut);
void scalarFused(const Image& in, Image& out, const PointwiseParams& p, const Lut& lut);
void scalarUnsharp(const Image& in, Image& out, const UnsharpParams& p);

void hwyPointwise(const Image& in, Image& out, const PointwiseParams& p);
void hwyLut(const Image& in, Image& out, const Lut& lut);
void hwyFused(const Image& in, Image& out, const PointwiseParams& p, const Lut& lut);
void hwyUnsharp(const Image& in, Image& out, const UnsharpParams& p);
const char* hwyTargetName();

// Halide (AOT, eigener Thread-Pool der Halide-Laufzeit).
void halideSetThreads(int n);
void halidePointwise(const Image& in, Image& out, const PointwiseParams& p);
void halideLut(const Image& in, Image& out, const Lut& lut);
void halideFused(const Image& in, Image& out, const PointwiseParams& p, const Lut& lut);
void halideUnsharp(const Image& in, Image& out, const UnsharpParams& p);

} // namespace bench
