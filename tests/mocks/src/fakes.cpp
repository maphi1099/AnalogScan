#include "autoneg/mocks/fakes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace autoneg::mocks {

namespace {

base::Expected<void> checkFile(const std::filesystem::path& p)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        return base::makeError(base::ErrorCode::NotFound, p.filename().string() + ": Datei nicht gefunden");
    }
    if (std::filesystem::file_size(p, ec) == 0 || ec) {
        return base::makeError(base::ErrorCode::Corrupt, p.filename().string() + ": Datei ist leer");
    }
    return {};
}

base::Expected<image::ImageBuffer> readPfm(const std::filesystem::path& p)
{
    if (auto ok = checkFile(p); !ok) {
        return std::unexpected(ok.error());
    }
    std::ifstream in(p, std::ios::binary);
    std::string magic;
    int w = 0;
    int h = 0;
    double scale = 0.0;
    in >> magic >> w >> h >> scale;
    in.get();
    if (!in || magic != "PF" || w <= 0 || h <= 0 || scale >= 0.0) {
        return base::makeError(base::ErrorCode::Unsupported, p.filename().string() + ": kein PFM");
    }
    const std::size_t row = static_cast<std::size_t>(w) * 3;
    std::vector<float> v(row * static_cast<std::size_t>(h));
    for (int y = h - 1; y >= 0; --y) {
        in.read(reinterpret_cast<char*>(v.data() + static_cast<std::size_t>(y) * row),
                static_cast<std::streamsize>(row * sizeof(float)));
    }
    if (!in) {
        return base::makeError(base::ErrorCode::Corrupt, p.filename().string() + ": unvollständig");
    }
    return image::ImageBuffer::fromFloats(
        image::ImageSpec{w, h, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, v);
}

image::ImageBuffer halve(const image::ImageBuffer& in)
{
    const auto& s = in.spec();
    const int w = (s.width + 1) / 2;
    const int h = (s.height + 1) / 2;
    const auto src = in.host();
    std::vector<float> v(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                float sum = 0.0F;
                int n = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = 2 * x + dx;
                        const int sy = 2 * y + dy;
                        if (sx < s.width && sy < s.height) {
                            sum += src[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(s.width) +
                                        static_cast<std::size_t>(sx)) *
                                           3 +
                                       static_cast<std::size_t>(c)];
                            ++n;
                        }
                    }
                }
                v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) *
                      3 +
                  static_cast<std::size_t>(c)] = sum / static_cast<float>(n);
            }
        }
    }
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{w, h, 3, image::PixelFormat::F32, image::ColorSpace::WorkingLinear}, v)
        .value();
}

} // namespace

bool writePfm(const std::filesystem::path& p, const image::ImageBuffer& img)
{
    const auto& s = img.spec();
    std::ofstream out(p, std::ios::binary);
    out << "PF\n" << s.width << " " << s.height << "\n-1\n";
    const auto v = img.host();
    const std::size_t row = static_cast<std::size_t>(s.width) * 3;
    for (int y = s.height - 1; y >= 0; --y) {
        out.write(reinterpret_cast<const char*>(v.data() + static_cast<std::size_t>(y) * row),
                  static_cast<std::streamsize>(row * sizeof(float)));
    }
    return static_cast<bool>(out);
}

base::Expected<model::SourceInfo> FakeRawDecoder::probe(const std::filesystem::path& p)
{
    auto img = readPfm(p);
    if (!img) {
        return std::unexpected(img.error());
    }
    return model::SourceInfo{img->spec().width, img->spec().height, "Fake", "Sensor",
                             model::SourceFormat::Raw};
}

base::Expected<image::ImageBuffer> FakeRawDecoder::decode(const std::filesystem::path& p,
                                                          const ports::DecodeOptions& o)
{
    auto img = readPfm(p);
    if (!img || !o.half) {
        return img;
    }
    return halve(*img);
}

base::Expected<image::ImageBuffer> FakeRawDecoder::preview(const std::filesystem::path& p)
{
    ports::DecodeOptions o;
    o.half = true;
    return decode(p, o);
}

