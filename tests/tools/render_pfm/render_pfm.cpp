// Werkzeug zur Sichtprüfung (D-14): rechnet ein lineares PFM (Rec. 2020) mit den
// Standardschritten und dem CpuBackend und schreibt das Ergebnis wieder als PFM.
// Dekodieren der RAW-Dateien und Umwandeln in PNG übernehmen bis M4 die Python-Skripte in
// tools/visual_check. Kein Teil der Anwendung.
//
//   autoneg_render_pfm <ein.pfm> <aus.pfm> <film> [<schritt>.<parameter>=<wert> ...]
//
// film: color | bw | slide. Werte: Zahl, true/false, Text oder Komma-Liste (Vec3, Rechteck).
// Beispiel: crop.rect=0.1,0.1,0.8,0.8 filmbase.base=0.7,0.4,0.3 gradation.grade=3

#include "autoneg/backend/cpu/cpu_backend.hpp"
#include "autoneg/pipeline/pipeline_compiler.hpp"
#include "autoneg/pipeline/pipeline_executor.hpp"
#include "autoneg/steps/other_steps.hpp"
#include "autoneg/steps/registry.hpp"
#include "autoneg/steps/step_ids.hpp"

#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace autoneg;

/// PFM (Portable Float Map), drei Kanäle; Zeilen von unten nach oben.
std::optional<image::ImageBuffer> readPfm(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int w = 0;
    int h = 0;
    double scale = 0.0;
    in >> magic >> w >> h >> scale;
    in.get();
    if (!in || magic != "PF" || w <= 0 || h <= 0) {
        return std::nullopt;
    }
    const bool little = scale < 0.0;
    const std::size_t row = static_cast<std::size_t>(w) * 3;
    std::vector<float> v(row * static_cast<std::size_t>(h));
    for (int y = h - 1; y >= 0; --y) {
        in.read(reinterpret_cast<char*>(v.data() + static_cast<std::size_t>(y) * row),
                static_cast<std::streamsize>(row * sizeof(float)));
    }
    if (!in) {
        return std::nullopt;
    }
    if (little != (std::endian::native == std::endian::little)) {
        for (float& f : v) {
            f = std::bit_cast<float>(std::byteswap(std::bit_cast<std::uint32_t>(f)));
        }
    }
    auto img = image::ImageBuffer::fromFloats(
        image::ImageSpec{w, h, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, v);
    return img ? std::optional(*img) : std::nullopt;
}

bool writePfm(const std::string& path, const image::ImageBuffer& img)
{
    const auto& s = img.spec();
    std::ofstream out(path, std::ios::binary);
    out << "PF\n"
        << s.width << " " << s.height << "\n"
        << (std::endian::native == std::endian::little ? "-1" : "1") << "\n";
    const auto v = img.host();
    const std::size_t row = static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.channels);
    std::vector<float> rgb(static_cast<std::size_t>(s.width) * 3);
    for (int y = s.height - 1; y >= 0; --y) {
        for (int x = 0; x < s.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                const int k = s.channels == 1 ? 0 : c;
                rgb[static_cast<std::size_t>(x) * 3 + static_cast<std::size_t>(c)] =
                    v[static_cast<std::size_t>(y) * row + static_cast<std::size_t>(x * s.channels + k)];
            }
        }
        out.write(reinterpret_cast<const char*>(rgb.data()),
                  static_cast<std::streamsize>(rgb.size() * sizeof(float)));
    }
    return static_cast<bool>(out);
}

std::vector<double> numbers(const std::string& text)
{
    std::vector<double> v;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ',')) {
        v.push_back(std::stod(part));
    }
    return v;
}

std::optional<model::ParamValue> parseValue(pipeline::ParamType t, const std::string& text)
{
    switch (t) {
    case pipeline::ParamType::Double:
        return std::stod(text);
    case pipeline::ParamType::Int:
        return std::stoi(text);
    case pipeline::ParamType::Bool:
        return text == "true" || text == "1";
    case pipeline::ParamType::String:
        return text;
    case pipeline::ParamType::Vec3: {
        const auto n = numbers(text);
        return n.size() == 3 ? std::optional<model::ParamValue>(base::Vec3{n[0], n[1], n[2]}) : std::nullopt;
    }
    case pipeline::ParamType::Rect: {
        const auto n = numbers(text);
        return n.size() == 4 ? std::optional<model::ParamValue>(model::NormRect{n[0], n[1], n[2], n[3]})
                             : std::nullopt;
    }
    case pipeline::ParamType::Curve: { // x0,y0,x1,y1,...
        const auto n = numbers(text);
        model::Curve c;
        for (std::size_t i = 0; i + 1 < n.size(); i += 2) {
            c.points.push_back({n[i], n[i + 1]});
        }
        return c;
    }
    case pipeline::ParamType::Spots:
        break;
    }
    return std::nullopt;
}

