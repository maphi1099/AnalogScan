#include "autoneg/adapter/json/file_thumbnail_cache.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

namespace autoneg::adapter::json {

namespace fs = std::filesystem;

FileThumbnailCache::FileThumbnailCache(fs::path dir, std::uintmax_t limit, AtomicFileWriter writer)
    : dir_(std::move(dir))
    , limit_(limit)
    , writer_(std::move(writer))
{
    std::error_code ec;
    fs::create_directories(dir_, ec);
    // Bestand einlesen; Reihenfolge der letzten Nutzung nach Änderungszeit der Dateien
    std::vector<std::pair<fs::file_time_type, std::string>> found;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file(ec)) {
            continue;
        }
        const std::string name = e.path().filename().string();
        if (e.path().extension() == ".tmp") {
            fs::remove(e.path(), ec); // Rest eines abgebrochenen Schreibens
            continue;
        }
        if (e.path().extension() != ".jpg") {
            continue;
        }
        found.emplace_back(e.last_write_time(ec), name);
        entries_[name].bytes = e.file_size(ec);
        total_ += entries_[name].bytes;
    }
    std::ranges::sort(found);
    for (const auto& [time, name] : found) {
        entries_[name].lastUse = ++clock_;
    }
    evictLru();
}

std::string FileThumbnailCache::fileName(const model::FrameId& h, int size, std::uint64_t editHash)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "_%d_%016llx.jpg", size, static_cast<unsigned long long>(editHash));
    return h.toHex() + buf;
}

std::optional<base::Bytes> FileThumbnailCache::thumbnail(const model::FrameId& h, int size,
                                                         std::uint64_t editHash)
{
    const std::string name = fileName(h, size, editHash);
    {
        const std::scoped_lock lock(mtx_);
        const auto it = entries_.find(name);
        if (it == entries_.end()) {
            return std::nullopt;
        }
        it->second.lastUse = ++clock_;
    }
    std::ifstream in(dir_ / name, std::ios::binary);
    if (!in) {
        const std::scoped_lock lock(mtx_); // von außen gelöscht
        if (const auto it = entries_.find(name); it != entries_.end()) {
            total_ -= it->second.bytes;
            entries_.erase(it);
        }
        return std::nullopt;
    }
    base::Bytes out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::error_code ec;
    fs::last_write_time(dir_ / name, fs::file_time_type::clock::now(), ec); // für den nächsten Start
    return out;
}

void FileThumbnailCache::put(const model::FrameId& h, int size, std::uint64_t editHash,
                             const base::Bytes& jpeg)
{
    const std::string name = fileName(h, size, editHash);
    if (!writer_.write(dir_ / name, std::as_bytes(std::span(jpeg)))) {
        return; // Cache ist entbehrlich: Fehler beim Schreiben werden ignoriert
    }
    const std::scoped_lock lock(mtx_);
    Entry& e = entries_[name];
    total_ -= e.bytes;
    e.bytes = jpeg.size();
    e.lastUse = ++clock_;
    total_ += e.bytes;
    evictLru();
}

std::uintmax_t FileThumbnailCache::usage() const
{
    const std::scoped_lock lock(mtx_);
    return total_;
}

void FileThumbnailCache::evictLru()
{
    while (total_ > limit_ && !entries_.empty()) {
        const auto oldest =
            std::ranges::min_element(entries_, {}, [](const auto& kv) { return kv.second.lastUse; });
        std::error_code ec;
        fs::remove(dir_ / oldest->first, ec);
        total_ -= oldest->second.bytes;
        entries_.erase(oldest);
    }
}

} // namespace autoneg::adapter::json