// --- FakeImageWriter -----------------------------------------------------------------------

namespace {

std::atomic<unsigned> fakeTemp{0};

base::Expected<void> validate(const image::ImageBuffer& img, const ports::WriteOptions& o,
                              std::span<const ports::FileFormat> formats)
{
    const auto& s = img.spec();
    if (img.empty() || s.format != image::PixelFormat::F32 || img.host().empty()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Export erwartet F32 im Hauptspeicher");
    }
    if (s.channels != 1 && s.channels != 3) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Export erwartet 1 oder 3 Kanäle");
    }
    if (std::ranges::find(formats, o.format) == formats.end()) {
        return base::makeError(base::ErrorCode::Unsupported, "Format wird nicht unterstützt");
    }
    const int b = o.bitDepth;
    const bool ok = (o.format == ports::FileFormat::Tiff && (b == 8 || b == 16 || b == 32)) ||
                    (o.format == ports::FileFormat::Png && (b == 8 || b == 16)) ||
                    (o.format == ports::FileFormat::Jpeg && b == 8);
    if (!ok) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Bittiefe wird nicht unterstützt");
    }
    return {};
}

base::Bytes serialize(const image::ImageBuffer& img, const ports::WriteOptions& o)
{
    std::ostringstream head;
    head << "AUTONEG-FAKE " << static_cast<int>(o.format) << " " << o.bitDepth << " " << img.spec().width
         << " " << img.spec().height << " " << img.spec().channels << "\n";
    const std::string h = head.str();
    base::Bytes out(h.begin(), h.end());
    const auto v = img.host();
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(v.data());
    out.insert(out.end(), bytes, bytes + v.size_bytes());
    return out;
}

} // namespace

std::span<const ports::FileFormat> FakeImageWriter::formats() const
{
    return formats_;
}

base::Expected<void> FakeImageWriter::write(const std::filesystem::path& p, const image::ImageBuffer& img,
                                            const ports::WriteOptions& o)
{
    if (auto ok = validate(img, o, formats_); !ok) {
        return ok;
    }
    std::filesystem::path tmp = p;
    tmp += ".fake-" + std::to_string(fakeTemp.fetch_add(1)) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) {
            return base::makeError(base::ErrorCode::Io, p.filename().string() + ": Datei nicht anlegbar");
        }
        const base::Bytes data = serialize(img, o);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return base::makeError(base::ErrorCode::Io, p.filename().string() + ": Schreiben fehlgeschlagen");
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return base::makeError(base::ErrorCode::Io, p.filename().string() + ": Umbenennen fehlgeschlagen");
    }
    return {};
}

base::Expected<base::Bytes> FakeImageWriter::encode(const image::ImageBuffer& img,
                                                    const ports::WriteOptions& o)
{
    if (auto ok = validate(img, o, formats_); !ok) {
        return std::unexpected(ok.error());
    }
    return serialize(img, o);
}

// --- FakeColorEngine -----------------------------------------------------------------------

namespace {

using Mat = std::array<double, 9>;
using Vec = std::array<double, 3>;

Mat mul(const Mat& a, const Mat& b)
{
    Mat r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                r[static_cast<std::size_t>(i * 3 + j)] +=
                    a[static_cast<std::size_t>(i * 3 + k)] * b[static_cast<std::size_t>(k * 3 + j)];
            }
        }
    }
    return r;
}

