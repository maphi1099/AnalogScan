#pragma once

#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/ids.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace autoneg::pipeline {

/// Zwischenbilder an den Cache-Punkten der Vorschau (AE-06), am längsten ungenutzte zuerst
/// verdrängt, begrenzt auf ein Speicherbudget in Byte. Threadsicher.
///
/// Abweichung von 9.4: put erhält zusätzlich das Bild, damit evict(frame) alle Einträge eines
/// Bildes entfernen kann; find verschiebt den Eintrag nach vorn und braucht deshalb eine
/// exklusive Sperre (std::mutex statt shared_mutex).
class StageCache
{
public:
    explicit StageCache(std::size_t budgetBytes)
        : budget_(budgetBytes)
    {}

    [[nodiscard]] std::optional<image::ImageBuffer> find(std::uint64_t key);
    /// Einträge größer als das Budget werden nicht aufgenommen.
    void put(const model::FrameId& frame, std::uint64_t key, image::ImageBuffer img);
    void evict(const model::FrameId& frame);

    [[nodiscard]] std::size_t usedBytes() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool contains(std::uint64_t key) const;

private:
    struct Entry
    {
        std::uint64_t key;
        model::FrameId frame;
        image::ImageBuffer img;
    };

    void shrink();

    mutable std::mutex mtx_;
    std::list<Entry> lru_; ///< vorn: zuletzt genutzt
    std::unordered_map<std::uint64_t, std::list<Entry>::iterator> index_;
    std::size_t budget_;
    std::size_t used_ = 0;
};

} // namespace autoneg::pipeline
