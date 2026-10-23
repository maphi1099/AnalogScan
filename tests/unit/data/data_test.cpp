// Tests der Datenhaltung (Architektur 8, 9.11): JsonSidecarStore, JsonConfigStore,
// AtomicFileWriter, SchemaMigrator. Arbeiten in einem eigenen temporären Ordner (T-03);
// die Prüfsumme liefert ein zählender Fake.

#include "autoneg/adapter/json/atomic_file_writer.hpp"
#include "autoneg/adapter/json/json_codec.hpp"
#include "autoneg/adapter/json/json_config_store.hpp"
#include "autoneg/adapter/json/json_sidecar_store.hpp"
#include "autoneg/adapter/json/schema_migrator.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

namespace autoneg::adapter::json {
namespace {

namespace fs = std::filesystem;
using model::JsonValue;
using model::StepId;

/// Prüfsumme aus dem Dateiinhalt (erste 16 Byte, aufgefüllt); zählt die Aufrufe.
class CountingHasher final : public ports::IContentHasher
{
public:
    base::Expected<model::FrameId> hashFile(const fs::path& p) override
    {
        ++calls;
        std::ifstream in(p, std::ios::binary);
        if (!in) {
            return base::makeError(base::ErrorCode::NotFound, "fehlt");
        }
        std::array<std::uint8_t, 16> h{};
        char c = 0;
        std::size_t i = 0;
        while (in.get(c)) {
            h[i % 16] = static_cast<std::uint8_t>(h[i % 16] * 31 + static_cast<std::uint8_t>(c));
            ++i;
        }
        return model::FrameId(h);
    }
    std::atomic<int> calls{0};
};

ports::StepCatalog catalog()
{
    using K = ports::ParamKind;
    return {
        {"autoneg.filmmode", 40, 1, {{"type", K::String}}},
        {"autoneg.crop", 30, 1, {{"rect", K::Rect}, {"aspect", K::String}, {"inset", K::Double}}},
        {"autoneg.retouch", 35, 1, {{"spots", K::Spots}}},
        {"autoneg.filmbase", 50, 1, {{"base", K::Vec3}}},
        {"autoneg.exposure", 70, 1, {{"ev", K::Double}}},
        {"autoneg.colorgrade", 90, 1, {{"level", K::Int}}},
        {"autoneg.gradation", 85, 1, {{"grade", K::Double}, {"compensate", K::Bool}}},
        {"autoneg.tonecurve", 110, 1, {{"master", K::Curve}}},
    };
}

model::EditEntry entry(const std::string& id, int rank, model::ParamSet p,
                       model::Origin o = model::Origin::Manual)
{
    model::EditEntry e;
    e.step = StepId(id);
    e.rank = rank;
    e.params = std::move(p);
    e.origin = o;
    return e;
}

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

void writeFile(const fs::path& p, std::string_view text)
{
    std::ofstream out(p, std::ios::binary);
    out << text;
}

std::pair<std::uint64_t, std::int64_t> stampOf(const fs::path& p)
{
    const auto t = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(p));
    return {fs::file_size(p), std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count()};
}

class DataStore : public ::testing::Test
{
protected:
    DataStore()
    {
        scan = dir.path() / "DSC01234.ARW";
        writeFile(scan, "RAW-Inhalt des Scans");
        frame.path = scan;
        frame.frameNo = 12;
    }

    JsonSidecarStore store(JsonSidecarStore::WritableProbe probe = {})
    {
        return JsonSidecarStore(catalog(), writer, migrator, hasher, dir.path() / "benutzer" / "sidecars",
                                std::move(probe));
    }

    ports::SidecarData dataFor(const fs::path& p)
    {
        ports::SidecarData d;
        const auto [size, mtime] = stampOf(p);
        d.source = {p.filename().string(), size, mtime, hasher.hashFile(p).value()};
        hasher.calls = 0;
        d.frameNo = 12;
        return d;
    }

