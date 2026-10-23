#include "autoneg/ui/presets.hpp"

namespace autoneg::ui {

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

model::JsonValue exportToJson(const application::ExportSettings& s)
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

application::ExportSettings exportFromJson(const model::JsonValue& j)
{
    application::ExportSettings s;
    s.write.format = static_cast<ports::FileFormat>(intOr(j, "format", static_cast<int>(s.write.format)));
    s.write.bitDepth = intOr(j, "bitDepth", s.write.bitDepth);
    s.write.quality = intOr(j, "quality", s.write.quality);
    s.profile = static_cast<ports::ProfileId>(intOr(j, "profile", static_cast<int>(s.profile)));
    s.longEdge = intOr(j, "longEdge", 0);
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
    s.paper = static_cast<layout::PaperSize>(intOr(j, "paper", static_cast<int>(s.paper)));
    s.landscape = boolOr(j, "landscape", s.landscape);
    s.bindingMm = doubleOr(j, "bindingMm", s.bindingMm);
    s.punchMarks = boolOr(j, "punchMarks", s.punchMarks);
    s.framesPerStrip = intOr(j, "framesPerStrip", s.framesPerStrip);
    s.showRebate = boolOr(j, "showRebate", s.showRebate);
    s.content = static_cast<layout::SheetContent>(intOr(j, "content", static_cast<int>(s.content)));
    s.background =
        static_cast<layout::SheetBackground>(intOr(j, "background", static_cast<int>(s.background)));
    s.scale = static_cast<layout::SheetScale>(intOr(j, "scale", static_cast<int>(s.scale)));
    s.dpi = intOr(j, "dpi", s.dpi);
    s.headerPt = doubleOr(j, "headerPt", s.headerPt);
    s.numberPt = doubleOr(j, "numberPt", s.numberPt);
    return p;
}

} // namespace autoneg::ui
