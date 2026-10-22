#include "autoneg/pipeline/pixel_program_builder.hpp"

#include <algorithm>

namespace autoneg::pipeline {

namespace {

template <class T>
T* lastAs(image::PixelProgram& p)
{
    return p.ops.empty() ? nullptr : std::get_if<T>(&p.ops.back());
}

base::Vec3 mul(const base::Vec3& a, const base::Vec3& b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

} // namespace

PixelProgramBuilder& PixelProgramBuilder::matrix(const base::Mat3& m)
{
    if (auto* last = lastAs<image::Matrix3>(program_)) {
        last->m = m * last->m; // erst die vorhandene, dann die neue Matrix
    } else if (auto* s = lastAs<image::Scale3>(program_)) {
        program_.ops.back() = image::Matrix3{m * base::Mat3::diagonal(s->v)};
    } else {
        program_.ops.emplace_back(image::Matrix3{m});
    }
    return *this;
}

PixelProgramBuilder& PixelProgramBuilder::offset(const base::Vec3& v)
{
    if (auto* last = lastAs<image::Offset3>(program_)) {
        last->v = {last->v.x + v.x, last->v.y + v.y, last->v.z + v.z};
    } else {
        program_.ops.emplace_back(image::Offset3{v});
    }
    return *this;
}

PixelProgramBuilder& PixelProgramBuilder::scale(const base::Vec3& v)
{
    if (auto* last = lastAs<image::Scale3>(program_)) {
        last->v = mul(last->v, v);
    } else if (auto* m = lastAs<image::Matrix3>(program_)) {
        m->m = base::Mat3::diagonal(v) * m->m;
    } else {
        program_.ops.emplace_back(image::Scale3{v});
    }
    return *this;
}

PixelProgramBuilder& PixelProgramBuilder::log10()
{
    program_.ops.emplace_back(image::Log10{});
    return *this;
}

PixelProgramBuilder& PixelProgramBuilder::exp10()
{
    program_.ops.emplace_back(image::Exp10{});
    return *this;
}

PixelProgramBuilder& PixelProgramBuilder::lut(image::Lut1D l)
{
    return lut(std::make_shared<const image::Lut1D>(std::move(l)));
}

PixelProgramBuilder& PixelProgramBuilder::lut(image::LutRef l)
{
    program_.ops.emplace_back(std::move(l));
    return *this;
}

PixelProgramBuilder& PixelProgramBuilder::clamp(float lo, float hi)
{
    if (auto* last = lastAs<image::Clamp>(program_)) {
        last->lo = std::max(last->lo, lo);
        last->hi = std::min(last->hi, hi);
    } else {
        program_.ops.emplace_back(image::Clamp{lo, hi});
    }
    return *this;
}

} // namespace autoneg::pipeline
