// Werkzeug für K-11 (Vorbereitung): Bildfeld-, Schräglagen- und Basiserkennung auf echten
// Scans mit den echten Adaptern. Schreibt bericht.csv und je Scan ein Übersichtsbild (PNG)
// mit eingezeichnetem Bildfeld (grün) und Filmrand-Klassen. Kein Teil der Anwendung.
//
//   autoneg_analyze <color|bw|slide> <ausgabeordner> <raw-datei> [<raw-datei> ...]

#include "autoneg/adapter/libraw/libraw_decoder.hpp"
#include "autoneg/adapter/oiio/oiio_image_io.hpp"
#include "autoneg/adapter/opencv/opencv_vision.hpp"
#include "autoneg/analysis/base_meter.hpp"
#include "autoneg/analysis/frame_detector.hpp"
#include "autoneg/backend/cpu/cpu_backend.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace autoneg;

/// Übersichtsbild: Log-Helligkeit (Negative sichtbar), Klassen eingefärbt, Bildfeld grün.
image::ImageBuffer overlay(const image::ImageBuffer& img, const analysis::FrameDetection* d)
{
    const auto& s = img.spec();
    std::vector<float> lum(static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height));
    float lo = 1e9F;
    float hi = -1e9F;
    const auto v = img.host();
    for (std::size_t i = 0; i < lum.size(); ++i) {
        const float l = 0.2627F * v[i * 3] + 0.678F * v[i * 3 + 1] + 0.0593F * v[i * 3 + 2];
        lum[i] = std::log10(std::max(l, 1e-4F));
        lo = std::min(lo, lum[i]);
        hi = std::max(hi, lum[i]);
    }
    std::vector<float> out(lum.size() * 3);
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            const std::size_t i =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) + static_cast<std::size_t>(x);
            const float g = (lum[i] - lo) / std::max(hi - lo, 1e-6F);
            float r = g;
            float gg = g;
            float b = g;
            if (d != nullptr) {
                switch (d->regions.classOf({(x + 0.5) / s.width, (y + 0.5) / s.height})) {
                case analysis::RegionClass::Holder:
                    r = 0.5F * g + 0.4F; // rötlich
                    break;
                case analysis::RegionClass::Rebate:
                    b = 0.5F * g + 0.4F; // bläulich
                    break;
                default:
                    break;
                }
            }
            out[i * 3] = r;
            out[i * 3 + 1] = gg;
            out[i * 3 + 2] = b;
        }
    }
    if (d != nullptr) {
        // Bildfeld (entzerrte Koordinaten) als Rahmen; Rückdrehung um die Bildmitte
        const double rad = -d->angleDeg * 3.14159265358979 / 180.0; // Schräglage = −Korrektur
        const double c = std::cos(rad);
        const double sn = std::sin(rad);
        const auto mark = [&](double qx, double qy) {
            const double dx = qx - s.width / 2.0;
            const double dy = qy - s.height / 2.0;
            const int px = static_cast<int>(s.width / 2.0 + c * dx - sn * dy);
            const int py = static_cast<int>(s.height / 2.0 + sn * dx + c * dy);
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int X = px + ox;
                    const int Y = py + oy;
                    if (X >= 0 && Y >= 0 && X < s.width && Y < s.height) {
                        const std::size_t k =
                            (static_cast<std::size_t>(Y) * static_cast<std::size_t>(s.width) +
                             static_cast<std::size_t>(X)) *
                            3;
                        out[k] = 0.0F;
                        out[k + 1] = 1.0F;
                        out[k + 2] = 0.0F;
                    }
                }
            }
        };
        const double x0 = d->frame.x * s.width;
        const double y0 = d->frame.y * s.height;
        const double x1 = (d->frame.x + d->frame.w) * s.width;
        const double y1 = (d->frame.y + d->frame.h) * s.height;
        for (double t = 0; t <= 1.0; t += 0.5 / std::max(s.width, s.height)) {
            mark(x0 + t * (x1 - x0), y0);
            mark(x0 + t * (x1 - x0), y1);
            mark(x0, y0 + t * (y1 - y0));
            mark(x1, y0 + t * (y1 - y0));
        }
    }
    return image::ImageBuffer::fromFloats(
               image::ImageSpec{s.width, s.height, 3, image::PixelFormat::F32, image::ColorSpace::Output},
               out)
        .value();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Aufruf: autoneg_analyze <color|bw|slide> <ausgabeordner> <raw-datei> ...\n";
        return 2;
    }
    const std::string filmName = argv[1];
    const model::FilmType film = filmName == "bw"      ? model::FilmType::BwNegative
                                 : filmName == "slide" ? model::FilmType::Slide
                                                       : model::FilmType::ColorNegative;
    const std::filesystem::path outDir = argv[2];
    std::filesystem::create_directories(outDir);
    adapter::libraw::LibRawDecoder decoder;
    adapter::opencv::OpenCvVision vision;
    adapter::oiio::OiioImageWriter writer;
    backend::cpu::CpuBackend backend;
    const analysis::FrameDetector detector(vision);

    std::ofstream report(outDir / "bericht.csv", std::ios::app);
    for (int i = 3; i < argc; ++i) {
        const std::filesystem::path path = argv[i];
        const auto t0 = std::chrono::steady_clock::now();
        ports::DecodeOptions half;
        half.half = true;
        auto img = decoder.decode(path, half);
        if (!img) {
            report << path.string() << ";Dekodierfehler;" << img.error().message << "\n";
            continue;
        }
        // Analyseauflösung: längste Seite 1500 px
        const double f = 1500.0 / std::max(img->spec().width, img->spec().height);
        const base::Size size{static_cast<int>(std::lround(img->spec().width * f)),
                              static_cast<int>(std::lround(img->spec().height * f))};
        const auto small = backend.resample({}, *img, size);
        if (!small) {
            continue;
        }
        const auto d = detector.detect(*small, film);
        std::string base = "-";
        if (d && film != model::FilmType::Slide) {
            const auto m = analysis::BaseMeter{}.measure(*small, d->regions);
            if (m) {
                base = std::to_string(m->transmission.x) + "/" + std::to_string(m->transmission.y) + "/" +
                       std::to_string(m->transmission.z) + "/n=" + std::to_string(m->samples);
            } else {
                base = "Fehler: " + m.error().message;
            }
        }
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        const std::string name = path.parent_path().filename().string() + "_" + path.stem().string();
        if (d) {
            report << path.string() << ";ok;" << d->frame.x << ";" << d->frame.y << ";" << d->frame.w << ";"
                   << d->frame.h << ";" << d->angleDeg << ";" << d->confidence << ";" << base << ";"
                   << ms.count() << "\n";
        } else {
            report << path.string() << ";nicht erkannt;" << d.error().message << ";" << ms.count() << "\n";
        }
        ports::WriteOptions o;
        o.format = ports::FileFormat::Png;
        o.bitDepth = 8;
        (void)writer.write(outDir / (name + ".png"), overlay(*small, d ? &*d : nullptr), o);
        std::cout << name << (d ? " ok" : " nicht erkannt") << " (" << ms.count() << " ms)\n";
    }
    return 0;
}
