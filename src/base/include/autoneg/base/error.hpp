#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace autoneg::base {

/// Fehlerart. Bestimmt, wie die Anwendung reagiert (Meldung, Rückfall, Abbruch).
enum class ErrorCode
{
    InvalidArgument, ///< Parameter außerhalb des erlaubten Bereichs
    OutOfRange,      ///< Zugriff außerhalb von Bild, Liste o. ä.
    NotFound,        ///< Datei, Schlüssel oder Eintrag fehlt
    TypeMismatch,    ///< Wert hat einen anderen Typ als angefragt
    Unsupported,     ///< Format, Kamera oder Funktion wird nicht unterstützt
    Corrupt,         ///< Datei oder Daten beschädigt
    Io,              ///< Lese- oder Schreibfehler im Dateisystem
    ReadOnly,        ///< Ziel nicht beschreibbar
    Cancelled,       ///< kooperativ abgebrochen (CancelToken)
    Internal         ///< Programmfehler
};

/// Fehler als Rückgabewert (AE-14). Wird nie als Ausnahme über Modulgrenzen geworfen.
struct Error
{
    ErrorCode code = ErrorCode::Internal;
    std::string message; ///< für Protokoll und Meldung an den Nutzer (Deutsch)

    friend bool operator==(const Error&, const Error&) = default;
};

/// Ergebnis oder Fehler; entspricht expected<T> in den Klassendiagrammen der Architektur.
template <class T>
using Expected = std::expected<T, Error>;

/// Erzeugt den Fehlerzweig eines Expected.
[[nodiscard]] inline std::unexpected<Error> makeError(ErrorCode code, std::string message)
{
    return std::unexpected<Error>(Error{code, std::move(message)});
}

/// Kurzname der Fehlerart, z. B. für das Protokoll.
[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

} // namespace autoneg::base
