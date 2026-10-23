#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/layout/contact_sheet.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/ports/image_io.hpp"

#include <filesystem>
#include <map>
#include <span>
#include <string>

namespace autoneg::ports {

/// Eine Seite zum Ausgeben: Layout (mm) und die Bilder der Slots, bereits im Ausgabefarbraum
/// (Werte 0…1, codiert wie das Profil), mindestens in der Auflösung des Layouts (dpi).
struct RenderedPage
{
    layout::PageLayout layout;
    std::map<model::FrameId, image::ImageBuffer> images;
    IccProfile outputProfile; ///< wird eingebettet (LF-KTA-06)
    int dpi = 300;
};

struct PrintSettings
{
    bool showDialog = true; ///< Systemdruckdialog (LF-KTA-06); sonst Standarddrucker
    int copies = 1;
};

/// Ausgabe des Kontaktabzugs als PDF und auf den Drucker (Architektur 9.11, LF-KTA-06).
/// Adapter: QtDocumentRenderer. Ergänzung K-M1-13 (in M7 umgesetzt).
class IDocumentRenderer
{
public:
    virtual ~IDocumentRenderer() = default;
    [[nodiscard]] virtual base::Expected<void> writePdf(std::span<const RenderedPage> pages,
                                                        const std::filesystem::path& p) = 0;
    /// Fehler Cancelled, wenn der Anwender den Druckdialog abbricht.
    [[nodiscard]] virtual base::Expected<void> print(std::span<const RenderedPage> pages,
                                                     const PrintSettings& s) = 0;
};

} // namespace autoneg::ports
