#include "autoneg/adapter/spdlog/spd_logger.hpp"

#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <system_error>

namespace autoneg::adapter::spdlog {

namespace fs = std::filesystem;

namespace {

::spdlog::level::level_enum levelOf(ports::LogLevel l)
{
    switch (l) {
    case ports::LogLevel::Trace:
        return ::spdlog::level::trace;
    case ports::LogLevel::Debug:
        return ::spdlog::level::debug;
    case ports::LogLevel::Info:
        return ::spdlog::level::info;
    case ports::LogLevel::Warning:
        return ::spdlog::level::warn;
    case ports::LogLevel::Error:
        return ::spdlog::level::err;
    }
    return ::spdlog::level::info;
}

/// Datei mit Rotation. Eigene Senke statt rotating_file_sink: spdlog öffnet Dateien unter
/// Windows ohne SPDLOG_WCHAR_FILENAMES über die ANSI-Codepage, Benutzerverzeichnisse mit
/// Umlauten wären dann nicht beschreibbar; std::ofstream mit std::filesystem::path schon.
class RotatingSink final : public ::spdlog::sinks::base_sink<std::mutex>
{
public:
    RotatingSink(fs::path file, std::size_t maxBytes, std::size_t maxFiles)
        : file_(std::move(file))
        , maxBytes_(maxBytes)
        , maxFiles_(maxFiles)
    {
        std::error_code ec;
        size_ = fs::exists(file_, ec) ? fs::file_size(file_, ec) : 0;
        out_.open(file_, std::ios::binary | std::ios::app);
    }

protected:
    void sink_it_(const ::spdlog::details::log_msg& msg) override
    {
        ::spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        if (size_ + buf.size() > maxBytes_ && size_ > 0) {
            rotate();
        }
        out_.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        size_ += buf.size();
    }
    void flush_() override { out_.flush(); }

private:
    /// autoneg.log → autoneg.1.log → … → autoneg.<maxFiles>.log (älteste fällt weg)
    [[nodiscard]] fs::path numbered(std::size_t i) const
    {
        fs::path p = file_;
        p.replace_filename(file_.stem().native() + fs::path("." + std::to_string(i)).native() +
                           file_.extension().native());
        return p;
    }
    void rotate()
    {
        out_.close();
        std::error_code ec;
        fs::remove(numbered(maxFiles_), ec);
        for (std::size_t i = maxFiles_; i > 1; --i) {
            fs::rename(numbered(i - 1), numbered(i), ec);
        }
        fs::rename(file_, numbered(1), ec);
        out_.open(file_, std::ios::binary | std::ios::trunc);
        size_ = 0;
    }

    fs::path file_;
    std::size_t maxBytes_;
    std::size_t maxFiles_;
    std::size_t size_ = 0;
    std::ofstream out_;
};

} // namespace

struct SpdLogger::Impl
{
    std::shared_ptr<::spdlog::logger> logger;
};

SpdLogger::SpdLogger(const fs::path& file, std::size_t maxBytes, std::size_t maxFiles,
                     ports::LogLevel minimum)
    : impl_(std::make_unique<Impl>())
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // Eigener Logger ohne globales Register (keine Singletons, Architekturregel 4)
    auto sink = std::make_shared<RotatingSink>(file, maxBytes, std::max<std::size_t>(1, maxFiles));
    impl_->logger = std::make_shared<::spdlog::logger>("autoneg", std::move(sink));
    impl_->logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%t] %v");
    impl_->logger->set_level(levelOf(minimum));
    impl_->logger->flush_on(::spdlog::level::warn);
}

SpdLogger::~SpdLogger()
{
    impl_->logger->flush();
}

void SpdLogger::log(ports::LogLevel l, std::string_view msg)
{
    impl_->logger->log(levelOf(l), "{}", msg);
}

void SpdLogger::flush()
{
    impl_->logger->flush();
}

} // namespace autoneg::adapter::spdlog
