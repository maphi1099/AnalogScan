#include "autoneg/adapter/lcms/lcms_color_engine.hpp"

#include <lcms2.h>

#include <cstring>
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

/// Fertige Transformation; cmsDoTransform ist ohne Zwischenspeicher threadsicher.
class LcmsTransform final : public ports::IColorTransform
{
public:
    LcmsTransform(cmsHTRANSFORM h, int inChannels, int outChannels)
        : h_(h)
        , in_(inChannels)
        , out_(outChannels)
    {}
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
    return std::shared_ptr<const ports::IColorTransform>(std::make_shared<LcmsTransform>(h, ci, co));
}

} // namespace autoneg::adapter::lcms
