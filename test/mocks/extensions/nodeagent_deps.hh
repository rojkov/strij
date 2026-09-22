#pragma once

#include <utility>

#include "absl/status/status.h"
#include "strij/nodeagent/child_task_forwarder.hh"
#include "strij/nodeagent/child_task_submitter.hh"

namespace strij::extensions {

// Test double for the nodeagent ChildTaskForwarder port. Returns OkStatus by
// default; tests that exercise the forward path can set a custom status or
// override Forward via a subclass.
class StubChildTaskForwarder final : public nodeagent::ChildTaskForwarder {
public:
  explicit StubChildTaskForwarder(absl::Status status = absl::OkStatus())
      : status_{std::move(status)} {}

  auto Forward(const task::Task& /*task*/) -> absl::Status override { return status_; }

  void SetStatus(absl::Status status) { status_ = std::move(status); }

private:
  absl::Status status_;
};

// Test double for the nodeagent ChildTaskSubmitter handle: records nothing and
// completes no child. Tests that build a TaskHandlerDeps but do not submit
// children use this.
class StubChildTaskSubmitter final : public nodeagent::ChildTaskSubmitter {
public:
  void Submit(task::Task /*task*/, gateway::ResultReceiverPtr /*receiver*/) override {}
};

} // namespace strij::extensions
