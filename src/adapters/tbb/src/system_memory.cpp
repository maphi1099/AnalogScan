#include "autoneg/adapter/tbb/system_memory.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // plattformabhängig: Arbeitsspeicher (PROGRESS.md)
#else
#include <unistd.h> // plattformabhängig: Arbeitsspeicher (PROGRESS.md)
#endif

namespace autoneg::adapter::tbb {

std::uint64_t physicalMemoryBytes()
{
#if defined(_WIN32)
    MEMORYSTATUSEX s{};
    s.dwLength = sizeof s;
    return GlobalMemoryStatusEx(&s) != 0 ? s.ullTotalPhys : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long size = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && size > 0 ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(size) : 0;
#endif
}

} // namespace autoneg::adapter::tbb
