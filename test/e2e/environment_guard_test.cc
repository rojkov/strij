#include <filesystem>

#include "test/e2e/support/environment.hh"

#include "gtest/gtest.h"

namespace strij::e2e {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(EnvironmentGuardTest, IoUringProbePassesOrSkips) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }
  SUCCEED();
}

TEST(EnvironmentGuardTest, RunfilesExposeGatewayAndNodeagent) {
  const std::filesystem::path gateway = RunfilePath("src/gateway/exe/gateway");
  const std::filesystem::path nodeagent = RunfilePath("src/nodeagent/exe/nodeagent");
  EXPECT_TRUE(std::filesystem::exists(gateway)) << gateway;
  EXPECT_TRUE(std::filesystem::exists(nodeagent)) << nodeagent;
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::e2e