    autoneg::testing::TempDir dir;
    fs::path scan;
    model::Frame frame;
    AtomicFileWriter writer;
    SchemaMigrator migrator;
    CountingHasher hasher;
};

// LF-DAT-01, LF-DAT-02
TEST_F(DataStore, U_DAT_01_SidecarRoundTrip)
{
    ports::SidecarData d = dataFor(scan);
    d.rating = model::Rating::Keep;
    model::EditEntry copied =
        entry("autoneg.exposure", 70, model::ParamSet{}.with("ev", 0.5), model::Origin::Copied);
    copied.copiedFrom = model::FrameId::fromHex("00112233445566778899aabbccddeeff").value();
    model::EditEntry off = entry("autoneg.colorgrade", 90, model::ParamSet{}.with("level", 2));
    off.enabled = false;
    d.edits =
        d.edits
            .with(entry("autoneg.filmmode", 40, model::ParamSet{}.with("type", std::string("bw")),
                        model::Origin::RollDefault))
            .with(entry(
                "autoneg.crop", 30,
                model::ParamSet{}.with("rect", model::NormRect{0.04, 0.06, 0.92, 0.88}).with("inset", 0.02),
                model::Origin::Automatic))
            .with(
                entry("autoneg.retouch", 35,
                      model::ParamSet{}.with("spots", model::SpotList{{{0.5, 0.25}, {0.01, -0.02}, 0.004}})))
            .with(entry("autoneg.filmbase", 50, model::ParamSet{}.with("base", base::Vec3{0.71, 0.43, 0.28})))
            .with(copied)
            .with(off)
            .with(
                entry("autoneg.gradation", 85, model::ParamSet{}.with("grade", 3.5).with("compensate", true)))
            .with(entry("autoneg.tonecurve", 110,
                        model::ParamSet{}.with("master", model::Curve{{{0, 0}, {0.5, 0.6}, {1, 1}}})));
    auto s = store();
    ASSERT_TRUE(s.save(frame, d).has_value());
    const auto loaded = s.load(frame);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_TRUE(loaded->has_value());
    EXPECT_EQ(**loaded, d);
    EXPECT_EQ((*loaded)->edits.hash(), d.edits.hash());
}

// NF-ARC-09
TEST_F(DataStore, U_DAT_02_UnknownFieldsAndStepsArePreserved)
{
    const auto [size, mtime] = stampOf(scan);
    const std::string hash = hasher.hashFile(scan).value().toHex();
    const std::string text = R"({ "schema": 1, "app": "AutoNeg 1.0.0",
        "source": { "file": "DSC01234.ARW", "size": )" +
                             std::to_string(size) + R"(, "mtime": )" + std::to_string(mtime) +
                             R"(, "hash": ")" + hash + R"(" },
        "frameNo": 12, "rating": "none",
        "zukunft": { "a": [1, 2, 3], "b": "x" },
        "edits": [
          { "step": "autoneg.exposure", "v": 1, "on": true, "origin": "manual", "p": { "ev": 0.25 } },
          { "step": "autoneg.zukunft", "v": 3, "on": true, "origin": "manual", "p": { "staerke": 0.7, "art": [1, 2] } }
        ] })";
    writeFile(JsonSidecarStore::sidecarPath(frame), text);
    auto s = store();
    auto d = s.load(frame);
    ASSERT_TRUE(d.has_value()) << d.error().message;
    ASSERT_TRUE(d->has_value());
    ports::SidecarData changed = **d;
    changed.edits = changed.edits.with(entry("autoneg.colorgrade", 90, model::ParamSet{}.with("level", 3)));
    ASSERT_TRUE(s.save(frame, changed).has_value());
    const auto j = parse(readFile(JsonSidecarStore::sidecarPath(frame))).value();
    ASSERT_NE(j.find("zukunft"), nullptr);
    EXPECT_EQ(*j.find("zukunft"), parse(R"({ "a": [1, 2, 3], "b": "x" })").value());
    const auto& edits = *j.find("edits")->asArray();
    const auto future = std::ranges::find_if(edits, [](const JsonValue& e) {
        return e.find("step") != nullptr && *e.find("step")->asString() == "autoneg.zukunft";
    });
    ASSERT_NE(future, edits.end());
    EXPECT_EQ(*future, parse(R"({ "step": "autoneg.zukunft", "v": 3, "on": true, "origin": "manual",
                                   "p": { "staerke": 0.7, "art": [1, 2] } })")
                           .value());
    EXPECT_EQ(edits.size(), 3U);
}

