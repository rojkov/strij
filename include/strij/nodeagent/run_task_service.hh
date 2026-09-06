#pragma once

#include "common/task/task.pb.h"
#include "strij/common/pure.hh"
#include "strij/nodeagent/admission_controller.hh"

namespace strij {

// Forward declared per the public-surface rule: the RunTaskService contract
// only names io::Connection (the accepted concrete exclusion on the extension
// surface); the impl pulls in the full type from src/.
namespace io {
class Connection;
} // namespace io

namespace nodeagent {

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

  // Runs a task whose capacity was already reserved by the caller. This is the
  // execution path for schedulers that preallocate admission up front (e.g.
  // probe scheduling): no Admit is called (a double-admit would never unwind)
  // and no kTaskRejected is sent. `reserved` transfers ownership to the result
  // sender, which releases the held capacity on the final result (or on
  // destruction). Runs synchronously on the caller's (event-loop) thread.
  virtual void RunTask(const task::Task& task, io::Connection& conn,
                       AdmissionScopePtr reserved) PURE;
};

} // namespace strij::nodeagent
} // namespace strij
