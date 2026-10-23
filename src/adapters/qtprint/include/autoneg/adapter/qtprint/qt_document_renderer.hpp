#pragma once

#include "autoneg/ports/document_renderer.hpp"

#include <memory>

namespace autoneg::adapter::qtprint {

/// Sorgt für eine Qt-Anwendungsinstanz, die QPdfWriter und Schriften brauchen.
/// Gibt es schon eine (GUI-Programm), tut sie nichts; sonst erzeugt sie eine QGuiApplication
/// (Kommandozeile, Komponententests; Plattform über QT_QPA_PLATFORM, z. B. offscreen).
/// Nur im Hauptthread anlegen, in der Kompositionswurzel.
class QtGuiSession
{
public:
    QtGuiSession();
    ~QtGuiSession();
    QtGuiSession(const QtGuiSession&) = delete;
    QtGuiSession& operator=(const QtGuiSession&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// IDocumentRenderer über Qt (Architektur 9.11, LF-KTA-06): PDF mit QPdfWriter in der
/// Auflösung der Seiten, Seitengröße in mm, Ausgabeprofil als ICC eingebettet (Output Intent
/// und Farbraum der Bilder); Druck über QPrinter, mit Systemdruckdialog im Thread der Anwendung.
/// writePdf ist aus jedem Thread nutzbar, sobald eine Qt-Anwendungsinstanz besteht.
class QtDocumentRenderer final : public ports::IDocumentRenderer
{
public:
    [[nodiscard]] base::Expected<void> writePdf(std::span<const ports::RenderedPage> pages,
                                                const std::filesystem::path& p) override;
    [[nodiscard]] base::Expected<void> print(std::span<const ports::RenderedPage> pages,
                                             const ports::PrintSettings& s) override;
};

} // namespace autoneg::adapter::qtprint