// NF-ARC-09 (Mechanismus; ältere Schemaversionen gibt es noch nicht, T-15)
TEST_F(DataStore, U_DAT_03_OldSchemaIsMigrated)
{
    const SchemaMigrator v2({{1,
                              [](JsonValue j) -> base::Expected<JsonValue> {
                                  // Version 2 (nur für den Test): Feld "bewertung" statt "rating"
                                  if (const JsonValue* r = j.find("rating")) {
                                      j.set("bewertung", *r);
                                  }
                                  return j;
                              }},
                             {0,
                              [](JsonValue j) -> base::Expected<JsonValue> {
                                  // Version 0 (ohne Feld schema): "frame" hieß früher so
                                  if (const JsonValue* f = j.find("frame")) {
                                      j.set("frameNo", *f);
                                  }
                                  return j;
                              }}},
                            2);
    const auto j = v2.migrate(parse(R"({ "frame": 7, "rating": "keep" })").value());
    ASSERT_TRUE(j.has_value());
    EXPECT_EQ(SchemaMigrator::schemaOf(*j), 2);
    EXPECT_EQ(*j->find("frameNo"), JsonValue(std::int64_t{7}));
    EXPECT_EQ(*j->find("bewertung"), JsonValue("keep"));
    // fehlender Schritt
    const auto bad = SchemaMigrator({}, 2).migrate(parse(R"({ "schema": 1 })").value());
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, base::ErrorCode::Unsupported);
    // geschrieben wird immer im aktuellen Schema
    ASSERT_TRUE(store().save(frame, dataFor(scan)).has_value());
    EXPECT_EQ(SchemaMigrator::schemaOf(parse(readFile(JsonSidecarStore::sidecarPath(frame))).value()),
              SchemaMigrator::currentSchema);
}

// NF-ARC-09
TEST_F(DataStore, U_DAT_04_NewerSchemaIsReadOnly)
{
    auto s = store();
    ASSERT_TRUE(s.save(frame, dataFor(scan)).has_value());
    const fs::path p = JsonSidecarStore::sidecarPath(frame);
    auto j = parse(readFile(p)).value();
    j.set("schema", JsonValue(std::int64_t{SchemaMigrator::currentSchema + 1}));
    writeFile(p, dump(j));
    const std::string before = readFile(p);
    auto fresh = store();
    const auto d = fresh.load(frame);
    ASSERT_TRUE(d.has_value()) << d.error().message;
    ASSERT_TRUE(d->has_value());
    const auto saved = fresh.save(frame, **d);
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error().code, base::ErrorCode::ReadOnly);
    EXPECT_EQ(readFile(p), before);
}

// NF-REL-02, LF-DAT-04
TEST_F(DataStore, U_DAT_05_DefectiveFiles)
{
    const fs::path p = JsonSidecarStore::sidecarPath(frame);
    for (const std::string& text : {std::string(R"({ "schema": 1, "edits": [ { "step": )"),
                                    std::string(R"({ "schema": 1, "edits": "keine Liste" })")}) {
        writeFile(p, text);
        auto s = store();
        const auto d = s.load(frame);
        ASSERT_FALSE(d.has_value());
        EXPECT_EQ(d.error().code, base::ErrorCode::Corrupt);
        EXPECT_FALSE(fs::exists(p));
        fs::path bad = p;
        bad += ".defekt";
        EXPECT_TRUE(fs::exists(bad));
        fs::remove(bad);
    }
    // settings.json: abgeschnitten → Standardwerte und Meldung
    const fs::path root = dir.path() / "benutzer";
    fs::create_directories(root);
    writeFile(root / "settings.json", R"({ "schema": 1, "values": { "threads": )");
    JsonConfigStore config(root, writer, {{"threads", JsonValue(std::int64_t{0})}});
    EXPECT_FALSE(config.loadMessage().empty());
    EXPECT_EQ(config.get("threads"), JsonValue(std::int64_t{0}));
    EXPECT_TRUE(fs::exists(root / "settings.json.defekt"));
    // falscher Datentyp → Standardwert
    writeFile(root / "settings.json", R"({ "schema": 1, "values": { "threads": "viele" } })");
    JsonConfigStore typed(root, writer, {{"threads", JsonValue(std::int64_t{0})}});
    EXPECT_EQ(typed.get("threads"), JsonValue(std::int64_t{0}));
    EXPECT_FALSE(typed.loadMessage().empty());
}

