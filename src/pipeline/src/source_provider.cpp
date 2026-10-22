#include "autoneg/pipeline/source_provider.hpp"

#include <algorithm>

namespace autoneg::pipeline {

base::Expected<image::ImageBuffer> SourceProvider::decode(const std::filesystem::path& path, int level)
{
    ports::DecodeOptions o;
    o.half = level >= 1;
    auto raw = decoder_.decode(path, o);
    if (!raw && raw.error().code == base::ErrorCode::Unsupported) {
        return reader_.read(path); // kein RAW: TIFF, JPEG …
    }
    return raw;
}

base::Expected<image::ImageBuffer> SourceProvider::source(const model::FrameId& f,
                                                          const std::filesystem::path& path, int level)
{
    const Key key{f, std::clamp(level, 0, 1)};
    std::promise<base::Expected<image::ImageBuffer>> promise;
    std::shared_future<base::Expected<image::ImageBuffer>> future;
    bool owner = false;
    {
        const std::scoped_lock lock(mtx_);
        if (const auto it = sources_.find(key); it != sources_.end()) {
            future = it->second;
        } else {
            future = promise.get_future().share();
            sources_.emplace(key, future);
            owner = true;
        }
    }
    if (owner) {
        auto result = decode(path, key.level);
        const bool failed = !result.has_value();
        promise.set_value(std::move(result));
        if (failed) { // Fehler nicht zwischenspeichern, damit ein neuer Versuch möglich ist
            const std::scoped_lock lock(mtx_);
            sources_.erase(key);
        }
    }
    return future.get();
}

base::Expected<model::SourceInfo> SourceProvider::info(const model::FrameId& f,
                                                       const std::filesystem::path& path)
{
    {
        const std::scoped_lock lock(mtx_);
        if (const auto it = infos_.find(f); it != infos_.end()) {
            return it->second;
        }
    }
    auto probed = decoder_.probe(path);
    if (!probed && probed.error().code == base::ErrorCode::Unsupported) {
        probed = reader_.probe(path);
    }
    if (probed) {
        const std::scoped_lock lock(mtx_);
        infos_[f] = *probed;
    }
    return probed;
}

void SourceProvider::prefetch(const model::FrameId& f, const std::filesystem::path& path)
{
    (void)source(f, path, 0);
}

void SourceProvider::release(const model::FrameId& f)
{
    const std::scoped_lock lock(mtx_);
    std::erase_if(sources_, [&](const auto& kv) { return kv.first.frame == f; });
    infos_.erase(f);
}

} // namespace autoneg::pipeline
