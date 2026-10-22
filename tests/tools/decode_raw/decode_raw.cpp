// Testwerkzeug: dekodiert eine RAW-Datei mit LibRawDecoder und schreibt das Ergebnis als PFM.
// Für K-02 (Linearität gegen die mit rawpy gelesenen Sensordaten). Kein Teil der Anwendung.
//
//   autoneg_decode_raw <ein.raw> <aus.pfm> [half]

#include "autoneg/adapter/libraw/libraw_decoder.hpp"

#include <bit>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Aufruf: autoneg_decode_raw <ein.raw> <aus.pfm> [half]\n";
        return 2;
    }
    autoneg::adapter::libraw::LibRawDecoder decoder;
    autoneg::ports::DecodeOptions o;
    o.half = argc > 3 && std::string(argv[3]) == "half";
    const auto img = decoder.decode(std::filesystem::path(argv[1]), o);
    if (!img) {
        std::cerr << img.error().message << "\n";
        return 1;
    }
    const auto& s = img->spec();
    std::ofstream out(std::filesystem::path(argv[2]), std::ios::binary);
    out << "PF\n"
        << s.width << " " << s.height << "\n"
        << (std::endian::native == std::endian::little ? "-1" : "1") << "\n";
    const auto v = img->host();
    const std::size_t row = static_cast<std::size_t>(s.width) * 3;
    for (int y = s.height - 1; y >= 0; --y) {
        out.write(reinterpret_cast<const char*>(v.data() + static_cast<std::size_t>(y) * row),
                  static_cast<std::streamsize>(row * sizeof(float)));
    }
    std::cout << s.width << "x" << s.height << "\n";
    return out ? 0 : 1;
}