Vec applyMat(const Mat& m, const Vec& v)
{
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Mat inverse(const Mat& m)
{
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    return {(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det,
            (m[1] * m[5] - m[2] * m[4]) / det, (m[5] * m[6] - m[3] * m[8]) / det,
            (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
            (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det,
            (m[0] * m[4] - m[1] * m[3]) / det};
}

Vec xyzOf(double x, double y)
{
    return {x / y, 1.0, (1.0 - x - y) / y};
}

/// RGB -> XYZ (D50, Bradford-adaptiert) aus Primärfarben und Weißpunkt.
Mat toPcs(const std::array<double, 6>& xy, double wx, double wy)
{
    const Vec r = xyzOf(xy[0], xy[1]);
    const Vec g = xyzOf(xy[2], xy[3]);
    const Vec b = xyzOf(xy[4], xy[5]);
    const Mat p{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    const Vec s = applyMat(inverse(p), xyzOf(wx, wy));
    const Mat m{p[0] * s[0], p[1] * s[1], p[2] * s[2], p[3] * s[0], p[4] * s[1],
                p[5] * s[2], p[6] * s[0], p[7] * s[1], p[8] * s[2]};
    const Mat bradford{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    const Vec src = applyMat(bradford, xyzOf(wx, wy));
    const Vec dst = applyMat(bradford, {0.9642, 1.0, 0.8249});
    const Mat scale{dst[0] / src[0], 0, 0, 0, dst[1] / src[1], 0, 0, 0, dst[2] / src[2]};
    return mul(mul(inverse(bradford), mul(scale, bradford)), m);
}

enum class Trc
{
    Linear,
    Srgb,
    Gamma
};

struct FakeSpace
{
    int channels = 3;
    Mat toXyz{};
    Trc trc = Trc::Linear;
    double gamma = 1.0;
};

FakeSpace spaceOf(ports::ProfileId id)
{
    switch (id) {
    case ports::ProfileId::WorkingRec2020Linear:
        return {3, toPcs({0.708, 0.292, 0.170, 0.797, 0.131, 0.046}, 0.3127, 0.3290), Trc::Linear, 1.0};
    case ports::ProfileId::Srgb:
        return {3, toPcs({0.64, 0.33, 0.30, 0.60, 0.15, 0.06}, 0.3127, 0.3290), Trc::Srgb, 1.0};
    case ports::ProfileId::AdobeRgb:
        return {3, toPcs({0.64, 0.33, 0.21, 0.71, 0.15, 0.06}, 0.3127, 0.3290), Trc::Gamma, 563.0 / 256.0};
    case ports::ProfileId::ProPhoto:
        return {3, toPcs({0.7347, 0.2653, 0.1596, 0.8404, 0.0366, 0.0001}, 0.3457, 0.3585), Trc::Gamma, 1.8};
    case ports::ProfileId::GrayGamma22:
        return {1, {}, Trc::Gamma, 2.2};
    }
    return {};
}

double decodeTrc(const FakeSpace& s, double v)
{
    switch (s.trc) {
    case Trc::Linear:
        return v;
    case Trc::Srgb:
        return std::copysign(
            std::abs(v) <= 0.04045 ? std::abs(v) / 12.92 : std::pow((std::abs(v) + 0.055) / 1.055, 2.4), v);
    case Trc::Gamma:
        return std::copysign(std::pow(std::abs(v), s.gamma), v);
    }
    return v;
}

double encodeTrc(const FakeSpace& s, double v)
{
    switch (s.trc) {
    case Trc::Linear:
        return v;
    case Trc::Srgb:
        return std::copysign(std::abs(v) <= 0.0031308 ? std::abs(v) * 12.92
                                                      : 1.055 * std::pow(std::abs(v), 1 / 2.4) - 0.055,
                             v);
    case Trc::Gamma:
        return std::copysign(std::pow(std::abs(v), 1.0 / s.gamma), v);
    }
    return v;
}

constexpr std::string_view fakeMagic = "AUTONEG-FAKE-ICC:";

base::Expected<FakeSpace> parse(const ports::IccProfile& p)
{
    const std::string s(p.bytes.begin(), p.bytes.end());
    if (!s.starts_with(fakeMagic)) {
        return base::makeError(base::ErrorCode::Corrupt, "ICC-Profil nicht lesbar: " + p.name);
    }
    const int id = std::atoi(s.c_str() + fakeMagic.size());
    if (id < 0 || id > static_cast<int>(ports::ProfileId::GrayGamma22)) {
        return base::makeError(base::ErrorCode::Corrupt, "ICC-Profil nicht lesbar: " + p.name);
    }
    return spaceOf(static_cast<ports::ProfileId>(id));
}

class FakeTransform final : public ports::IColorTransform
{
public:
    FakeTransform(FakeSpace src, FakeSpace dst)
        : src_(src)
        , dst_(dst)
        , fromXyz_(inverse(dst.toXyz))
    {}

    [[nodiscard]] base::Expected<image::ImageBuffer> apply(const image::ImageBuffer& in) const override
    {
        const auto& s = in.spec();
        if (in.empty() || s.format != image::PixelFormat::F32 || in.host().empty() ||
            (s.channels != src_.channels && !(src_.channels == 3 && s.channels == 1))) {
            return base::makeError(base::ErrorCode::InvalidArgument, "Bild passt nicht zum Quellprofil");
        }
        const auto v = in.host();
        const std::size_t n = static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height);
        std::vector<float> out(n * static_cast<std::size_t>(dst_.channels));
        for (std::size_t i = 0; i < n; ++i) {
            Vec rgb{};
            for (std::size_t c = 0; c < 3; ++c) {
                rgb[c] = v[i * static_cast<std::size_t>(s.channels) + (s.channels == 1 ? 0 : c)];
            }
            const Vec o = convert(rgb);
            for (std::size_t c = 0; c < static_cast<std::size_t>(dst_.channels); ++c) {
                out[i * static_cast<std::size_t>(dst_.channels) + c] = static_cast<float>(o[c]);
            }
        }
        image::ImageSpec spec = s;
        spec.channels = dst_.channels;
        spec.space = image::ColorSpace::Output;
        return image::ImageBuffer::fromFloats(spec, out);
    }

    [[nodiscard]] image::Lut3D lut3d(int n) const override
    {
        image::Lut3D lut;
        if (n < 2) {
            return lut;
        }
        lut.size = n;
        for (int b = 0; b < n; ++b) {
            for (int g = 0; g < n; ++g) {
                for (int r = 0; r < n; ++r) {
                    const double k = 1.0 / (n - 1);
                    const Vec o = convert({r * k, g * k, b * k});
                    for (int c = 0; c < 3; ++c) {
                        lut.data.push_back(
                            static_cast<float>(dst_.channels == 1 ? o[0] : o[static_cast<std::size_t>(c)]));
                    }
                }
            }
        }
        return lut;
    }

private:
    Vec convert(const Vec& in) const
    {
        Vec xyz{};
        if (src_.channels == 1) {
            const double y = decodeTrc(src_, in[0]);
            xyz = {0.9642 * y, y, 0.8249 * y};
        } else {
            xyz = applyMat(src_.toXyz,
                           {decodeTrc(src_, in[0]), decodeTrc(src_, in[1]), decodeTrc(src_, in[2])});
        }
        if (dst_.channels == 1) {
            const double y = encodeTrc(dst_, xyz[1]);
            return {y, y, y};
        }
        const Vec lin = applyMat(fromXyz_, xyz);
        return {encodeTrc(dst_, lin[0]), encodeTrc(dst_, lin[1]), encodeTrc(dst_, lin[2])};
    }

    FakeSpace src_;
    FakeSpace dst_;
    Mat fromXyz_;
};

} // namespace

base::Expected<ports::IccProfile> FakeColorEngine::profile(ports::ProfileId id)
{
    const std::string s = std::string(fakeMagic) + std::to_string(static_cast<int>(id));
    return ports::IccProfile{base::Bytes(s.begin(), s.end()), "Fake " + std::to_string(static_cast<int>(id))};
}

base::Expected<ports::IccProfile> FakeColorEngine::display(int /*screen*/)
{
    return base::makeError(base::ErrorCode::Unsupported, "kein Bildschirmprofil");
}

base::Expected<std::shared_ptr<const ports::IColorTransform>>
FakeColorEngine::transform(const ports::IccProfile& src, const ports::IccProfile& dst,
                           ports::Intent /*intent*/)
{
    const auto s = parse(src);
    const auto d = parse(dst);
    if (!s || !d) {
        return std::unexpected(!s ? s.error() : d.error());
    }
    return std::shared_ptr<const ports::IColorTransform>(std::make_shared<FakeTransform>(*s, *d));
}

} // namespace autoneg::mocks
