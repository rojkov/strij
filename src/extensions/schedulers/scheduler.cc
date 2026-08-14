#include "extensions/schedulers/scheduler.hh"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "google/protobuf/any.pb.h"

namespace strij::extensions {

auto CreateScheduler(const strij::config::ExtensionConfig* config, FactoryContext& context)
    -> absl::StatusOr<std::unique_ptr<Scheduler>> {
  if (config == nullptr) {
    return absl::InvalidArgumentError(
        "gateway 'scheduler' extension is required; add a 'scheduler' section to the config, "
        "e.g. 'scheduler: {name: \"round_robin\"}'");
  }

  auto* factory = Registry<SchedulerFactory>::instance().GetFactory(config->name());
  if (factory == nullptr) {
    const auto names = Registry<SchedulerFactory>::instance().GetRegisteredNames();
    return absl::NotFoundError(absl::StrCat(
        "Scheduler '", config->name(), "' is not registered. Registered: ",
        absl::StrJoin(names, ", ")));
  }

  ::google::protobuf::Any unpacked;
  unpacked.CopyFrom(config->typed_config());
  auto config_msg = factory->CreateEmptyConfigProto();
  if (!unpacked.UnpackTo(config_msg.get())) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to unpack typed_config for scheduler '", config->name(),
        "': unknown type '", unpacked.type_url(), "'"));
  }

  return factory->Create(*config_msg, context);
}

} // namespace strij::extensions
