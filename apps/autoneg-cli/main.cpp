// Kompositionswurzel der Kommandozeile (LF-BAT-11): dieselbe Komposition und dieselben Dienste
// wie die Oberfläche, ohne Fenster. Ergebnisse sind deshalb identisch zum GUI-Export (K-17).
//
//   autoneg-cli info <ordner>
//   autoneg-cli detect <ordner>                      Bildfeld/Basis für Bilder ohne Zuschnitt
//   autoneg-cli export <ordner> -o <ziel> [Optionen]
//   autoneg-cli contact-sheet <ordner> -o <datei.pdf> [Optionen]
//   autoneg-cli --version | --help
//
// Gemeinsame Optionen: --config <ordner> --cache <ordner> (Benutzerverzeichnisse ersetzen)

#include "autoneg/app/composition.hpp"
#include "autoneg/application/presets.hpp"
#include "autoneg/steps/other_steps.hpp"
#include "autoneg/steps/step_ids.hpp"

#if AUTONEG_WITH_GUI
#include "autoneg/adapter/qtprint/qt_document_renderer.hpp"
#endif

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace autoneg;

constexpr int exitOk = 0;
constexpr int exitError = 1;
constexpr int exitUsage = 2;

void usage()
{
    std::cout
        << "autoneg-cli 1.0.0 – Filmnegative und Dias ohne Oberfläche konvertieren\n\n"
           "  autoneg-cli info <ordner>\n"
           "  autoneg-cli detect <ordner> [--film color|bw|slide]\n"
           "  autoneg-cli export <ordner> -o <ziel> [--format tiff|jpeg|png|jxl] [--bits 8|16]\n"
           "              [--quality 1-100] [--profile srgb|adobergb|prophoto] [--long-edge <px>]\n"
           "              [--gray] [--pattern <muster>] [--preset <exportvorlage>] [--only-kept]\n"
           "              [--overwrite]\n"
           "  autoneg-cli contact-sheet <ordner> -o <datei.pdf> [--preset <layoutvorlage>]\n"
           "              [--film-format <name>] [--uniform] [--no-rebate] [--white]\n"
           "              [--paper a4|a3|letter] [--landscape] [--note <text>]\n\n"
           "Optionen für alle: --config <ordner> --cache <ordner>\n"
           "  --film color|bw|slide  Filmtyp (Farbnegativ, S/W-Negativ, Dia) für Bilder ohne eigenen\n"
           "                         Filmtyp; detect trägt ihn in die Begleitdateien ein. Ohne Filmtyp\n"
           "                         bleibt ein Bild der unveränderte Scan.\n"
           "Die Bearbeitung kommt aus den Begleitdateien (*.autoneg.json) und dem Rollen-Standard.\n";
}

/// Führt Aufgaben aus Jobs im Hauptthread aus (er besitzt den CommandBus).
class MainThreadQueue
{
public:
    void post(std::function<void()> f)
    {
        {
            const std::scoped_lock lock(mtx_);
            tasks_.push_back(std::move(f));
        }
        cv_.notify_all();
    }
    /// Arbeitet ab, bis done() gilt.
    void runUntil(const std::function<bool()>& done)
    {
        while (true) {
            std::deque<std::function<void()>> now;
            {
                std::unique_lock lock(mtx_);
                cv_.wait_for(lock, std::chrono::milliseconds(20), [this] { return !tasks_.empty(); });
                now.swap(tasks_);
            }
            for (auto& f : now) {
                f();
            }
            if (now.empty() && done()) {
                return;
            }
        }
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
};

struct Args
{
    std::string command;
    fs::path folder;
    std::optional<model::FilmType> film;        ///< --film
    std::map<std::string, std::string> options; ///< --name wert
    std::vector<std::string> flags;             ///< --name ohne Wert
    [[nodiscard]] bool flag(std::string_view f) const { return std::ranges::find(flags, f) != flags.end(); }
    [[nodiscard]] std::optional<std::string> opt(const std::string& k) const
    {
        const auto it = options.find(k);
        return it != options.end() ? std::optional(it->second) : std::nullopt;
    }
};

const std::vector<std::string_view>& flagNames()
{
    static const std::vector<std::string_view> f{"--version",   "--help",      "--gray",
                                                 "--only-kept", "--overwrite", "--uniform",
                                                 "--no-rebate", "--white",     "--landscape"};
    return f;
}

std::optional<Args> parse(std::span<char*> argv)
{
    Args a;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string s = argv[i];
        if (s.starts_with("-")) {
            if (std::ranges::find(flagNames(), s) != flagNames().end()) {
                a.flags.push_back(s);
            } else if (i + 1 < argv.size()) {
                a.options[s == "-o" ? std::string("--out") : s] = argv[++i];
            } else {
                std::cerr << "Wert fehlt: " << s << "\n";
                return std::nullopt;
            }
        } else if (a.command.empty()) {
            a.command = s;
        } else if (a.folder.empty()) {
            a.folder = fs::path(reinterpret_cast<const char8_t*>(s.c_str())); // Argumente als UTF-8
        } else {
            std::cerr << "Unerwartetes Argument: " << s << "\n";
            return std::nullopt;
        }
    }
    return a;
}

