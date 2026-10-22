// Benchmark OE-01: misst skalar+oneTBB, Highway+oneTBB und Halide für Basiskorrektur,
// Gradation (Tabelle), beides verschmolzen und Schärfen. Ausgabe als Markdown-Tabelle.
//
// Aufruf: oe01_benchmark [--sizes 8,100] [--threads N] [--runs 7]
//   Größen in Megapixeln (3:2), Threads 0 = alle logischen Kerne. Je Threadzahl ein eigener
//   Prozess, weil der Halide-Thread-Pool nur vor dem ersten Aufruf begrenzt werden kann.
//   Bitgleichheit über Threadzahlen: Prüfsummen der Läufe vergleichen.

#include "common.hpp"

#include <tbb/global_control.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace bench;

namespace {

std::vector<int> parseList(const char* s)
{
    std::vector<int> out;
    std::string str(s);
    std::size_t pos = 0;
    while (pos <= str.size()) {
        const std::size_t comma = str.find(',', pos);
        out.push_back(std::stoi(str.substr(pos, comma - pos)));
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return out;
}

// Scan-ähnliches Eingangsbild: Transmission 0.02..0.9 mit glatten Verläufen und Feinstruktur.
void fillInput(Image& img)
{
    std::mt19937 rng(20261022);
    std::vector<float> noise(4096);
    for (float& v : noise) {
        v = static_cast<float>(rng() % 10000) / 10000.0F * 0.02F;
    }
    for (int y = 0; y < img.h; ++y) {
        float* r = img.row(y);
        for (int x = 0; x < img.w; ++x) {
            const float base = 0.05F + 0.8F * (0.5F + 0.5F * std::sin(x * 0.0013F + y * 0.0007F));
            for (int c = 0; c < 3; ++c) {
                r[x * 3 + c] = base * (0.7F + 0.1F * c) + noise[(x * 7 + y * 13 + c) & 4095];
            }
        }
    }
}

Lut makePaperLut()
{
    Lut lut;
    lut.values.resize(4096);
    for (std::size_t i = 0; i < lut.values.size(); ++i) {
        const double t = static_cast<double>(i) / (lut.values.size() - 1);
        lut.values[i] = static_cast<float>(1.0 / (1.0 + std::exp(-8.0 * (t - 0.5)))); // Fuß, Gerade, Schulter
    }
    return lut;
}

double medianMs(const std::function<void()>& fn, int runs)
{
    fn(); // Aufwärmen
    std::vector<double> ms;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::ranges::sort(ms);
    return ms[ms.size() / 2];
}

struct Diff
{
    double maxAbs = 0.0;
    double share = 0.0; ///< Anteil der Werte mit Abweichung > 1e-5
};

Diff compare(const Image& a, const Image& b)
{
    Diff d;
    std::size_t off = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double abs = std::abs(static_cast<double>(a.data[i]) - b.data[i]);
        d.maxAbs = std::max(d.maxAbs, abs);
        off += abs > 1e-5 ? 1U : 0U;
    }
    d.share = static_cast<double>(off) / static_cast<double>(a.size());
    return d;
}

/// Prüfsumme des Ergebnisses (FNV-1a über die Bytes); gleich = bitgleich.
unsigned long long checksum(const Image& img)
{
    unsigned long long h = 14695981039346656037ULL;
    const auto* p = reinterpret_cast<const unsigned char*>(img.data.get());
    for (std::size_t i = 0; i < img.size() * sizeof(float); ++i) {
        h = (h ^ p[i]) * 1099511628211ULL;
    }
    return h;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<int> sizes{8, 100};
    int threads = 0;
    int runs = 7;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--sizes") == 0) {
            sizes = parseList(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--threads") == 0) {
            threads = std::stoi(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--runs") == 0) {
            runs = std::stoi(argv[i + 1]);
        }
    }
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    if (threads <= 0) {
        threads = hw;
    }
    halideSetThreads(threads); // vor dem ersten Halide-Aufruf
    tbb::global_control gc(tbb::global_control::max_allowed_parallelism, static_cast<std::size_t>(threads));
    std::printf("Highway-Ziel: %s, Threads: %d von %d, Läufe je Messung: %d (Median)\n\n", hwyTargetName(), threads,
                hw, runs);

    const PointwiseParams pp;
    const Lut lut = makePaperLut();
    const UnsharpParams up;

    struct Kernel
    {
        const char* name;
        std::function<void(const Image&, Image&)> scalar, hwy, halide;
    };
    const std::vector<Kernel> kernels = {
        {"Basiskorrektur (log10)", [&](auto& i, auto& o) { scalarPointwise(i, o, pp); },
         [&](auto& i, auto& o) { hwyPointwise(i, o, pp); }, [&](auto& i, auto& o) { halidePointwise(i, o, pp); }},
        {"Gradation (Tabelle 4096)", [&](auto& i, auto& o) { scalarLut(i, o, lut); },
         [&](auto& i, auto& o) { hwyLut(i, o, lut); }, [&](auto& i, auto& o) { halideLut(i, o, lut); }},
        {"Basis + Gradation verschmolzen", [&](auto& i, auto& o) { scalarFused(i, o, pp, lut); },
         [&](auto& i, auto& o) { hwyFused(i, o, pp, lut); }, [&](auto& i, auto& o) { halideFused(i, o, pp, lut); }},
        {"Schärfen (USM, r=4)", [&](auto& i, auto& o) { scalarUnsharp(i, o, up); },
         [&](auto& i, auto& o) { hwyUnsharp(i, o, up); }, [&](auto& i, auto& o) { halideUnsharp(i, o, up); }},
    };

    for (const int mp : sizes) {
        const int h = static_cast<int>(std::lround(std::sqrt(mp * 1e6 / 1.5)));
        const int w = static_cast<int>(std::lround(h * 1.5));
        Image in(w, h);
        fillInput(in);
        // Tabelle und Schärfen arbeiten auf positiven Bildwerten 0..1 wie nach der Konvertierung
        Image positive(w, h);
        scalarFused(in, positive, pp, lut);
        Image ref(w, h), out(w, h);

        std::printf("## %d MP (%d x %d)\n\n", mp, w, h);
        std::printf("| Kernel | Threads | skalar+TBB ms | Highway+TBB ms | Halide ms | Abw. Highway (max / Anteil) "
                    "| Abw. Halide (max / Anteil) | Prüfsumme Highway | Prüfsumme Halide |\n");
        std::printf("|---|---|---|---|---|---|---|---|---|\n");
        for (std::size_t k = 0; k < kernels.size(); ++k) {
            const Kernel& kern = kernels[k];
            const Image& src = k == 0 || k == 2 ? in : positive;
            const double sMs = medianMs([&] { kern.scalar(src, ref); }, runs);
            const double hMs = medianMs([&] { kern.hwy(src, out); }, runs);
            const Diff dh = compare(ref, out);
            const unsigned long long hh = checksum(out);
            const double lMs = medianMs([&] { kern.halide(src, out); }, runs);
            const Diff dl = compare(ref, out);
            const unsigned long long hl = checksum(out);
            std::printf("| %s | %d | %.1f | %.1f | %.1f | %.1e / %.4f%% | %.1e / %.4f%% | %016llx | %016llx |\n",
                        kern.name, threads, sMs, hMs, lMs, dh.maxAbs, dh.share * 100, dl.maxAbs, dl.share * 100, hh,
                        hl);
            std::fflush(stdout);
        }
        std::printf("\n");
    }
    return 0;
}
