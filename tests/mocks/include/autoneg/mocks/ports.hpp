#pragma once

// GoogleMock-Attrappen aller Ports (NF-ARC-12). Unit-Tests ersetzen damit jede
// Fremdbibliothek, jedes Dateisystem und jedes Backend.

#include "autoneg/ports/color.hpp"
#include "autoneg/ports/compute_backend.hpp"
#include "autoneg/ports/film_profile_source.hpp"
#include "autoneg/ports/image_io.hpp"
#include "autoneg/ports/inference.hpp"
#include "autoneg/ports/raw_decoder.hpp"
#include "autoneg/ports/repositories.hpp"
#include "autoneg/ports/services.hpp"
#include "autoneg/ports/task_executor.hpp"
#include "autoneg/ports/vision.hpp"

#include <gmock/gmock.h>

namespace autoneg::mocks {

using base::Expected;
using image::ImageBuffer;
namespace fs = std::filesystem;

class MockRawDecoder : public ports::IRawDecoder
{
public:
    MOCK_METHOD(Expected<model::SourceInfo>, probe, (const fs::path&), (override));
    MOCK_METHOD(Expected<ImageBuffer>, decode, (const fs::path&, const ports::DecodeOptions&), (override));
    MOCK_METHOD(Expected<ImageBuffer>, preview, (const fs::path&), (override));
};

class MockImageReader : public ports::IImageReader
{
public:
    MOCK_METHOD(Expected<model::SourceInfo>, probe, (const fs::path&), (override));
    MOCK_METHOD(Expected<ImageBuffer>, read, (const fs::path&), (override));
    MOCK_METHOD(Expected<ports::IccProfile>, profile, (const fs::path&), (override));
};

class MockImageWriter : public ports::IImageWriter
{
public:
    MOCK_METHOD(std::span<const ports::FileFormat>, formats, (), (const, override));
    MOCK_METHOD(Expected<void>, write, (const fs::path&, const ImageBuffer&, const ports::WriteOptions&),
                (override));
    MOCK_METHOD(Expected<base::Bytes>, encode, (const ImageBuffer&, const ports::WriteOptions&), (override));
};

class MockColorTransform : public ports::IColorTransform
{
public:
    MOCK_METHOD(Expected<ImageBuffer>, apply, (const ImageBuffer&), (const, override));
    MOCK_METHOD(image::Lut3D, lut3d, (int), (const, override));
};

class MockColorEngine : public ports::IColorEngine
{
public:
    MOCK_METHOD(Expected<ports::IccProfile>, profile, (ports::ProfileId), (override));
    MOCK_METHOD(Expected<ports::IccProfile>, display, (int), (override));
    MOCK_METHOD(Expected<std::shared_ptr<const ports::IColorTransform>>, transform,
                (const ports::IccProfile&, const ports::IccProfile&, ports::Intent), (override));
};

class MockComputeBackend : public ports::IComputeBackend
{
public:
    using Ctx = const ports::KernelContext&;
    using In = const ImageBuffer&;