const char* filmLabel(model::FilmType f)
{
    switch (f) {
    case model::FilmType::ColorNegative:
        return "Farbnegativ";
    case model::FilmType::BwNegative:
        return "S/W-Negativ";
    case model::FilmType::Slide:
        return "Dia";
    case model::FilmType::Unconverted:
        return "Filmtyp offen";
    }
    return "";
}

int fail(const std::string& what, const autoneg::base::Error& e)
{
    std::cerr << what << ": " << e.message << "\n";
    return exitError;
}

int wait(MainThreadQueue& q, const application::JobHandle& h, const std::string& what)
{
    int shown = -1;
    q.runUntil([&] {
        const int pct = static_cast<int>(h.progress() * 100.0);
        if (pct != shown && pct > 0) {
            std::cout << "\r" << what << " " << pct << " %" << std::flush;
            shown = pct;
        }
        return h.done();
    });
    if (shown >= 0) {
        std::cout << "\n";
    }
    if (const auto r = h.wait(); !r) {
        return fail(what, r.error());
    }
    return exitOk;
}

int info(app::Composition& c, const Args& a)
{
    auto roll = c.rolls().open(a.folder);
    if (!roll) {
        return fail("Ordner", roll.error());
    }
    if (a.film) {
        roll->roll.filmType = *a.film;
    }
    std::cout << "Rolle " << roll->roll.name << ": " << roll->frames.size() << " Bilder"
              << (roll->readOnly ? " (nur lesend, in einer anderen Instanz geöffnet)" : "") << "\n";
    for (const application::OpenedFrame& f : roll->frames) {
        std::cout << "  " << f.frame.frameNo << "\t" << f.frame.path.filename().string() << "\t"
                  << filmLabel(steps::FilmModeStep::effective(f.data.edits, roll->roll.filmType)) << "\t"
                  << f.data.edits.size() << " Änderungen"
                  << (f.frame.rating == model::Rating::Keep     ? "\tbehalten"
                      : f.frame.rating == model::Rating::Reject ? "\tverworfen"
                                                                : "")
                  << (f.problem ? "\tProblem: " + f.problem->message : "") << "\n";
    }
    return exitOk;
}

int detect(app::Composition& c, MainThreadQueue& q, const Args& a)
{
    auto roll = c.rolls().open(a.folder);
    if (!roll) {
        return fail("Ordner", roll.error());
    }
    if (a.film) {
        roll->roll.filmType = *a.film;
    }
    auto shared = std::make_shared<const model::Roll>(roll->roll);
    const model::StepId filmStep{std::string(steps::id::filmmode)};
    int rc = exitOk;
    for (const application::OpenedFrame& f : roll->frames) {
        c.bus().open(f.frame, f.data);
        const std::vector<model::FrameId> target{f.frame.id};
        if (a.film && f.data.edits.get(filmStep) == nullptr) { // Filmtyp in die Begleitdatei
            model::EditEntry e;
            e.step = filmStep;
            e.params = model::ParamSet{}.with("type", std::string(steps::FilmModeStep::toString(*a.film)));
            c.bus().execute(application::SetEntryCommand(e), target, *shared);
        }
        if (f.data.edits.get(model::StepId(std::string(steps::id::crop))) != nullptr) {
            continue; // Zuschnitt vorhanden: nichts überschreiben
        }
        const model::FilmType film =
            steps::FilmModeStep::effective(c.bus().history(f.frame.id).current(), shared->filmType);
        if (film == model::FilmType::Unconverted) {
            std::cerr << f.frame.path.filename().string() << ": Filmtyp fehlt (--film color|bw|slide)\n";
            rc = exitError;
            continue;
        }
        const int r =
            wait(q, c.analysis().detectFrame(f.frame, shared, film), f.frame.path.filename().string());
        rc = r != exitOk ? r : rc;
    }
    q.runUntil([] { return true; }); // letzte Ergebnisse eintragen
    c.queue().flush();
    c.rolls().close(a.folder);
    return rc;
}

