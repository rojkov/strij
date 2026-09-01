#pragma once

#include "common/core/io/connection.hh"
#include "common/task/task.pb.h"
#include "strij/common/pure.hh"

namespace strij::nodeagent {

// Pure-abstract contract for running admitted tasks to completion on the
// nodeagent event-loop thread. NodeagentFactoryContext exposes this service so
// scheduler factories can reach it without knowing the concrete handler manager.
class RunTaskService {
public:
  RunTaskService() = default;
  virtual ~RunTaskService() = default;

  RunTaskService(const RunTaskService&) = delete;
  auto operator=(const RunTaskService&) -> RunTaskService& = delete;
  RunTaskService(RunTaskService&&) noexcept = delete;
  auto operator=(RunTaskService&&) noexcept -> RunTaskService& = delete;

  // Admits `task` and runs it via its configured task handler, sending results
  // back over `conn`. Sends a kTaskRejected frame on admission failure. Tasks
  // with an unknown handler type are dropped with a warning. Runs synchronously
  // on the caller's (event-loop) thread.
  virtual void RunTask(const task::Task& task, io::Connection& conn) PURE;
};

} // namespace strij::nodeagent