// LF-DAT-03
// LF-DAT-01, LF-DAT-03: ein anderer Prozess (Virenscanner, Indexdienst) hält die Zieldatei kurz
// offen; das Speichern darf daran nicht scheitern (unter Windows Freigabeverletzung beim Ersetzen)
TEST_F(DataStore, M9_ReplaceSucceedsWhileTargetBrieflyOpen)
{
    const fs::path target = dir.path() / "bild.ARW.autoneg.json";
    ASSERT_TRUE(writer.write(target, std::string_view("alt")).has_value());
    auto reader = std::make_unique<std::ifstream>(target, std::ios::binary); // Datei geöffnet halten
    ASSERT_TRUE(reader->is_open());
    std::thread release([&reader] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        reader.reset();
    });
    const auto r = writer.write(target, std::string_view("neu"));
    release.join();
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(readFile(target), "neu");
}

TEST_F(DataStore, U_DAT_06_AbortInEveryStage)
{
    const fs::path target = dir.path() / "ziel.json";
    for (const auto stage : {AtomicFileWriter::Stage::Written, AtomicFileWriter::Stage::Synced,
                             AtomicFileWriter::Stage::BeforeReplace}) {
        // ohne alten Stand: Ziel entsteht nicht
        const AtomicFileWriter failing([stage](AtomicFileWriter::Stage s) { return s == stage; });
        fs::remove(target);
        EXPECT_FALSE(failing.write(target, std::string_view("neu")).has_value());
        EXPECT_FALSE(fs::exists(target));
        // mit altem Stand: alter Stand bleibt vollständig
        ASSERT_TRUE(writer.write(target, std::string_view("alter Stand")).has_value());
        EXPECT_FALSE(failing.write(target, std::string_view("neuer, längerer Stand")).has_value());
        EXPECT_EQ(readFile(target), "alter Stand");
    }
    ASSERT_TRUE(writer.write(target, std::string_view("neuer Stand")).has_value());
    EXPECT_EQ(readFile(target), "neuer Stand");
    EXPECT_FALSE(fs::exists(AtomicFileWriter::tempPath(target)));
}

// LF-DAT-03
TEST_F(DataStore, U_DAT_07_LeftoversAreRemovedOnOpen)
{
    auto s = store();
    ASSERT_TRUE(s.save(frame, dataFor(scan)).has_value());
    const fs::path sidecar = JsonSidecarStore::sidecarPath(frame);
    const std::string before = readFile(sidecar);
    const fs::path tmp = AtomicFileWriter::tempPath(sidecar);
    writeFile(tmp, R"({ "schema": 1, "halb)");
    ASSERT_TRUE(s.load(dir.path()).has_value()); // Rolle öffnen
    EXPECT_FALSE(fs::exists(tmp));
    EXPECT_EQ(readFile(sidecar), before);
}

// LF-DAT-01
TEST(SidecarPath, U_DAT_08_FullExtensionKeepsNamesApart)
{
    model::Frame raw;
    raw.path = fs::path("rolle") / "bild.ARW";
    model::Frame tif;
    tif.path = fs::path("rolle") / "bild.TIF";
    EXPECT_EQ(JsonSidecarStore::sidecarPath(raw).filename().string(), "bild.ARW.autoneg.json");
    EXPECT_EQ(JsonSidecarStore::sidecarPath(tif).filename().string(), "bild.TIF.autoneg.json");
    EXPECT_NE(JsonSidecarStore::sidecarPath(raw), JsonSidecarStore::sidecarPath(tif));
}

// LF-DAT-05
TEST_F(DataStore, U_DAT_09_ReadOnlyFolderUsesFallback)
{
    auto s = store([](const fs::path&) { return false; }); // Fake: Ordner nicht beschreibbar
    EXPECT_FALSE(s.writable(dir.path()));
    const ports::SidecarData d = dataFor(scan);
    ASSERT_TRUE(s.save(frame, d).has_value());
    EXPECT_FALSE(fs::exists(JsonSidecarStore::sidecarPath(frame)));
    const fs::path fallback =
        dir.path() / "benutzer" / "sidecars" / (d.source.hash.toHex() + ".autoneg.json");
    EXPECT_TRUE(fs::exists(fallback));
    // Lesen über die Ersatzablage (Prüfsumme bekannt)
    model::Frame known = frame;
    known.id = d.source.hash;
    const auto loaded = s.load(known);
    ASSERT_TRUE(loaded.has_value() && loaded->has_value());
    EXPECT_EQ((*loaded)->frameNo, 12);
}

