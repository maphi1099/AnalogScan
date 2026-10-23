// Vertragstests der Datenhaltung (K-14, NF-ARC-03, NF-ARC-12): dieselben Suiten gegen die
// Fakes im Arbeitsspeicher (tests/mocks/fake_repositories.hpp) und gegen die echten Adapter
// JsonSidecarStore (mit XxHasher) und FileThumbnailCache auf einem temporären Ordner.

#include "autoneg/adapter/json/atomic_file_writer.hpp"
#include "autoneg/adapter/json/file_thumbnail_cache.hpp"
#include "autoneg/adapter/json/json_sidecar_store.hpp"
#include "autoneg/adapter/json/schema_migrator.hpp"
#include "autoneg/adapter/xxhash/xx_hasher.hpp"
#include "autoneg/mocks/fake_repositories.hpp"
#include "autoneg/testing/temp_dir.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

namespace autoneg {
namespace {

namespace fs = std::filesystem;

ports::StepCatalog catalog()
{
    using K = ports::ParamKind;
    return {
        {"autoneg.crop", 40, 1, {{"rect", K::Rect}, {"inset", K::Double}}},
        {"autoneg.exposure", 110, 1, {{"ev", K::Double}}},
    };
}

model::EditEntry entry(const std::string& id, int rank, model::ParamSet p)
{
    model::EditEntry e;
    e.step = model::StepId(id);
    e.rank = rank;
    e.params = std::move(p);
    return e;
}

/// Ordner mit zwei kleinen Scans (Inhalt beliebig, nur für Prüfsumme und Zeitstempel).
struct Folder
{
    Folder()
    {
        for (const char* name : {"DSC00001.ARW", "DSC00002.ARW"}) {
            std::ofstream(dir.path() / name, std::ios::binary) << "Scan " << name;
        }
    }
    model::Frame frame(int n) const
    {
        model::Frame f;
        f.path = dir.path() / ("DSC0000" + std::to_string(n) + ".ARW");
        f.frameNo = n;
        adapter::xxhash::XxHasher h;
        f.id = h.hashFile(f.path).value();
        return f;
    }
    ports::SidecarData data(const model::Frame& f, double ev) const
    {
        ports::SidecarData d;
        const auto t = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(f.path));
        d.source = {f.path.filename().string(), fs::file_size(f.path),
                    std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count(), f.id};
        d.frameNo = f.frameNo;
        d.rating = model::Rating::Keep;
        d.edits = model::EditList{}
                      .with(entry("autoneg.crop", 40,
                                  model::ParamSet{}.with("rect", model::NormRect{0.1, 0.1, 0.8, 0.8})))
                      .with(entry("autoneg.exposure", 110, model::ParamSet{}.with("ev", ev)));
        return d;
    }
    autoneg::testing::TempDir dir;
};

struct FakeData
{
    static constexpr const char* name = "Fake";
    explicit FakeData(const fs::path& /*user*/) {}
    ports::IEditRepository& edits() { return edits_; }
    ports::IRollRepository& rolls() { return rolls_; }
    ports::ICacheStore& cache() { return cache_; }

    mocks::FakeEditRepository edits_;
    mocks::FakeRollRepository rolls_;
    mocks::FakeCacheStore cache_;
};

struct JsonData
{
    static constexpr const char* name = "Json";
    explicit JsonData(const fs::path& user)
        : store_(catalog(), writer_, migrator_, hasher_, user / "sidecars")
        , cache_(user / "thumbnails")
    {}
    ports::IEditRepository& edits() { return store_; }
    ports::IRollRepository& rolls() { return store_; }
    ports::ICacheStore& cache() { return cache_; }

