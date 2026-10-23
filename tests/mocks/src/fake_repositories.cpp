#include "autoneg/mocks/fake_repositories.hpp"

namespace autoneg::mocks {

base::Expected<std::optional<ports::SidecarData>> FakeEditRepository::load(const model::Frame& f)
{
    const std::scoped_lock lock(mtx_);
    const auto it = data_.find(f.path);
    if (it == data_.end()) {
        return std::optional<ports::SidecarData>{};
    }
    return std::optional<ports::SidecarData>(it->second);
}

base::Expected<void> FakeEditRepository::save(const model::Frame& f, const ports::SidecarData& d)
{
    const std::scoped_lock lock(mtx_);
    data_.insert_or_assign(f.path, d);
    ++saves_;
    return {};
}

std::vector<ports::SidecarRef> FakeEditRepository::orphans(const std::filesystem::path& folder)
{
    // wie die Begleitdateien neben den Scans: Einträge im Ordner, deren Scan fehlt
    const std::scoped_lock lock(mtx_);
    std::vector<ports::SidecarRef> out;
    for (const auto& [path, d] : data_) {
        std::error_code ec;
        if (path.parent_path() == folder && !std::filesystem::exists(path, ec)) {
            out.push_back({path, d.source});
        }
    }
    return out;
}

base::Expected<void> FakeEditRepository::relink(const ports::SidecarRef& orphan, const model::Frame& f)
{
    const std::scoped_lock lock(mtx_);
    const auto it = data_.find(orphan.path);
    if (it == data_.end()) {
        return base::makeError(base::ErrorCode::NotFound, "keine verwaiste Begleitdatei");
    }
    if (it->second.source.hash != f.id) {
        return base::makeError(base::ErrorCode::Conflict, "Prüfsumme passt nicht");
    }
    ports::SidecarData d = it->second;
    data_.erase(it);
    data_.insert_or_assign(f.path, std::move(d));
    return {};
}

int FakeEditRepository::saves() const
{
    const std::scoped_lock lock(mtx_);
    return saves_;
}

base::Expected<std::optional<model::Roll>> FakeRollRepository::load(const std::filesystem::path& folder)
{
    const std::scoped_lock lock(mtx_);
    const auto it = rolls_.find(folder);
    if (it == rolls_.end()) {
        return std::optional<model::Roll>{};
    }
    return std::optional<model::Roll>(it->second);
}

base::Expected<void> FakeRollRepository::save(const model::Roll& r)
{
    const std::scoped_lock lock(mtx_);
    model::Roll stored = r;
    stored.frames.clear(); // wie die Rollendatei: Bilder ergeben sich aus dem Ordner
    rolls_.insert_or_assign(r.folder, std::move(stored));
    return {};
}

base::Expected<bool> FakeRollRepository::lock(const std::filesystem::path& folder)
{
    const std::scoped_lock lock(mtx_);
    if (foreign_.contains(folder)) {
        return false;
    }
    locks_.insert(folder);
    return true;
}

void FakeRollRepository::unlock(const std::filesystem::path& folder)
{
    const std::scoped_lock lock(mtx_);
    locks_.erase(folder);
}

void FakeRollRepository::lockedElsewhere(const std::filesystem::path& folder)
{
    const std::scoped_lock lock(mtx_);
    foreign_.insert(folder);
}

std::optional<base::Bytes> FakeCacheStore::thumbnail(const model::FrameId& h, int size,
                                                     std::uint64_t editHash)
{
    const std::scoped_lock lock(mtx_);
    const auto it = data_.find({h, size, editHash});
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void FakeCacheStore::put(const model::FrameId& h, int size, std::uint64_t editHash, const base::Bytes& jpeg)
{
    const std::scoped_lock lock(mtx_);
    data_.insert_or_assign({h, size, editHash}, jpeg);
}

} // namespace autoneg::mocks
