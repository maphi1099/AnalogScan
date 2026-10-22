#include "autoneg/adapter/xxhash/xx_hasher.hpp"

#include <xxhash.h>

#include <array>
#include <fstream>
#include <memory>
#include <vector>

namespace autoneg::adapter::xxhash {

base::Expected<model::FrameId> XxHasher::hashFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return base::makeError(base::ErrorCode::NotFound, p.filename().string() + ": Datei nicht lesbar");
    }
    const std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), &XXH3_freeState);
    if (!state || XXH3_128bits_reset(state.get()) != XXH_OK) {
        return base::makeError(base::ErrorCode::Internal, "XXH3 nicht initialisierbar");
    }
    std::vector<char> buffer(std::size_t{8} << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto n = static_cast<std::size_t>(in.gcount());
        if (n > 0 && XXH3_128bits_update(state.get(), buffer.data(), n) != XXH_OK) {
            return base::makeError(base::ErrorCode::Internal, "XXH3 fehlgeschlagen");
        }
    }
    if (in.bad()) {
        return base::makeError(base::ErrorCode::Io, p.filename().string() + ": Lesefehler");
    }
    const XXH128_hash_t h = XXH3_128bits_digest(state.get());
    std::array<std::uint8_t, model::FrameId::size> bytes{};
    for (int i = 0; i < 8; ++i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(h.high64 >> (56 - 8 * i));
        bytes[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(h.low64 >> (56 - 8 * i));
    }
    return model::FrameId(bytes);
}

} // namespace autoneg::adapter::xxhash
