#include "autoneg/pipeline/stage_cache.hpp"

namespace autoneg::pipeline {

std::optional<image::ImageBuffer> StageCache::find(std::uint64_t key)
{
    const std::scoped_lock lock(mtx_);
    const auto it = index_.find(key);
    if (it == index_.end()) {
        return std::nullopt;
    }
    lru_.splice(lru_.begin(), lru_, it->second); // zuletzt genutzt nach vorn
    return it->second->img;
}

void StageCache::put(const model::FrameId& frame, std::uint64_t key, image::ImageBuffer img)
{
    const std::size_t bytes = img.sizeBytes();
    const std::scoped_lock lock(mtx_);
    if (bytes > budget_) {
        return;
    }
    if (const auto it = index_.find(key); it != index_.end()) {
        used_ -= it->second->img.sizeBytes();
        lru_.erase(it->second);
        index_.erase(it);
    }
    lru_.push_front(Entry{key, frame, std::move(img)});
    index_[key] = lru_.begin();
    used_ += bytes;
    shrink();
}

void StageCache::evict(const model::FrameId& frame)
{
    const std::scoped_lock lock(mtx_);
    for (auto it = lru_.begin(); it != lru_.end();) {
        if (it->frame == frame) {
            used_ -= it->img.sizeBytes();
            index_.erase(it->key);
            it = lru_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t StageCache::usedBytes() const
{
    const std::scoped_lock lock(mtx_);
    return used_;
}

std::size_t StageCache::size() const
{
    const std::scoped_lock lock(mtx_);
    return lru_.size();
}

bool StageCache::contains(std::uint64_t key) const
{
    const std::scoped_lock lock(mtx_);
    return index_.contains(key);
}

void StageCache::shrink()
{
    while (used_ > budget_ && !lru_.empty()) {
        const Entry& last = lru_.back();
        used_ -= last.img.sizeBytes();
        index_.erase(last.key);
        lru_.pop_back();
    }
}

} // namespace autoneg::pipeline
