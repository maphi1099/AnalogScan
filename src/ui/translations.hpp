#pragma once

#include <QtCore/QTranslator>

#include <string>

namespace autoneg::ui {

/// Englische Oberfläche (LF-UI-03) aus einer festen Tabelle Deutsch → Englisch, ohne
/// .ts/.qm-Dateien (kein Qt-Linguist-Werkzeug im Bau). Unbekannte Texte bleiben deutsch.
class TableTranslator final : public QTranslator
{
    Q_OBJECT

public:
    using QTranslator::QTranslator;
    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] QString translate(const char* context, const char* source,
                                    const char* disambiguation = nullptr, int n = -1) const override;
};

/// Text aus dem Kern (Schrittname, Herkunft) in der eingestellten Sprache.
[[nodiscard]] QString translated(const std::string& text);

} // namespace autoneg::ui
