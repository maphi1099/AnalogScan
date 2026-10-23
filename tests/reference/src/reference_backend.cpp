#include "autoneg/reference/reference_backend.hpp"

#include "autoneg/reference/kernels.hpp"

namespace autoneg::reference {

namespace {
base::Expected<image::ImageBuffer> cancelled()
{
    return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
}

} // namespace

bool ReferenceBackend::begin(Ctx c, const std::string& name)
{
    int n = 0;
    {
        const std::scoped_lock lock(mtx_);
        ++calls_[name];
        n = ++total_;
    }
    if (hook_) {
        hook_(n);
    }
    return !c.cancel.cancelled();
}

int ReferenceBackend::calls(const std::string& name) const
{
    const std::scoped_lock lock(mtx_);
    const auto it = calls_.find(name);
    return it == calls_.end() ? 0 : it->second;
}

int ReferenceBackend::totalCalls() const
{
    const std::scoped_lock lock(mtx_);
    return total_;
}

void ReferenceBackend::resetCalls()
{
    const std::scoped_lock lock(mtx_);
    calls_.clear();
    total_ = 0;
}

ReferenceBackend::Result ReferenceBackend::runPixel(Ctx c, const image::PixelProgram& p, In in)
{
    return begin(c, "runPixel") ? Result(reference::runPixel(p, in)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::warp(Ctx c, In in, const image::WarpPlan& w)
{
    return begin(c, "warp") ? Result(reference::warp(in, w)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::resample(Ctx c, In in, base::Size s)
{
    return begin(c, "resample") ? Result(reference::resample(in, s)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::multiply(Ctx c, In in, In gain)
{
    return begin(c, "multiply") ? Result(reference::multiply(in, gain)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::gaussian(Ctx c, In in, float sigma)
{
    return begin(c, "gaussian") ? Result(reference::gaussian(in, sigma)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::localTone(Ctx c, In in, float s, float h, float r)
{
    return begin(c, "localTone") ? Result(reference::localTone(in, s, h, r)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::unsharp(Ctx c, In in, float a, float r, float t)
{
    return begin(c, "unsharp") ? Result(reference::unsharp(in, a, r, t)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::denoise(Ctx c, In in, float strength)
{
    return begin(c, "denoise") ? Result(reference::denoise(in, strength)) : cancelled();
}

ReferenceBackend::Result ReferenceBackend::inpaint(Ctx c, In in, const model::SpotList& spots)
{
    return begin(c, "inpaint") ? Result(reference::inpaint(in, spots)) : cancelled();
}

base::Expected<image::Histogram> ReferenceBackend::histogram(Ctx c, In in, int bins)
{
    if (!begin(c, "histogram")) {
        return base::makeError(base::ErrorCode::Cancelled, "abgebrochen");
    }
    return reference::histogram(in, bins);
}

base::Expected<image::ImageBuffer> ReferenceBackend::colorTransform(Ctx c, In in,
                                                                    const ports::IColorTransform& xf)
{
    return begin(c, "colorTransform") ? xf.apply(in) : cancelled(); // Referenz: am Stück
}

} // namespace autoneg::reference