// NF-PERF-02
TEST_F(DataStore, U_DAT_10_NoChecksumWhenStampMatches)
{
    auto s = store();
    ASSERT_TRUE(s.save(frame, dataFor(scan)).has_value());
    hasher.calls = 0;
    const auto d = s.load(frame);
    ASSERT_TRUE(d.has_value() && d->has_value());
    EXPECT_EQ(hasher.calls, 0);
}

// LF-DAT-06
TEST_F(DataStore, U_DAT_11_RenamedScanIsPaired)
{
    auto s = store();
    ASSERT_TRUE(s.save(frame, dataFor(scan)).has_value());
    const fs::path renamed = dir.path() / "Toskana_12.ARW";
    fs::rename(scan, renamed);
    const auto orphans = s.orphans(dir.path());
    ASSERT_EQ(orphans.size(), 1U);
    EXPECT_EQ(orphans[0].path.filename().string(), "DSC01234.ARW.autoneg.json");
    model::Frame moved = frame;
    moved.path = renamed;
    ASSERT_TRUE(s.relink(orphans[0], moved).has_value());
    EXPECT_FALSE(fs::exists(orphans[0].path));
    EXPECT_TRUE(fs::exists(JsonSidecarStore::sidecarPath(moved)));
    EXPECT_TRUE(s.orphans(dir.path()).empty());
    const auto d = s.load(moved);
    ASSERT_TRUE(d.has_value() && d->has_value()) << (d ? "" : d.error().message);
    EXPECT_EQ((*d)->source.file, "Toskana_12.ARW");
}

// LF-DAT-06, NF-REL-01
TEST_F(DataStore, U_DAT_12_ReplacedScanIsReported)
{
    auto s = store();
    ASSERT_TRUE(s.save(frame, dataFor(scan)).has_value());
    writeFile(scan, "ganz anderer Inhalt, andere Größe"); // gleicher Name, neuer Scan
    const auto d = s.load(frame);
    ASSERT_FALSE(d.has_value());
    EXPECT_EQ(d.error().code, base::ErrorCode::Conflict);
    EXPECT_GT(hasher.calls, 0);
}

// LF-DAT-03
TEST_F(DataStore, U_DAT_13_SecondInstanceAndStaleLock)
{
#if defined(_WIN32)
    const std::int64_t alive = 4; // System-Prozess
    const std::int64_t dead = 0x7FFFFFF0;
#else
    const std::int64_t alive = 1;         // init
    const std::int64_t dead = 0x7FFFFFF0; // über pid_max
#endif
    ASSERT_TRUE(JsonSidecarStore::processAlive(alive));
    ASSERT_FALSE(JsonSidecarStore::processAlive(dead));
    const std::string host = JsonSidecarStore::hostName();
    const fs::path lock = dir.path() / ".autoneg.lock";
    auto s = store();
    writeFile(lock, dump(JsonValue::object({{"pid", JsonValue(alive)}, {"host", JsonValue(host)}})));
    const auto busy = s.lock(dir.path());
    ASSERT_TRUE(busy.has_value());
    EXPECT_FALSE(*busy); // andere laufende Instanz: nur lesend
    writeFile(lock, dump(JsonValue::object({{"pid", JsonValue(dead)}, {"host", JsonValue(host)}})));
    const auto taken = s.lock(dir.path());
    ASSERT_TRUE(taken.has_value());
    EXPECT_TRUE(*taken); // verwaiste Sperre übernommen
    EXPECT_EQ(*parse(readFile(lock)).value().find("pid"), JsonValue(JsonSidecarStore::currentProcessId()));
    s.unlock(dir.path());
    EXPECT_FALSE(fs::exists(lock));
}

