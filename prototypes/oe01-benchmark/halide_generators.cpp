// Halide-Generatoren (AOT): werden beim Build ausgeführt und erzeugen optimierte Objektdateien.
// Bilder verschachtelt (x-Schritt 3, Kanal-Schritt 1), Kanäle 0..2.

#include <Halide.h>

#include <cmath>

using namespace Halide;

namespace {

constexpr int blockRows = 64;

void interleaved(OutputImageParam p)
{
    p.dim(0).set_stride(3);
    p.dim(2).set_stride(1).set_bounds(0, 3);
}

Expr pointwiseExpr(Expr v, Expr c, Func params)
{
    // params: invBase[3], dMin[3], scale[3], exposure
    Expr x = max(v * params(c), 1e-6f);
    Expr dens = -log(x) * float(1.0 / std::log(10.0));
    return (dens - params(3 + c)) * params(6 + c) + params(9);
}

Expr lutExpr(Expr v, Func lut, Expr n)
{
    Expr x = clamp(v, 0.0f, 1.0f) * cast<float>(n - 1);
    Expr i = min(cast<int>(x), n - 2);
    Expr f = x - cast<float>(i);
    Expr a = lut(i);
    return a + f * (lut(i + 1) - a);
}

void schedulePointwise(Func out, Var x, Var y, Var c, const Target& t)
{
    Var yo("yo"), yi("yi");
    out.reorder(c, x, y)
        .bound(c, 0, 3)
        .unroll(c)
        .split(y, yo, yi, blockRows, TailStrategy::GuardWithIf)
        .parallel(yo)
        .vectorize(x, t.natural_vector_size<float>());
}

class PointwiseGen : public Generator<PointwiseGen>
{
public:
    Input<Buffer<float, 3>> input{"input"};
    Input<Buffer<float, 1>> params{"params"};
    Output<Buffer<float, 3>> output{"output"};

    void generate()
    {
        output(x, y, c) = pointwiseExpr(input(x, y, c), c, params);
        interleaved(input);
        interleaved(output);
        schedulePointwise(output, x, y, c, get_target());
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

class LutGen : public Generator<LutGen>
{
public:
    Input<Buffer<float, 3>> input{"input"};
    Input<Buffer<float, 1>> lut{"lut"};
    Output<Buffer<float, 3>> output{"output"};

    void generate()
    {
        output(x, y, c) = lutExpr(input(x, y, c), lut, lut.dim(0).extent());
        interleaved(input);
        interleaved(output);
        schedulePointwise(output, x, y, c, get_target());
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

class FusedGen : public Generator<FusedGen>
{
public:
    Input<Buffer<float, 3>> input{"input"};
    Input<Buffer<float, 1>> params{"params"};
    Input<Buffer<float, 1>> lut{"lut"};
    Output<Buffer<float, 3>> output{"output"};

    void generate()
    {
        output(x, y, c) = lutExpr(pointwiseExpr(input(x, y, c), c, params), lut, lut.dim(0).extent());
        interleaved(input);
        interleaved(output);
        schedulePointwise(output, x, y, c, get_target());
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

class UnsharpGen : public Generator<UnsharpGen>
{
public:
    static constexpr int radius = 4;

    Input<Buffer<float, 3>> input{"input"};
    Input<Buffer<float, 1>> weights{"weights"}; // 2*radius+1
    Input<float> amount{"amount"};
    Input<float> threshold{"threshold"};
    Output<Buffer<float, 3>> output{"output"};

    void generate()
    {
        Func clamped = BoundaryConditions::repeat_edge(input);
        Func blurX("blurX"), blurY("blurY");
        Expr sx = 0.0f;
        Expr sy = 0.0f;
        for (int k = -radius; k <= radius; ++k) {
            sx += weights(k + radius) * clamped(x + k, y, c);
        }
        blurX(x, y, c) = sx;
        for (int k = -radius; k <= radius; ++k) {
            sy += weights(k + radius) * blurX(x, y + k, c);
        }
        blurY(x, y, c) = sy;
        Expr src = input(x, y, c);
        Expr diff = src - blurY(x, y, c);
        output(x, y, c) = select(abs(diff) > threshold, src + amount * diff, src);

        interleaved(input);
        interleaved(output);

        const int vec = get_target().natural_vector_size<float>();
        Var yo("yo"), yi("yi");
        output.reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c)
            .split(y, yo, yi, blockRows, TailStrategy::GuardWithIf)
            .parallel(yo)
            .vectorize(x, vec);
        blurX.compute_at(output, yo).reorder(c, x, y).bound(c, 0, 3).unroll(c).vectorize(x, vec);
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

} // namespace

HALIDE_REGISTER_GENERATOR(PointwiseGen, pointwise)
HALIDE_REGISTER_GENERATOR(LutGen, lut)
HALIDE_REGISTER_GENERATOR(FusedGen, fused)
HALIDE_REGISTER_GENERATOR(UnsharpGen, unsharp)
