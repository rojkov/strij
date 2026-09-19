#pragma once

#include <memory>
#include <string_view>

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

// ResultSender lives in the task-handler extension contract (src/), but is
// named here on the sender-backed overloads. The impl includes the full type;
// extension authors already depend on task_handlers.hh.
class ResultSender;

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

  // Sender-backed variants of the above: results are delivered through the
  // caller's ResultSender instead of a connection-bound sender. Used by the
  // local child policy step with a StorageResultSender.
  //
  // Admitting overload: on admission failure or an unknown handler type the
  // task is dropped with a warning (there is no connection to send kTaskRejected
  // over; the caller drives fallback behavior from its own Admit/HasHandler
  // checks before calling this).
  virtual void RunTask(const task::Task& task, std::unique_ptr<ResultSender> sender) PURE;

  // Preallocated overload: runs the task with an already-held reservation; the
  // caller's successful Admit must have produced `reserved`. An unknown handler
  // type drops the task; the scope releases in its destructor.
  virtual void RunTask(const task::Task& task, std::unique_ptr<ResultSender> sender,
                       AdmissionScopePtr reserved) PURE;

  // Whether a task handler is registered for `type`. Local schedulers check
  // this before attempting admission so an unhandled child can be forwarded
  // rather than run (a successful Admit for an unknown type is capacity-only,
  // and RunTask would then drop it).
  [[nodiscard]] virtual auto HasHandler(std::string_view type) const -> bool PURE;
};

} // namespace strij::nodeagent
} // namespace strij
