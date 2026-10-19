#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/ports/compute_backend.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace autoneg::ports {

/// Kennzahlen eines geladenen Modells.
struct ModelInfo
{
    std::string name;
    std::string version;
    int inputChannels = 0;
    int outputChannels = 0;
    int tileSize = 0; ///< empfohlene Kachelgröße (NF-ARC-08), 0 = beliebig
    int halo = 0;     ///< nötiger Rand je Kachel

    friend bool operator==(const ModelInfo&, const ModelInfo&) = default;
};

/// Geladenes KI-Modell inklusive Vor- und Nachverarbeitung. Threadsicher.
class IInferenceModel
{
public:
    virtual ~IInferenceModel() = default;

    [[nodiscard]] virtual ModelInfo info() const = 0;
    [[nodiscard]] virtual base::Expected<image::ImageBuffer> run(const KernelContext& c,
                                                                 const image::ImageBuffer& in) = 0;
};

/// Austauschbare KI-Laufzeit (NF-ARC-07), z. B. ONNX Runtime.
///
/// Version 1.0: nur die Schnittstelle (D-04); eine Implementierung folgt mit Version 1.1.
/// Ein KI-Schritt ist ein gewöhnlicher RegionStep, der ein IInferenceModel nutzt.
class IInferenceRuntime
{
public:
    virtual ~IInferenceRuntime() = default;

    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual bool available() const = 0;
    /// Modell aus Datei; Austausch der Datei ohne Neukompilierung.
    [[nodiscard]] virtual base::Expected<std::unique_ptr<IInferenceModel>>
    load(const std::filesystem::path& model) = 0;
};

} // namespace autoneg::ports