application::ExportSettings exportSettings(app::Composition& c, const Args& a)
{
    application::ExportSettings s;
    if (const auto p = a.opt("--preset")) {
        if (const auto j = c.presets().load({ports::PresetKind::Export, *p})) {
            s = application::exportFromJson(*j);
        } else {
            std::cerr << "Exportvorlage " << *p << ": " << j.error().message << "\n";
        }
    }
    static const std::map<std::string, ports::FileFormat> formats{{"tiff", ports::FileFormat::Tiff},
                                                                  {"jpeg", ports::FileFormat::Jpeg},
                                                                  {"png", ports::FileFormat::Png},
                                                                  {"jxl", ports::FileFormat::JpegXl}};
    static const std::map<std::string, ports::ProfileId> profiles{{"srgb", ports::ProfileId::Srgb},
                                                                  {"adobergb", ports::ProfileId::AdobeRgb},
                                                                  {"prophoto", ports::ProfileId::ProPhoto}};
    if (const auto f = a.opt("--format"); f && formats.contains(*f)) {
        s.write.format = formats.at(*f);
        if (s.write.format == ports::FileFormat::Jpeg) {
            s.write.bitDepth = 8;
        }
    }
    if (const auto b = a.opt("--bits")) {
        s.write.bitDepth = std::stoi(*b);
    }
    if (const auto q = a.opt("--quality")) {
        s.write.quality = std::stoi(*q);
    }
    if (const auto p = a.opt("--profile"); p && profiles.contains(*p)) {
        s.profile = profiles.at(*p);
    }
    if (const auto l = a.opt("--long-edge")) {
        s.longEdge = std::stoi(*l);
    }
    if (const auto p = a.opt("--pattern")) {
        s.pattern = *p;
    }
    s.grayscale = s.grayscale || a.flag("--gray");
    s.overwrite =
        a.flag("--overwrite") ? application::OverwritePolicy::Overwrite : application::OverwritePolicy::Never;
    s.folder = fs::path(reinterpret_cast<const char8_t*>(a.opt("--out").value_or("").c_str()));
    return s;
}

int exportRoll(app::Composition& c, MainThreadQueue& q, const Args& a)
{
    if (!a.opt("--out")) {
        std::cerr << "Zielordner fehlt (-o)\n";
        return exitUsage;
    }
    auto roll = c.rolls().open(a.folder);
    if (!roll) {
        return fail("Ordner", roll.error());
    }
    if (a.film) {
        roll->roll.filmType = *a.film;
    }
    const application::ExportSettings s = exportSettings(c, a);
    std::error_code ec;
    fs::create_directories(s.folder, ec);
    std::vector<application::ExportService::Item> items;
    for (const application::OpenedFrame& f : roll->frames) {
        if (a.flag("--only-kept") && f.frame.rating != model::Rating::Keep) {
            continue;
        }
        items.push_back({f.frame, f.data.edits});
    }
    const std::size_t n = items.size();
    const int rc = wait(
        q, c.exports().exportFrames(std::move(items), std::make_shared<const model::Roll>(roll->roll), s),
        "Export");
    c.rolls().close(a.folder);
    if (rc == exitOk) {
        std::cout << n << " Bilder exportiert nach " << s.folder.string() << "\n";
    }
    return rc;
}

