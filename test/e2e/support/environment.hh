#pragma once

#include <string>
#include <string_view>

namespace strij::e2e {

/**
 * @brief Resolves a workspace-relative path through the Bazel runfiles tree.
 *
 * Tests must not reach into `bazel-bin`: the binaries they drive are `data`
 * dependencies, so the runfiles lookup is what works under the sandbox and in
 * remote execution. Falls back to the path as written when no runfiles
 * environment is present.
 */
auto RunfilePath(std::string_view relative_path) -> std::string;

/**
 * @brief Skips the current test unless the `io_uring` event-loop facility works.
 *
 * Both the gateway and the nodeagent construct their `io_uring` dispatcher before
 * any configuration short-circuit, so a host that denies the setup call crashes
 * them at startup. Calling this before `Cluster::Start` keeps an unsupported
 * sandbox a skip rather than a failure; a crashed child is always a failure.
 *
 * `GTEST_SKIP()` only returns from this function, so test bodies must return
 * immediately afterwards:
 *
 *   RequireIoUring();
 *   if (testing::Test::IsSkipped()) {
 *     return;
 *   }
 */
void RequireIoUring();

} // namespace strij::e2e
