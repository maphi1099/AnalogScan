#include "autoneg/adapter/json/atomic_file_writer.hpp"

#include <algorithm>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace autoneg::adapter::json {

namespace {

std::string describe(const std::filesystem::path& p)
{
    return p.filename().string();
}

#if defined(_WIN32)

std::string lastError()
{
    return "Windows-Fehler " + std::to_string(GetLastError());
}

/// Datei schreiben und auf den Datenträger erzwingen (FlushFileBuffers).
base::Expected<void> writeSynced(const std::filesystem::path& tmp, std::span<const std::byte> bytes,
                                 const AtomicFileWriter::FaultHook& hook)
{
    HANDLE h = CreateFileW(tmp.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return base::makeError(base::ErrorCode::Io, describe(tmp) + ": nicht anlegbar (" + lastError() + ")");
    }
    std::size_t done = 0;
    while (done < bytes.size()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1U << 30));
        DWORD written = 0;
        if (WriteFile(h, bytes.data() + done, chunk, &written, nullptr) == 0) {
            CloseHandle(h);
            return base::makeError(base::ErrorCode::Io,
                                   describe(tmp) + ": Schreibfehler (" + lastError() + ")");
        }
        done += written;
    }
    if (hook && hook(AtomicFileWriter::Stage::Written)) {
        CloseHandle(h);
        return base::makeError(base::ErrorCode::Io, "simulierter Abbruch nach dem Schreiben");
    }
    const bool synced = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    if (!synced) {
        return base::makeError(base::ErrorCode::Io, describe(tmp) + ": Synchronisieren fehlgeschlagen");
    }
    return {};
}

/// Ziel atomar ersetzen: ReplaceFileW, falls das Ziel existiert, sonst MoveFileExW.
base::Expected<void> replace(const std::filesystem::path& tmp, const std::filesystem::path& target)
{
    std::error_code ec;
    BOOL ok = FALSE;
    if (std::filesystem::exists(target, ec)) {
        ok = ReplaceFileW(target.wstring().c_str(), tmp.wstring().c_str(), nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
    } else {
        ok = MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(), MOVEFILE_WRITE_THROUGH);
    }
    if (ok == FALSE) {
        return base::makeError(base::ErrorCode::Io,
                               describe(target) + ": Ersetzen fehlgeschlagen (" + lastError() + ")");
    }
    return {};
}

#else

std::string lastError()
{
    return std::strerror(errno);
}

base::Expected<void> writeSynced(const std::filesystem::path& tmp, std::span<const std::byte> bytes,
                                 const AtomicFileWriter::FaultHook& hook)
{
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return base::makeError(base::ErrorCode::Io, describe(tmp) + ": nicht anlegbar (" + lastError() + ")");
    }
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return base::makeError(base::ErrorCode::Io,
                                   describe(tmp) + ": Schreibfehler (" + lastError() + ")");
        }
        done += static_cast<std::size_t>(n);
    }
    if (hook && hook(AtomicFileWriter::Stage::Written)) {
        ::close(fd);
        return base::makeError(base::ErrorCode::Io, "simulierter Abbruch nach dem Schreiben");
    }
    const bool synced = ::fsync(fd) == 0;
    ::close(fd);
    if (!synced) {
        return base::makeError(base::ErrorCode::Io, describe(tmp) + ": Synchronisieren fehlgeschlagen");
    }
    return {};
}

base::Expected<void> replace(const std::filesystem::path& tmp, const std::filesystem::path& target)
{
    if (::rename(tmp.c_str(), target.c_str()) != 0) {
        return base::makeError(base::ErrorCode::Io,
                               describe(target) + ": Ersetzen fehlgeschlagen (" + lastError() + ")");
    }
    // Ordner synchronisieren, damit der neue Verzeichniseintrag dauerhaft ist
    const std::filesystem::path dir =
        target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        (void)::fsync(dfd);
        ::close(dfd);
    }
    return {};
}

#endif

} // namespace

std::filesystem::path AtomicFileWriter::tempPath(const std::filesystem::path& p)
{
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    return tmp;
}

base::Expected<void> AtomicFileWriter::write(const std::filesystem::path& p,
                                             std::span<const std::byte> bytes) const
{
    const std::filesystem::path tmp = tempPath(p);
    if (auto ok = writeSynced(tmp, bytes, hook_); !ok) {
        return ok;
    }
    if (hook_ && (hook_(Stage::Synced) || hook_(Stage::BeforeReplace))) {
        return base::makeError(base::ErrorCode::Io, "simulierter Abbruch vor dem Ersetzen");
    }
    if (auto ok = replace(tmp, p); !ok) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return ok;
    }
    return {};
}

base::Expected<void> AtomicFileWriter::write(const std::filesystem::path& p, std::string_view text) const
{
    return write(p, std::as_bytes(std::span(text.data(), text.size())));
}

} // namespace autoneg::adapter::json
