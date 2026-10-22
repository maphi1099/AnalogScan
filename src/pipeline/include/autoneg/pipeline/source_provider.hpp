#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/image/image_buffer.hpp"
#include "autoneg/model/film.hpp"
#include "autoneg/model/ids.hpp"
#include "autoneg/ports/image_io.hpp"
#include "autoneg/ports/raw_decoder.hpp"

#include <filesystem>
#include <future>
#include <map>
#include <mutex>

namespace autoneg::pipeline {

/// Liefert die dekodierte Quelle eines Bildes (Architektur 9.4) und hält sie bis release().
///
/// Stufe 0 = volle Auflösung, Stufe >= 1 = halbe Auflösung ohne Demosaicing (Vorschau,
/// Analyse). RAW-Dateien dekodiert der IRawDecoder; meldet er Unsupported, liest der
/// IImageReader (TIFF, JPEG). Gleichzeitige Anfragen nach derselben Quelle dekodieren nur
/// einmal und warten auf dasselbe Ergebnis ("Single Flight", Kapitel 6.4). Threadsicher.
class SourceProvider
{
public:
    SourceProvider(ports::IRawDecoder& decoder, ports::IImageReader& reader)
        : decoder_(decoder)
        , reader_(reader)
    {}

    [[nodiscard]] base::Expected<image::ImageBuffer> source(const model::FrameId& f,
                                                            const std::filesystem::path& path, int level);
    /// Kopfdaten (volle Größe, Kamera); ebenfalls zwischengespeichert.
    [[nodiscard]] base::Expected<model::SourceInfo> info(const model::FrameId& f,
                                                         const std::filesystem::path& path);
    /// Dekodiert die volle Auflösung im Voraus. Gedacht für einen Hintergrund-Job, der das
    /// nächste Bild lädt, während das aktuelle gerechnet wird (NF-PERF-11).
    void prefetch(const model::FrameId& f, const std::filesystem::path& path);
    /// Gibt alle Stufen des Bildes frei.
    void release(const model::FrameId& f);

private:
    struct Key
    {
        model::FrameId frame;
        int level = 0;
        friend auto operator<=>(const Key&, const Key&) = default;
    };

    base::Expected<image::ImageBuffer> decode(const std::filesystem::path& path, int level);

    ports::IRawDecoder& decoder_;
    ports::IImageReader& reader_;
    std::mutex mtx_;
    std::map<Key, std::shared_future<base::Expected<image::ImageBuffer>>> sources_;
    std::map<model::FrameId, model::SourceInfo> infos_;
};

} // namespace autoneg::pipeline