class NoReader final : public ports::IImageReader
{
public:
    base::Expected<model::SourceInfo> probe(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein Bild");
    }
    base::Expected<image::ImageBuffer> read(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein Bild");
    }
    base::Expected<ports::IccProfile> profile(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein Bild");
    }
};

class NoDecoder final : public ports::IRawDecoder
{
public:
    base::Expected<model::SourceInfo> probe(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein RAW");
    }
    base::Expected<image::ImageBuffer> decode(const std::filesystem::path& /*p*/,
                                              const ports::DecodeOptions& /*o*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein RAW");
    }
    base::Expected<image::ImageBuffer> preview(const std::filesystem::path& /*p*/) override
    {
        return base::makeError(base::ErrorCode::Unsupported, "kein RAW");
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr
            << "Aufruf: autoneg_render_pfm <ein.pfm> <aus.pfm> <color|bw|slide> [schritt.param=wert ...]\n";
        return 2;
    }
    const auto src = readPfm(argv[1]);
    if (!src) {
        std::cerr << "PFM nicht lesbar: " << argv[1] << "\n";
        return 1;
    }
    const std::string filmName = argv[3];
    const model::FilmType film = filmName == "bw"      ? model::FilmType::BwNegative
                                 : filmName == "slide" ? model::FilmType::Slide
                                                       : model::FilmType::ColorNegative;

    steps::FlatFieldCache flatFields;
    pipeline::StepRegistry registry;
    if (!steps::registerStandardSteps(registry, flatFields)) {
        return 1;
    }
    // Parameter je Schritt sammeln
    std::map<std::string, model::ParamSet> params;
    params["filmmode"] = model::ParamSet{}.with("type", std::string(steps::FilmModeStep::toString(film)));
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto dot = arg.find('.');
        const auto eq = arg.find('=');
        if (dot == std::string::npos || eq == std::string::npos || eq < dot) {
            std::cerr << "Argument ungültig: " << arg << "\n";
            return 2;
        }
        const std::string step = arg.substr(0, dot);
        const std::string name = arg.substr(dot + 1, eq - dot - 1);
        const pipeline::IStep* s = registry.find(steps::stepId("autoneg." + step));
        if (s == nullptr) {
            std::cerr << "Schritt unbekannt: " << step << "\n";
            return 2;
        }
        const auto& specs = s->descriptor().params;
        const auto spec = std::ranges::find(specs, name, &pipeline::ParamSpec::name);
        const auto value = spec != specs.end() ? parseValue(spec->type, arg.substr(eq + 1)) : std::nullopt;
        if (!value) {
            std::cerr << "Parameter ungültig: " << arg << "\n";
            return 2;
        }
        params[step] = params[step].with(name, *value);
    }
    model::EditList list;
    for (const auto& [step, p] : params) {
        const pipeline::IStep* s = registry.find(steps::stepId("autoneg." + step));
        model::EditEntry e;
        e.step = s->descriptor().id;
        e.rank = s->descriptor().rank;
        e.version = s->descriptor().version;
        e.params = p;
        list = list.with(e);
    }

    pipeline::PipelineCompiler compiler(registry);
    backend::cpu::CpuBackend backend;
    pipeline::StageCache cache(std::size_t{1} << 28);
    NoDecoder decoder;
    NoReader reader;
    pipeline::SourceProvider sources(decoder, reader);
    pipeline::PipelineExecutor executor(compiler, backend, cache, sources);

    pipeline::RenderProfile profile;
    profile.mode = pipeline::RenderMode::Export;
    const auto t0 = std::chrono::steady_clock::now();
    const pipeline::CompiledPipeline cp = compiler.compile(list, film, profile, src->spec());
    pipeline::RenderRequest r;
    r.list = std::make_shared<const model::EditList>(list);
    r.film = film;
    r.profile = profile;
    const auto out = executor.run(cp, *src, r);
    if (!out) {
        std::cerr << "Fehler: " << out.error().message << "\n";
        return 1;
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    std::cout << src->spec().width << "x" << src->spec().height << " -> " << out->spec().width << "x"
              << out->spec().height << ", " << cp.stages.size() << " Stufen, " << ms.count() << " ms\n";
    return writePfm(argv[2], *out) ? 0 : 1;
}