int contactSheet(app::Composition& c, MainThreadQueue& q, const Args& a)
{
    if (!a.opt("--out")) {
        std::cerr << "PDF-Datei fehlt (-o)\n";
        return exitUsage;
    }
    auto roll = c.rolls().open(a.folder);
    if (!roll) {
        return fail("Ordner", roll.error());
    }
    if (a.film) {
        roll->roll.filmType = *a.film;
    }
    application::SheetPreset p;
    if (const auto name = a.opt("--preset")) {
        if (const auto j = c.presets().load({ports::PresetKind::ContactSheet, *name})) {
            p = application::sheetFromJson(*j);
        } else {
            std::cerr << "Layoutvorlage " << *name << ": " << j.error().message << "\n";
        }
    }
    p.format = a.opt("--film-format").value_or(p.format);
    layout::ContactSheetSpec& s = p.spec;
    s.content = a.flag("--uniform") ? layout::SheetContent::Uniform : s.content;
    s.showRebate = a.flag("--no-rebate") ? false : s.showRebate;
    s.background = a.flag("--white") ? layout::SheetBackground::White : s.background;
    s.landscape = s.landscape || a.flag("--landscape");
    if (const auto paper = a.opt("--paper")) {
        s.paper = *paper == "a3"       ? layout::PaperSize::A3
                  : *paper == "letter" ? layout::PaperSize::Letter
                                       : layout::PaperSize::A4;
    }
    s.header.rollNo = roll->roll.name;
    s.header.film = roll->roll.film;
    s.header.notes = a.opt("--note").value_or("");
    const layout::FilmFormat* fmt = layout::FilmFormat::find(p.format);
    if (fmt == nullptr) {
        std::cerr << "Filmformat unbekannt: " << p.format << "\n";
        return exitUsage;
    }
    std::vector<application::ContactSheetService::Item> items;
    for (const application::OpenedFrame& f : roll->frames) {
        double aspect = 1.5;
        if (const auto i = c.sourceInfo(f.frame); i && i->height > 0) {
            aspect = static_cast<double>(i->width) / i->height;
        }
        items.push_back({{f.frame.id, f.frame.frameNo, aspect}, f.frame, f.data.edits});
    }
    const fs::path pdf(reinterpret_cast<const char8_t*>(a.opt("--out")->c_str()));
    const int rc = wait(q,
                        c.contactSheet().exportPdf(
                            std::move(items), std::make_shared<const model::Roll>(roll->roll), s, *fmt, pdf),
                        "Kontaktabzug");
    c.rolls().close(a.folder);
    if (rc == exitOk) {
        std::cout << "Kontaktabzug geschrieben: " << pdf.string() << "\n";
    }
    return rc;
}

} // namespace

int main(int argc, char** argv)
{
    const std::span args(argv, static_cast<std::size_t>(argc));
    auto a = parse(args);
    if (!a) {
        return exitUsage;
    }
    if (a->command == "--version" || a->flag("--version")) {
        std::cout << "autoneg-cli 1.0.0\n";
        return exitOk;
    }
    if (a->flag("--help") || a->command == "help") {
        usage();
        return exitOk;
    }
    if (a->command.empty()) {
        usage();
        return exitUsage;
    }
    if (a->folder.empty()) {
        std::cerr << "Scan-Ordner fehlt\n";
        usage();
        return exitUsage;
    }
    if (const auto f = a->opt("--film")) {
        if (*f != "color" && *f != "bw" && *f != "slide") {
            std::cerr << "Unbekannter Filmtyp: " << *f << " (color, bw oder slide)\n";
            return exitUsage;
        }
        a->film = steps::FilmModeStep::fromString(*f);
    }
    app::Directories dirs = app::userDirectories();
    if (const auto v = a->opt("--config")) {
        dirs.config = fs::path(reinterpret_cast<const char8_t*>(v->c_str()));
    }
    if (const auto v = a->opt("--cache")) {
        dirs.cache = fs::path(reinterpret_cast<const char8_t*>(v->c_str()));
    }
#if AUTONEG_WITH_GUI
    adapter::qtprint::QtGuiSession qt; // Schriften und QPdfWriter (offscreen unter Linux ohne Anzeige)
    adapter::qtprint::QtDocumentRenderer document;
#else
    application::NoDocumentRenderer document;
#endif
    MainThreadQueue queue;
    app::Composition c(dirs, {}, [&queue](std::function<void()> f) { queue.post(std::move(f)); }, document);
    if (a->command == "info") {
        return info(c, *a);
    }
    if (a->command == "detect") {
        return detect(c, queue, *a);
    }
    if (a->command == "export") {
        return exportRoll(c, queue, *a);
    }
    if (a->command == "contact-sheet") {
        return contactSheet(c, queue, *a);
    }
    std::cerr << "Unbekannter Befehl: " << a->command << "\n";
    usage();
    return exitUsage;
}
