#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "openworkflow/events.hh"
#include "openworkflow/types.hh"
#include "openworkflow/value.hh"

namespace strij::openworkflow {

// The lifetime configuration of a container.
struct ContainerLifetime {
  std::string cleanup_;
  std::optional<Duration> after_;
};

struct ContainerProcess {
  std::string image_;
  std::optional<std::string> name_;
  std::optional<std::string> command_;
  std::optional<Value> ports_;
  std::optional<Value> volumes_;
  std::optional<Value> environment_;
  std::optional<std::string> stdin_;
  std::vector<std::string> arguments_;
  std::optional<ContainerLifetime> lifetime_;
  std::optional<std::string> pull_policy_;
};

struct ScriptSourceCode {
  std::string code_;
};

struct ScriptSourceResource {
  ExternalResource source_;
};

struct ScriptProcess {
  std::string language_;
  std::optional<std::string> stdin_;
  std::vector<std::string> arguments_;
  std::optional<Value> environment_;
  std::variant<ScriptSourceCode, ScriptSourceResource> source_;
};

struct ShellProcess {
  std::string command_;
  std::optional<std::string> stdin_;
  std::vector<std::string> arguments_;
  std::optional<Value> environment_;
};

struct WorkflowProcess {
  std::string namespace_;
  std::string name_;
  std::string version_;
  std::optional<Value> input_;
};

// The configuration of the process to execute; exactly one process kind.
struct RunBody {
  std::variant<ContainerProcess, ShellProcess, ScriptProcess, WorkflowProcess> process_;
  std::optional<bool> await_;
  std::optional<std::string> return_;
};

struct CallBody {
  std::string call_;
  Value with_;
};

struct DoBody {
  Tasks do_;
};

struct Event {
  EventProperties with_;
};

struct EmitBody {
  Event event_;
};

struct ForSpec {
  std::optional<std::string> each_;
  std::optional<std::string> at_;
  std::variant<std::string, Value> in_;
};

struct ForBody {
  ForSpec for_;
  std::optional<std::string> while_;
  Tasks do_;
};

struct ForkBody {
  Tasks branches_;
  bool compete_ = false;
};

struct ListenBody {
  EventConsumptionStrategy to_;
  std::optional<std::string> read_;
  std::optional<SubscriptionIterator> foreach_;
};

struct RaiseBody {
  std::variant<Error, std::string> error_;
};

struct SetBody {
  std::variant<std::string, Value> set_;
};

struct NamedCase {
  std::string name_;
  std::optional<std::string> when_;
  std::optional<std::string> then_;
};

struct SwitchBody {
  std::vector<NamedCase> cases_;
};

struct Catch {
  std::optional<ErrorFilter> errors_;
  std::optional<std::string> as_;
  std::optional<std::string> when_;
  std::optional<std::string> exceptWhen_;
  std::optional<std::variant<RetryPolicy, std::string>> retry_;
  Tasks do_;
  std::optional<std::string> then_;
};

struct TryBody {
  Tasks try_;
  std::optional<Catch> catch_;
};

struct WaitBody {
  Duration wait_;
};

// Exactly one of the twelve task kinds.
using TaskBody = std::variant<CallBody, DoBody, EmitBody, ForBody, ForkBody, ListenBody, RaiseBody,
                              RunBody, SetBody, SwitchBody, TryBody, WaitBody>;

struct Task {
  std::optional<std::string> if_;
  std::optional<Input> input_;
  std::optional<Output> output_;
  std::optional<Export> export_;
  std::optional<std::variant<Timeout, std::string>> timeout_;
  std::optional<std::string> then_;
  Value metadata_;
  TaskBody body_;
};

struct NamedTask {
  std::string name_;
  Task task_;
};

} // namespace strij::openworkflow