    adapter::json::AtomicFileWriter writer_;
    adapter::json::SchemaMigrator migrator_;
    adapter::xxhash::XxHasher hasher_;
    adapter::json::JsonSidecarStore store_;
    adapter::json::FileThumbnailCache cache_;
};

struct SetupName
{
    template <typename T>
    static std::string GetName(int /*i*/)
    {
        return T::name;
    }
};

template <typename Impl>
class DataContract : public ::testing::Test
{
protected:
    Folder folder;
    autoneg::testing::TempDir user;
    Impl setup{user.path()};
};

using DataSetups = ::testing::Types<FakeData, JsonData>;
TYPED_TEST_SUITE(DataContract, DataSetups, SetupName);

// IEditRepository
TYPED_TEST(DataContract, K_14_EditsMissingThenRoundTrip)
{
    auto& repo = this->setup.edits();
    const model::Frame f = this->folder.frame(1);
    const auto none = repo.load(f);
    ASSERT_TRUE(none.has_value());
    EXPECT_FALSE(none->has_value());
    const ports::SidecarData d = this->folder.data(f, 0.5);
    ASSERT_TRUE(repo.save(f, d).has_value());
    const auto back = repo.load(f);
    ASSERT_TRUE(back.has_value() && back->has_value());
    EXPECT_EQ((*back)->edits, d.edits);
    EXPECT_EQ((*back)->frameNo, 1);
    EXPECT_EQ((*back)->rating, model::Rating::Keep);
    EXPECT_EQ((*back)->source.hash, f.id);
    // jede Begleitdatei gehört zu genau einem Bild
    const auto other = repo.load(this->folder.frame(2));
    ASSERT_TRUE(other.has_value());
    EXPECT_FALSE(other->has_value());
}

TYPED_TEST(DataContract, K_14_EditsLastSaveWins)
{
    auto& repo = this->setup.edits();
    const model::Frame f = this->folder.frame(1);
    for (int i = 1; i <= 5; ++i) {
        const auto saved = repo.save(f, this->folder.data(f, 0.1 * i));
        ASSERT_TRUE(saved.has_value()) << saved.error().message;
    }
    const auto back = repo.load(f);
    ASSERT_TRUE(back.has_value() && back->has_value());
    EXPECT_EQ((*back)->edits, this->folder.data(f, 0.5).edits);
}

// IRollRepository
TYPED_TEST(DataContract, K_14_RollMissingThenRoundTrip)
{
    auto& repo = this->setup.rolls();
    const fs::path folder = this->folder.dir.path();
    const auto none = repo.load(folder);
    ASSERT_TRUE(none.has_value());
    EXPECT_FALSE(none->has_value());
    model::Roll r;
    r.folder = folder;
    r.name = "Rolle 12";
    r.filmType = model::FilmType::BwNegative;
    r.film.filmName = "Ilford HP5 Plus";
    r.film.iso = 400;
    r.frames = {this->folder.frame(1).id, this->folder.frame(2).id};
    r.defaults = model::EditList{}.with(entry("autoneg.exposure", 110, model::ParamSet{}.with("ev", 0.3)));
    ASSERT_TRUE(repo.save(r).has_value());
    const auto back = repo.load(folder);
    ASSERT_TRUE(back.has_value() && back->has_value());
    EXPECT_EQ((*back)->name, r.name);
    EXPECT_EQ((*back)->filmType, r.filmType);
    EXPECT_EQ((*back)->film, r.film);
    // Bildliste steht nicht in der Rollendatei (Architektur 8: Film, Entwicklung, Rollen-Standard);
    // die Bilder ergeben sich aus dem Ordner
    EXPECT_TRUE((*back)->frames.empty());
    EXPECT_EQ((*back)->defaults, r.defaults);
}

TYPED_TEST(DataContract, K_14_RollLockIsReentrantForThisInstance)
{
    auto& repo = this->setup.rolls();
    const fs::path folder = this->folder.dir.path();
    EXPECT_EQ(repo.lock(folder), base::Expected<bool>(true));
    EXPECT_EQ(repo.lock(folder), base::Expected<bool>(true));
    repo.unlock(folder);
    EXPECT_EQ(repo.lock(folder), base::Expected<bool>(true));
    repo.unlock(folder);
}

// ICacheStore
TYPED_TEST(DataContract, K_14_CacheKeyedByFrameSizeAndEdits)
{
    auto& cache = this->setup.cache();
    const model::FrameId h = this->folder.frame(1).id;
    EXPECT_FALSE(cache.thumbnail(h, 256, 7).has_value());
    const base::Bytes jpeg{0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3};
    cache.put(h, 256, 7, jpeg);
    EXPECT_EQ(cache.thumbnail(h, 256, 7), jpeg);
    EXPECT_FALSE(cache.thumbnail(h, 512, 7).has_value()); // andere Größenklasse
    EXPECT_FALSE(cache.thumbnail(h, 256, 8).has_value()); // andere Bearbeitung
    EXPECT_FALSE(cache.thumbnail(this->folder.frame(2).id, 256, 7).has_value());
    const base::Bytes newer{0xFF, 0xD8, 9};
    cache.put(h, 256, 7, newer);
    EXPECT_EQ(cache.thumbnail(h, 256, 7), newer);
}

TYPED_TEST(DataContract, K_14_CacheConcurrentPutAndGet)
{
    auto& cache = this->setup.cache();
    const model::FrameId h = this->folder.frame(1).id;
    std::vector<std::jthread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&cache, &h, t] {
            for (int i = 0; i < 25; ++i) {
                const auto key = static_cast<std::uint64_t>(t * 100 + i);
                const base::Bytes b(16, static_cast<std::uint8_t>(t * 25 + i));
                cache.put(h, 128, key, b);
                const auto back = cache.thumbnail(h, 128, key);
                EXPECT_EQ(back, b);
            }
        });
    }
    threads.clear();
    EXPECT_EQ(cache.thumbnail(h, 128, 324), base::Bytes(16, static_cast<std::uint8_t>(99)));
}

// FileThumbnailCache: Verdrängung der am längsten nicht genutzten Miniaturen (Architektur 8.8)
TEST(FileThumbnailCacheTest, M8_EvictsLeastRecentlyUsedAboveLimit)
{
    autoneg::testing::TempDir dir;
    const model::FrameId h;
    {
        adapter::json::FileThumbnailCache cache(dir.path(), 300);
        for (std::uint64_t k = 1; k <= 3; ++k) {
            cache.put(h, 64, k, base::Bytes(100, 1));
        }
        ASSERT_TRUE(cache.thumbnail(h, 64, 1).has_value()); // 1 wieder benutzt
        cache.put(h, 64, 4, base::Bytes(100, 1));           // verdrängt 2
        EXPECT_TRUE(cache.thumbnail(h, 64, 1).has_value());
        EXPECT_FALSE(cache.thumbnail(h, 64, 2).has_value());
        EXPECT_TRUE(cache.thumbnail(h, 64, 3).has_value());
        EXPECT_TRUE(cache.thumbnail(h, 64, 4).has_value());
        EXPECT_LE(cache.usage(), 300U);
    }
    // nach Neustart: Bestand bleibt, ohne Reste
    adapter::json::FileThumbnailCache again(dir.path(), 300);
    EXPECT_EQ(again.usage(), 300U);
    EXPECT_TRUE(again.thumbnail(h, 64, 4).has_value());
    EXPECT_FALSE(fs::exists(dir.path() / (adapter::json::FileThumbnailCache::fileName(h, 64, 2))));
}

} // namespace
} // namespace autoneg
