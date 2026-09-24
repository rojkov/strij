#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "openworkflow/events.hh"
#include "openworkflow/task.hh"
#include "openworkflow/types.hh"

namespace strij::openworkflow {

// A reusable extension applies before/after tasks to a kind of task.
struct Extension {
  std::string extend_;
  std::optional<std::string> when_;
  Tasks before_;
  Tasks after_;
};

struct NamedExtension {
  std::string name_;
  Extension extension_;
};

// Defines the workflow's reusable components.
struct Use {
  std::map<std::string, Authentication> authentications_;
  std::map<std::string, Error> errors_;
  std::vector<NamedExtension> extensions_;
  std::map<std::string, Task> functions_;
  std::map<std::string, RetryPolicy> retries_;
  std::vector<std::string> secrets_;
  std::map<std::string, Timeout> timeouts_;
  std::map<std::string, Catalog> catalogs_;
};

} // namespace strij::openworkflow
