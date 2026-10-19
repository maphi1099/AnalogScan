// Die Ports sind reine Schnittstellen (Architektur 9.9-9.11). Der Test stellt sicher, dass
// jede Schnittstelle vollständig durch eine Attrappe ersetzbar ist (NF-ARC-12).

#include "autoneg/mocks/ports.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

namespace autoneg::mocks {
namespace {

using ::testing::Return;

TEST(Ports, M1_EveryPortHasAMock)
{
    MockRawDecoder rawDecoder;
    MockImageReader imageReader;
    MockImageWriter imageWriter;
    MockColorTransform colorTransform;
    MockColorEngine colorEngine;
    MockComputeBackend computeBackend;
    MockVision vision;
    MockRollRepository rollRepository;
    MockEditRepository editRepository;
    MockPresetRepository presetRepository;
    MockSettingsStore settingsStore;
    MockCacheStore cacheStore;
    MockContentHasher contentHasher;
    MockLogger logger;
    MockTaskExecutor taskExecutor;
    MockInferenceModel inferenceModel;
    MockInferenceRuntime inferenceRuntime;
    MockFilmProfileSource filmProfileSource;
    SUCCEED();
}

TEST(Ports, M1_MockBackendBehavesLikeUnavailableGpu)
{
    MockComputeBackend gpu;
    EXPECT_CALL(gpu, available()).WillOnce(Return(false));
    EXPECT_CALL(gpu, id()).WillOnce(Return("gpu-attrappe"));

    const ports::IComputeBackend& backend = gpu;
    EXPECT_FALSE(backend.available());
    EXPECT_EQ(backend.id(), "gpu-attrappe");
}

TEST(Ports, M1_MockReturnsErrorsAsValues)
{
    MockRawDecoder decoder;
    EXPECT_CALL(decoder, probe(::testing::_))
        .WillOnce(Return(base::Expected<model::SourceInfo>(
            base::makeError(base::ErrorCode::Unsupported, "Kamera nicht unterstützt"))));

    ports::IRawDecoder& port = decoder;
    const auto result = port.probe("scan.xyz");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, base::ErrorCode::Unsupported);
}

} // namespace
} // namespace autoneg::mocks
