#include "autoneg/base/error.hpp"

namespace autoneg::base {

std::string_view toString(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::OutOfRange:
        return "OutOfRange";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::TypeMismatch:
        return "TypeMismatch";
    case ErrorCode::Unsupported:
        return "Unsupported";
    case ErrorCode::Corrupt:
        return "Corrupt";
    case ErrorCode::Io:
        return "Io";
    case ErrorCode::ReadOnly:
        return "ReadOnly";
    case ErrorCode::Cancelled:
        return "Cancelled";
    case ErrorCode::Internal:
        return "Internal";
    }
    return "Unknown";
}

} // namespace autoneg::base
