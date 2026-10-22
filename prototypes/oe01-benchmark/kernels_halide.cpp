// Aufruf der von Halide erzeugten Funktionen; die Bilder werden ohne Kopie eingewickelt.

#include "common.hpp"

#include "halide_fused.h"
#include "halide_lut.h"
#include "halide_pointwise.h"
#include "halide_unsharp.h"

#include <HalideBuffer.h>

#include <stdexcept>
#include <string>

namespace bench {

namespace {

using HBuffer = Halide::Runtime::Buffer<float>;

HBuffer wrap(const Image& img)
{
    halide_dimension_t dims[3] = {{0, img.w, 3, 0}, {0, img.h, 3 * img.w, 0}, {0, 3, 1, 0}};
    return HBuffer(const_cast<float*>(img.data.get()), 3, dims);
}

HBuffer wrap1(const float* data, int n)
{
    halide_dimension_t dims[1] = {{0, n, 1, 0}};
    return HBuffer(const_cast<float*>(data), 1, dims);
}

void check(int rc, const char* what)
{
    if (rc != 0) {
        throw std::runtime_error(std::string("Halide-Fehler in ") + what + ": " + std::to_string(rc));
    }
}

std::array<float, 10> packParams(const PointwiseParams& p)
{
    return {p.invBase[0], p.invBase[1], p.invBase[2], p.dMin[0],  p.dMin[1],
            p.dMin[2],    p.scale[0],   p.scale[1],   p.scale[2], p.exposure};
}

} // namespace

void halideSetThreads(int n)
{
    halide_set_num_threads(n);
}

void halidePointwise(const Image& in, Image& out, const PointwiseParams& p)
{
    auto params = packParams(p);
    auto pin = wrap(in);
    auto pout = wrap(out);
    auto pp = wrap1(params.data(), 10);
    check(halide_pointwise(pin, pp, pout), "pointwise");
}

void halideLut(const Image& in, Image& out, const Lut& lut)
{
    auto pin = wrap(in);
    auto pout = wrap(out);
    auto pl = wrap1(lut.values.data(), static_cast<int>(lut.values.size()));
    check(halide_lut(pin, pl, pout), "lut");
}

void halideFused(const Image& in, Image& out, const PointwiseParams& p, const Lut& lut)
{
    auto params = packParams(p);
    auto pin = wrap(in);
    auto pout = wrap(out);
    auto pp = wrap1(params.data(), 10);
    auto pl = wrap1(lut.values.data(), static_cast<int>(lut.values.size()));
    check(halide_fused(pin, pp, pl, pout), "fused");
}

void halideUnsharp(const Image& in, Image& out, const UnsharpParams& p)
{
    auto w = p.weights();
    auto pin = wrap(in);
    auto pout = wrap(out);
    auto pw = wrap1(w.data(), static_cast<int>(w.size()));
    check(halide_unsharp(pin, pw, p.amount, p.threshold, pout), "unsharp");
}

} // namespace bench
