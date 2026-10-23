#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/base/geometry.hpp"
#include "autoneg/image/cancel_token.hpp"
#include "autoneg/image/histogram.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/image/pixel_program.hpp"
#include "autoneg/image/warp_plan.hpp"
#include "autoneg/model/values.hpp"
#include "autoneg/ports/color.hpp"

#include <string>

namespace autoneg::ports {

/// Laufzeitangaben für einen Kernel.
///
/// Ergänzung zu Architektur 9.10: Kernels prüfen das CancelToken zwischen Zeilenblöcken
/// bzw. Kacheln und melden einen Abbruch als Fehler Cancelled; deshalb liefern alle
/// Rechenmethoden Expected.
struct KernelContext
{
    image::CancelToken cancel;
};

/// Rechen-Backend (NF-ARC-05). Datenparallel über alle Kerne (NF-PERF-05).
/// Implementierungen: CpuBackend (Referenz); GPU ab Version 1.1 (D-03).
///
/// Vertrag:
///  - threadsicher, beliebig viele gleichzeitige Aufrufe;
///  - Ergebnisse unabhängig von der Threadzahl bitidentisch (NF-ARC-16);
///  - Eingaben bleiben unverändert, Ausgaben haben das Format F32;
///  - Puffer anderer Backends werden bei Bedarf mit upload/download übertragen.
class IComputeBackend
{
public:
    virtual ~IComputeBackend() = default;

    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual int concurrency() const = 0;

    [[nodiscard]] virtual base::Expected<image::ImageBuffer> upload(const image::ImageBuffer& img) = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> download(const image::ImageBuffer& img) = 0;

    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    runPixel(const KernelContext& c, const image::PixelProgram& p, const image::ImageBuffer& in) = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    warp(const KernelContext& c, const image::ImageBuffer& in, const image::WarpPlan& w) = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    resample(const KernelContext& c, const image::ImageBuffer& in, base::Size s) = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    multiply(const KernelContext& c, const image::ImageBuffer& in, const image::ImageBuffer& gain) = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    gaussian(const KernelContext& c, const image::ImageBuffer& in, float sigma) = 0;
    /// Lichter und Tiefen: s, h Stärke, r Radius in Pixeln.
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    localTone(const KernelContext& c, const image::ImageBuffer& in, float s, float h, float r) = 0;
    /// Unscharfmaskierung: a Stärke, r Radius in Pixeln, t Schwellwert.
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    unsharp(const KernelContext& c, const image::ImageBuffer& in, float a, float r, float t) = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    denoise(const KernelContext& c, const image::ImageBuffer& in, float strength) = 0;
    /// Retusche; Koordinaten der Stellen normiert auf das Eingabebild.
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    inpaint(const KernelContext& c, const image::ImageBuffer& in, const model::SpotList& spots) = 0;
    /// Feste Blockeinteilung, unabhängig von der Threadzahl (Kapitel 6.4).
    [[nodiscard]] virtual base::Expected<image::Histogram>
    histogram(const KernelContext& c, const image::ImageBuffer& in, int bins) = 0;
    /// Farbtransformation datenparallel in festen Zeilenblöcken (NF-PERF-03, NF-PERF-05); xf muss
    /// threadsicher sein. Ergebnis gleich xf.apply(in). Ergänzung D-17.
    [[nodiscard]] virtual base::Expected<image::ImageBuffer>
    colorTransform(const KernelContext& c, const image::ImageBuffer& in, const IColorTransform& xf) = 0;
};

} // namespace autoneg::ports
