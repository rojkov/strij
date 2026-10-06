#pragma once

#include <string_view>

#include "common/task/task.pb.h"

namespace strij::utils {

// The single success predicate for a task result: absence of `status` or the
// enum's zero member reads as success, any other value — including one this
// schema does not name — reads as failure. Written once so every consumer
// applies the same rule.
auto IsTaskResultOk(const task::TaskResult& result) -> bool;

// Total mapping from a TaskStatus member to the HTTP status that reports it.
// A value the schema does not name maps to the internal-failure
// status, never to 200.
auto TaskStatusToHttpStatus(task::TaskStatus status) -> int;

// The reason phrase written into an HTTP status line for a status code.
auto HttpStatusPhrase(int http_status) -> std::string_view;

} // namespace strij::utils
