#pragma once

#include "autoneg/ports/services.hpp"

namespace autoneg::adapter::xxhash {

/// Inhaltsprüfsumme eines Scans (Architektur 9.11): XXH3-128 über die ganze Datei in
/// Blöcken zu 8 MB. Ergebnis als FrameId (16 Byte, Big-Endian: high64, dann low64).
/// Threadsicher (jeder Aufruf hat seinen eigenen Zustand).
class XxHasher final : public ports::IContentHasher
{
public:
    [[nodiscard]] base::Expected<model::FrameId> hashFile(const std::filesystem::path& p) override;
};

} // namespace autoneg::adapter::xxhash
