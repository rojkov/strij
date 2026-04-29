#include "test/mocks/event/mocks.hh"

#include "core/logging/log_frontend.hh"
#include "gtest/gtest.h"

namespace carrot::logging {
namespace {

TEST(LogFrontendTest, simple_log) {
  auto mocked_dispatcher = std::make_shared<event::MockDispatcher>();
  LogFrontend lf(mocked_dispatcher);
  LogEntry entry{};

  lf.Log(std::move(entry));
}

} // namespace
} // namespace carrot::logging
