#include "autoneg/application/roll_service.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <string_view>

namespace autoneg::application {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

ports::SourceStamp stampOf(const fs::path& p, const model::FrameId& hash)
{
    std::error_code ec;
    ports::SourceStamp s;
    s.file = p.filename().string();
    s.size = fs::file_size(p, ec);
    const auto t = fs::last_write_time(p, ec);
    if (!ec) {
        const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
        s.mtime = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    }
    s.hash = hash;
    return s;
}

model::EditList withRollDefaults(const model::EditList& defaults)
{
    model::EditList l;
    for (model::EditEntry e : defaults.entries()) {
        e.origin = model::Origin::RollDefault;
        l = l.with(std::move(e));
    }
    return l;
}

} // namespace

bool RollService::isScanFile(const fs::path& p)
{
    // RAW-Formate aus LF-IMP-01 sowie TIFF und JPEG (LF-IMP-05)
    static constexpr std::array<std::string_view, 21> exts{
        ".arw", ".srf", ".sr2", ".cr2", ".cr3", ".nef", ".nrw",  ".raf", ".rw2",  ".orf", ".pef",
        ".dng", ".rwl", ".3fr", ".fff", ".iiq", ".tif", ".tiff", ".jpg", ".jpeg", ".x3f"};
    const std::string e = lower(p.extension().string());
    return std::ranges::find(exts, e) != exts.end();
}

std::vector<fs::path> RollService::scanFiles(const fs::path& folder)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        if (e.is_regular_file(ec) && isScanFile(e.path())) {
            out.push_back(e.path());
        }
    }
    std::ranges::sort(out, {}, [](const fs::path& p) { return p.filename().string(); });
    return out;
}

base::Expected<OpenedRoll> RollService::open(const fs::path& folder, const image::CancelToken& cancel,
                                             const std::function<void(int, int)>& progress)
{
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return base::makeError(base::ErrorCode::NotFound, "Ordner nicht gefunden: " + folder.string());
    }
    OpenedRoll out;
    const auto locked = rolls_.lock(folder);
    out.readOnly = !locked || !*locked;
    auto stored = rolls_.load(folder);
    if (stored && stored->has_value()) {
        out.roll = **stored;
    }
    out.roll.folder = folder;
    out.roll.id = model::RollId{fs::weakly_canonical(folder, ec).generic_string()};
    if (out.roll.name.empty()) {
        out.roll.name = folder.filename().string();
    }
    out.roll.frames.clear();

    const auto files = scanFiles(folder);
    const int total = static_cast<int>(files.size());
    for (int i = 0; i < total; ++i) {
        if (cancel.cancelled()) {
            return base::makeError(base::ErrorCode::Cancelled, "Öffnen abgebrochen");
        }
        const fs::path& p = files[static_cast<std::size_t>(i)];
        OpenedFrame of;
        of.frame.path = p;
        of.frame.roll = out.roll.id;
        of.frame.frameNo = i + 1;
        auto hash = hasher_.hashFile(p);
        if (!hash) {
            of.problem = hash.error();
        } else {
            of.frame.id = *hash;
        }
        of.data.source = stampOf(p, of.frame.id);
        of.data.frameNo = of.frame.frameNo;
        if (hash) {
            auto loaded = edits_.load(of.frame);
            if (!loaded) {
                of.problem = loaded.error();
            } else if (loaded->has_value()) {
                of.data = **loaded;
                of.stored = true;
                of.frame.rating = of.data.rating;
                if (of.data.frameNo > 0) {
                    of.frame.frameNo = of.data.frameNo;
                }
            }
        }
        if (!of.stored) {
            of.data.edits = withRollDefaults(out.roll.defaults);
        }
        out.roll.frames.push_back(of.frame.id);
        out.frames.push_back(std::move(of));
        if (progress) {
            progress(i + 1, total);
        }
    }

    // Verwaiste Begleitdateien umbenannter Scans zuordnen (LF-DAT-06)
    for (const ports::SidecarRef& orphan : edits_.orphans(folder)) {
        const auto it = std::ranges::find_if(out.frames, [&](const OpenedFrame& f) {
            return f.frame.id == orphan.source.hash && !f.problem && !f.stored;
        });
        if (it == out.frames.end() || out.readOnly || !edits_.relink(orphan, it->frame)) {
            out.orphans.push_back(orphan);
            continue;
        }
        if (auto loaded = edits_.load(it->frame); loaded && loaded->has_value()) {
            it->data = **loaded;
            it->stored = true;
            it->frame.rating = it->data.rating;
        }
    }
    return out;
}

JobHandle RollService::openFolder(fs::path folder, std::function<void(base::Expected<OpenedRoll>)> done)
{
    class OpenJob final : public Job
    {
    public:
        OpenJob(RollService& svc, fs::path folder, std::function<void(base::Expected<OpenedRoll>)> done)
            : svc_(svc)
            , folder_(std::move(folder))
            , done_(std::move(done))
        {}
        std::string name() const override { return "Rolle öffnen"; }
        base::Expected<void> run(JobContext& c) override
        {
            auto r = svc_.open(folder_, c.cancel, c.progress);
            base::Expected<void> status = r ? base::Expected<void>{} : std::unexpected(r.error());
            if (done_) {
                done_(std::move(r));
            }
            return status;
        }

    private:
        RollService& svc_;
        fs::path folder_;
        std::function<void(base::Expected<OpenedRoll>)> done_;
    };
    return jobs_.submit(std::make_unique<OpenJob>(*this, std::move(folder), std::move(done)),
                        ports::Priority::Interactive);
}

} // namespace autoneg::application
