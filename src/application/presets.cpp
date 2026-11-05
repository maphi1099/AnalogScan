#include "application/presets.hpp"

#include <algorithm>

namespace autoneg::application {

namespace {

int intOr(const model::JsonValue& j, std::string_view key, int fallback)
{
    const model::JsonValue* v = j.find(key);
    const auto n = v != nullptr ? v->number() : std::nullopt;
    return n ? static_cast<int>(*n) : fallback;
}

double doubleOr(const model::JsonValue& j, std::string_view key, double fallback)
{
    const model::JsonValue* v = j.find(key);
    const auto n = v != nullptr ? v->number() : std::nullopt;
    return n ? *n : fallback;
}

/// Aufzählungswert aus der Datei; außerhalb [0, last] der Standard.
template <class E>
E enumOr(const model::JsonValue& j, std::string_view key, E fallback, E last)
{
    const int v = intOr(j, key, static_cast<int>(fallback));
    return v >= 0 && v <= static_cast<int>(last) ? static_cast<E>(v) : fallback;
}

bool boolOr(const model::JsonValue& j, std::string_view key, bool fallback)
{
    const model::JsonValue* v = j.find(key);
    return v != nullptr && v->asBool() != nullptr ? *v->asBool() : fallback;
}

std::string stringOr(const model::JsonValue& j, std::string_view key, std::string fallback)
{
    const model::JsonValue* v = j.find(key);
    return v != nullptr && v->asString() != nullptr ? *v->asString() : fallback;
}

} // namespace

model::JsonValue exportToJson(const ExportSettings& s)
{
    model::JsonValue j;
    j.set("format", static_cast<int>(s.write.format));
    j.set("bitDepth", s.write.bitDepth);
    j.set("quality", s.write.quality);
    j.set("profile", static_cast<int>(s.profile));
    j.set("longEdge", s.longEdge);
    j.set("grayscale", s.grayscale);
    j.set("pattern", s.pattern);
    j.set("folder", s.folder.generic_u8string().empty()
                        ? std::string()
                        : std::string(reinterpret_cast<const char*>(s.folder.generic_u8string().c_str())));
    return j;
}

ExportSettings exportFromJson(const model::JsonValue& j)
{
    ExportSettings s;
    // Grenzen wie im Exportdialog: die Datei kann von Hand geändert sein
    s.write.format = enumOr(j, "format", s.write.format, ports::FileFormat::LinearDng);
    if (const int bits = intOr(j, "bitDepth", s.write.bitDepth); bits == 8 || bits == 16 || bits == 32) {
        s.write.bitDepth = bits;
    }
    s.write.quality = std::clamp(intOr(j, "quality", s.write.quality), 1, 100);
    s.profile = enumOr(j, "profile", s.profile, ports::ProfileId::GrayGamma22);
    s.longEdge = std::clamp(intOr(j, "longEdge", 0), 0, 20000);
    s.grayscale = boolOr(j, "grayscale", false);
    s.pattern = stringOr(j, "pattern", s.pattern);
    const std::string folder = stringOr(j, "folder", "");
    s.folder = std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    return s;
}

model::JsonValue sheetToJson(const SheetPreset& p)
{
    const layout::ContactSheetSpec& s = p.spec;
    model::JsonValue j;
    j.set("format", p.format);
    j.set("paper", static_cast<int>(s.paper));
    j.set("landscape", s.landscape);
    j.set("bindingMm", s.bindingMm);
    j.set("punchMarks", s.punchMarks);
    j.set("framesPerStrip", s.framesPerStrip);
    j.set("showRebate", s.showRebate);
    j.set("content", static_cast<int>(s.content));
    j.set("background", static_cast<int>(s.background));
    j.set("scale", static_cast<int>(s.scale));
    j.set("dpi", s.dpi);
    j.set("headerPt", s.headerPt);
    j.set("numberPt", s.numberPt);
    return j;
}

SheetPreset sheetFromJson(const model::JsonValue& j)
{
    SheetPreset p;
    layout::ContactSheetSpec& s = p.spec;
    p.format = stringOr(j, "format", p.format);
    s.paper = enumOr(j, "paper", s.paper, layout::PaperSize::Letter);
    s.landscape = boolOr(j, "landscape", s.landscape);
    s.bindingMm = std::clamp(doubleOr(j, "bindingMm", s.bindingMm), 0.0, 50.0);
    s.punchMarks = boolOr(j, "punchMarks", s.punchMarks);
    s.framesPerStrip = std::clamp(intOr(j, "framesPerStrip", s.framesPerStrip), 0, 20);
    s.showRebate = boolOr(j, "showRebate", s.showRebate);
    s.content = enumOr(j, "content", s.content, layout::SheetContent::Uniform);
    s.background = enumOr(j, "background", s.background, layout::SheetBackground::White);
    s.scale = enumOr(j, "scale", s.scale, layout::SheetScale::OneToOne);
    s.dpi = std::clamp(intOr(j, "dpi", s.dpi), 72, 1200);
    s.headerPt = std::clamp(doubleOr(j, "headerPt", s.headerPt), 7.0, 72.0);
    s.numberPt = std::clamp(doubleOr(j, "numberPt", s.numberPt), 7.0, 72.0);
    return p;
}

} // namespace autoneg::application
