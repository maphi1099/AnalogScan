#pragma once

#include "autoneg/application/jobs.hpp"
#include "autoneg/application/render_service.hpp"
#include "autoneg/layout/contact_sheet.hpp"
#include "autoneg/model/roll.hpp"
#include "autoneg/ports/color.hpp"
#include "autoneg/ports/document_renderer.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace autoneg::application {

/// Kontaktabzug als PDF oder Druck (Architektur 9.13, LF-KTA-01…10).
///
/// Layout über den ContactSheetLayouter; je Slot rendert der Dienst das Bild in der
/// Auflösung des Layouts (spec.dpi):
///  - AsEdited: die aktuelle Bearbeitungsliste;
///  - Uniform: nur die bildbezogenen Schritte (Geometrie, Retusche, Filmtyp, Basis) und für
///    Tonwerte der Rollen-Standard, damit Belichtungsunterschiede sichtbar bleiben (LF-KTA-04);
///  - mit Filmrand: der Zuschnitt wird auf das ungeschnittene Feld (Bildabstand ×
///    Filmbreite) erweitert (LF-KTA-03).
/// Danach Umrechnung ins Ausgabeprofil (Standard sRGB), das eingebettet wird.
class ContactSheetService
{
public:
    struct Item
    {
        layout::SheetFrame frame;
        model::Frame source;
        model::EditList edits;
    };

    /// Nach jedem Feld: dekodierte Quelle freigeben (wie beim Export, NF-PERF-07).
    using Release = std::function<void(const model::FrameId&)>;

    ContactSheetService(JobScheduler& jobs, IRenderer& renderer, ports::IColorEngine& color,
                        ports::IDocumentRenderer& document, Release release = {})
        : jobs_(jobs)
        , renderer_(renderer)
        , color_(color)
        , document_(document)
        , release_(std::move(release))
    {}

    [[nodiscard]] std::vector<layout::PageLayout> build(const layout::ContactSheetSpec& s,
                                                        std::span<const Item> items,
                                                        const layout::FilmFormat& fmt) const;
    /// Seiten mit gerenderten Bildern (für PDF und Druck; auch in Tests nutzbar).
    [[nodiscard]] base::Expected<std::vector<ports::RenderedPage>>
    render(const layout::ContactSheetSpec& s, std::span<const Item> items, const layout::FilmFormat& fmt,
           const model::Roll& roll, const image::CancelToken& cancel,
           const std::function<void(int done, int total)>& progress = {}) const;

    JobHandle exportPdf(std::vector<Item> items, std::shared_ptr<const model::Roll> roll,
                        layout::ContactSheetSpec s, layout::FilmFormat fmt, std::filesystem::path pdf);
    JobHandle print(std::vector<Item> items, std::shared_ptr<const model::Roll> roll,
                    layout::ContactSheetSpec s, layout::FilmFormat fmt, ports::PrintSettings ps);

    /// Bearbeitungsliste für einen Slot (Inhalt einheitlich/wie bearbeitet, Filmrand).
    [[nodiscard]] static model::EditList slotEdits(const Item& item, const layout::ContactSheetSpec& s,
                                                   const layout::FilmFormat& fmt, const model::Roll& roll);

private:
    JobScheduler& jobs_;
    IRenderer& renderer_;
    ports::IColorEngine& color_;
    ports::IDocumentRenderer& document_;
    Release release_;
};

} // namespace autoneg::application
