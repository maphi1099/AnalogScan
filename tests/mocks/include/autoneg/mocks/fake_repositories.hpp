#pragma once

// Vertragstreue Fakes der Datenhaltung im Arbeitsspeicher (K-14): IEditRepository,
// IRollRepository, ICacheStore. Threadsicher wie die echten Adapter.

#include "autoneg/ports/repositories.hpp"

#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace autoneg::mocks {

class FakeEditRepository final : public ports::IEditRepository
{
public:
    base::Expected<std::optional<ports::SidecarData>> load(const model::Frame& f) override;
    base::Expected<void> save(const model::Frame& f, const ports::SidecarData& d) override;
    std::vector<ports::SidecarRef> orphans(const std::filesystem::path& folder) override;
    base::Expected<void> relink(const ports::SidecarRef& orphan, const model::Frame& f) override;

    [[nodiscard]] int saves() const;

private:
    mutable std::mutex mtx_;
    std::map<std::filesystem::path, ports::SidecarData> data_;
    int saves_ = 0;
};

class FakeRollRepository final : public ports::IRollRepository
{
public:
    base::Expected<std::optional<model::Roll>> load(const std::filesystem::path& folder) override;
    base::Expected<void> save(const model::Roll& r) override;
    base::Expected<bool> lock(const std::filesystem::path& folder) override;
    void unlock(const std::filesystem::path& folder) override;

    /// Simuliert eine zweite laufende Instanz, die die Rolle hält.
    void lockedElsewhere(const std::filesystem::path& folder);

private:
    std::mutex mtx_;
    std::map<std::filesystem::path, model::Roll> rolls_;
    std::set<std::filesystem::path> locks_;
    std::set<std::filesystem::path> foreign_;
};

class FakeCacheStore final : public ports::ICacheStore
{
public:
    std::optional<base::Bytes> thumbnail(const model::FrameId& h, int size, std::uint64_t editHash) override;
    void put(const model::FrameId& h, int size, std::uint64_t editHash, const base::Bytes& jpeg) override;

private:
    std::mutex mtx_;
    std::map<std::tuple<model::FrameId, int, std::uint64_t>, base::Bytes> data_;
};

} // namespace autoneg::mocks
