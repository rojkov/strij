#include "common/extensions/evaluators/evaluator.hh"
#include "gtest/gtest.h"
#include "strij/extensions/extension_registry.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent {
namespace {

// Guards the nodeagent binary's extension bundle: every bundled factory must be
// linked and self-registered. Without this, a missing entry only fails at
// runtime (e.g. an "Evaluator 'jq' is not registered" per-task failure).
TEST(BundledExtensionsTest, FactoriesAreRegistered) {
  auto& evaluators = extensions::Registry<extensions::evaluators::EvaluatorFactory>::instance();
  EXPECT_NE(evaluators.GetFactory("jq"), nullptr);

  auto& handlers = extensions::Registry<TaskHandlerFactory>::instance();
  EXPECT_NE(handlers.GetFactory("echo"), nullptr);
  EXPECT_NE(handlers.GetFactory("openworkflow"), nullptr);
  EXPECT_NE(handlers.GetFactory("piped_executable"), nullptr);

  auto& schedulers = extensions::Registry<NodeSchedulerFactory>::instance();
  EXPECT_NE(schedulers.GetFactory("default"), nullptr);
  EXPECT_NE(schedulers.GetFactory("push"), nullptr);
  EXPECT_NE(schedulers.GetFactory("probe"), nullptr);
}

} // namespace
} // namespace strij::nodeagent
