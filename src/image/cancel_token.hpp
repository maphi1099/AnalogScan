#pragma once

#include <atomic>
#include <memory>

namespace autoneg::image {

/// Kooperativer Abbruch (LF-BAT-09). Kopien teilen denselben Zustand; geprüft wird zwischen
/// Zeilenblöcken und Kacheln. Threadsicher.
///
/// Liegt nach A-01 in autoneg::image, weil Ports (Rechen-Backend) und Pipeline ihn brauchen.
class CancelToken
{
public:
    CancelToken()
        : flag_(std::make_shared<std::atomic<bool>>(false))
    {}

    void cancel() const noexcept { flag_->store(true, std::memory_order_release); }
    [[nodiscard]] bool cancelled() const noexcept { return flag_->load(std::memory_order_acquire); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

} // namespace autoneg::image
