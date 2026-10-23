#include "autoneg/adapter/lcms/lcms_color_engine.hpp"

#include <lcms2.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

namespace autoneg::adapter::lcms {

namespace {

using ports::IccProfile;
using ports::ProfileId;

/// Little CMS beschreibt in cmsD50_xyY einen statischen Puffer (Schwarzpunktkompensation beim
/// Anlegen von Transformationen, Graustufenprofil). Der Puffer ist prozessweit, unabhängig vom
/// Kontext; deshalb serialisiert diese Sperre alle Aufrufe, die ihn berühren (B-35). Angelegt
/// wird selten, angewendet wird ohne Sperre.
std::mutex& lcmsGlobalsMutex()
{
    static std::mutex m;
    return m;
}

/// Besitz eines Little-CMS-Profils.
struct ProfileHandle
{
    cmsHPROFILE h = nullptr;
    ProfileHandle() = default;
    explicit ProfileHandle(cmsHPROFILE p)
        : h(p)
    {}
    ~ProfileHandle()
    {
        if (h != nullptr) {
            cmsCloseProfile(h);
        }
    }
    ProfileHandle(const ProfileHandle&) = delete;
    ProfileHandle& operator=(const ProfileHandle&) = delete;
    ProfileHandle(ProfileHandle&& o) noexcept
        : h(o.h)
    {
        o.h = nullptr;
    }
    ProfileHandle& operator=(ProfileHandle&&) = delete;
};

void setDescription(cmsContext ctx, cmsHPROFILE p, const std::string& name)
{
    cmsMLU* mlu = cmsMLUalloc(ctx, 1);
    cmsMLUsetASCII(mlu, "en", "US", name.c_str());
    cmsWriteTag(p, cmsSigProfileDescriptionTag, mlu);
    cmsMLUfree(mlu);
}

base::Expected<IccProfile> toBytes(cmsContext ctx, cmsHPROFILE p, const std::string& name)
{
    if (p == nullptr) {
        return base::makeError(base::ErrorCode::Internal, "Profil " + name + " konnte nicht erzeugt werden");
    }
    setDescription(ctx, p, name);
    cmsUInt32Number size = 0;
    if (cmsSaveProfileToMem(p, nullptr, &size) == 0) {
        return base::makeError(base::ErrorCode::Internal, "Profil " + name + " nicht speicherbar");
    }
    IccProfile out;
    out.name = name;
    out.bytes.resize(size);
    if (cmsSaveProfileToMem(p, out.bytes.data(), &size) == 0) {
        return base::makeError(base::ErrorCode::Internal, "Profil " + name + " nicht speicherbar");
    }
    out.bytes.resize(size);
    return out;
}

/// RGB-Matrix-Profil aus Primärfarben, Weißpunkt und Gamma (1 = linear).
cmsHPROFILE rgbProfile(cmsContext ctx, cmsCIExyY white, const cmsCIExyYTRIPLE& primaries, double gamma)
{
    cmsToneCurve* curve = cmsBuildGamma(ctx, gamma);
    cmsToneCurve* curves[3] = {curve, curve, curve};
    cmsHPROFILE p = cmsCreateRGBProfileTHR(ctx, &white, &primaries, curves);
    cmsFreeToneCurve(curve);
    return p;
}

constexpr cmsCIExyY d65{0.3127, 0.3290, 1.0};
constexpr cmsCIExyY d50{0.3457, 0.3585, 1.0};

cmsHPROFILE build(cmsContext ctx, ProfileId id)
{
    switch (id) {
    case ProfileId::WorkingRec2020Linear:
        return rgbProfile(ctx, d65, {{0.708, 0.292, 1.0}, {0.170, 0.797, 1.0}, {0.131, 0.046, 1.0}}, 1.0);
    case ProfileId::Srgb:
        return cmsCreate_sRGBProfileTHR(ctx);
    case ProfileId::AdobeRgb: // Adobe RGB (1998): Gamma 563/256
        return rgbProfile(ctx, d65, {{0.64, 0.33, 1.0}, {0.21, 0.71, 1.0}, {0.15, 0.06, 1.0}}, 563.0 / 256.0);
    case ProfileId::ProPhoto: // ROMM RGB, Weißpunkt D50, Gamma 1,8
        return rgbProfile(ctx, d50, {{0.7347, 0.2653, 1.0}, {0.1596, 0.8404, 1.0}, {0.0366, 0.0001, 1.0}},
                          1.8);
    case ProfileId::GrayGamma22: {
        cmsToneCurve* curve = cmsBuildGamma(ctx, 2.2);
        cmsHPROFILE p = cmsCreateGrayProfileTHR(ctx, cmsD50_xyY(), curve);
        cmsFreeToneCurve(curve);
        return p;
    }
    }
    return nullptr;
}

const char* nameOf(ProfileId id)
{
    switch (id) {
    case ProfileId::WorkingRec2020Linear:
        return "AutoNeg Rec. 2020 linear";
    case ProfileId::Srgb:
        return "sRGB IEC61966-2.1";
    case ProfileId::AdobeRgb:
        return "Adobe RGB (1998) kompatibel";
    case ProfileId::ProPhoto:
        return "ProPhoto RGB (ROMM) kompatibel";
    case ProfileId::GrayGamma22:
        return "Graustufen Gamma 2,2";
    }
    return "?";
}

cmsUInt32Number intentOf(ports::Intent i)
{
    switch (i) {
    case ports::Intent::Perceptual:
        return INTENT_PERCEPTUAL;
    case ports::Intent::RelativeColorimetric:
        return INTENT_RELATIVE_COLORIMETRIC;
    case ports::Intent::Saturation:
        return INTENT_SATURATION;
    case ports::Intent::AbsoluteColorimetric:
        return INTENT_ABSOLUTE_COLORIMETRIC;
    }
    return INTENT_PERCEPTUAL;
}

// --- Matrix-Kurven-Form (D-18) -----------------------------------------------------------

using Mat3 = std::array<double, 9>;

/// Spalten = Primärfarben (XYZ, D50) eines RGB-Matrixprofils.
std::optional<Mat3> colorants(cmsHPROFILE p)
{
    const auto* r = static_cast<const cmsCIEXYZ*>(cmsReadTag(p, cmsSigRedColorantTag));
    const auto* g = static_cast<const cmsCIEXYZ*>(cmsReadTag(p, cmsSigGreenColorantTag));
    const auto* b = static_cast<const cmsCIEXYZ*>(cmsReadTag(p, cmsSigBlueColorantTag));
    if (r == nullptr || g == nullptr || b == nullptr) {
        return std::nullopt;
    }
    return Mat3{r->X, g->X, b->X, r->Y, g->Y, b->Y, r->Z, g->Z, b->Z};
}

std::optional<Mat3> inverse(const Mat3& m)
{
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12) {
        return std::nullopt;
    }
    const double k = 1.0 / det;
    return Mat3{(m[4] * m[8] - m[5] * m[7]) * k, (m[2] * m[7] - m[1] * m[8]) * k, (m[1] * m[5] - m[2] * m[4]) * k,
                (m[5] * m[6] - m[3] * m[8]) * k, (m[0] * m[8] - m[2] * m[6]) * k, (m[2] * m[3] - m[0] * m[5]) * k,
                (m[3] * m[7] - m[4] * m[6]) * k, (m[1] * m[6] - m[0] * m[7]) * k, (m[0] * m[4] - m[1] * m[3]) * k};
}

Mat3 multiply(const Mat3& a, const Mat3& b)
{
    Mat3 r{};
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

const cmsToneCurve* curve(cmsHPROFILE p, cmsTagSignature sig)
{
    return static_cast<const cmsToneCurve*>(cmsReadTag(p, sig));
}

/// Tonkurve als Parameterkurve aus dem rohen ICC-Tag: Typ nach Little-CMS-Zählung (1 = Gamma,
/// 4 = IEC 61966-2.1) und Parameter. 'para' (ICC 10.18) und 'curv' mit 0 oder 1 Eintrag (Gamma);
/// Tabellenkurven: nullopt. Little CMS stellt die Parameter nicht öffentlich bereit.
struct Parametric
{
    int type = 0;
    std::array<double, 7> q{};
    friend bool operator==(const Parametric&, const Parametric&) = default;
};

std::optional<Parametric> parametric(cmsHPROFILE p, cmsTagSignature sig)
{
    const cmsUInt32Number size = cmsReadRawTag(p, sig, nullptr, 0);
    if (size < 12 || size > 256) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> b(size);
    if (cmsReadRawTag(p, sig, b.data(), size) != size) {
        return std::nullopt;
    }
    const auto u16 = [&](std::size_t i) { return static_cast<unsigned>(b[i] << 8U | b[i + 1]); };
    const auto u32 = [&](std::size_t i) {
        return static_cast<std::uint32_t>(b[i]) << 24U | static_cast<std::uint32_t>(b[i + 1]) << 16U |
               static_cast<std::uint32_t>(b[i + 2]) << 8U | b[i + 3];
    };
    const auto s15 = [&](std::size_t i) { return static_cast<std::int32_t>(u32(i)) / 65536.0; };
    Parametric r;
    if (std::memcmp(b.data(), "curv", 4) == 0) {
        const std::uint32_t n = u32(8);
        if (n == 0) {
            r.type = 1;
            r.q[0] = 1.0;
            return r;
        }
        if (n == 1 && size >= 14) {
            r.type = 1;
            r.q[0] = u16(12) / 256.0; // u8Fixed8
            return r;
        }
        return std::nullopt;
    }
    if (std::memcmp(b.data(), "para", 4) != 0) {
        return std::nullopt;
    }
    static constexpr std::array<std::size_t, 5> count{1, 3, 4, 5, 7};
    const unsigned fn = u16(8);
    if (fn >= count.size() || size < 12 + 4 * count[fn]) {
        return std::nullopt;
    }
    r.type = static_cast<int>(fn) + 1;
    for (std::size_t i = 0; i < count[fn]; ++i) {
        r.q[i] = s15(12 + 4 * i);
    }
    return r;
}

/// Kodierkurve (linear → Gerät) als Umkehrung einer Parameterkurve Typ 1 (Gamma) oder Typ 4
/// (sRGB); andere Kurven: keine geschlossene Form.
bool encoding(const std::optional<Parametric>& c, ports::MatrixShaper& ms)
{
    if (!c) {
        return false;
    }
    const int type = c->type;
    const auto& q = c->q;
    if (type == 1 && q[0] > 0.0) { // lin = dev^g
        ms.t = 0.0F;
        ms.p = static_cast<float>(1.0 / q[0]);
        ms.k1 = 1.0F;
        ms.k0 = 0.0F;
        ms.s = q[0] == 1.0 ? 1.0F : 0.0F; // Little CMS: negative Werte nur bei Gamma 1 erhalten
        return true;
    }
    if (type == 4 && q[0] > 0.0 && q[1] != 0.0 && q[3] != 0.0) { // lin = (a·dev + b)^g bzw. c·dev
        ms.t = static_cast<float>(q[3] * q[4]);
        ms.p = static_cast<float>(1.0 / q[0]);
        ms.k1 = static_cast<float>(1.0 / q[1]);
        ms.k0 = static_cast<float>(-q[2] / q[1]);
        ms.s = static_cast<float>(1.0 / q[3]);
        return true;
    }
    return false;
}

/// Matrix-Kurven-Form von src (linear, RGB) nach dst (RGB oder Grau); nullopt, wenn eines der
/// Profile nicht passt.
std::optional<ports::MatrixShaper> matrixShaperOf(cmsHPROFILE src, cmsHPROFILE dst)
{
    if (cmsIsMatrixShaper(src) == 0 || cmsIsMatrixShaper(dst) == 0 || cmsGetColorSpace(src) != cmsSigRgbData) {
        return std::nullopt;
    }
    for (const cmsTagSignature t : {cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag}) {
        const cmsToneCurve* c = curve(src, t);
        if (c == nullptr || cmsIsToneCurveLinear(c) == 0) {
            return std::nullopt;
        }
    }
    const auto ms = colorants(src);
    if (!ms) {
        return std::nullopt;
    }
    ports::MatrixShaper out;
    if (cmsGetColorSpace(dst) == cmsSigGrayData) {
        if (!encoding(parametric(dst, cmsSigGrayTRCTag), out)) {
            return std::nullopt;
        }
        out.outChannels = 1;
        out.matrix = {static_cast<float>((*ms)[3]), static_cast<float>((*ms)[4]), static_cast<float>((*ms)[5]),
                      0, 0, 0, 0, 0, 0}; // Helligkeit Y
        return out;
    }
    const auto md = colorants(dst);
    const auto inv = md ? inverse(*md) : std::nullopt;
    const auto r = parametric(dst, cmsSigRedTRCTag);
    if (!inv || !r || r != parametric(dst, cmsSigGreenTRCTag) || r != parametric(dst, cmsSigBlueTRCTag) ||
        !encoding(r, out)) {
        return std::nullopt;
    }
    const Mat3 m = multiply(*inv, *ms);
    for (std::size_t i = 0; i < 9; ++i) {
        out.matrix[i] = static_cast<float>(m[i]);
    }
    out.outChannels = 3;
    return out;
}

/// Stichprobe gegen Little CMS selbst: die geschlossene Form gilt nur, wenn sie überall
/// innerhalb von 1e-5 liegt (unter einer 16-Bit-Stufe).
bool agrees(cmsHTRANSFORM h, const ports::MatrixShaper& ms)
{
    std::vector<float> in;
    const std::array<float, 9> levels{-0.05F, 0.0F, 0.001F, 0.003F, 0.02F, 0.18F, 0.5F, 0.9F, 1.0F};
    for (const float r : levels) {
        for (const float g : levels) {
            for (const float b : levels) {
                in.insert(in.end(), {r, g, b});
            }
        }
    }
    const std::size_t n = in.size() / 3;
    std::vector<float> out(n * static_cast<std::size_t>(ms.outChannels));
    cmsDoTransform(h, in.data(), out.data(), static_cast<cmsUInt32Number>(n));
    for (std::size_t i = 0; i < n; ++i) {
        for (int c = 0; c < ms.outChannels; ++c) {
            const auto row = static_cast<std::size_t>(c) * 3;
            const float lin = ms.matrix[row] * in[i * 3] + ms.matrix[row + 1] * in[i * 3 + 1] +
                              ms.matrix[row + 2] * in[i * 3 + 2];
            const float want = out[i * static_cast<std::size_t>(ms.outChannels) + static_cast<std::size_t>(c)];
            if (!(std::abs(ms.encode(lin) - want) <= 1e-5F)) {
                return false;
            }
        }
    }
    return true;
}

/// Fertige Transformation; cmsDoTransform ist ohne Zwischenspeicher threadsicher.
class LcmsTransform final : public ports::IColorTransform
{
public:
    LcmsTransform(cmsHTRANSFORM h, int inChannels, int outChannels,
                  std::optional<ports::MatrixShaper> shaper = std::nullopt)
        : h_(h)
        , in_(inChannels)
        , out_(outChannels)
        , shaper_(shaper)
    {}

    [[nodiscard]] std::optional<ports::MatrixShaper> matrixShaper() const override { return shaper_; }
    ~LcmsTransform() override { cmsDeleteTransform(h_); }
    LcmsTransform(const LcmsTransform&) = delete;
    LcmsTransform& operator=(const LcmsTransform&) = delete;
    LcmsTransform(LcmsTransform&&) = delete;
    LcmsTransform& operator=(LcmsTransform&&) = delete;

    [[nodiscard]] base::Expected<image::ImageBuffer> apply(const image::ImageBuffer& in) const override
    {
        const image::ImageSpec& s = in.spec();
        if (in.empty() || s.format != image::PixelFormat::F32 || in.host().empty()) {
            return base::makeError(base::ErrorCode::InvalidArgument,
                                   "Farbtransformation erwartet F32 im Hauptspeicher");
        }
        if (s.channels != in_ && !(in_ == 3 && s.channels == 1)) {
            return base::makeError(base::ErrorCode::InvalidArgument,
                                   "Kanalzahl des Bilds passt nicht zum Quellprofil");
        }
        const auto src = in.host();
        const auto width = static_cast<std::size_t>(s.width);
        std::vector<float> row(width * static_cast<std::size_t>(in_));
        std::vector<float> out(width * static_cast<std::size_t>(s.height) * static_cast<std::size_t>(out_));
        for (int y = 0; y < s.height; ++y) {
            const float* p =
                src.data() + static_cast<std::size_t>(y) * width * static_cast<std::size_t>(s.channels);
            const float* rowIn = p;
            if (s.channels != in_) { // Graubild in ein RGB-Profil: Kanal verdreifachen
                for (std::size_t x = 0; x < width; ++x) {
                    row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = p[x];
                }
                rowIn = row.data();
            }
            cmsDoTransform(h_, rowIn,
                           out.data() + static_cast<std::size_t>(y) * width * static_cast<std::size_t>(out_),
                           static_cast<cmsUInt32Number>(width));
        }
        image::ImageSpec spec = s;
        spec.channels = out_;
        spec.space = image::ColorSpace::Output;
        return image::ImageBuffer::fromFloats(spec, out);
    }

    [[nodiscard]] image::Lut3D lut3d(int n) const override
    {
        image::Lut3D lut;
        if (n < 2 || in_ != 3) {
            return lut;
        }
        lut.size = n;
        const auto count =
            static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
        std::vector<float> grid(count * 3);
        std::size_t i = 0;
        const float step = 1.0F / static_cast<float>(n - 1);
        for (int b = 0; b < n; ++b) {
            for (int g = 0; g < n; ++g) {
                for (int r = 0; r < n; ++r) {
                    grid[i++] = static_cast<float>(r) * step;
                    grid[i++] = static_cast<float>(g) * step;
                    grid[i++] = static_cast<float>(b) * step;
                }
            }
        }
        std::vector<float> out(count * static_cast<std::size_t>(out_));
        cmsDoTransform(h_, grid.data(), out.data(), static_cast<cmsUInt32Number>(count));
        if (out_ == 3) {
            lut.data = std::move(out);
        } else { // Grau: auf drei Kanäle verteilen
            lut.data.resize(count * 3);
            for (std::size_t k = 0; k < count; ++k) {
                lut.data[k * 3] = lut.data[k * 3 + 1] = lut.data[k * 3 + 2] = out[k];
            }
        }
        return lut;
    }

private:
    cmsHTRANSFORM h_;
    int in_;
    int out_;
    std::optional<ports::MatrixShaper> shaper_;
};

} // namespace

struct LcmsColorEngine::Impl
{
    cmsContext ctx = nullptr;
    std::array<base::Expected<IccProfile>, 5> profiles{
        base::makeError(base::ErrorCode::Internal, ""), base::makeError(base::ErrorCode::Internal, ""),
        base::makeError(base::ErrorCode::Internal, ""), base::makeError(base::ErrorCode::Internal, ""),
        base::makeError(base::ErrorCode::Internal, "")};
};

LcmsColorEngine::LcmsColorEngine()
    : impl_(std::make_unique<Impl>())
{
    impl_->ctx = cmsCreateContext(nullptr, nullptr);
    const std::scoped_lock lock(lcmsGlobalsMutex());
    for (const ProfileId id : {ProfileId::WorkingRec2020Linear, ProfileId::Srgb, ProfileId::AdobeRgb,
                               ProfileId::ProPhoto, ProfileId::GrayGamma22}) {
        const ProfileHandle p(build(impl_->ctx, id));
        impl_->profiles[static_cast<std::size_t>(id)] = toBytes(impl_->ctx, p.h, nameOf(id));
    }
}

LcmsColorEngine::~LcmsColorEngine()
{
    if (impl_ && impl_->ctx != nullptr) {
        cmsDeleteContext(impl_->ctx);
    }
}

base::Expected<IccProfile> LcmsColorEngine::profile(ProfileId id)
{
    const auto i = static_cast<std::size_t>(id);
    if (i >= impl_->profiles.size()) {
        return base::makeError(base::ErrorCode::InvalidArgument, "Unbekanntes Profil");
    }
    return impl_->profiles[i];
}

base::Expected<IccProfile> LcmsColorEngine::display(int screen)
{
    return base::makeError(base::ErrorCode::Unsupported,
                           "Bildschirmprofil " + std::to_string(screen) +
                               " noch nicht ermittelbar (folgt mit der Oberfläche)");
}

base::Expected<std::shared_ptr<const ports::IColorTransform>>
LcmsColorEngine::transform(const IccProfile& src, const IccProfile& dst, ports::Intent intent)
{
    const auto open = [this](const IccProfile& p) {
        return ProfileHandle(cmsOpenProfileFromMemTHR(impl_->ctx, p.bytes.data(),
                                                      static_cast<cmsUInt32Number>(p.bytes.size())));
    };
    const ProfileHandle in = open(src);
    const ProfileHandle out = open(dst);
    if (in.h == nullptr || out.h == nullptr) {
        return base::makeError(base::ErrorCode::Corrupt,
                               "ICC-Profil nicht lesbar: " + (in.h == nullptr ? src.name : dst.name));
    }
    const auto channels = [](cmsHPROFILE p) -> int {
        const cmsColorSpaceSignature cs = cmsGetColorSpace(p);
        return cs == cmsSigGrayData ? 1 : cs == cmsSigRgbData ? 3 : 0;
    };
    const int ci = channels(in.h);
    const int co = channels(out.h);
    if (ci == 0 || co == 0) {
        return base::makeError(base::ErrorCode::Unsupported,
                               "Nur RGB- und Graustufenprofile werden unterstützt");
    }
    const cmsUInt32Number fin = ci == 1 ? TYPE_GRAY_FLT : TYPE_RGB_FLT;
    const cmsUInt32Number fout = co == 1 ? TYPE_GRAY_FLT : TYPE_RGB_FLT;
    cmsHTRANSFORM h = nullptr;
    {
        const std::scoped_lock lock(lcmsGlobalsMutex());
        h = cmsCreateTransformTHR(impl_->ctx, in.h, fin, out.h, fout, intentOf(intent), cmsFLAGS_NOCACHE);
    }
    if (h == nullptr) {
        return base::makeError(base::ErrorCode::Unsupported,
                               "Transformation " + src.name + " -> " + dst.name + " nicht möglich");
    }
    std::optional<ports::MatrixShaper> shaper;
    if (intent != ports::Intent::AbsoluteColorimetric) {
        shaper = matrixShaperOf(in.h, out.h);
        if (shaper && !agrees(h, *shaper)) {
            shaper.reset();
        }
    }
    return std::shared_ptr<const ports::IColorTransform>(std::make_shared<LcmsTransform>(h, ci, co, shaper));
}

} // namespace autoneg::adapter::lcms
