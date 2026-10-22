#include "autoneg/pipeline/warp_builder.hpp"

#include <cmath>

namespace autoneg::pipeline {

WarpBuilder::WarpBuilder(base::Size source)
    : source_(source)
    , width_(source.width)
    , height_(source.height)
{}

WarpBuilder& WarpBuilder::orient(int quarterTurns, bool flipH, bool flipV)
{
    // Spiegeln und Vierteldrehungen exakt (ohne cos/sin), damit vier Drehungen die Identität ergeben
    if (flipH) {
        forward_ = base::Mat2x3{{-1, 0, width_, 0, 1, 0}} * forward_;
    }
    if (flipV) {
        forward_ = base::Mat2x3{{1, 0, 0, 0, -1, height_}} * forward_;
    }
    const int q = ((quarterTurns % 4) + 4) % 4;
    for (int i = 0; i < q; ++i) {
        // 90° im Uhrzeigersinn: (x, y) -> (H - y, x); Breite und Höhe tauschen
        forward_ = base::Mat2x3{{0, -1, height_, 1, 0, 0}} * forward_;
        std::swap(width_, height_);
    }
    return *this;
}

WarpBuilder& WarpBuilder::rotate(double degrees)
{
    if (degrees == 0.0) {
        return *this;
    }
    const double cx = width_ / 2.0;
    const double cy = height_ / 2.0;
    // In Bildkoordinaten (y nach unten) dreht Mat2x3::rotation im Uhrzeigersinn
    forward_ = base::Mat2x3::translation(cx, cy) * base::Mat2x3::rotation(degrees) *
               base::Mat2x3::translation(-cx, -cy) * forward_;
    return *this;
}

WarpBuilder& WarpBuilder::crop(const model::NormRect& r)
{
    forward_ = base::Mat2x3::translation(-r.x * width_, -r.y * height_) * forward_;
    width_ *= r.w;
    height_ *= r.h;
    return *this;
}

WarpBuilder& WarpBuilder::filter(image::Filter f)
{
    filter_ = f;
    return *this;
}

bool WarpBuilder::isIdentity() const noexcept
{
    const base::Mat2x3 id;
    for (std::size_t i = 0; i < 6; ++i) {
        if (std::abs(forward_.m[i] - id.m[i]) > 1e-9) {
            return false;
        }
    }
    return std::lround(width_) == source_.width && std::lround(height_) == source_.height;
}

image::WarpPlan WarpBuilder::build() const
{
    image::WarpPlan plan;
    plan.affine = forward_.inverse().value_or(base::Mat2x3{});
    plan.out = {static_cast<int>(std::lround(width_)), static_cast<int>(std::lround(height_))};
    plan.filter = filter_;
    return plan;
}

} // namespace autoneg::pipeline