// LF-DAT-04
TEST_F(DataStore, U_DAT_14_SettingsPersistWithoutDefaults)
{
    const fs::path root = dir.path() / "benutzer";
    const std::map<std::string, JsonValue> defaults{{"threads", JsonValue(std::int64_t{0})},
                                                    {"sprache", JsonValue("de")}};
    {
        JsonConfigStore config(root, writer, defaults);
        config.set("threads", JsonValue(std::int64_t{6}));
        config.set("sprache", JsonValue("de")); // Standardwert
    }
    JsonConfigStore reloaded(root, writer, defaults);
    EXPECT_EQ(reloaded.get("threads"), JsonValue(std::int64_t{6}));
    EXPECT_EQ(reloaded.get("sprache"), JsonValue("de"));
    const auto file = parse(readFile(root / "settings.json")).value();
    EXPECT_EQ(file.find("values")->find("sprache"), nullptr); // Standard nicht gespeichert
    reloaded.set("threads", JsonValue(std::int64_t{0}));      // zurück auf Standard
    EXPECT_EQ(parse(readFile(root / "settings.json")).value().find("values")->find("threads"), nullptr);
}

// LF-BAT-07, LF-EXP-10, LF-KTA-10
TEST_F(DataStore, U_DAT_15_PresetsPerKind)
{
    JsonConfigStore config(dir.path() / "benutzer", writer, {});
    const JsonValue edit =
        parse(R"({ "edits": [ { "step": "autoneg.exposure", "p": { "ev": 0.3 } } ] })").value();
    const JsonValue exportJ = parse(R"({ "format": "tiff", "bits": 16 })").value();
    const JsonValue sheet = parse(R"({ "framesPerStrip": 6 })").value();
    ASSERT_TRUE(config.save(ports::PresetKind::Edit, "Portra warm", edit).has_value());
    ASSERT_TRUE(config.save(ports::PresetKind::Edit, "HP5 hart", edit).has_value());
    ASSERT_TRUE(config.save(ports::PresetKind::Export, "Archiv", exportJ).has_value());
    ASSERT_TRUE(config.save(ports::PresetKind::ContactSheet, "A4", sheet).has_value());
    const auto edits = config.list(ports::PresetKind::Edit);
    ASSERT_EQ(edits.size(), 2U);
    EXPECT_EQ(edits[0].id.name, "HP5 hart");
    EXPECT_EQ(edits[1].id.name, "Portra warm");
    EXPECT_EQ(config.list(ports::PresetKind::Export).size(), 1U);
    EXPECT_EQ(config.load({ports::PresetKind::Export, "Archiv"}).value(), exportJ);
    EXPECT_EQ(config.load({ports::PresetKind::ContactSheet, "A4"}).value(), sheet);
    ASSERT_TRUE(config.remove({ports::PresetKind::Edit, "HP5 hart"}).has_value());
    EXPECT_EQ(config.list(ports::PresetKind::Edit).size(), 1U);
    EXPECT_FALSE(config.load({ports::PresetKind::Edit, "HP5 hart"}).has_value());
    EXPECT_FALSE(config.save(ports::PresetKind::Edit, "../ausbruch", edit).has_value());
}

// LF-KTA-10, LF-BAT-07: Vorlagennamen sind Anwendereingaben ("KB 1:1", Umlaute); sie müssen auf
// beiden Systemen speicherbar sein und unverändert zurückkommen
TEST_F(DataStore, M8_PresetNamesWithSpecialCharacters)
{
    JsonConfigStore config(dir.path() / "benutzer", writer, {});
    const JsonValue sheet = parse(R"({ "framesPerStrip": 6 })").value();
    const std::vector<std::string> names{"KB 1:1", "Größe ½ – warm", "Was? *Neu* <A|B> \"x\" 100%"};
    for (const std::string& n : names) {
        ASSERT_TRUE(config.save(ports::PresetKind::ContactSheet, n, sheet).has_value()) << n;
    }
    const auto listed = config.list(ports::PresetKind::ContactSheet);
    ASSERT_EQ(listed.size(), names.size());
    for (const std::string& n : names) {
        EXPECT_TRUE(std::ranges::any_of(listed, [&n](const ports::PresetInfo& p) { return p.id.name == n; }))
            << n;
        EXPECT_EQ(config.load({ports::PresetKind::ContactSheet, n}).value(), sheet) << n;
    }
    ASSERT_TRUE(config.remove({ports::PresetKind::ContactSheet, "KB 1:1"}).has_value());
    EXPECT_EQ(config.list(ports::PresetKind::ContactSheet).size(), names.size() - 1);
}

} // namespace
} // namespace autoneg::adapter::json