    MOCK_METHOD(std::string, id, (), (const, override));
    MOCK_METHOD(bool, available, (), (const, override));
    MOCK_METHOD(int, concurrency, (), (const, override));
    MOCK_METHOD(Expected<ImageBuffer>, upload, (In), (override));
    MOCK_METHOD(Expected<ImageBuffer>, download, (In), (override));
    MOCK_METHOD(Expected<ImageBuffer>, runPixel, (Ctx, const image::PixelProgram&, In), (override));
    MOCK_METHOD(Expected<ImageBuffer>, warp, (Ctx, In, const image::WarpPlan&), (override));
    MOCK_METHOD(Expected<ImageBuffer>, resample, (Ctx, In, base::Size), (override));
    MOCK_METHOD(Expected<ImageBuffer>, multiply, (Ctx, In, In), (override));
    MOCK_METHOD(Expected<ImageBuffer>, gaussian, (Ctx, In, float), (override));
    MOCK_METHOD(Expected<ImageBuffer>, localTone, (Ctx, In, float, float, float), (override));
    MOCK_METHOD(Expected<ImageBuffer>, unsharp, (Ctx, In, float, float, float), (override));
    MOCK_METHOD(Expected<ImageBuffer>, denoise, (Ctx, In, float), (override));
    MOCK_METHOD(Expected<ImageBuffer>, inpaint, (Ctx, In, const model::SpotList&), (override));
    MOCK_METHOD(Expected<image::Histogram>, histogram, (Ctx, In, int), (override));
};

class MockVision : public ports::IVision
{
public:
    MOCK_METHOD(Expected<ImageBuffer>, edges, (const ImageBuffer&, float), (override));
    MOCK_METHOD(Expected<std::vector<ports::Polygon>>, contours, (const ImageBuffer&), (override));
    MOCK_METHOD(Expected<std::vector<ports::Line>>, lines, (const ImageBuffer&), (override));
    MOCK_METHOD(Expected<ImageBuffer>, morphology, (const ImageBuffer&, ports::MorphOp, int), (override));
};

class MockRollRepository : public ports::IRollRepository
{
public:
    MOCK_METHOD(Expected<std::optional<model::Roll>>, load, (const fs::path&), (override));
    MOCK_METHOD(Expected<void>, save, (const model::Roll&), (override));
};

class MockEditRepository : public ports::IEditRepository
{
public:
    MOCK_METHOD(Expected<std::optional<ports::SidecarData>>, load, (const model::Frame&), (override));
    MOCK_METHOD(Expected<void>, save, (const model::Frame&, const ports::SidecarData&), (override));
    MOCK_METHOD(std::vector<ports::SidecarRef>, orphans, (const fs::path&), (override));
};

class MockPresetRepository : public ports::IPresetRepository
{
public:
    MOCK_METHOD(std::vector<ports::PresetInfo>, list, (ports::PresetKind), (override));
    MOCK_METHOD(Expected<model::JsonValue>, load, (const ports::PresetId&), (override));
    MOCK_METHOD(Expected<void>, save, (ports::PresetKind, const std::string&, const model::JsonValue&),
                (override));
    MOCK_METHOD(Expected<void>, remove, (const ports::PresetId&), (override));
};

class MockSettingsStore : public ports::ISettingsStore
{
public:
    MOCK_METHOD(std::optional<model::JsonValue>, get, (const std::string&), (override));
    MOCK_METHOD(void, set, (const std::string&, const model::JsonValue&), (override));
};

class MockCacheStore : public ports::ICacheStore
{
public:
    MOCK_METHOD(std::optional<base::Bytes>, thumbnail, (const model::FrameId&, int, std::uint64_t),
                (override));
    MOCK_METHOD(void, put, (const model::FrameId&, int, std::uint64_t, const base::Bytes&), (override));
};

class MockContentHasher : public ports::IContentHasher
{
public:
    MOCK_METHOD(Expected<model::FrameId>, hashFile, (const fs::path&), (override));
};

class MockLogger : public ports::ILogger
{
public:
    MOCK_METHOD(void, log, (ports::LogLevel, std::string_view), (override));
};

class MockTaskExecutor : public ports::ITaskExecutor
{
public:
    MOCK_METHOD(void, enqueue, (ports::Priority, std::function<void()>), (override));
    MOCK_METHOD(void, run, (ports::Priority, const std::function<void()>&), (override));
    MOCK_METHOD(int, concurrency, (), (const, override));
    MOCK_METHOD(void, setConcurrency, (int), (override));
    MOCK_METHOD(void, shutdown, (), (override));
};

class MockInferenceModel : public ports::IInferenceModel
{
public:
    MOCK_METHOD(ports::ModelInfo, info, (), (const, override));
    MOCK_METHOD(Expected<ImageBuffer>, run, (const ports::KernelContext&, const ImageBuffer&), (override));
};

class MockInferenceRuntime : public ports::IInferenceRuntime
{
public:
    MOCK_METHOD(std::string, id, (), (const, override));
    MOCK_METHOD(bool, available, (), (const, override));
    MOCK_METHOD(Expected<std::unique_ptr<ports::IInferenceModel>>, load, (const fs::path&), (override));
};

class MockFilmProfileSource : public ports::IFilmProfileSource
{
public:
    MOCK_METHOD(std::vector<ports::FilmProfileInfo>, list, (), (override));
    MOCK_METHOD(Expected<model::JsonValue>, load, (const std::string&), (override));
};

} // namespace autoneg::mocks
