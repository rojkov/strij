#include "nodeagent/extensions/task_handlers/openworkflow/listen_frame.hh"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "common/core/utils/jv.hh"
#include "common/task/task.pb.h"
#include "openworkflow/events.hh"
#include "openworkflow/task.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

using ::strij::openworkflow::EventConsumptionStrategy;
using ::strij::openworkflow::ListenBody;
using ::strij::openworkflow::NamedTask;

namespace {

auto jsonField(std::string_view name, std::string_view value) -> std::string {
  return absl::StrCat("\"", name, "\":\"", absl::CEscape(value), "\"");
}

auto jsonValueOrString(std::string_view value) -> std::string {
  auto parsed = utils::Jv::Parse(value);
  if (parsed.ok()) {
    return std::string(value);
  }
  return absl::StrCat("\"", absl::CEscape(value), "\"");
}

} // namespace

ListenFrame::ListenFrame(const NamedTask& task, const ListenBody& body,
                         std::shared_ptr<const LexicalScope> scope)
    : task_{task}, body_{body}, scope_{std::move(scope)} {}

void ListenFrame::Enter(StepDataValue data) {
  input_ = std::move(data);
  all_matches_.resize(body_.to_.filters_.size());
  markActive();
}

void ListenFrame::SetSubscription(EventSubscriptionPtr subscription) {
  subscription_ = std::move(subscription);
}

void ListenFrame::SetReady(absl::Status status, event::DeliveryMode mode) {
  ready_ = true;
  ready_status_ = std::move(status);
  mode_ = mode;
}

void ListenFrame::OnEvent(event::CloudEvent event, std::string token) {
  queued_.push_back(QueuedEvent{.event_ = std::move(event), .token_ = std::move(token)});
}

auto ListenFrame::itemBytes(const event::CloudEvent& event) const -> std::string {
  const bool envelope = body_.read_.has_value() && *body_.read_ == "envelope";
  if (!envelope) {
    return event.data();
  }

  std::string json = "{";
  absl::StrAppend(&json, jsonField("specversion", event.specversion()), ",");
  absl::StrAppend(&json, jsonField("id", event.id()), ",");
  absl::StrAppend(&json, jsonField("source", event.source()), ",");
  absl::StrAppend(&json, jsonField("type", event.type()));
  if (!event.subject().empty()) {
    absl::StrAppend(&json, ",", jsonField("subject", event.subject()));
  }
  absl::StrAppend(&json, ",\"data\":", jsonValueOrString(event.data()), "}");
  return json;
}

auto ListenFrame::completeWith(std::vector<event::CloudEvent> events) -> FrameResult {
  std::string json = "[";
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index > 0) {
      json.push_back(',');
    }
    json.append(itemBytes(events[index]));
  }
  json.push_back(']');

  complete_ = true;
  result_ = StepDataValue::FromBytes(std::move(json));
  markCompleted();
  return Completed{.value_ = result_.Copy()};
}

auto ListenFrame::fail(task::TaskStatus status, std::string message) -> FrameResult {
  failure_ = ExecutionFailure{.status_ = status,
                              .origin_ = nullptr,
                              .step_ = task_.name_,
                              .message_ = std::move(message),
                              .composed_ = false};
  markFailed();
  return Failed{.failure_ = *failure_};
}

auto ListenFrame::start(FrameContext& context) -> std::optional<FrameResult> {
  const absl::Status status = context.StartListen(*this, task_, body_, input_, scope_.get());
  if (!status.ok()) {
    return fail(task::TASK_STATUS_INTERNAL, std::string(status.message()));
  }

  started_ = true;
  return std::nullopt;
}

auto ListenFrame::process(const QueuedEvent& queued, FrameContext& context)
    -> std::optional<FrameResult> {
  const std::string key = queued.event_.source() + ":" + queued.event_.id();
  if (seen_.contains(key)) {
    if (subscription_ != nullptr) {
      subscription_->Ack(queued.token_);
    }
    return std::nullopt;
  }
  seen_[key] = true;

  bool matched = false;
  for (std::size_t index = 0; index < body_.to_.filters_.size(); ++index) {
    auto match = context.EvaluateListenFilter(task_, body_.to_.filters_[index], queued.event_,
                                              scope_.get());
    if (!match.ok()) {
      if (subscription_ != nullptr) {
        subscription_->Ack(queued.token_);
      }
      return fail(task::TASK_STATUS_INTERNAL, std::string(match.status().message()));
    }

    if (!*match) {
      continue;
    }

    matched = true;
    if (body_.to_.mode_ == EventConsumptionStrategy::Mode::kAll) {
      all_matches_[index] = queued.event_;
    }
  }

  if (subscription_ != nullptr) {
    subscription_->Ack(queued.token_);
  }

  switch (body_.to_.mode_) {
  case EventConsumptionStrategy::Mode::kOne:
    if (matched) {
      return completeWith({queued.event_});
    }
    break;
  case EventConsumptionStrategy::Mode::kAll: {
    bool all = true;
    std::vector<event::CloudEvent> events;
    for (const auto& slot : all_matches_) {
      if (!slot.has_value()) {
        all = false;
        break;
      }
      events.push_back(*slot);
    }
    if (all) {
      return completeWith(std::move(events));
    }
    break;
  }
  case EventConsumptionStrategy::Mode::kAny:
    if (matched) {
      consumed_.push_back(queued.event_);
      if (!body_.to_.until_.has_value()) {
        return completeWith({queued.event_});
      }

      auto until = context.EvaluateListenUntil(task_, body_, consumed_, scope_.get());
      if (!until.ok()) {
        return fail(task::TASK_STATUS_INTERNAL, std::string(until.status().message()));
      }
      if (*until) {
        return completeWith(consumed_);
      }
    }
    break;
  }

  return std::nullopt;
}

auto ListenFrame::Advance(FrameContext& context) -> FrameResult {
  if (IsAbandoned()) {
    return Suspended{};
  }

  if (failure_.has_value()) {
    if (IsActive()) {
      markFailed();
    }
    return Failed{.failure_ = *failure_};
  }

  if (complete_) {
    return Completed{.value_ = result_.Copy()};
  }

  assert(IsActive());

  if (!started_) {
    if (auto failure = start(context); failure.has_value()) {
      return std::move(*failure);
    }
  }

  if (!ready_) {
    return Suspended{};
  }

  if (!ready_status_.ok()) {
    if (subscription_ != nullptr) {
      subscription_->Delete();
      subscription_.reset();
    }
    return fail(task::TASK_STATUS_UNAVAILABLE, std::string(ready_status_.message()));
  }

  while (!queued_.empty()) {
    QueuedEvent queued = std::move(queued_.front());
    queued_.pop_front();
    if (auto result = process(queued, context); result.has_value()) {
      if (subscription_ != nullptr) {
        subscription_->Delete();
        subscription_.reset();
      }
      return std::move(*result);
    }
  }

  return Suspended{};
}

void ListenFrame::Abandon(FrameContext& context) {
  if (IsTerminal()) {
    return;
  }

  if (subscription_ != nullptr) {
    subscription_->Delete();
    subscription_.reset();
  }

  markAbandoned();
}

} // namespace strij::nodeagent::task_handlers::openworkflow
