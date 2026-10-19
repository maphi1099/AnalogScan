#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/model/ids.hpp"

#include <filesystem>
#include <string_view>

namespace autoneg::ports {

/// Inhaltsprüfsumme XXH3-128 eines Scans (AE-09). Adapter: XxHasher. Threadsicher.
class IContentHasher
{
public:
    virtual ~IContentHasher() = default;

    [[nodiscard]] virtual base::Expected<model::FrameId> hashFile(const std::filesystem::path& p) = 0;
};

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error
};

/// Protokoll (NF-REL-03). Adapter: SpdLogger. Threadsicher.
class ILogger
{
public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel l, std::string_view msg) = 0;
};

} // namespace autoneg::ports
